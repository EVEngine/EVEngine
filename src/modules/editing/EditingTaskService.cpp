#include "editing/EditingTaskService.h"

#include <algorithm>
#include <exception>

namespace eve::editing {

struct TaskService::TaskRecord {
    TaskSnapshot                      snapshot;
    Work                              work;
    std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
};

TaskContext::TaskContext(std::shared_ptr<std::atomic_bool> cancelled, std::function<void(float)> progress)
    : cancelled_(std::move(cancelled)), progress_(std::move(progress)) {}

bool TaskContext::isCancellationRequested() const {
    return cancelled_ && cancelled_->load(std::memory_order_relaxed);
}

void TaskContext::reportProgress(float progress) const {
    if (progress_) progress_(std::clamp(progress, 0.0F, 1.0F));
}

TaskService::TaskService() : worker_([this] { workerLoop(); }) {}

TaskService::~TaskService() {
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

Result<TaskId> TaskService::submit(std::string name, Work work) {
    if (name.empty() || !work)
        return Result<TaskId>::error(Status::Rejected, RuleId("editing.task.invalid"),
                                           "Task name and worker are required");
    auto task           = std::make_shared<TaskRecord>();
    task->snapshot.name = std::move(name);
    task->work          = std::move(work);
    TaskId id;
    {
        std::lock_guard lock(mutex_);
        if (stopping_)
            return Result<TaskId>::error(Status::Rejected, RuleId("editing.task.stopping"),
                                               "Task service is stopping");
        task->snapshot.id = TaskId("editor.task." + std::to_string(++sequence_));
        id                = task->snapshot.id;
        tasks_.emplace(id, task);
        queue_.push_back(std::move(task));
    }
    workReady_.notify_one();
    return Result<TaskId>::applied(id);
}

Result<void> TaskService::cancel(const TaskId& task) {
    std::lock_guard lock(mutex_);
    auto            found = tasks_.find(task);
    if (found == tasks_.end())
        return Result<void>::error(Status::NotFound, RuleId("editing.task.not-found"),
                                         "Task does not exist");
    auto& record = *found->second;
    if (record.snapshot.state == TaskState::Succeeded || record.snapshot.state == TaskState::Failed ||
        record.snapshot.state == TaskState::Cancelled)
        return Result<void>::error(Status::Conflict, RuleId("editing.task.already-finished"),
                                         "Finished tasks cannot be cancelled");
    record.cancelled->store(true, std::memory_order_relaxed);
    if (record.snapshot.state == TaskState::Queued) record.snapshot.state = TaskState::Cancelled;
    workReady_.notify_all();
    return Result<void>::applied();
}

Result<TaskSnapshot> TaskService::snapshot(const TaskId& task) const {
    std::lock_guard lock(mutex_);
    auto            found = tasks_.find(task);
    if (found == tasks_.end())
        return Result<TaskSnapshot>::error(Status::NotFound, RuleId("editing.task.not-found"),
                                                       "Task does not exist");
    return Result<TaskSnapshot>::applied(found->second->snapshot);
}

Result<void> TaskService::waitIdle(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    const bool idle = idle_.wait_for(lock, timeout, [this] {
        if (!queue_.empty()) return false;
        return std::none_of(tasks_.begin(), tasks_.end(),
                            [](const auto& entry) { return entry.second->snapshot.state == TaskState::Running; });
    });
    if (!idle)
        return Result<void>::error(Status::Pending, RuleId("editing.task.wait-timeout"),
                                   "Task service did not become idle before the deadline");
    return Result<void>::applied();
}

void TaskService::workerLoop() {
    while (true) {
        std::shared_ptr<TaskRecord> task;
        {
            std::unique_lock lock(mutex_);
            workReady_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) return;
            task = queue_.front();
            queue_.pop_front();
            if (task->cancelled->load(std::memory_order_relaxed)) {
                task->snapshot.state = TaskState::Cancelled;
                idle_.notify_all();
                continue;
            }
            task->snapshot.state = TaskState::Running;
        }

        TaskOutcome outcome;
        try {
            TaskContext context(task->cancelled, [this, weak = std::weak_ptr<TaskRecord>(task)](float value) {
                if (auto record = weak.lock()) {
                    std::lock_guard lock(mutex_);
                    record->snapshot.progress = value;
                }
            });
            outcome = task->work(context);
        } catch (const std::exception& error) {
            outcome.status = Status::Failed;
            outcome.diagnostics.push_back({RuleId("editor.task.exception"), DiagnosticSeverity::Error, error.what()});
        } catch (...) {
            outcome.status = Status::Failed;
            outcome.diagnostics.push_back(
                {RuleId("editor.task.exception"), DiagnosticSeverity::Error, "Unknown worker exception"});
        }

        {
            std::lock_guard lock(mutex_);
            task->snapshot.output      = std::move(outcome.output);
            task->snapshot.diagnostics = std::move(outcome.diagnostics);
            task->snapshot.progress    = 1.0F;
            if (task->cancelled->load(std::memory_order_relaxed) || outcome.status == Status::Cancelled)
                task->snapshot.state = TaskState::Cancelled;
            else if (outcome.status == Status::Applied || outcome.status == Status::NoOp)
                task->snapshot.state = TaskState::Succeeded;
            else
                task->snapshot.state = TaskState::Failed;
            task->work = {};
        }
        idle_.notify_all();
    }
}

}  // namespace eve::editing
