// RadarWorker: gets radar pictures without ever holding up the screen. A thread of its own
// fetches a picture, reads the PNG and turns it into a RadarFrame; the main loop asks for a
// picture with request() and, frame after frame, looks with take() whether it has arrived.
// Neither call ever waits.
//
// The two threads share exactly one atomic variable, the state, and take turns owning the
// request and the result by passing the state back and forth:
//
//   Idle       main owns everything. request() writes the request, then sets Requested.
//   Requested  the worker owns it. It sets Working and goes to the web.
//   Working    ... fetching, reading, converting.
//   Ready      the worker is done, main owns the result. take() moves it out and sets Idle.
//   Stop       main is shutting down; the worker gives up (even in the middle of a download).
//
// Only the owner touches the request and result, so they need no locks; the state is stored
// with release and read with acquire, which is what hands the data over. Only the main thread
// moves the state out of Idle or Ready or to Stop, and only the worker moves it out of
// Requested or Working, with compare-and-swap so that a Stop is never overwritten.
//
// The worker's Fetcher follows the request policy (pacing, backing off, an hourly limit), so
// a result may say that nothing was asked yet: the service is being left in peace.
#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "fetch.h"
#include "radar_frame.h"

namespace vtw {

struct RadarRequest {
    std::string url;
    GeoBox box;               // the area the picture will show (what the url asks for)
};

struct RadarResult {
    bool ok = false;          // frame holds the picture
    bool asked = false;       // a request really went out to the service
    std::string error;        // why not, in words
    double retry_in = 0;      // after a failure or a refusal: seconds until another try is allowed
    GeoBox box;               // the area that was asked for
    RadarFrame frame;
};

class RadarWorker {
public:
    explicit RadarWorker(FetchSettings settings);      // starts the thread
    ~RadarWorker();                                    // stops it, even mid-download, and waits
    RadarWorker(const RadarWorker&) = delete;
    RadarWorker& operator=(const RadarWorker&) = delete;

    bool can_request() const;                // nothing in progress, nothing waiting to be taken
    bool request(RadarRequest request);      // false if it cannot be taken now
    bool take(RadarResult& result);          // true if a result was waiting; it is moved out

private:
    enum State { Idle, Requested, Working, Ready, Stop };

    void run();
    void do_request(Fetcher& fetcher);

    std::atomic<int> state_{Idle};           // the one shared variable
    RadarRequest request_;                   // owned by whoever the state says
    RadarResult result_;
    FetchSettings settings_;
    std::thread thread_;                     // last: it starts in the constructor
};

}  // namespace vtw
