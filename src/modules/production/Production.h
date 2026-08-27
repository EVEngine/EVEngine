#pragma once

#include "common/Module.h"
#include "common/BorrowedRef.h"
#include "common/Snapshot.h"
#include "common/Scheduling.h"
#include "common/SquirrelOwnership.h"
#include "common/Time.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace eve::production {

/** @brief Handle domain for a module-owned continuous-progress work queue. */
struct WorkQueueHandleTag {};
/** @brief Generation- and module-epoch-qualified production work-queue reference. */
using WorkQueueHandleRef = eve::script::RuntimeHandleRef<WorkQueueHandleTag>;

/** @brief Lifecycle state of a production task. */
enum class TaskState { Queued, Running, Paused, Completed, Cancelled, Failed };

/** @brief Kind of a deterministic production lifecycle event. */
enum class ProductionEventKind { Enqueued, Started, Paused, Resumed, Completed, Cancelled, Failed };

/** @brief A subject-agnostic continuous task retained for audit and save games. */
struct ProductionTask : eve::scheduling::ItemMetadata {
    std::string owner;
    std::string kind;
    std::string product;
    eve::Value context          = eve::Value(eve::Value::Object{});
    eve::Duration duration       = eve::Duration::zero();
    eve::Duration progress       = eve::Duration::zero();
    TaskState   state           = TaskState::Queued;
    uint64_t    enqueueSequence = 0;
};

/** @brief Deterministically sequenced production lifecycle event. */
struct ProductionEvent : eve::scheduling::EventMetadata {
    eve::SimulationTick tick     = eve::SimulationTick::zero();
    ProductionEventKind kind     = ProductionEventKind::Enqueued;
    std::string         taskId;
    std::string         owner;
    std::string         taskKind;
    std::string         product;
};

/** @brief Generic multi-owner, multi-slot continuous-progress work queue. */
class WorkQueue {
public:
    /** @brief Creates an empty queue with an optional persistent identity. */
    explicit WorkQueue(eve::PersistentId instanceId = {});
    /**
     * @brief Enqueues a task and returns its stable ID.
     * @param owner Logical owner of the work slots.
     * @param kind Domain task kind; the queue does not interpret it.
     * @param product Domain product identifier; the queue does not interpret it.
     * @param context Owning canonical payload for the task.
     * @param duration Positive work duration in seconds.
     * @param priority Higher priorities run first; enqueue order breaks ties.
     * @return A stable task ID, or a structured validation/allocation failure.
     */
    [[nodiscard]] eve::Result<std::string> enqueue(
        std::string_view owner, std::string_view kind, std::string_view product,
        eve::Value context, double duration, int priority = 0);
    /** @brief Pauses a queued or running task, or returns NotFound/Conflict. */
    [[nodiscard]] eve::Result<void> pause(std::string_view taskId);
    /** @brief Returns a paused task to deterministic scheduling. */
    [[nodiscard]] eve::Result<void> resume(std::string_view taskId);
    /** @brief Cancels a non-terminal task with a structured outcome. */
    [[nodiscard]] eve::Result<void> cancel(std::string_view taskId,
                                           std::string_view reason = "cancelled");
    /** @brief Marks a non-terminal task failed with a structured outcome. */
    [[nodiscard]] eve::Result<void> fail(std::string_view taskId,
                                         std::string_view reason = "failed");

    /**
     * @brief Applies one injected deterministic simulation step.
     * @param step Tick and fixed duration supplied by a SimulationClock.
     * @return Success, or a structured failure if the tick is not strictly newer
     *         or the step is invalid.
     * @remarks Owner-thread only. The queue never reads a wall clock.
     */
    [[nodiscard]] eve::Result<void> advance(const eve::SimulationStep& step);

    /** @brief Return the latest simulation tick applied to this queue. */
    [[nodiscard]] eve::SimulationTick currentTick() const noexcept { return tick_; }

    /** @brief Sets an owner's parallel slot count; zero prevents new tasks from running. */
    [[nodiscard]] eve::Result<void> setSlotCount(std::string_view owner, int slots);
    /** @brief Returns an owner's slot count, defaulting to one. */
    int slotCount(std::string_view owner) const;
    /** @brief Returns the number of currently running tasks for an owner. */
    int runningCount(std::string_view owner) const;

    /**
     * @brief Finds a retained task by stable ID.
     * @return An immediate borrowed reference, empty when the ID is absent.
     * @ownership WorkQueue owns the task; the reference is not owning.
     * @lifetime Valid until queue mutation, restore, clear, or destruction.
     * @thread Owner-thread only; no synchronization is provided.
     */
    [[nodiscard]] eve::OptionalRef<ProductionTask> find(std::string_view taskId);
    /** @brief Const overload of find with the same immediate-borrow lifetime. */
    [[nodiscard]] eve::OptionalRef<const ProductionTask> find(std::string_view taskId) const;
    /** @brief Returns retained task count in enqueue order. */
    int taskCount() const;
    /** @brief Returns a retained task by enqueue index, or an empty borrowed reference. */
    [[nodiscard]] eve::OptionalRef<ProductionTask> taskAt(int index);
    /** @brief Const overload of taskAt with the same immediate-borrow lifetime. */
    [[nodiscard]] eve::OptionalRef<const ProductionTask> taskAt(int index) const;
    /** @brief Returns retained task count for an owner. */
    int ownerTaskCount(std::string_view owner) const;
    /** @brief Returns an owner's retained task by enqueue index, or an empty borrowed reference. */
    [[nodiscard]] eve::OptionalRef<ProductionTask> ownerTaskAt(std::string_view owner, int index);
    /** @brief Const overload of ownerTaskAt with the same immediate-borrow lifetime. */
    [[nodiscard]] eve::OptionalRef<const ProductionTask> ownerTaskAt(
        std::string_view owner, int index) const;

