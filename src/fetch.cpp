#include "fetch.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <thread>

#include <curl/curl.h>

namespace vtw {

namespace {

using Clock = std::chrono::steady_clock;

constexpr const char* PROGRAM_VERSION = "0.1";
constexpr const char* PROGRAM_URL = "https://github.com/briefnotion/vtweather";

// libcurl needs one global start-up before any thread uses it.
void start_curl_once() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

bool starts_with(const std::string& text, const std::string& prefix) {
    return text.compare(0, prefix.size(), prefix) == 0;
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// What libcurl hands to the callbacks.
struct Transfer {
    std::string* body = nullptr;
    size_t max_body = 0;
    bool too_large = false;
    const Fetcher::StopCheck* should_stop = nullptr;
    bool stopped = false;
    std::string content_type;
    double retry_after = 0;
};

size_t on_body(char* data, size_t size, size_t count, void* user) {
    Transfer& t = *static_cast<Transfer*>(user);
    const size_t n = size * count;
    if (t.body->size() + n > t.max_body) { t.too_large = true; return 0; }   // 0 aborts
    t.body->append(data, n);
    return n;
}

size_t on_header(char* data, size_t size, size_t count, void* user) {
    Transfer& t = *static_cast<Transfer*>(user);
    const size_t n = size * count;
    const std::string line(data, n);
    if (starts_with(line, "HTTP/")) {                  // a new answer (after a redirect): start over
        t.content_type.clear();
        t.retry_after = 0;
        return n;
    }
    const size_t colon = line.find(':');
    if (colon == std::string::npos) return n;
    const std::string name = lower(line.substr(0, colon));
    std::string value = line.substr(colon + 1);
    value.erase(0, value.find_first_not_of(" \t"));
    value.erase(value.find_last_not_of(" \r\n\t") + 1);
    if (name == "content-type") t.content_type = value;
    else if (name == "retry-after") {
        char* end = nullptr;
        const double seconds = std::strtod(value.c_str(), &end);
        t.retry_after = end != value.c_str() ? seconds : 60;    // a date: just wait a minute
    }
    return n;
}

int on_progress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    Transfer& t = *static_cast<Transfer*>(user);
    if (t.should_stop && *t.should_stop && (*t.should_stop)()) { t.stopped = true; return 1; }   // 1 aborts
    return 0;
}

}  // namespace

std::string make_user_agent(const std::string& contact) {
    std::string agent = std::string("vtweather/") + PROGRAM_VERSION + " (+" + PROGRAM_URL;
    if (!contact.empty()) agent += "; " + contact;
    return agent + ")";
}

struct Fetcher::Impl {
    FetchSettings settings;
    StopCheck should_stop;
    RequestPolicy policy;
    CURL* handle = nullptr;
    Clock::time_point start = Clock::now();

    Impl(FetchSettings s, StopCheck stop) : settings(std::move(s)), should_stop(std::move(stop)), policy(settings.limits) {
        start_curl_once();
        handle = curl_easy_init();
    }
    ~Impl() { if (handle) curl_easy_cleanup(handle); }

    double now() const { return std::chrono::duration<double>(Clock::now() - start).count(); }
    bool stopping() const { return should_stop && should_stop(); }

    // Wait out the policy's pacing. False if told to stop, or if the policy refuses.
    bool wait_for_turn(FetchResult& result) {
        for (;;) {
            const Decision d = policy.ask(now());
            if (d.kind == Decision::Go) return true;
            if (d.kind == Decision::Refuse) {
                result.error = d.reason;
                result.retry_in = d.seconds;
                return false;
            }
            if (stopping()) { result.error = "stopped"; return false; }
            std::this_thread::sleep_for(std::chrono::duration<double>(std::min(d.seconds, 0.05)));
        }
    }

    void set_up(const std::string& url, Transfer& transfer) {
        curl_easy_reset(handle);
        curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
        curl_easy_setopt(handle, CURLOPT_USERAGENT, settings.user_agent.c_str());
        curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);                     // we use threads
        curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, (long)(settings.connect_timeout_seconds * 1000));
        curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, (long)(settings.total_timeout_seconds * 1000));
        curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");              // let the server compress
        curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 3L);
        const char* allowed = settings.allow_plain_http ? "http,https" : "https";
#if LIBCURL_VERSION_NUM >= 0x075500
        curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, allowed);
        curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS_STR, allowed);
#else
        const long mask = settings.allow_plain_http ? (CURLPROTO_HTTP | CURLPROTO_HTTPS) : CURLPROTO_HTTPS;
        curl_easy_setopt(handle, CURLOPT_PROTOCOLS, mask);
        curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS, mask);
#endif
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, on_body);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, &transfer);
        curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, on_header);
        curl_easy_setopt(handle, CURLOPT_HEADERDATA, &transfer);
        curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, on_progress);
        curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &transfer);
    }
};

Fetcher::Fetcher(FetchSettings settings, StopCheck should_stop)
    : impl_(new Impl(std::move(settings), std::move(should_stop))) {}

Fetcher::~Fetcher() = default;

FetchResult Fetcher::get(const std::string& url) {
    Impl& m = *impl_;
    FetchResult result;

    const bool https = starts_with(url, "https://");
    const bool http = starts_with(url, "http://");
    if (!https && !(http && m.settings.allow_plain_http)) {
        result.error = "only https addresses are allowed";
        return result;
    }
    if (!m.handle) { result.error = "could not start libcurl"; return result; }
    if (!m.wait_for_turn(result)) return result;

    Transfer transfer;
    transfer.body = &result.body;
    transfer.max_body = m.settings.max_body_bytes;
    transfer.should_stop = &m.should_stop;
    m.set_up(url, transfer);

    m.policy.started(m.now());
    result.asked = true;
    const CURLcode code = curl_easy_perform(m.handle);
    const double finished = m.now();

    long status = 0;
    curl_easy_getinfo(m.handle, CURLINFO_RESPONSE_CODE, &status);
    result.status = (int)status;
    result.content_type = transfer.content_type;

    if (transfer.stopped) {
        result.error = "stopped";
        m.policy.cancelled(finished);
        return result;
    }
    if (transfer.too_large) result.error = "the answer is too large";
    else if (code == CURLE_OPERATION_TIMEDOUT) result.error = "the server did not answer in time (timed out)";
    else if (code != CURLE_OK) result.error = std::string("no answer: ") + curl_easy_strerror(code);
    else if (status < 200 || status > 299) result.error = "the server answered HTTP " + std::to_string(status);

    if (result.error.empty()) {
        result.ok = true;
        m.policy.succeeded(finished);
        return result;
    }
    result.body.clear();
    m.policy.failed(finished, transfer.retry_after);
    const Decision after = m.policy.ask(finished);
    result.retry_in = after.kind == Decision::Refuse ? after.seconds : 0;
    return result;
}

}  // namespace vtw
