#include "npc_ai/NpcAi.h"

#include "common/Status.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace eve::npc_ai {
namespace {
template <class T>
Result<T> failure(DiagnosticCode code, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), {}, {}, "npc_ai"));
}

bool compareValues(const BlackboardValue& lhs, CompareOp op, const BlackboardValue& rhs) {
    if (lhs.index() != rhs.index()) return op == CompareOp::NotEqual;
    if (op == CompareOp::Equal) return lhs == rhs;
    if (op == CompareOp::NotEqual) return lhs != rhs;
    return std::visit(
        [op](const auto& a, const auto& b) {
            using A = std::decay_t<decltype(a)>;
            using B = std::decay_t<decltype(b)>;
            if constexpr (!std::is_same_v<A, B> || std::is_same_v<A, bool>)
                return false;
            else {
                switch (op) {
                    case CompareOp::Less: return a < b;
                    case CompareOp::LessEqual: return a <= b;
                    case CompareOp::Greater: return a > b;
                    case CompareOp::GreaterEqual: return a >= b;
                    default: return false;
                }
            }
        },
        lhs, rhs);
}

BlackboardType valueType(const BlackboardValue& value) {
    if (std::holds_alternative<bool>(value)) return BlackboardType::Boolean;
    if (std::holds_alternative<std::int64_t>(value)) return BlackboardType::Integer;
    if (std::holds_alternative<double>(value)) return BlackboardType::Number;
    return BlackboardType::String;
}

bool validPredicate(const BlackboardPredicate& predicate) {
    return !predicate.key.empty() && (predicate.op == CompareOp::Exists || predicate.value.has_value());
}
}  // namespace

NpcAiWorld::NpcAiWorld(NpcAiWorldConfig config) : config_(config) {}

Result<void> NpcAiWorld::validate(const BehaviorDefinition& definition) {
    if (definition.schemaVersion != 1)
        return failure<void>(DiagnosticCode::Unsupported, "unsupported NPC behavior schema version");
    if (definition.id.empty() || definition.initialState.empty())
        return failure<void>(DiagnosticCode::InvalidArgument, "behavior id and initial state are required");
    std::set<std::string> ids;
    std::set<std::string> schemaKeys;
    for (const auto& key : definition.blackboardSchema) {
        if (key.key.empty() || !schemaKeys.insert(key.key).second)
            return failure<void>(DiagnosticCode::InvalidArgument,
                                 "blackboard schema keys must be non-empty and unique");
        if (key.defaultValue && valueType(*key.defaultValue) != key.type)
            return failure<void>(DiagnosticCode::InvalidArgument,
                                 "blackboard default value does not match its declared type");
        if (key.required && !key.defaultValue)
            return failure<void>(DiagnosticCode::InvalidArgument,
                                 "required blackboard keys need a default for atomic agent creation");
    }
    for (const auto& state : definition.states) {
        if (state.id.empty() || !ids.insert(state.id).second)
            return failure<void>(DiagnosticCode::InvalidArgument, "state ids must be non-empty and unique");
        std::set<std::string> taskIds;
        for (const auto& task : state.tasks)
            if (task.id.empty() || task.type.empty() || !taskIds.insert(task.id).second)
                return failure<void>(DiagnosticCode::InvalidArgument,
                                     "task ids must be unique per state and task types are required");
    }
    if (!ids.contains(definition.initialState))
        return failure<void>(DiagnosticCode::NotFound, "initial state does not exist");
    for (const auto& state : definition.states) {
        if (state.parent && (!ids.contains(*state.parent) || *state.parent == state.id))
            return failure<void>(DiagnosticCode::InvalidArgument, "state parent is missing or self-referential");
        for (const auto& transition : state.transitions) {
            if (!ids.contains(transition.targetState))
                return failure<void>(DiagnosticCode::NotFound, "transition target does not exist");
            if (transition.targetState == state.id && transition.signal.empty())
                return failure<void>(DiagnosticCode::InvalidArgument,
                                     "an unconditional self-transition would exhaust the transition budget");
            for (const auto& predicate : transition.conditions)
                if (!validPredicate(predicate))
                    return failure<void>(DiagnosticCode::InvalidArgument, "invalid transition predicate");
        }
        for (const auto& predicate : state.enterConditions)
            if (!validPredicate(predicate))
                return failure<void>(DiagnosticCode::InvalidArgument, "invalid enter predicate");
    }
    for (const auto& origin : definition.states) {
        std::set<std::string>  chain;
        const StateDefinition* current = &origin;
        while (current->parent) {
            if (!chain.insert(current->id).second)
                return failure<void>(DiagnosticCode::InvalidArgument, "state parent cycle detected");
            auto it = std::find_if(definition.states.begin(), definition.states.end(),
                                   [&](const auto& s) { return s.id == *current->parent; });
            current = &*it;
        }
    }
    return Result<void>::success();
}

