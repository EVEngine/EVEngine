#pragma once

#include "common/Module.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace eve::production {

/** @brief Lifecycle state of a production task. */
enum class TaskState { Queued, Running, Paused, Completed, Cancelled, Failed };

/** @brief Kind of a deterministic production lifecycle event. */
enum class ProductionEventKind { Enqueued, Started, Paused, Resumed, Completed, Cancelled, Failed };

/** @brief A subject-agnostic continuous task retained for audit and save games. */
struct ProductionTask {
    std::string id;
    std::string owner;
    std::string kind;
    std::string product;
    std::string contextJson     = "{}";
    double      duration        = 0.0;
    double      progress        = 0.0;
    int         priority        = 0;
    TaskState   state           = TaskState::Queued;
    uint64_t    enqueueSequence = 0;
    std::string reason;
};

/** @brief Deterministically sequenced production lifecycle event. */
struct ProductionEvent {
    uint64_t            sequence = 0;
    ProductionEventKind kind     = ProductionEventKind::Enqueued;
    std::string         taskId;
    std::string         owner;
    std::string         taskKind;
    std::string         product;
    std::string         reason;
};

/** @brief Generic multi-owner, multi-slot continuous task queue. */
class ProductionQueue {
public:
    /** @brief Enqueues a task and returns its stable ID, or an empty string on invalid input. */
    std::string enqueue(const std::string& owner, const std::string& kind, const std::string& product,
                        const std::string& contextJson, double duration, int priority = 0);
    /** @brief Pauses a queued or running task. */
    bool pause(const std::string& taskId);
    /** @brief Returns a paused task to deterministic scheduling. */
    bool resume(const std::string& taskId);
    /** @brief Cancels a non-terminal task. */
    bool cancel(const std::string& taskId, const std::string& reason = "cancelled");
    /** @brief Marks a non-terminal task failed. */
    bool fail(const std::string& taskId, const std::string& reason = "failed");
    /** @brief Advances running tasks by fixed dt multiplied by speedMultiplier. */
    void update(double dt, double speedMultiplier = 1.0);

    /** @brief Sets an owner's parallel slot count; zero prevents new tasks from running. */
    bool setSlotCount(const std::string& owner, int slots);
    /** @brief Returns an owner's slot count, defaulting to one. */
    int slotCount(const std::string& owner) const;
    /** @brief Returns the number of currently running tasks for an owner. */
    int runningCount(const std::string& owner) const;

    /** @brief Finds a retained task by stable ID, or nullptr. */
    ProductionTask* find(const std::string& taskId);
    /** @brief Returns retained task count in enqueue order. */
    int taskCount() const;
    /** @brief Returns a retained task by enqueue index, or nullptr. */
    ProductionTask* taskAt(int index);
    /** @brief Returns retained task count for an owner. */
    int ownerTaskCount(const std::string& owner) const;
    /** @brief Returns an owner's retained task by enqueue index, or nullptr. */
    ProductionTask* ownerTaskAt(const std::string& owner, int index);

    /** @brief Returns retained event count. */
    int eventCount() const;
    /** @brief Returns an event by sequence index, or nullptr. */
    ProductionEvent* eventAt(int index);
    /** @brief Clears retained events without resetting sequence numbering. */
    void clearEvents();
    /** @brief Serializes the complete queue as deterministic JSON. */
    std::string snapshot() const;
    /** @brief Transactionally restores a snapshot; failure preserves current state. */
    bool restore(const std::string& json);
    /** @brief Returns the last restore or validation error. */
    const std::string& lastError() const;
    /** @brief Clears tasks, owner settings, events, and stable counters. */
    void clear();

private:
    void schedule(const std::string& owner);
    void emit(ProductionEventKind kind, const ProductionTask& task, const std::string& reason = {});

    uint64_t                                    nextTaskId_          = 1;
    uint64_t                                    nextEnqueueSequence_ = 1;
    uint64_t                                    nextEventSequence_   = 1;
    std::deque<std::unique_ptr<ProductionTask>> tasks_;
    std::deque<ProductionEvent>                 events_;
    std::vector<std::pair<std::string, int>>    slots_;
    std::string                                 lastError_;
};

/** @brief Returns the stable lowercase name of a task state. */
std::string taskStateName(TaskState state);
/** @brief Returns the stable lowercase name of an event kind. */
std::string eventKindName(ProductionEventKind kind);

/** @brief Script module factory for generic production queues. */
class Production : public Module {
public:
    Module_REG(Production);
    Production()           = default;
    ~Production() override = default;

    /** @brief Allocates a module-owned generic production queue. */
    static ProductionQueue* newQueue();

private:
    std::vector<std::unique_ptr<ProductionQueue>> queues_;
};

}  // namespace eve::production
