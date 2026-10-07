#pragma once
#include "common/Export.h"


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
class EVENGINE_API_FOUNDATION Task {
public:
    /** @brief Task. */
    explicit Task(std::function<void()> fn);
    /** @brief Task. */
    ~Task();

    /** @brief Returns the status. */
    std::string getStatus() const;
    /** @brief True when done. */
    bool isDone() const;
    /** @brief True when failed. */
    bool hasFailed() const;
    /** @brief Returns the error. */
    std::string getError() const;

    /** @brief Block until the task finishes (done or failed). */
    void wait();

    // Internal — called by ThreadPool workers.
    /** @brief Run. */
    void run();

private:
    struct State {
        /** @brief Constructs a State. */
        explicit State(std::function<void()> taskFn) : fn(std::move(taskFn)) {}

        /** @brief Void. */
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