Result<void> NpcAiWorld::registerBehavior(BehaviorDefinition definition) {
    auto checked = validate(definition);
    if (!checked.ok()) return Result<void>::failure(checked.status());
    if (behaviors_.contains(definition.id)) {
        return failure<void>(DiagnosticCode::AlreadyExists,
                             "behavior definition already exists; replacement requires an explicit migration");
    }
    behaviors_.emplace(definition.id, std::move(definition));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> NpcAiWorld::registerTaskService(std::string type, std::unique_ptr<ITaskService> service) {
    if (type.empty() || !service)
        return failure<void>(DiagnosticCode::InvalidArgument, "task service type and owner are required");
    taskServices_.insert_or_assign(std::move(type), std::move(service));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<AgentHandle> NpcAiWorld::createAgent(std::string_view behaviorId) {
    auto behavior = behaviors_.find(std::string(behaviorId));
    if (behavior == behaviors_.end())
        return failure<AgentHandle>(DiagnosticCode::NotFound, "behavior definition was not registered");
    std::uint32_t index;
    if (freeSlots_.empty()) {
        index = static_cast<std::uint32_t>(slots_.size());
        slots_.push_back({});
    } else {
        index = freeSlots_.back();
        freeSlots_.pop_back();
    }
    Agent candidate;
    candidate.behaviorId  = behavior->first;
    candidate.activeState = behavior->second.initialState;
    for (const auto& key : behavior->second.blackboardSchema)
        if (key.defaultValue) candidate.blackboard.emplace(key.key, *key.defaultValue);
    for (const auto state : activePath(behavior->second, candidate.activeState)) {
        if (!predicatesPass(state.get().enterConditions, candidate)) {
            freeSlots_.push_back(index);
            return failure<AgentHandle>(DiagnosticCode::PreconditionViolation,
                                        "initial state path enter conditions are false");
        }
    }
    slots_[index].agent = std::move(candidate);
    const AgentHandle handle(index, slots_[index].generation);
    pushTrace({0, handle, TraceKind::AgentCreated, behavior->first, {}});
    return Result<AgentHandle>::success(handle);
}

Result<std::reference_wrapper<NpcAiWorld::Agent>> NpcAiWorld::resolve(AgentHandle handle) {
    if (!handle.isValid() || handle.index() >= slots_.size())
        return failure<std::reference_wrapper<Agent>>(DiagnosticCode::StaleHandle, "NPC agent handle is stale");
    auto& slot = slots_[handle.index()];
    if (!slot.agent || slot.generation != handle.generation())
        return failure<std::reference_wrapper<Agent>>(DiagnosticCode::StaleHandle, "NPC agent handle is stale");
    return Result<std::reference_wrapper<Agent>>::success(std::ref(*slot.agent));
}
Result<std::reference_wrapper<const NpcAiWorld::Agent>> NpcAiWorld::resolve(AgentHandle handle) const {
    if (!handle.isValid() || handle.index() >= slots_.size())
        return failure<std::reference_wrapper<const Agent>>(DiagnosticCode::StaleHandle, "NPC agent handle is stale");
    const auto& slot = slots_[handle.index()];
    if (!slot.agent || slot.generation != handle.generation())
        return failure<std::reference_wrapper<const Agent>>(DiagnosticCode::StaleHandle, "NPC agent handle is stale");
    return Result<std::reference_wrapper<const Agent>>::success(std::cref(*slot.agent));
}

Result<void> NpcAiWorld::destroyAgent(AgentHandle handle) {
    auto resolved = resolve(handle);
    if (!resolved.ok()) return Result<void>::failure(resolved.status());
    Agent&      agent    = resolved.value().get();
    const auto& behavior = behaviors_.at(agent.behaviorId);
    auto        path     = activePath(behavior, agent.activeState);
    for (auto state = path.rbegin(); state != path.rend(); ++state)
        stopStateTasks(handle, agent, state->get(), StopReason::AgentDestroyed);
    pushTrace({agent.lastTick, handle, TraceKind::AgentDestroyed, agent.behaviorId, {}});
    auto& slot = slots_[handle.index()];
    slot.agent.reset();
    auto next = AgentHandle::nextGeneration(slot.generation);
    if (next) {
        slot.generation = *next;
        freeSlots_.push_back(handle.index());
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> NpcAiWorld::setBlackboard(AgentHandle handle, std::string key, BlackboardValue value) {
    if (key.empty()) return failure<void>(DiagnosticCode::InvalidArgument, "blackboard key is required");
    const std::string traceKey = key;
    auto              resolved = resolve(handle);
    if (!resolved.ok()) return Result<void>::failure(resolved.status());
    auto&       agent    = resolved.value().get();
    const auto& behavior = behaviors_.at(agent.behaviorId);
    const auto  schema   = std::find_if(behavior.blackboardSchema.begin(), behavior.blackboardSchema.end(),
                                        [&](const auto& item) { return item.key == key; });
    if (!behavior.blackboardSchema.empty() && schema == behavior.blackboardSchema.end())
        return failure<void>(DiagnosticCode::NotFound, "blackboard key is not declared by the behavior schema");
    if (schema != behavior.blackboardSchema.end() && schema->type != valueType(value))
        return failure<void>(DiagnosticCode::InvalidArgument, "blackboard value type does not match its schema");
    agent.blackboard.insert_or_assign(std::move(key), std::move(value));
    pushTrace({agent.lastTick, handle, TraceKind::BlackboardChanged, traceKey, {}});
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> NpcAiWorld::signal(AgentHandle handle, std::string signalName) {
    if (signalName.empty()) return failure<void>(DiagnosticCode::InvalidArgument, "signal name is required");
    auto resolved = resolve(handle);
    if (!resolved.ok()) return Result<void>::failure(resolved.status());
    auto& agent = resolved.value().get();
    agent.signals.push_back(std::move(signalName));
    pushTrace({agent.lastTick, handle, TraceKind::SignalQueued, agent.signals.back(), {}});
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> NpcAiWorld::remember(AgentHandle handle, PerceptionMemory memory) {
    if (memory.subject.empty() || memory.sense.empty() || !std::isfinite(memory.confidence) ||
        memory.confidence < 0.0 || memory.confidence > 1.0 || memory.forgetAfterTicks == 0)
        return failure<void>(DiagnosticCode::InvalidArgument, "perception memory fields are invalid");
    auto resolved = resolve(handle);
    if (!resolved.ok()) return Result<void>::failure(resolved.status());
    auto& agent = resolved.value().get();
    auto  found = std::find_if(agent.perception.begin(), agent.perception.end(), [&](const auto& item) {
        return item.subject == memory.subject && item.sense == memory.sense;
    });
    if (found != agent.perception.end()) {
        *found = memory;
    } else {
        if (config_.maxMemoriesPerAgent == 0)
            return failure<void>(DiagnosticCode::Unsupported, "perception memory is disabled by world configuration");
        if (agent.perception.size() == config_.maxMemoriesPerAgent) {
            auto oldest =
                std::min_element(agent.perception.begin(), agent.perception.end(), [](const auto& a, const auto& b) {
                    return std::pair{a.observedTick, a.subject + "\n" + a.sense} <
                           std::pair{b.observedTick, b.subject + "\n" + b.sense};
                });
            pushTrace({memory.observedTick, handle, TraceKind::PerceptionForgotten, oldest->subject, oldest->sense});
            *oldest = memory;
        } else {
            agent.perception.push_back(memory);
        }
    }
    std::sort(agent.perception.begin(), agent.perception.end(), [](const auto& a, const auto& b) {
        return std::pair{a.subject, a.sense} < std::pair{b.subject, b.sense};
    });
    agent.signals.push_back("perception." + memory.sense);
    pushTrace({memory.observedTick, handle, TraceKind::PerceptionUpdated, memory.subject, memory.sense});
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> NpcAiWorld::forget(AgentHandle handle, std::string_view subject, std::string_view sense) {
    if (subject.empty() || sense.empty())
        return failure<void>(DiagnosticCode::InvalidArgument, "perception subject and sense are required");
    auto resolved = resolve(handle);
    if (!resolved.ok()) return Result<void>::failure(resolved.status());
    auto&      agent    = resolved.value().get();
    auto&      memories = agent.perception;
    const auto found = std::find_if(memories.begin(), memories.end(),
                                    [&](const auto& item) { return item.subject == subject && item.sense == sense; });
    if (found == memories.end()) return failure<void>(DiagnosticCode::NotFound, "perception memory was not found");
    pushTrace({agent.lastTick, handle, TraceKind::PerceptionForgotten, found->subject, found->sense});
    memories.erase(found);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

bool NpcAiWorld::predicatesPass(std::span<const BlackboardPredicate> predicates, const Agent& agent) const {
    for (const auto& predicate : predicates) {
        const auto found = agent.blackboard.find(predicate.key);
        if (predicate.op == CompareOp::Exists) {
            if (found == agent.blackboard.end()) return false;
            continue;
        }
        if (found == agent.blackboard.end() || !predicate.value ||
            !compareValues(found->second, predicate.op, *predicate.value))
            return false;
    }
    return true;
}

std::optional<std::reference_wrapper<const StateDefinition>> NpcAiWorld::findState(const BehaviorDefinition& behavior,
                                                                                   std::string_view          id) const {
    const auto found =
        std::find_if(behavior.states.begin(), behavior.states.end(), [&](const auto& state) { return state.id == id; });
    if (found == behavior.states.end()) return std::nullopt;
    return std::cref(*found);
}

std::vector<std::reference_wrapper<const StateDefinition>> NpcAiWorld::activePath(const BehaviorDefinition& behavior,
                                                                                  std::string_view leaf) const {
    std::vector<std::reference_wrapper<const StateDefinition>> path;
    auto                                                       current = findState(behavior, leaf);
    while (current) {
        path.push_back(*current);
        current = current->get().parent ? findState(behavior, *current->get().parent) : std::nullopt;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

std::string NpcAiWorld::taskRuntimeKey(std::string_view state, std::string_view task) {
    std::string result;
    result.reserve(state.size() + task.size() + 1);
    result.append(state);
    result.push_back('\n');
    result.append(task);
    return result;
}

void NpcAiWorld::pushTrace(TraceEvent event) {
    if (config_.traceCapacity == 0) return;
    if (trace_.size() == config_.traceCapacity) trace_.pop_front();
    trace_.push_back(std::move(event));
}

void NpcAiWorld::stopStateTasks(AgentHandle handle, Agent& agent, const StateDefinition& state,
                                StopReason reason) noexcept {
    for (const auto& task : state.tasks) {
        const auto key     = taskRuntimeKey(state.id, task.id);
        auto       runtime = agent.taskRuntime.find(key);
        auto       service = taskServices_.find(task.type);
        if (runtime != agent.taskRuntime.end() && runtime->second.started && service != taskServices_.end()) {
            const TaskContext context{handle, state.id, agent.lastTick, 0.0, agent.blackboard, agent.perception};
            service->second->stop(context, task, reason, runtime->second.memoryJson);
        }
        agent.taskRuntime.erase(key);
    }
}

Result<TickReport> NpcAiWorld::tick(const TickContext& context) {
    if (!std::isfinite(context.deltaSeconds) || context.deltaSeconds < 0.0 || context.maxTransitionsPerAgent == 0)
        return failure<TickReport>(DiagnosticCode::InvalidArgument, "tick dt and transition budget are invalid");
    TickReport report;
    if (slots_.empty() || context.maxAgents == 0) return Result<TickReport>::success(report);
    std::uint32_t visited = 0;
    while (visited < slots_.size() && report.agentsUpdated < context.maxAgents) {
        const std::uint32_t index = (scheduleCursor_ + visited) % static_cast<std::uint32_t>(slots_.size());
        ++visited;
        auto& slot = slots_[index];
        if (!slot.agent) continue;
        AgentHandle handle(index, slot.generation);
        Agent&      agent    = *slot.agent;
        auto&       behavior = behaviors_.at(agent.behaviorId);
        auto        path     = activePath(behavior, agent.activeState);
        if (path.empty())
            return failure<TickReport>(DiagnosticCode::InvariantViolation, "agent active state is missing");

        for (auto memory = agent.perception.begin(); memory != agent.perception.end();) {
            const bool expired = context.simulationTick >= memory->observedTick &&
                                 context.simulationTick - memory->observedTick >= memory->forgetAfterTicks;
            if (!expired) {
                ++memory;
                continue;
            }
            const std::string signalName = "perception.forgotten." + memory->sense;
            agent.signals.push_back(signalName);
            pushTrace({context.simulationTick, handle, TraceKind::PerceptionForgotten, memory->subject, memory->sense});
            memory = agent.perception.erase(memory);
        }

        std::uint32_t transitions = 0;
        while (transitions < context.maxTransitionsPerAgent) {
            const Transition*                                          selected = nullptr;
            std::vector<std::reference_wrapper<const StateDefinition>> selectedPath;
            for (auto state = path.rbegin(); state != path.rend(); ++state) {
                for (const auto& candidate : state->get().transitions) {
                    const bool signalMatches =
                        candidate.signal.empty() ||
                        std::find(agent.signals.begin(), agent.signals.end(), candidate.signal) != agent.signals.end();
                    if (!signalMatches || !predicatesPass(candidate.conditions, agent) ||
                        (selected && candidate.priority <= selected->priority))
                        continue;
                    auto       candidatePath = activePath(behavior, candidate.targetState);
                    const bool canEnter      = std::all_of(
                        candidatePath.begin(), candidatePath.end(),
                        [&](const auto target) { return predicatesPass(target.get().enterConditions, agent); });
                    if (canEnter) {
                        selected     = &candidate;
                        selectedPath = std::move(candidatePath);
                    }
                }
            }
            if (!selected) break;

            std::size_t commonPrefix = 0;
            while (commonPrefix < path.size() && commonPrefix < selectedPath.size() &&
                   path[commonPrefix].get().id == selectedPath[commonPrefix].get().id)
                ++commonPrefix;
            for (std::size_t exiting = path.size(); exiting > commonPrefix; --exiting)
                stopStateTasks(handle, agent, path[exiting - 1].get(), StopReason::Transition);

            const std::string previousState = agent.activeState;
            agent.activeState               = selected->targetState;
            path                            = std::move(selectedPath);
            ++transitions;
            ++report.transitionsApplied;
            pushTrace({context.simulationTick, handle, TraceKind::TransitionApplied, previousState, agent.activeState});
            if (!selected->signal.empty())
                agent.signals.erase(std::remove(agent.signals.begin(), agent.signals.end(), selected->signal),
                                    agent.signals.end());
        }
        if (transitions == context.maxTransitionsPerAgent) ++report.transitionBudgetsExhausted;
        agent.signals.clear();
        agent.lastTick = context.simulationTick;

        for (const auto state : path)
            for (const auto& task : state.get().tasks)
                if (!taskServices_.contains(task.type))
                    return failure<TickReport>(DiagnosticCode::NotFound,
                                               "task service is not registered: " + task.type);

        struct StartedTask {
            std::reference_wrapper<const StateDefinition> state;
            std::reference_wrapper<const TaskSpec>        task;
            std::string                                   runtimeKey;
        };
        std::vector<StartedTask> startedThisTick;
        for (const auto state : path) {
            for (const auto& task : state.get().tasks) {
                auto&      service    = taskServices_.at(task.type);
                const auto runtimeKey = taskRuntimeKey(state.get().id, task.id);
                auto&      runtime    = agent.taskRuntime[runtimeKey];
                if (runtime.completed) continue;
                TaskContext taskContext{handle,           state.get().id,  context.simulationTick, context.deltaSeconds,
                                        agent.blackboard, agent.perception};
                if (!runtime.started) {
                    auto started = service->start(taskContext, task, runtime.memoryJson);
                    if (!started.ok()) {
                        agent.taskRuntime.erase(runtimeKey);
                        for (auto rollback = startedThisTick.rbegin(); rollback != startedThisTick.rend(); ++rollback) {
                            auto active = agent.taskRuntime.find(rollback->runtimeKey);
                            if (active == agent.taskRuntime.end()) continue;
                            auto&             rollbackService = taskServices_.at(rollback->task.get().type);
                            const TaskContext rollbackContext{handle,
                                                              rollback->state.get().id,
                                                              context.simulationTick,
                                                              context.deltaSeconds,
                                                              agent.blackboard,
                                                              agent.perception};
                            rollbackService->stop(rollbackContext, rollback->task.get(), StopReason::StartupRollback,
                                                  active->second.memoryJson);
                            agent.taskRuntime.erase(active);
                        }
                        return Result<TickReport>::failure(started.status());
                    }
                    runtime.started = true;
                    startedThisTick.push_back({state, std::cref(task), runtimeKey});
                    pushTrace({context.simulationTick, handle, TraceKind::TaskStarted, state.get().id, task.id});
                }
            }
        }

        for (const auto state : path) {
            for (const auto& task : state.get().tasks) {
                auto& service = taskServices_.at(task.type);
                auto& runtime = agent.taskRuntime.at(taskRuntimeKey(state.get().id, task.id));
                if (runtime.completed) continue;
                TaskContext taskContext{handle,           state.get().id,  context.simulationTick, context.deltaSeconds,
                                        agent.blackboard, agent.perception};
                auto        taskResult = service->tick(taskContext, task, runtime.memoryJson);
                if (!taskResult.ok()) return Result<TickReport>::failure(taskResult.status());
                ++report.tasksTicked;
                if (taskResult.value() != TaskStatus::Running) {
                    service->stop(taskContext, task, StopReason::Completed, runtime.memoryJson);
                    runtime.started      = false;
                    runtime.completed    = true;
                    const bool succeeded = taskResult.value() == TaskStatus::Succeeded;
                    agent.signals.push_back(std::string("task.") + (succeeded ? "succeeded." : "failed.") + task.id);
                    pushTrace({context.simulationTick, handle,
                               succeeded ? TraceKind::TaskCompleted : TraceKind::TaskFailed, state.get().id, task.id});
                }
            }
        }
        ++report.agentsUpdated;
    }
    scheduleCursor_ = (scheduleCursor_ + visited) % static_cast<std::uint32_t>(slots_.size());
    for (const auto& slot : slots_)
        if (slot.agent) ++report.agentsDeferred;
    report.agentsDeferred -= report.agentsUpdated;
    return Result<TickReport>::success(report);
}

Result<AgentSnapshot> NpcAiWorld::snapshot(AgentHandle handle) const {
    auto resolved = resolve(handle);
    if (!resolved.ok()) return Result<AgentSnapshot>::failure(resolved.status());
    const Agent&             agent = resolved.value().get();
    std::vector<std::string> path;
    for (const auto state : activePath(behaviors_.at(agent.behaviorId), agent.activeState))
        path.push_back(state.get().id);
    return Result<AgentSnapshot>::success({handle, agent.behaviorId, agent.activeState, std::move(path),
                                           agent.blackboard, agent.perception, agent.lastTick});
}

Result<AgentArchive> NpcAiWorld::archive(AgentHandle handle) const {
    auto resolved = resolve(handle);
    if (!resolved.ok()) return Result<AgentArchive>::failure(resolved.status());
    const Agent& agent = resolved.value().get();
    AgentArchive result;
    result.behaviorId     = agent.behaviorId;
    result.activeState    = agent.activeState;
    result.blackboard     = agent.blackboard;
    result.pendingSignals = agent.signals;
    result.perception     = agent.perception;
    result.lastTick       = agent.lastTick;
    const auto& behavior  = behaviors_.at(agent.behaviorId);
    for (const auto state : activePath(behavior, agent.activeState)) {
        for (const auto& task : state.get().tasks) {
            const auto runtime = agent.taskRuntime.find(taskRuntimeKey(state.get().id, task.id));
            if (runtime == agent.taskRuntime.end()) continue;
            result.tasks.push_back({state.get().id, task.id, runtime->second.completed, runtime->second.memoryJson});
        }
    }
    return Result<AgentArchive>::success(std::move(result));
}

Result<AgentHandle> NpcAiWorld::restoreAgent(const AgentArchive& archive) {
    if (archive.schemaId != AgentArchive::SchemaId || archive.schemaVersion != AgentArchive::SchemaVersion)
        return failure<AgentHandle>(DiagnosticCode::Unsupported, "unsupported NPC agent archive schema");
    const auto behaviorIt = behaviors_.find(archive.behaviorId);
    if (behaviorIt == behaviors_.end())
        return failure<AgentHandle>(DiagnosticCode::NotFound, "archive behavior definition was not registered");
    const auto& behavior = behaviorIt->second;
    if (!findState(behavior, archive.activeState))
        return failure<AgentHandle>(DiagnosticCode::NotFound, "archive active state does not exist");
    if (archive.perception.size() > config_.maxMemoriesPerAgent)
        return failure<AgentHandle>(DiagnosticCode::PreconditionViolation,
                                    "archive exceeds the perception memory budget");

    Agent candidate;
    candidate.behaviorId  = archive.behaviorId;
    candidate.activeState = archive.activeState;
    candidate.blackboard  = archive.blackboard;
    candidate.signals     = archive.pendingSignals;
    candidate.perception  = archive.perception;
    candidate.lastTick    = archive.lastTick;
    for (const auto& signalName : candidate.signals)
        if (signalName.empty())
            return failure<AgentHandle>(DiagnosticCode::InvalidArgument, "archive contains an empty signal");
    std::set<std::pair<std::string, std::string>> memoryKeys;
    for (const auto& memory : candidate.perception) {
        if (memory.subject.empty() || memory.sense.empty() || !std::isfinite(memory.confidence) ||
            memory.confidence < 0.0 || memory.confidence > 1.0 || memory.forgetAfterTicks == 0)
            return failure<AgentHandle>(DiagnosticCode::InvalidArgument, "archive contains invalid perception memory");
        if (!memoryKeys.emplace(memory.subject, memory.sense).second)
            return failure<AgentHandle>(DiagnosticCode::InvalidArgument,
                                        "archive contains duplicate perception memory");
    }

    if (!behavior.blackboardSchema.empty()) {
        if (candidate.blackboard.size() > behavior.blackboardSchema.size())
            return failure<AgentHandle>(DiagnosticCode::InvalidArgument, "archive contains undeclared blackboard keys");
        for (const auto& [key, value] : candidate.blackboard) {
            const auto spec = std::find_if(behavior.blackboardSchema.begin(), behavior.blackboardSchema.end(),
                                           [&](const auto& item) { return item.key == key; });
            if (spec == behavior.blackboardSchema.end() || spec->type != valueType(value))
                return failure<AgentHandle>(DiagnosticCode::InvalidArgument,
                                            "archive blackboard does not match the behavior schema");
        }
        for (const auto& spec : behavior.blackboardSchema)
            if (spec.required && !candidate.blackboard.contains(spec.key))
                return failure<AgentHandle>(DiagnosticCode::InvalidArgument,
                                            "archive is missing a required blackboard key");
    }

    const auto path = activePath(behavior, candidate.activeState);
    if (!std::all_of(path.begin(), path.end(),
                     [&](const auto state) { return predicatesPass(state.get().enterConditions, candidate); }))
        return failure<AgentHandle>(DiagnosticCode::PreconditionViolation,
                                    "archive active path enter conditions are false");
    std::set<std::string> validTasks;
    for (const auto state : path)
        for (const auto& task : state.get().tasks) validTasks.insert(taskRuntimeKey(state.get().id, task.id));
    std::set<std::string> archivedTasks;
    for (const auto& task : archive.tasks) {
        const auto key = taskRuntimeKey(task.stateId, task.taskId);
        if (!validTasks.contains(key) || !archivedTasks.insert(key).second)
            return failure<AgentHandle>(DiagnosticCode::InvalidArgument,
                                        "archive task is duplicated or outside the active path");
        candidate.taskRuntime.emplace(key, TaskRuntime{false, task.completed, task.memoryJson});
    }
    std::sort(candidate.perception.begin(), candidate.perception.end(), [](const auto& a, const auto& b) {
        return std::pair{a.subject, a.sense} < std::pair{b.subject, b.sense};
    });

    std::uint32_t index;
    if (freeSlots_.empty()) {
        index = static_cast<std::uint32_t>(slots_.size());
        slots_.push_back({});
    } else {
        index = freeSlots_.back();
        freeSlots_.pop_back();
    }
    slots_[index].agent = std::move(candidate);
    const AgentHandle handle(index, slots_[index].generation);
    pushTrace({archive.lastTick, handle, TraceKind::AgentRestored, archive.behaviorId, archive.activeState});
    return Result<AgentHandle>::success(handle, Status::success(StatusCode::Applied));
}

bool NpcAiWorld::isStale(AgentHandle handle) const noexcept {
    if (!handle.isValid() || handle.index() >= slots_.size()) return true;
    const auto& slot = slots_[handle.index()];
    return !slot.agent || slot.generation != handle.generation();
}

Result<std::vector<TraceEvent>> NpcAiWorld::trace(AgentHandle handle, std::uint64_t fromTick) const {
    auto resolved = resolve(handle);
    if (!resolved.ok()) return Result<std::vector<TraceEvent>>::failure(resolved.status());
    std::vector<TraceEvent> result;
    for (const auto& event : trace_)
        if (event.agent == handle && event.tick >= fromTick) result.push_back(event);
    return Result<std::vector<TraceEvent>>::success(std::move(result));
}

}  // namespace eve::npc_ai
