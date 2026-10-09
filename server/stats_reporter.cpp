#include "stats_reporter.h"

#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXNetSystem.h>

#include <algorithm>
#include <cstdio>
#include <utility>

namespace {

constexpr auto kMaxBackoff = std::chrono::seconds(30);
constexpr int kTimeoutSeconds = 5;

}  // namespace

StatsReporter::StatsReporter(std::string url, std::string token, std::chrono::milliseconds interval,
                             std::size_t maxBacklog)
    : url_(std::move(url)), token_(std::move(token)), interval_(interval), maxBacklog_(maxBacklog) {
    ix::initNetSystem();
    thread_ = std::thread([this] { run(); });
}

StatsReporter::~StatsReporter() {
    {
        const std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    thread_.join();
}

void StatsReporter::add(std::vector<StatEvent> events) {
    const std::lock_guard lock(mutex_);
    pending_.insert(pending_.end(), std::make_move_iterator(events.begin()), std::make_move_iterator(events.end()));
}

void StatsReporter::setHealth(const ServerHealth& health) {
    const std::lock_guard lock(mutex_);
    health_ = health;
}

std::size_t StatsReporter::backlog() const {
    const std::lock_guard lock(mutex_);
    return batches_.size();
}

void StatsReporter::run() {
    auto backoff = interval_;
    auto nextBatch = std::chrono::steady_clock::now();
    while (true) {
        std::unique_lock lock(mutex_);
        wake_.wait_until(lock, nextBatch, [this] { return stopping_; });
        if (stopping_) return;
        // A new batch every interval, even an empty one: it's the heartbeat with the players online.
        if (std::chrono::steady_clock::now() >= nextBatch) {
            const auto at = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
            batches_.push_back(toJson(online_, std::exchange(pending_, {}), at, health_, batches_.size()));
            while (batches_.size() > maxBacklog_) {
                batches_.pop_front();
                std::printf("Stats: the stats service is unreachable, dropped the oldest batch\n");
            }
            nextBatch = std::chrono::steady_clock::now() + interval_;
        }
        // Send in order; stop at the first one that has to be retried.
        bool failed = false;
        while (!batches_.empty() && !stopping_) {
            const std::string body = batches_.front();
            lock.unlock();
            const bool done = post(body);
            lock.lock();
            if (!done) {
                failed = true;
                break;
            }
            if (!batches_.empty() && batches_.front() == body) batches_.pop_front();
        }
        if (failed) {
            nextBatch = std::max(nextBatch, std::chrono::steady_clock::now() + backoff);
            backoff = std::min<std::chrono::milliseconds>(backoff * 2, kMaxBackoff);
        } else {
            backoff = interval_;
        }
    }
}

bool StatsReporter::post(const std::string& body) {
    ix::HttpClient client;
    auto args = client.createRequest(url_, ix::HttpClient::kPost);
    args->extraHeaders["Authorization"] = "Bearer " + token_;
    args->extraHeaders["Content-Type"] = "application/json";
    args->connectTimeout = kTimeoutSeconds;
    args->transferTimeout = kTimeoutSeconds;
    args->compress = false;
    const ix::HttpResponsePtr response = client.post(url_, body, args);
    const int status = response ? response->statusCode : 0;
    if (status >= 200 && status < 300) return true;
    if (status >= 400 && status < 500) {
        std::printf("Stats: the stats service refused a batch (%d), dropped it\n", status);
        return true;
    }
    return false;  // unreachable or 5xx: retry later
}
