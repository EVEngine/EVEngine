#include "thread/Task.h"

#include "common/Exception.h"

namespace eve {
namespace thread {

Task::Task(std::function<void()> fn) : fn_(std::move(fn)) {
    if (!fn_)
        throw eve::Exception("Task function is null");
}

Task::~Task() = default;

std::string Task::getStatus() const {
    std::lock_guard<std::mutex> lock(mu_);
    return status_;
}

bool Task::isDone() const {
    std::lock_guard<std::mutex> lock(mu_);
    return status_ == "done" || status_ == "failed";
}

bool Task::hasFailed() const {
    std::lock_guard<std::mutex> lock(mu_);
    return status_ == "failed";
}

std::string Task::getError() const {
    std::lock_guard<std::mutex> lock(mu_);
    return error_;
}

void Task::wait() {
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait(lock, [this] { return status_ == "done" || status_ == "failed"; });
}

void Task::run() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        status_ = "running";
    }

    try {
        fn_();
        std::lock_guard<std::mutex> lock(mu_);
        status_ = "done";
    } catch (const eve::Exception &e) {
        std::lock_guard<std::mutex> lock(mu_);
        status_ = "failed";
        error_ = e.what();
    } catch (const std::exception &e) {
        std::lock_guard<std::mutex> lock(mu_);
        status_ = "failed";
        error_ = e.what();
    } catch (...) {
        std::lock_guard<std::mutex> lock(mu_);
        status_ = "failed";
        error_ = "unknown exception";
    }

    cv_.notify_all();
}

}  // namespace thread
}  // namespace eve
