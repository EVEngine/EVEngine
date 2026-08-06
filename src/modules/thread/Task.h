#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>

namespace eve {
namespace thread {

/**
 * A job executed by a ThreadPool worker.
 * Status strings (no enums): "pending" | "running" | "done" | "failed".
 */
class Task {
public:
    explicit Task(std::function<void()> fn);
    ~Task();

    std::string getStatus() const;
    bool isDone() const;
    bool hasFailed() const;
    std::string getError() const;

    /** Block until the task finishes (done or failed). */
    void wait();

    // Internal — called by ThreadPool workers.
    void run();

private:
    std::function<void()> fn_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::string status_ = "pending";
    std::string error_;
};

}  // namespace thread
}  // namespace eve
