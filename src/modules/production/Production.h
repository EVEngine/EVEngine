#pragma once

#include "common/BorrowedRef.h"
#include "common/Module.h"
#include "common/Scheduling.h"
#include "common/Snapshot.h"
#include "common/SquirrelOwnership.h"
#include "common/Time.h"
#include "common/definitions/DefinitionRuntime.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace eve::production {

/** @brief Handle domain for a module-owned continuous-progress work queue. */
struct WorkQueueHandleTag {};
/** @brief Generation- and module-epoch-qualified production work-queue reference. */
using WorkQueueHandleRef = eve::script::RuntimeHandleRef<WorkQueueHandleTag>;

/** @brief Lifecycle state of a production task. */
enum class TaskState { Queued, Running, Paused, ReadyToSettle, SettlementFailed, Completed, Cancelled, Failed };

/** @brief Kind of a deterministic production lifecycle event. */
enum class ProductionEventKind {
    Enqueued,
    Started,
    Paused,
    Resumed,
    ReadyToSettle,
    SettlementFailed,
    Completed,
    Cancelled,
    Failed
};

/** @brief Owning proof that a domain consumer published one task exactly once. */
struct ProductionSettlementReceipt {
    std::string settlementId;
    eve::Value payload = eve::Value(eve::Value::Object{});
};

/** @brief Completion rule for prerequisite production tasks. */
enum class TaskDependencyMode { AllOf, AnyOf };

/**
 * @brief Stable task-to-task constraints for a small production DAG.
 * @remarks Prerequisites must already exist when a task is enqueued. `blocks`
 * prevents concurrent execution while any named task remains non-terminal.
 */
struct ProductionDependencies {
    TaskDependencyMode       mode = TaskDependencyMode::AllOf;
    std::vector<std::string> prerequisites;
    std::vector<std::string> blocks;
    std::vector<eve::definition::DefinitionHandle> requiredDefinitions;
    std::vector<std::string> requiredTags;
};

/** @brief Named capacity consumed while a task is running. */
struct ProductionResourceRequirement {
    std::string resource;
    int         units = 1;
};

/** @brief Reservation refund rule applied when work terminates before settlement. */
enum class RefundPolicy { None, Full, Proportional };

/** @brief Lifecycle of opaque domain reservation evidence. */
enum class ReservationState { None, Reserved, Started, Consumed, Released };

/** @brief Idempotent release evidence used by a domain adapter to perform rollback/refund. */
struct ProductionReservationRelease {
    std::string   releaseId;
    eve::Value    reservation = eve::Value(eve::Value::Object{});
    std::uint32_t refundPermille = 0;
};

/** @brief Stable owner-local queue ordering strategy. */
enum class SchedulerStrategy { Priority, Fifo, ShortestRemaining };

/** @brief Replayable provenance for domain-owned random output draws. */
struct OutputRandomProvenance {
    std::string   stream;
    std::uint64_t seed      = 0;
    std::uint64_t drawStart = 0;
    std::uint32_t drawCount = 0;
};

/** @brief Repetition and externally reported maintain-stock contract. */
struct ProductionRepeatPolicy {
    std::uint32_t totalCycles = 1;
    bool          continuous = false;
    int           maintainStockTarget = -1;
    int           observedStock = 0;
};

/** @brief Side-effect-free estimate under an unchanged queue configuration. */
struct ProductionPrediction {
    std::string   blockReason;
    eve::Duration ownWorkRemaining = eve::Duration::zero();
    eve::Duration estimatedStartAfter = eve::Duration::zero();
    eve::Duration estimatedCompletionAfter = eve::Duration::zero();
    bool          exact = false;
};

/** @brief Structured task diagnostic carrying stable correlation identity. */
struct ProductionDiagnostic {
    std::string correlationId;
    std::string taskId;
    std::string code;
    std::string message;
    eve::SimulationTick tick = eve::SimulationTick::zero();
};

/** @brief Explicit cancellation and failure refund contract. */
struct ProductionTerminationPolicy {
    RefundPolicy cancellation = RefundPolicy::Full;
    RefundPolicy failure      = RefundPolicy::Full;
};

/** @brief Deterministic externally supplied work contribution. */
struct WorkContribution {
    std::string   contributor;
    eve::Duration effort = eve::Duration::zero();
    std::uint32_t efficiencyPermille = 1000;
};

