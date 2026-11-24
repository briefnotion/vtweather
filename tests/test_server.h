// A tiny web server on this machine, only for the fetch tests: nothing here goes to the
// internet. It answers a few paths, one connection at a time, and counts the requests.
//
//   /ok            200, an "image/png" body of 1000 bytes
//   /status/NNN    that status; /status/NNN?retry=S adds a Retry-After of S seconds
//   /agent         200, the body is the User-Agent the request carried
//   /slow          waits 3 seconds, then 200
//   /big           200, a 3 MB body
//   /redirect      302 to /ok
#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

class TestServer {
public:
    TestServer() {
        listener_ = socket(AF_INET, SOCK_STREAM, 0);
        int yes = 1;
        setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;                                  // any free port
        bind(listener_, (sockaddr*)&address, sizeof address);
        socklen_t length = sizeof address;
        getsockname(listener_, (sockaddr*)&address, &length);
        port_ = ntohs(address.sin_port);
        listen(listener_, 8);
        thread_ = std::thread([this] { serve(); });
    }
    ~TestServer() {
        stop_ = true;
        thread_.join();
        close(listener_);
    }

    std::string url(const std::string& path) const { return "http://127.0.0.1:" + std::to_string(port_) + path; }
    int requests() const { return requests_; }

private:
    void serve() {
        while (!stop_) {
            pollfd waiting{listener_, POLLIN, 0};
            if (poll(&waiting, 1, 50) <= 0) continue;
            const int client = accept(listener_, nullptr, nullptr);
            if (client < 0) continue;
            handle(client);
            close(client);
        }
    }

    static void send_all(int client, const std::string& text) {
        size_t sent = 0;
        while (sent < text.size()) {
            const ssize_t n = send(client, text.data() + sent, text.size() - sent, MSG_NOSIGNAL);
            if (n <= 0) return;
            sent += (size_t)n;
        }
    }

    void handle(int client) {
        std::string request;
        char buffer[1024];
        while (request.find("\r\n\r\n") == std::string::npos) {
            const ssize_t n = recv(client, buffer, sizeof buffer, 0);
            if (n <= 0) return;
            request.append(buffer, (size_t)n);
        }
        requests_++;
        const size_t path_start = request.find(' ') + 1;
        const std::string path = request.substr(path_start, request.find(' ', path_start) - path_start);
        std::string agent;
        const size_t at = request.find("User-Agent: ");
        if (at != std::string::npos) agent = request.substr(at + 12, request.find("\r\n", at) - at - 12);

        auto reply = [&](int status, const std::string& type, const std::string& body, const std::string& extra = "") {
            send_all(client, "HTTP/1.1 " + std::to_string(status) + " X\r\nContent-Type: " + type +
                                 "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n" + extra +
                                 "Connection: close\r\n\r\n" + body);
        };

        if (path == "/ok") reply(200, "image/png", std::string(1000, 'p'));
        else if (path == "/agent") reply(200, "text/plain", agent);
        else if (path == "/redirect") reply(302, "text/plain", "", "Location: /ok\r\n");
        else if (path == "/slow") {
            for (int i = 0; i < 60 && !stop_; i++) std::this_thread::sleep_for(std::chrono::milliseconds(50));
            reply(200, "text/plain", "late");
        } else if (path == "/big") reply(200, "application/octet-stream", std::string(3 * 1024 * 1024, 'b'));
        else if (path.compare(0, 8, "/status/") == 0) {
            const int status = std::stoi(path.substr(8));
            const size_t q = path.find("?retry=");
            reply(status, "text/plain", "no", q == std::string::npos ? "" : "Retry-After: " + path.substr(q + 7) + "\r\n");
        } else reply(404, "text/plain", "unknown");
    }

    int listener_ = -1;
    int port_ = 0;
    std::atomic<bool> stop_{false};
    std::atomic<int> requests_{0};
    std::thread thread_;
};
