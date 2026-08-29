#include "editor/EditorTaskService.h"

#include <algorithm>
#include <exception>

namespace eve::editor {

struct EditorTaskService::TaskRecord {
    EditorTaskSnapshot                snapshot;
    Work                              work;
    std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
};

EditorTaskContext::EditorTaskContext(std::shared_ptr<std::atomic_bool> cancelled, std::function<void(float)> progress)
    : cancelled_(std::move(cancelled)), progress_(std::move(progress)) {}

bool EditorTaskContext::cancelled() const { return cancelled_ && cancelled_->load(std::memory_order_relaxed); }

void EditorTaskContext::reportProgress(float progress) const {
    if (progress_) progress_(std::clamp(progress, 0.0F, 1.0F));
}

EditorTaskService::EditorTaskService() : worker_([this] { workerLoop(); }) {}

EditorTaskService::~EditorTaskService() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        for (auto& [id, task] : tasks_) {
            (void)id;
            task->cancelled->store(true, std::memory_order_relaxed);
        }
    }
    workReady_.notify_all();
    if (worker_.joinable()) worker_.join();
}

EditorResult<TaskId> EditorTaskService::submit(std::string name, Work work) {
    if (name.empty() || !work)
        return EditorResult<TaskId>::error(EditorStatus::Rejected, RuleId("editor.task.invalid"),
                                           "Task name and worker are required");
    auto task           = std::make_shared<TaskRecord>();
    task->snapshot.name = std::move(name);
    task->work          = std::move(work);
    TaskId id;
    {
        std::lock_guard lock(mutex_);
        if (stopping_)
            return EditorResult<TaskId>::error(EditorStatus::Rejected, RuleId("editor.task.stopping"),
                                               "Task service is stopping");
        task->snapshot.id = TaskId("editor.task." + std::to_string(++sequence_));
        id                = task->snapshot.id;
        tasks_.emplace(id, task);
        queue_.push_back(std::move(task));
    }
    workReady_.notify_one();
    return EditorResult<TaskId>::applied(id);
}

EditorResult<void> EditorTaskService::cancel(const TaskId& task) {
    std::lock_guard lock(mutex_);
    auto            found = tasks_.find(task);
    if (found == tasks_.end())
        return EditorResult<void>::error(EditorStatus::NotFound, RuleId("editor.task.not-found"),
                                         "Task does not exist");
    auto& record = *found->second;
    if (record.snapshot.state == EditorTaskState::Succeeded || record.snapshot.state == EditorTaskState::Failed ||
        record.snapshot.state == EditorTaskState::Cancelled)
        return EditorResult<void>::error(EditorStatus::Conflict, RuleId("editor.task.already-finished"),
                                         "Finished tasks cannot be cancelled");
    record.cancelled->store(true, std::memory_order_relaxed);
    if (record.snapshot.state == EditorTaskState::Queued) record.snapshot.state = EditorTaskState::Cancelled;
    workReady_.notify_all();
    return EditorResult<void>::applied();
}

EditorResult<EditorTaskSnapshot> EditorTaskService::snapshot(const TaskId& task) const {
    std::lock_guard lock(mutex_);
    auto            found = tasks_.find(task);
    if (found == tasks_.end())
        return EditorResult<EditorTaskSnapshot>::error(EditorStatus::NotFound, RuleId("editor.task.not-found"),
                                                       "Task does not exist");
    return EditorResult<EditorTaskSnapshot>::applied(found->second->snapshot);
}

bool EditorTaskService::waitIdle(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    return idle_.wait_for(lock, timeout, [this] {
        if (!queue_.empty()) return false;
        return std::none_of(tasks_.begin(), tasks_.end(),
                            [](const auto& entry) { return entry.second->snapshot.state == EditorTaskState::Running; });
    });
}

void EditorTaskService::workerLoop() {
    while (true) {
        std::shared_ptr<TaskRecord> task;
        {
            std::unique_lock lock(mutex_);
            workReady_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) return;
            task = queue_.front();
            queue_.pop_front();
            if (task->cancelled->load(std::memory_order_relaxed)) {
                task->snapshot.state = EditorTaskState::Cancelled;
                idle_.notify_all();
                continue;
            }
            task->snapshot.state = EditorTaskState::Running;
        }

        EditorTaskOutcome outcome;
        try {
            EditorTaskContext context(task->cancelled, [this, weak = std::weak_ptr<TaskRecord>(task)](float value) {
                if (auto record = weak.lock()) {
                    std::lock_guard lock(mutex_);
                    record->snapshot.progress = value;
                }
            });
            outcome = task->work(context);
        } catch (const std::exception& error) {
            outcome.status = EditorStatus::Failed;
            outcome.diagnostics.push_back({RuleId("editor.task.exception"), DiagnosticSeverity::Error, error.what()});
        } catch (...) {
            outcome.status = EditorStatus::Failed;
            outcome.diagnostics.push_back(
                {RuleId("editor.task.exception"), DiagnosticSeverity::Error, "Unknown worker exception"});
        }

        {
            std::lock_guard lock(mutex_);
            task->snapshot.output      = std::move(outcome.output);
            task->snapshot.diagnostics = std::move(outcome.diagnostics);
            task->snapshot.progress    = 1.0F;
            if (task->cancelled->load(std::memory_order_relaxed) || outcome.status == EditorStatus::Cancelled)
                task->snapshot.state = EditorTaskState::Cancelled;
            else if (outcome.status == EditorStatus::Applied || outcome.status == EditorStatus::NoOp)
                task->snapshot.state = EditorTaskState::Succeeded;
            else
                task->snapshot.state = EditorTaskState::Failed;
            task->work = {};
        }
        idle_.notify_all();
    }
}

}  // namespace eve::editor
