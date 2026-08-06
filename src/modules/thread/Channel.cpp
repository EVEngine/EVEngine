#include "thread/Channel.h"

#include <chrono>

namespace eve {
namespace thread {

Channel::Channel(std::string name) : name_(std::move(name)) {}

Channel::~Channel() = default;

std::string Channel::getName() const { return name_; }

void Channel::push(std::string value) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        queue_.push(std::move(value));
    }
    cv_.notify_one();
}

std::string Channel::pop() {
    std::lock_guard<std::mutex> lock(mu_);
    if (queue_.empty())
        return {};
    std::string v = std::move(queue_.front());
    queue_.pop();
    return v;
}

std::string Channel::demand() {
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait(lock, [this] { return !queue_.empty(); });
    std::string v = std::move(queue_.front());
    queue_.pop();
    return v;
}

std::string Channel::supply(int timeoutMs) {
    std::unique_lock<std::mutex> lock(mu_);
    if (timeoutMs < 0)
        timeoutMs = 0;
    if (!cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return !queue_.empty(); }))
        return {};
    std::string v = std::move(queue_.front());
    queue_.pop();
    return v;
}

bool Channel::hasData() const {
    std::lock_guard<std::mutex> lock(mu_);
    return !queue_.empty();
}

int Channel::getCount() const {
    std::lock_guard<std::mutex> lock(mu_);
    return static_cast<int>(queue_.size());
}

void Channel::clear() {
    std::lock_guard<std::mutex> lock(mu_);
    while (!queue_.empty())
        queue_.pop();
}

}  // namespace thread
}  // namespace eve