    /** @brief Returns retained event count. */
    int eventCount() const;
    /** @brief Returns an event by sequence index, or an empty borrowed reference. */
    [[nodiscard]] eve::OptionalRef<ProductionEvent> eventAt(int index);
    /** @brief Const overload of eventAt with the same immediate-borrow lifetime. */
    [[nodiscard]] eve::OptionalRef<const ProductionEvent> eventAt(int index) const;
    /** @brief Clears retained events without resetting sequence numbering. */
    void clearEvents();
    /** @brief Serializes the complete queue as deterministic JSON. */
    [[nodiscard]] eve::Result<std::string> snapshot() const;
    /** @brief Transactionally restores a snapshot; failure preserves current state. */
    [[nodiscard]] eve::Result<void> restore(std::string_view json);
    /** @brief Clears tasks, owner settings, events, and stable counters. */
    void clear();

    /** @brief Captures the production payload in the common snapshot envelope. */
    [[nodiscard]] eve::Result<eve::SnapshotEnvelope> snapshot(
        const eve::SnapshotHashProvider& hashProvider) const;
    /**
     * @brief Restores a verified or migrated production envelope atomically.
     * @param snapshot Source envelope with schema `production:queue`.
     * @param hashProvider Explicit content-digest provider.
     * @return Success, or a failure leaving queue state unchanged.
     */
    [[nodiscard]] eve::Result<void> restoreSnapshot(
        const eve::SnapshotEnvelope& snapshot, const eve::SnapshotHashProvider& hashProvider);
    /** @brief Serializes the common production snapshot envelope. */
    [[nodiscard]] eve::Result<std::string> snapshotEnvelopeJson(
        const eve::SnapshotHashProvider& hashProvider) const;
    /** @brief Parses and transactionally restores a common production envelope. */
    [[nodiscard]] eve::Result<void> restoreSnapshotJson(
        std::string_view json, const eve::SnapshotHashProvider& hashProvider);

private:
    void schedule(std::string_view owner);
    void emit(ProductionEventKind kind, const ProductionTask& task, std::string_view reason = {});

    uint64_t                                    nextTaskId_          = 1;
    uint64_t                                    nextEnqueueSequence_ = 1;
    uint64_t                                    nextEventSequence_   = 1;
    eve::PersistentId                            instanceId_;
    eve::Revision                                revision_           = eve::Revision::zero();
    eve::SimulationTick                         tick_                = eve::SimulationTick::zero();
    std::deque<std::unique_ptr<ProductionTask>> tasks_;
    std::deque<ProductionEvent>                 events_;
    std::vector<std::pair<std::string, int>>    slots_;
};

/** @brief Returns the stable lowercase name of a task state. */
std::string_view taskStateName(TaskState state);
/** @brief Writes the stable task-state spelling to a stream. */
inline std::ostream& operator<<(std::ostream& stream, TaskState state) {
    return stream << taskStateName(state);
}
/** @brief Returns the stable lowercase name of an event kind. */
std::string_view eventKindName(ProductionEventKind kind);

/** @brief Script module factory for generic production queues. */
class Production : public Module {
public:
    Module_REG(Production);
    Production()           = default;
    ~Production() override = default;

    /**
     * @brief Allocates a production queue and returns its ownership reference.
     * @return A generation-qualified reference; the current Production module owns the queue.
     * @remarks The reference becomes stale after release, module unload, or reload.
     */
    [[nodiscard]] static eve::Result<WorkQueueHandleRef> newQueueHandle();
    /**
     * @brief Resolves a generation-qualified work-queue identity.
     * @return A borrowed reference on success, or StaleHandle/NotFound.
     * @ownership Production owns the queue; the returned reference does not.
     * @lifetime Valid only until queue mutation, release, module unload, or reload.
     */
    [[nodiscard]] static eve::ResultRef<WorkQueue> resolve(
        WorkQueueHandleRef reference);
    /** @brief Releases a module-owned queue. */
    [[nodiscard]] static eve::Result<void> release(WorkQueueHandleRef reference);
    /** @brief Reports whether a queue reference is stale for the current module. */
    [[nodiscard]] static bool isStale(WorkQueueHandleRef reference) noexcept;

private:
    eve::script::RuntimeObjectRegistry<WorkQueue, WorkQueueHandleTag> queues_;
};

}  // namespace eve::production
