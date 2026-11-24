#include "radar_worker.h"

#include <chrono>

namespace vtw {

namespace {

// The start of an answer, for an error message, without anything that could mess up a screen.
std::string snippet(const std::string& body) {
    std::string s;
    for (size_t i = 0; i < body.size() && s.size() < 100; i++) {
        const unsigned char c = (unsigned char)body[i];
        s += (c >= 32 && c < 127) ? (char)c : ' ';
    }
    return s;
}

}  // namespace

RadarWorker::RadarWorker(FetchSettings settings) : settings_(std::move(settings)) {
    thread_ = std::thread([this] { run(); });
}

RadarWorker::~RadarWorker() {
    state_.store(Stop, std::memory_order_release);
    thread_.join();
}

bool RadarWorker::can_request() const { return state_.load(std::memory_order_acquire) == Idle; }

bool RadarWorker::request(RadarRequest request) {
    if (state_.load(std::memory_order_acquire) != Idle) return false;
    request_ = std::move(request);                         // main owns it while Idle
    state_.store(Requested, std::memory_order_release);    // and hands it over here
    return true;
}

bool RadarWorker::take(RadarResult& result) {
    if (state_.load(std::memory_order_acquire) != Ready) return false;
    result = std::move(result_);                           // main owns the result while Ready
    state_.store(Idle, std::memory_order_release);
    return true;
}

void RadarWorker::run() {
    Fetcher fetcher(settings_, [this] { return state_.load(std::memory_order_relaxed) == Stop; });
    for (;;) {
        const int state = state_.load(std::memory_order_acquire);
        if (state == Stop) return;
        if (state != Requested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        int expected = Requested;
        if (!state_.compare_exchange_strong(expected, Working, std::memory_order_acq_rel)) continue;   // Stop

        result_ = RadarResult();
        result_.box = request_.box;
        do_request(fetcher);

        expected = Working;
        state_.compare_exchange_strong(expected, Ready, std::memory_order_release);   // not if Stop came
    }
}

void RadarWorker::do_request(Fetcher& fetcher) {
    const FetchResult answer = fetcher.get(request_.url);
    result_.asked = answer.asked;
    result_.retry_in = answer.retry_in;
    if (!answer.ok) {
        result_.error = answer.error;
        return;
    }
    // A map server answers a bad request with a 200 and some XML; only a PNG is a picture.
    if (answer.content_type.compare(0, 9, "image/png") != 0) {
        result_.error = "the service did not send a picture (it said: " + snippet(answer.body) + ")";
        return;
    }
    Image image;
    std::string problem;
    if (!decode_png(answer.body, image, problem)) {
        result_.error = "the picture could not be read: " + problem;
        return;
    }
    if (!make_radar_frame(image, request_.box, result_.frame, problem)) {
        result_.error = "the picture could not be used: " + problem;
        return;
    }
    result_.ok = true;
}

}  // namespace vtw
