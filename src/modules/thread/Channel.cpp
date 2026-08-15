#include "thread/Channel.h"

#include <chrono>

namespace eve {
namespace thread {

Channel::Channel() : state_(std::make_shared<State>()) {}

Channel::Channel(std::string name) : state_(std::make_shared<State>(std::move(name))) {}

Channel::~Channel() = default;

std::string Channel::getName() const { return state_->name; }

void Channel::push(std::string value) {
    {
        std::lock_guard<std::mutex> lock(state_->mu);
        state_->queue.push(std::move(value));
    }
    state_->cv.notify_one();
}

std::string Channel::pop() {
    std::lock_guard<std::mutex> lock(state_->mu);
    if (state_->queue.empty())
        return {};
    std::string v = std::move(state_->queue.front());
    state_->queue.pop();
    return v;
}

std::string Channel::demand() {
    auto state = state_;
    std::unique_lock<std::mutex> lock(state->mu);
    state->cv.wait(lock, [&state] { return !state->queue.empty(); });
    std::string v = std::move(state->queue.front());
    state->queue.pop();
    return v;
}

std::string Channel::supply(int timeoutMs) {
    auto state = state_;
    std::unique_lock<std::mutex> lock(state->mu);
    if (timeoutMs < 0)
        timeoutMs = 0;
    if (!state->cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&state] { return !state->queue.empty(); }))
        return {};
    std::string v = std::move(state->queue.front());
    state->queue.pop();
    return v;
}

bool Channel::hasData() const {
    std::lock_guard<std::mutex> lock(state_->mu);
    return !state_->queue.empty();
}

int Channel::getCount() const {
    std::lock_guard<std::mutex> lock(state_->mu);
    return static_cast<int>(state_->queue.size());
}

void Channel::clear() {
    std::lock_guard<std::mutex> lock(state_->mu);
    while (!state_->queue.empty())
        state_->queue.pop();
}

}  // namespace thread
}  // namespace eve
