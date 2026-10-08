#pragma once

#include "stats_json.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Sends the stats to the stats service on its own thread: every `interval`, what came in since becomes a batch
// (even an empty one: it doubles as a heartbeat with the number of players online), and the batches go out in
// order. A batch the service couldn't take (network down, 5xx) is kept and retried, with backoff; one it refused
// (4xx) is dropped. At most `maxBacklog` batches wait: past that the oldest go, so a long outage can't eat memory.
class StatsReporter {
public:
    StatsReporter(std::string url, std::string token,
                  std::chrono::milliseconds interval = std::chrono::seconds(1), std::size_t maxBacklog = 600);
    ~StatsReporter();
    StatsReporter(const StatsReporter&) = delete;
    StatsReporter& operator=(const StatsReporter&) = delete;

    void add(std::vector<StatEvent> events);
    void setOnline(int online) { online_ = online; }
    std::size_t backlog() const;  // batches waiting to go out

private:
    void run();
    // Posts one batch: true if it's done with (accepted, or refused and dropped).
    bool post(const std::string& body);

    const std::string url_;
    const std::string token_;
    const std::chrono::milliseconds interval_;
    const std::size_t maxBacklog_;
    std::atomic<int> online_{0};

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::vector<StatEvent> pending_;   // since the last batch
    std::deque<std::string> batches_;  // ready to send, oldest first
    bool stopping_ = false;
    std::thread thread_;  // last: started once everything above exists
};
