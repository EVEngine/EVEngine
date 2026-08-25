#pragma once

#include "editor/EditorResult.h"
#include "editor/EditorValue.h"

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

namespace eve::editor {

/** @brief Observable lifecycle of a background editor task. */
enum class EditorTaskState { Queued, Running, Succeeded, Failed, Cancelled };

/** @brief Immutable status returned to UI, scripts and automation clients. */
struct EditorTaskSnapshot {
    TaskId                        id;
    std::string                   name;
    EditorTaskState               state    = EditorTaskState::Queued;
    float                         progress = 0.0F;
    EditorValue                   output;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Cooperative cancellation and progress API passed to worker code. */
class EditorTaskContext {
public:
    /** @brief True after cancellation was requested. */
    bool cancelled() const;
    /** @brief Publish normalized progress in the inclusive range 0..1. */
    void reportProgress(float progress) const;

private:
    friend class EditorTaskService;
    EditorTaskContext(std::shared_ptr<std::atomic_bool> cancelled, std::function<void(float)> progress);
    std::shared_ptr<std::atomic_bool> cancelled_;
    std::function<void(float)>        progress_;
};

/** @brief Type-erased result produced by a background editor task. */
struct EditorTaskOutcome {
    EditorStatus                  status = EditorStatus::Applied;
    EditorValue                   output;
    std::vector<EditorDiagnostic> diagnostics;
};

/**
 * @brief Single-worker background queue shared by editor domains.
 *
 * Worker functions receive only immutable captures. State publication is
 * synchronized and can be queried from the host/UI thread without callbacks
 * into game objects from the worker.
 */
class EditorTaskService {
public:
    using Work = std::function<EditorTaskOutcome(const EditorTaskContext&)>;

    /** @brief Start the editor background worker. */
    EditorTaskService();
    /** @brief Cooperatively cancel pending work and join the worker. */
    ~EditorTaskService();
    EditorTaskService(const EditorTaskService&)            = delete;
    EditorTaskService& operator=(const EditorTaskService&) = delete;

    /** @brief Queue background work and return its stable task identity. */
    EditorResult<TaskId> submit(std::string name, Work work);
    /** @brief Request cooperative cancellation of queued or running work. */
    EditorResult<void> cancel(const TaskId& task);
    /** @brief Return a thread-safe task snapshot. */
    EditorResult<EditorTaskSnapshot> snapshot(const TaskId& task) const;
    /** @brief Wait until no task is queued or running; intended for shutdown and tests. */
    bool waitIdle(std::chrono::milliseconds timeout);

private:
    struct TaskRecord;
    void workerLoop();

    mutable std::mutex                                                                  mutex_;
    std::condition_variable                                                             workReady_;
    std::condition_variable                                                             idle_;
    std::unordered_map<TaskId, std::shared_ptr<TaskRecord>, StrongEditorIdHash<TaskId>> tasks_;
    std::deque<std::shared_ptr<TaskRecord>>                                             queue_;
    std::thread                                                                         worker_;
    bool                                                                                stopping_ = false;
    std::uint64_t                                                                       sequence_ = 0;
};

}  // namespace eve::editor
