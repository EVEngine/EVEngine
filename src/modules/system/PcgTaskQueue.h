#pragma once

#include "common/Export.h"
#include "common/Result.h"
#include <cstdint>
#include <vector>
namespace ssq { class Table; }
namespace eve::system {
/** @brief Observable state of Pcg's delayed task processor. */
enum class PcgTaskQueueStatus { Idle = 0, Waiting = 1, Ready = 2 };
/**
 * @brief Deterministic, callback-free port of PcgTask's delayed task queue.
 * @details tick() only publishes a ready task ID. The caller executes work outside the queue and calls resolveReady().
 * @thread Owning thread only; no pointers or callbacks are retained.
 */
class EVENGINE_API_FOUNDATION PcgTaskQueue {
public:
    /** @brief Append a task with Pcg's default 0.25-second delay unless overridden. */
    [[nodiscard]] Result<std::uint64_t> add(double waitSeconds = 0.25);
    /** @brief Advance injected time and publish at most one due task. */
    [[nodiscard]] Result<PcgTaskQueueStatus> tick(double deltaSeconds);
    /** @brief Resolve the ready task; finished tasks are removed, unfinished tasks await their next pass. */
    [[nodiscard]] Result<PcgTaskQueueStatus> resolveReady(bool finished);
    /** @brief Cancel every queued task and return Idle. */
    [[nodiscard]] PcgTaskQueueStatus cancelAll() noexcept;
    /** @brief Return the ready task ID, or zero when none is ready. */
    [[nodiscard]] std::uint64_t readyTaskId() const noexcept { return readyId_; }
    /** @brief Return the number of queued tasks. */
    [[nodiscard]] int queueSize() const noexcept { return static_cast<int>(tasks_.size()); }
    /** @brief Return current queue state. */
    [[nodiscard]] PcgTaskQueueStatus status() const noexcept { return status_; }
private:
    struct Task { std::uint64_t id; double wait; double remaining; };
    std::vector<Task> tasks_;
    std::size_t cursor_ = 0;
    std::uint64_t nextId_ = 1, readyId_ = 0;
    PcgTaskQueueStatus status_ = PcgTaskQueueStatus::Idle;
};
/** @brief Register PcgTaskQueue Squirrel bindings. */
void exposePcgTaskQueueBindings(ssq::Table& table);
}  // namespace eve::system
