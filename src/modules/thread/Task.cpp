#include "thread/Task.h"

#include "common/Exception.h"

namespace eve {
namespace thread {

Task::Task(std::function<void()> fn) {
    if (!fn)
        throw eve::Exception("Task function is null");
    state_ = std::make_shared<State>(std::move(fn));
}

Task::Task(std::shared_ptr<State> state) : state_(std::move(state)) {}

Task::~Task() = default;

std::string Task::getStatus() const {
    std::lock_guard<std::mutex> lock(state_->mu);
    return state_->status;
}

bool Task::isDone() const {
    std::lock_guard<std::mutex> lock(state_->mu);
    return state_->status == "done" || state_->status == "failed";
}

bool Task::hasFailed() const {
    std::lock_guard<std::mutex> lock(state_->mu);
    return state_->status == "failed";
}

std::string Task::getError() const {
    std::lock_guard<std::mutex> lock(state_->mu);
    return state_->error;
}

void Task::wait() {
    auto state = state_;
    std::unique_lock<std::mutex> lock(state->mu);
    state->cv.wait(lock, [&state] { return state->status == "done" || state->status == "failed"; });
}

void Task::run() {
    run(state_);
}

void Task::run(const std::shared_ptr<State> &state) {
    {
        std::lock_guard<std::mutex> lock(state->mu);
        state->status = "running";
    }

    try {
        state->fn();
        std::lock_guard<std::mutex> lock(state->mu);
        state->status = "done";
    } catch (const eve::Exception &e) {
        std::lock_guard<std::mutex> lock(state->mu);
        state->status = "failed";
        state->error = e.what();
    } catch (const std::exception &e) {
        std::lock_guard<std::mutex> lock(state->mu);
        state->status = "failed";
        state->error = e.what();
    } catch (...) {
        std::lock_guard<std::mutex> lock(state->mu);
        state->status = "failed";
        state->error = "unknown exception";
    }

    state->cv.notify_all();
}

}  // namespace thread
}  // namespace eve
