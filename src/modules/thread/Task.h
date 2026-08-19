#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace eve {
namespace thread {

/**
 * @brief A job executed by a ThreadPool worker.
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

    /** @brief Block until the task finishes (done or failed). */
    void wait();

    // Internal — called by ThreadPool workers.
    void run();

private:
    struct State {
        explicit State(std::function<void()> taskFn) : fn(std::move(taskFn)) {}

        std::function<void()> fn;
        mutable std::mutex mu;
        std::condition_variable cv;
        std::string status = "pending";
        std::string error;
    };

    explicit Task(std::shared_ptr<State> state);
    static void run(const std::shared_ptr<State> &state);

    std::shared_ptr<State> state_;

    friend class ThreadPool;
};

}  // namespace thread
}  // namespace eve