/** @brief Validated input used to create a domain-neutral production task. */
struct ProductionRequest {
    std::string                       owner;
    std::string                       kind;
    std::string                       product;
    eve::Value                        context = eve::Value(eve::Value::Object{});
    eve::Duration                     duration = eve::Duration::zero();
    int                               priority = 0;
    eve::definition::DefinitionHandle definition;
    eve::Value                        reservation = eve::Value(eve::Value::Object{});
    ProductionDependencies            dependencies;
    std::vector<ProductionResourceRequirement> resources;
    ProductionTerminationPolicy       termination;
    OutputRandomProvenance             random;
    ProductionRepeatPolicy             repeat;
    std::string                        correlationId;
    std::uint32_t                     batchSize = 1;
    std::uint32_t                     efficiencyPermille = 1000;
    bool                              settlementRequired = true;
};

/** @brief A subject-agnostic continuous task retained for audit and save games. */
struct ProductionTask : eve::scheduling::ItemMetadata {
    std::string owner;
    std::string kind;
    std::string product;
    eve::Value    context         = eve::Value(eve::Value::Object{});
    eve::Duration duration        = eve::Duration::zero();
    eve::Duration progress        = eve::Duration::zero();
    TaskState   state           = TaskState::Queued;
    uint64_t      enqueueSequence = 0;
    eve::definition::DefinitionHandle definition;
    eve::Value                        reservation = eve::Value(eve::Value::Object{});
    ReservationState                  reservationState = ReservationState::None;
    ProductionReservationRelease      reservationRelease;
    ProductionSettlementReceipt       settlement;
    std::string                       lastSettlementId;
    ProductionDependencies            dependencies;
    std::vector<ProductionResourceRequirement> resources;
    ProductionTerminationPolicy       termination;
    OutputRandomProvenance             random;
    ProductionRepeatPolicy             repeat;
    std::string                        correlationId;
    std::uint32_t                     completedCycles = 0;
    std::uint32_t                     batchSize = 1;
    std::uint32_t                     efficiencyPermille = 1000;
    std::uint32_t                     refundPermille = 0;
    bool                              settlementRequired = false;
};

/** @brief Deterministically sequenced production lifecycle event. */
struct ProductionEvent : eve::scheduling::EventMetadata {
    eve::SimulationTick tick     = eve::SimulationTick::zero();
    ProductionEventKind kind     = ProductionEventKind::Enqueued;
    std::string         taskId;
    std::string         owner;
    std::string         taskKind;
    std::string         product;
    std::string         correlationId;
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
    [[nodiscard]] eve::Result<std::string> enqueue(std::string_view owner, std::string_view kind,
                                                   std::string_view product, eve::Value context, double duration,
                                                   int priority = 0);
    /** @brief Enqueues a validated request with optional pinned definition and reservation evidence. */
    [[nodiscard]] eve::Result<std::string> enqueue(ProductionRequest request);
    /** @brief Pauses a queued or running task, or returns NotFound/Conflict. */
    [[nodiscard]] eve::Result<void> pause(std::string_view taskId);
    /** @brief Returns a paused task to deterministic scheduling. */
    [[nodiscard]] eve::Result<void> resume(std::string_view taskId);
    /** @brief Cancels a non-terminal task with a structured outcome. */
    [[nodiscard]] eve::Result<void> cancel(std::string_view taskId, std::string_view reason = "cancelled");
    /** @brief Marks a non-terminal task failed with a structured outcome. */
    [[nodiscard]] eve::Result<void> fail(std::string_view taskId, std::string_view reason = "failed");
    /** @brief Atomically marks a ready task settled and retains its idempotency receipt. */
    [[nodiscard]] eve::Result<void> settle(std::string_view taskId, ProductionSettlementReceipt receipt);
    /** @brief Records a retryable domain settlement failure without losing completed work. */
    [[nodiscard]] eve::Result<void> failSettlement(std::string_view taskId, std::string_view reason);
    /** @brief Returns a failed settlement to the ready state for an explicit retry. */
    [[nodiscard]] eve::Result<void> retrySettlement(std::string_view taskId);
    /** @brief Idempotently releases cancelled/failed reservation evidence for domain rollback. */
    [[nodiscard]] eve::Result<ProductionReservationRelease> releaseReservation(
        std::string_view taskId, std::string_view releaseId);
    /** @brief Applies deterministic external work after efficiency scaling. */
    [[nodiscard]] eve::Result<void> contribute(std::string_view taskId, WorkContribution contribution);
    /** @brief Reports current domain stock for a maintain-stock task and reschedules deterministically. */
    [[nodiscard]] eve::Result<void> reportStock(std::string_view taskId, int observedStock);

