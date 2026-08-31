#pragma once

#include "common/Result.h"
#include "common/RuntimeHandle.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace eve::npc_ai {

using BlackboardValue = std::variant<bool, std::int64_t, double, std::string>;

enum class BlackboardType : std::uint8_t { Boolean, Integer, Number, String };
enum class CompareOp : std::uint8_t { Exists, Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };
enum class TaskStatus : std::uint8_t { Running, Succeeded, Failed };
enum class StopReason : std::uint8_t { Completed, Transition, StartupRollback, AgentDestroyed, DefinitionReplaced };
enum class TraceKind : std::uint8_t {
    AgentCreated,
    AgentDestroyed,
    AgentRestored,
    BlackboardChanged,
    SignalQueued,
    TransitionApplied,
    TaskStarted,
    TaskCompleted,
    TaskFailed,
    PerceptionUpdated,
    PerceptionForgotten
};

struct BlackboardKeySpec {
    std::string                    key;
    BlackboardType                 type     = BlackboardType::Boolean;
    bool                           required = false;
    std::optional<BlackboardValue> defaultValue;
};

struct BlackboardPredicate {
    std::string                    key;
    CompareOp                      op = CompareOp::Exists;
    std::optional<BlackboardValue> value;
};

struct TaskSpec {
    std::string id;
    std::string type;
    std::string parametersJson = "{}";
};

struct Transition {
    std::string                      targetState;
    std::string                      signal;
    std::vector<BlackboardPredicate> conditions;
    std::uint32_t                    priority = 0;
};

struct StateDefinition {
    std::string                      id;
    std::optional<std::string>       parent;
    std::vector<BlackboardPredicate> enterConditions;
    std::vector<TaskSpec>            tasks;
    std::vector<Transition>          transitions;
};

/** @brief Immutable, validated source data for one hierarchical NPC state tree. */
struct BehaviorDefinition {
    std::string                    id;
    std::uint32_t                  schemaVersion = 1;
    std::string                    initialState;
    std::vector<BlackboardKeySpec> blackboardSchema;
    std::vector<StateDefinition>   states;
};

struct AgentHandleTag {};
using AgentHandle = eve::RuntimeHandle<AgentHandleTag>;

/** @brief One bounded, expiring item of NPC perception memory. */
struct PerceptionMemory {
    std::string   subject;
    std::string   sense;
    double        confidence       = 0.0;
    std::uint64_t observedTick     = 0;
    std::uint64_t forgetAfterTicks = 1;
    std::string   payloadJson      = "{}";
};

struct TraceEvent {
    std::uint64_t tick = 0;
    AgentHandle   agent;
    TraceKind     kind = TraceKind::SignalQueued;
    std::string   subject;
    std::string   detail;
};

struct NpcAiWorldConfig {
    std::size_t traceCapacity       = 2048;
    std::size_t maxMemoriesPerAgent = 128;
};

/** @brief Input time and work limits for one deterministic simulation update. */
struct TickContext {
    std::uint64_t simulationTick         = 0;
    double        deltaSeconds           = 0.0;
    std::uint32_t maxAgents              = 256;
    std::uint32_t maxTransitionsPerAgent = 8;
};

struct TickReport {
    std::uint32_t agentsUpdated              = 0;
    std::uint32_t agentsDeferred             = 0;
    std::uint32_t transitionsApplied         = 0;
    std::uint32_t transitionBudgetsExhausted = 0;
    std::uint32_t tasksTicked                = 0;
};

struct AgentSnapshot {
    AgentHandle                            handle;
    std::string                            behaviorId;
    std::string                            activeState;
    std::vector<std::string>               activePath;
    std::map<std::string, BlackboardValue> blackboard;
    std::vector<PerceptionMemory>          perception;
    std::uint64_t                          lastTick = 0;
};

/** @brief Portable task state stored inside an NPC archive. */
struct TaskArchive {
    std::string stateId;
    std::string taskId;
    bool        completed  = false;
    std::string memoryJson = "{}";
};

/**
 * @brief Versioned, owning NPC state used for save/load and hot-reload handoff.
 * @remarks Unknown schema ids or versions are rejected. Task providers are not
 * considered live after restore; incomplete tasks restart from `memoryJson`.
 */
struct AgentArchive {
    static constexpr std::string_view SchemaId      = "evengine.npc-ai-agent";
    static constexpr std::uint32_t    SchemaVersion = 1;

    std::string                            schemaId      = std::string(SchemaId);
    std::uint32_t                          schemaVersion = SchemaVersion;
    std::string                            behaviorId;
    std::string                            activeState;
    std::map<std::string, BlackboardValue> blackboard;
    std::vector<std::string>               pendingSignals;
    std::vector<PerceptionMemory>          perception;
    std::vector<TaskArchive>               tasks;
    std::uint64_t                          lastTick = 0;
};

struct TaskContext {
    AgentHandle                                   agent;
    std::string_view                              stateId;
    std::uint64_t                                 simulationTick = 0;
    double                                        deltaSeconds   = 0.0;
    const std::map<std::string, BlackboardValue>& blackboard;
    std::span<const PerceptionMemory>             perception;
};

/**
 * @brief Owned extension point for navigation, animation, combat and gameplay tasks.
 * @thread All calls are simulation-thread-affine.
 * @reentrancy Implementations must not call back into the owning NpcAiWorld.
 */
