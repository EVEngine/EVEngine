#include "network/NetWorker.h"
#include "network/Network.h"

#include <chrono>

namespace eve::network {

NetWorker::NetWorker(Network* owner) : owner_(owner) {}

NetWorker::~NetWorker() {
    stop();
}

void NetWorker::start() {
    if (running_) return;
    running_ = true;
    thread_  = std::thread([this] { threadMain(); });
}

void NetWorker::stop() {
    running_ = false;
    cv_.notify_all();
    // A job may request shutdown from the worker itself. Joining the current
    // thread would deadlock; leave it joinable for the owner/destructor.
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) thread_.join();
}

void NetWorker::post(NetCompletion c) {
    std::lock_guard<std::mutex> lock(mu_);
    completions_.push_back(std::move(c));
}

void NetWorker::drain(std::vector<NetCompletion>& out) {
    std::lock_guard<std::mutex> lock(mu_);
    out.swap(completions_);
}

void NetWorker::submit(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        jobs_.push_back(std::move(job));
    }
    cv_.notify_one();
}

void NetWorker::threadMain() {
    while (true) {
        std::vector<std::function<void()>> batch;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait_for(lock, std::chrono::milliseconds(5), [this] {
                return !running_ || !jobs_.empty();
            });
            if (!running_ && jobs_.empty()) break;
            batch.swap(jobs_);
        }
        for (auto& job : batch) {
            if (job) job();
        }
        if (owner_) owner_->pollSockets();
    }
}

}  // namespace eve::network