    /**
     * @brief Applies one injected deterministic simulation step.
     * @param step Tick and fixed duration supplied by a SimulationClock.
     * @return Success, or a structured failure if the tick is not strictly newer
     *         or the step is invalid.
     * @remarks Owner-thread only. The queue never reads a wall clock.
     */
    [[nodiscard]] eve::Result<void> advance(const eve::SimulationStep& step);
    /** @brief Advances through bounded fixed steps to a target tick. */
    [[nodiscard]] eve::Result<std::uint32_t> advanceTo(eve::SimulationTick target, eve::Duration fixedDelta,
                                                       std::uint32_t maxSteps);

    /** @brief Return the latest simulation tick applied to this queue. */
    [[nodiscard]] eve::SimulationTick currentTick() const noexcept { return tick_; }

    /** @brief Sets an owner's parallel slot count; zero prevents new tasks from running. */
    [[nodiscard]] eve::Result<void> setSlotCount(std::string_view owner, int slots);
    /** @brief Returns an owner's slot count, defaulting to one. */
    int slotCount(std::string_view owner) const;
    /** @brief Returns the number of currently running tasks for an owner. */
    int runningCount(std::string_view owner) const;
    /** @brief Selects an owner's deterministic queue ordering strategy. */
    [[nodiscard]] eve::Result<void> setSchedulerStrategy(std::string_view owner, SchedulerStrategy strategy);
    /** @brief Returns an owner's queue ordering strategy, defaulting to Priority. */
    SchedulerStrategy schedulerStrategy(std::string_view owner) const;
    /** @brief Sets named running capacity for an owner; zero disables that capability. */
    [[nodiscard]] eve::Result<void> setResourceCapacity(std::string_view owner, std::string_view resource,
                                                        int capacity);
    /** @brief Returns named capacity, defaulting `slot` to slotCount and other resources to zero. */
    int resourceCapacity(std::string_view owner, std::string_view resource) const;
    /** @brief Returns the deterministic reason a task cannot start, or an empty string when runnable. */
    [[nodiscard]] eve::Result<std::string> blockReason(std::string_view taskId) const;
    /** @brief Publishes or withdraws one generation-qualified definition fact for an owner. */
    [[nodiscard]] eve::Result<void> setDefinitionAvailable(
        std::string_view owner, const eve::definition::DefinitionHandle& definition, bool available);
    /** @brief Publishes or withdraws one exact tag fact for an owner. */
    [[nodiscard]] eve::Result<void> setTagAvailable(std::string_view owner, std::string_view tag, bool available);
    /** @brief Reports whether an exact definition incarnation is available to an owner. */
    [[nodiscard]] bool definitionAvailable(
        std::string_view owner, const eve::definition::DefinitionHandle& definition) const;
    /** @brief Reports whether an exact tag is available to an owner. */
    [[nodiscard]] bool tagAvailable(std::string_view owner, std::string_view tag) const;
    /** @brief Predicts task timing without mutating queue state. */
    [[nodiscard]] eve::Result<ProductionPrediction> predict(std::string_view taskId) const;
    /** @brief Returns structured diagnostic state for a task. */
    [[nodiscard]] eve::Result<ProductionDiagnostic> diagnose(std::string_view taskId) const;