class ITaskService {
public:
    virtual ~ITaskService()                                                             = default;
    [[nodiscard]] virtual Result<void>       start(const TaskContext& context, const TaskSpec& spec,
                                                   std::string& inOutMemoryJson)        = 0;
    [[nodiscard]] virtual Result<TaskStatus> tick(const TaskContext& context, const TaskSpec& spec,
                                                  std::string& inOutMemoryJson)         = 0;
    virtual void                             stop(const TaskContext& context, const TaskSpec& spec, StopReason reason,
                                                  std::string_view memoryJson) noexcept = 0;
};

/**
 * @brief Authoritative owner and deterministic scheduler for NPC AI agents.
 * @remarks Definitions are validated and copied on registration. Agent handles
 * are process-local and become stale after destruction. The world owns task
 * services and must be used only on its simulation thread.
 */
class NpcAiWorld {
public:
    explicit NpcAiWorld(NpcAiWorldConfig config = {});
    NpcAiWorld(const NpcAiWorld&)            = delete;
    NpcAiWorld& operator=(const NpcAiWorld&) = delete;

    /** @brief Validates and atomically publishes a behavior definition. */
    [[nodiscard]] Result<void> registerBehavior(BehaviorDefinition definition);
    /** @brief Transfers ownership of a task provider into this world. */
    [[nodiscard]] Result<void> registerTaskService(std::string type, std::unique_ptr<ITaskService> service);
    /** @brief Creates an agent bound to an existing behavior definition. */
    [[nodiscard]] Result<AgentHandle> createAgent(std::string_view behaviorId);
    /** @brief Stops active tasks and invalidates the agent handle. */
    [[nodiscard]] Result<void> destroyAgent(AgentHandle handle);
    /** @brief Writes one authoritative blackboard value. */
    [[nodiscard]] Result<void> setBlackboard(AgentHandle handle, std::string key, BlackboardValue value);
    /** @brief Queues a named wake-up signal for evaluation on the next tick. */
    [[nodiscard]] Result<void> signal(AgentHandle handle, std::string signalName);
    /** @brief Upserts one bounded memory and queues a sense-specific wake-up signal. */
    [[nodiscard]] Result<void> remember(AgentHandle handle, PerceptionMemory memory);
    /** @brief Explicitly forgets one subject/sense pair. */
    [[nodiscard]] Result<void> forget(AgentHandle handle, std::string_view subject, std::string_view sense);
    /** @brief Advances agents in stable handle order within the explicit budget. */
    [[nodiscard]] Result<TickReport> tick(const TickContext& context);
    /** @brief Returns an owning diagnostic snapshot, or StaleHandle. */
    [[nodiscard]] Result<AgentSnapshot> snapshot(AgentHandle handle) const;
    /** @brief Captures a versioned owning archive without invoking task providers. */
    [[nodiscard]] Result<AgentArchive> archive(AgentHandle handle) const;
    /** @brief Validates a complete candidate and atomically publishes a restored agent. */
    [[nodiscard]] Result<AgentHandle> restoreAgent(const AgentArchive& archive);
    /** @brief Reports whether a process-local agent handle no longer resolves. */
    [[nodiscard]] bool isStale(AgentHandle handle) const noexcept;
    /** @brief Returns owning trace records for one agent at or after a tick. */
    [[nodiscard]] Result<std::vector<TraceEvent>> trace(AgentHandle handle, std::uint64_t fromTick = 0) const;
    /** @brief Validates definition shape without publishing it. */
    [[nodiscard]] static Result<void> validate(const BehaviorDefinition& definition);

private:
    struct TaskRuntime {
        bool        started    = false;
        bool        completed  = false;
        std::string memoryJson = "{}";
    };
    struct Agent {
        std::string                            behaviorId;
        std::string                            activeState;
        std::map<std::string, BlackboardValue> blackboard;
        std::vector<std::string>               signals;
        std::vector<PerceptionMemory>          perception;
        std::map<std::string, TaskRuntime>     taskRuntime;
        std::uint64_t                          lastTick = 0;
    };
    struct Slot {
        std::uint32_t        generation = 1;
        std::optional<Agent> agent;
    };

    [[nodiscard]] Result<std::reference_wrapper<Agent>>       resolve(AgentHandle handle);
    [[nodiscard]] Result<std::reference_wrapper<const Agent>> resolve(AgentHandle handle) const;
    [[nodiscard]] bool predicatesPass(std::span<const BlackboardPredicate> predicates, const Agent& agent) const;
    [[nodiscard]] std::optional<std::reference_wrapper<const StateDefinition>> findState(
        const BehaviorDefinition& behavior, std::string_view id) const;
    [[nodiscard]] std::vector<std::reference_wrapper<const StateDefinition>> activePath(
        const BehaviorDefinition& behavior, std::string_view leaf) const;
    [[nodiscard]] static std::string taskRuntimeKey(std::string_view state, std::string_view task);
    void stopStateTasks(AgentHandle handle, Agent& agent, const StateDefinition& state, StopReason reason) noexcept;
    void pushTrace(TraceEvent event);

    NpcAiWorldConfig                                     config_;
    std::map<std::string, BehaviorDefinition>            behaviors_;
    std::map<std::string, std::unique_ptr<ITaskService>> taskServices_;
    std::vector<Slot>                                    slots_;
    std::vector<std::uint32_t>                           freeSlots_;
    std::uint32_t                                        scheduleCursor_ = 0;
    std::deque<TraceEvent>                               trace_;
};

}  // namespace eve::npc_ai
