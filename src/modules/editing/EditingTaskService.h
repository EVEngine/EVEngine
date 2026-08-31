#pragma once

#include "editing/EditingProtocol.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace eve::editing {

/** @brief Observable lifecycle of a background editor task. */
enum class TaskState { Queued, Running, Succeeded, Failed, Cancelled };

/** @brief Immutable status returned to UI, scripts and automation clients. */
struct TaskSnapshot {
    TaskId                        id;
    std::string                   name;
    TaskState                     state    = TaskState::Queued;
    float                         progress = 0.0F;
    Value                         output;
    std::vector<Diagnostic>       diagnostics;
};

/** @brief Cooperative cancellation and progress API passed to worker code. */
class TaskContext {
public:
    /** @brief True after cancellation was requested. */
    bool isCancellationRequested() const;
    /** @brief Publish normalized progress in the inclusive range 0..1. */
    void reportProgress(float progress) const;

private:
    friend class TaskService;
    TaskContext(std::shared_ptr<std::atomic_bool> cancelled, std::function<void(float)> progress);
    std::shared_ptr<std::atomic_bool> cancelled_;
    std::function<void(float)>        progress_;
};

/** @brief Type-erased result produced by a background editor task. */
struct TaskOutcome {
    Status                  status = Status::Applied;
    Value                   output;
    std::vector<Diagnostic> diagnostics;
};

/**
 * @brief Single-worker background queue shared by editor domains.
 *
 * Worker functions receive only immutable captures. State publication is
 * synchronized and can be queried from the host/UI thread without callbacks
 * into game objects from the worker.
 */
class TaskService {
public:
    using Work = std::function<TaskOutcome(const TaskContext&)>;

    /** @brief Start the editor background worker. */
    TaskService();
    /** @brief Cooperatively cancel pending work and join the worker. */
    ~TaskService();
    TaskService(const TaskService&)            = delete;
    TaskService& operator=(const TaskService&) = delete;

    /** @brief Queue background work and return its stable task identity. */
    [[nodiscard]] Result<TaskId> submit(std::string name, Work work);
    /** @brief Request cooperative cancellation of queued or running work. */
    [[nodiscard]] Result<void> cancel(const TaskId& task);
    /** @brief Return a thread-safe task snapshot. */
    [[nodiscard]] Result<TaskSnapshot> snapshot(const TaskId& task) const;
    /** @brief Wait until no task is queued or running; intended for shutdown and tests. */
    [[nodiscard]] Result<void> waitIdle(std::chrono::milliseconds timeout);

private:
    struct TaskRecord;
    void workerLoop();

    mutable std::mutex                                                                  mutex_;
    std::condition_variable                                                             workReady_;
    std::condition_variable                                                             idle_;
    std::unordered_map<TaskId, std::shared_ptr<TaskRecord>, StrongIdHash<TaskId>> tasks_;
    std::deque<std::shared_ptr<TaskRecord>>                                             queue_;
    std::thread                                                                         worker_;
    bool                                                                                stopping_ = false;
    std::uint64_t                                                                       sequence_ = 0;
};

}  // namespace eve::editing