    /**
     * @brief Finds a retained task by stable ID.
     * @return An immediate borrowed reference, empty when the ID is absent.
     * @ownership WorkQueue owns the task; the reference is not owning.
     * @lifetime Valid until queue mutation, restore, clear, or destruction.
     * @thread Owner-thread only; no synchronization is provided.
     */
    [[nodiscard]] eve::OptionalRef<const ProductionTask> find(std::string_view taskId);
    /** @brief Const overload of find with the same immediate-borrow lifetime. */
    [[nodiscard]] eve::OptionalRef<const ProductionTask> find(std::string_view taskId) const;
    /** @brief Returns retained task count in enqueue order. */
    int taskCount() const;
    /** @brief Returns a retained task by enqueue index, or an empty borrowed reference. */
    [[nodiscard]] eve::OptionalRef<const ProductionTask> taskAt(int index);
    /** @brief Const overload of taskAt with the same immediate-borrow lifetime. */
    [[nodiscard]] eve::OptionalRef<const ProductionTask> taskAt(int index) const;
    /** @brief Returns retained task count for an owner. */
    int ownerTaskCount(std::string_view owner) const;
    /** @brief Returns an owner's retained task by enqueue index, or an empty borrowed reference. */
    [[nodiscard]] eve::OptionalRef<const ProductionTask> ownerTaskAt(std::string_view owner, int index);
    /** @brief Const overload of ownerTaskAt with the same immediate-borrow lifetime. */
    [[nodiscard]] eve::OptionalRef<const ProductionTask> ownerTaskAt(std::string_view owner, int index) const;

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
    [[nodiscard]] eve::Result<eve::SnapshotEnvelope> snapshot(const eve::SnapshotHashProvider& hashProvider) const;
    /**
     * @brief Restores a verified or migrated production envelope atomically.
     * @param snapshot Source envelope with schema `production:queue`.
     * @param hashProvider Explicit content-digest provider.
     * @return Success, or a failure leaving queue state unchanged.
     */
    [[nodiscard]] eve::Result<void> restoreSnapshot(const eve::SnapshotEnvelope&     snapshot,
                                                    const eve::SnapshotHashProvider& hashProvider);
    /** @brief Serializes the common production snapshot envelope. */
    [[nodiscard]] eve::Result<std::string> snapshotEnvelopeJson(const eve::SnapshotHashProvider& hashProvider) const;
    /** @brief Parses and transactionally restores a common production envelope. */
    [[nodiscard]] eve::Result<void> restoreSnapshotJson(std::string_view                 json,
                                                        const eve::SnapshotHashProvider& hashProvider);

private:
    [[nodiscard]] ProductionTask* mutableFind(std::string_view taskId);
    [[nodiscard]] bool dependenciesSatisfied(const ProductionTask& task) const;
    [[nodiscard]] bool resourcesAvailable(const ProductionTask& task) const;
    void completeIfReady(ProductionTask& task);
    void continueAfterCycle(ProductionTask& task);
    static std::uint32_t refundFor(const ProductionTask& task, RefundPolicy policy);
    void scheduleAll();
    void schedule(std::string_view owner);
    void emit(ProductionEventKind kind, const ProductionTask& task, std::string_view reason = {});

    uint64_t                                    nextTaskId_          = 1;
    uint64_t                                    nextEnqueueSequence_ = 1;
    uint64_t                                    nextEventSequence_   = 1;
    eve::PersistentId                           instanceId_;
    eve::Revision                               revision_ = eve::Revision::zero();
    eve::SimulationTick                         tick_     = eve::SimulationTick::zero();
    std::deque<std::unique_ptr<ProductionTask>> tasks_;
    std::deque<ProductionEvent>                 events_;
    std::vector<std::pair<std::string, int>>    slots_;
    std::vector<std::pair<std::pair<std::string, std::string>, int>> resources_;
    std::vector<std::pair<std::string, SchedulerStrategy>> schedulerStrategies_;
    std::vector<std::tuple<std::string, std::string, std::uint64_t>> availableDefinitions_;
    std::vector<std::pair<std::string, std::string>> availableTags_;
};

/** @brief Returns the stable lowercase name of a task state. */
std::string_view taskStateName(TaskState state);
/** @brief Writes the stable task-state spelling to a stream. */
inline std::ostream& operator<<(std::ostream& stream, TaskState state) { return stream << taskStateName(state); }
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
    [[nodiscard]] static eve::ResultRef<WorkQueue> resolve(WorkQueueHandleRef reference);
    /** @brief Releases a module-owned queue. */
    [[nodiscard]] static eve::Result<void> release(WorkQueueHandleRef reference);
    /** @brief Reports whether a queue reference is stale for the current module. */
    [[nodiscard]] static bool isStale(WorkQueueHandleRef reference) noexcept;

private:
    eve::script::RuntimeObjectRegistry<WorkQueue, WorkQueueHandleTag> queues_;
};

}  // namespace eve::production
