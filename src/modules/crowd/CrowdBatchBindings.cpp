#include "common/SquirrelBinding.h"
#include "crowd/CrowdInternal.h"

#include <limits>

namespace eve::crowd {
namespace {
std::int64_t integerField(const ssq::Table& table, const char* key) {
    const auto value = table.find(key);
    if (value.getType() != ssq::Type::INTEGER) throw ssq::TypeException("Agent reference needs integer fields");
    return value.to<std::int64_t>();
}
AgentRef parseReference(const ssq::Table& table) {
    const auto owner = integerField(table, "owner"), index = integerField(table, "index"),
               generation = integerField(table, "generation");
    if (owner <= 0 || index < 0 || index >= AgentHandle::invalidIndex || generation <= 0 ||
        generation > std::numeric_limits<std::uint32_t>::max())
        throw ssq::TypeException("Agent reference out of range");
    return {AgentHandle(static_cast<std::uint32_t>(index), static_cast<std::uint32_t>(generation)),
            static_cast<std::uint64_t>(owner)};
}
Value projectReference(AgentRef reference) {
    return Value(Value::Object{{"owner", Value(static_cast<std::int64_t>(reference.owner))},
                               {"index", Value(static_cast<std::int64_t>(reference.handle.index()))},
                               {"generation", Value(static_cast<std::int64_t>(reference.handle.generation()))}});
}
Value projectState(AgentState state) {
    return Value(Value::Object{
        {"x", Value(state.x)},
        {"y", Value(state.y)},
        {"heading", Value(state.heading)},
        {"vx", Value(state.vx)},
        {"vy", Value(state.vy)},
        {"speed", Value(state.speed)},
        {"action", Value(state.action)},
        {"data", Value(state.data)},
        {"avoidancePriority", Value(state.avoidancePriority)},
        {"flowGeneration", Value(std::to_string(state.flowGeneration))},
        {"movement", Value(Value::Object{{"classId", Value(std::to_string(state.movement.movement.value()))},
                                         {"revision", Value(std::to_string(state.movement.revision))}})}});
}
Value projectReceipt(AgentBatchReceipt receipt) {
    return Value(Value::Object{{"applied", Value(static_cast<std::int64_t>(receipt.applied))}});
}
template <class T>
std::optional<T> optionalField(const ssq::Table& table, const char* key) {
    try {
        return table.find(key).to<T>();
    } catch (const ssq::NotFoundException&) {
        return std::nullopt;
    }
}
AgentUpdate parseUpdate(Crowd& crowd, const ssq::Table& table) {
    AgentUpdate update;
    update.reference       = parseReference(table.find("reference").to<ssq::Table>());
    const auto flow        = optionalField<ssq::Table>(table, "flowRequest");
    const auto defaultFlow = optionalField<bool>(table, "useDefaultFlow");
    if (flow && defaultFlow) throw ssq::TypeException("flowRequest and useDefaultFlow cannot be combined");
    if (flow) {
        auto resolved = resolveFlowRequestLease(crowd, *flow);
        if (!resolved) throw ssq::TypeException("Invalid, stale or foreign request-owned lease");
        update.flow = std::move(resolved).takeValue();
    }
    if (defaultFlow && *defaultFlow) update.flow = FlowLease{};

    if (auto item = optionalField<ssq::Table>(table, "movement")) update.movement = parseMovementBinding(*item);
    update.maxSpeed          = optionalField<float>(table, "maxSpeed");
    update.maxAcceleration   = optionalField<float>(table, "maxAcceleration");
    update.headingTurnRate   = optionalField<float>(table, "headingTurnRate");
    update.radius            = optionalField<float>(table, "radius");
    update.avoidancePriority = optionalField<int>(table, "avoidancePriority");
    update.data              = optionalField<int>(table, "data");
    update.holdPosition      = optionalField<bool>(table, "holdPosition");
    if (auto item = optionalField<ssq::Table>(table, "interaction")) {
        update.interaction = {item->get<float>("pushability"), item->get<bool>("holdPosition"), item->get<int>("layer"),
                              item->get<int>("mask")};
    }
    if (auto item = optionalField<ssq::Table>(table, "policy")) update.policy = parseInteractionPolicy(*item);
    if (auto item = optionalField<ssq::Table>(table, "intent")) {
        const auto  action = item->get<std::string>("action");
        AgentAction mode;
        if (action == "idle")
            mode = AgentAction::Idle;
        else if (action == "flow")
            mode = AgentAction::Flow;
        else if (action == "seek")
            mode = AgentAction::Seek;
        else if (action == "boids")
            mode = AgentAction::Boids;
        else
            throw ssq::TypeException("Unknown steering action");
        update.intent = {mode, item->get<bool>("hasTarget"), item->get<float>("targetX"), item->get<float>("targetY")};
    }
    return update;
}
template <class T>
Result<T> malformed(const std::exception& error) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, error.what(), "agentBatch"));
}
Value projectFrame(const AgentFrame& frame) {
    Value::Array rows;
    rows.reserve(frame.agents.size());
    for (const auto& entry : frame.agents)
        rows.emplace_back(
            Value::Object{{"reference", projectReference(entry.reference)}, {"state", projectState(entry.state)}});
    return Value(Value::Object{{"agents", Value(std::move(rows))},
                               {"tick", Value(std::to_string(frame.tick.value()))},
                               {"simulationSeconds", Value(frame.simulationSeconds)}});
}
}  // namespace

MovementBinding parseMovementBinding(const ssq::Table& table) {
    const auto identity = integerField(table, "classId"), revision = integerField(table, "revision");
    if (identity < 0 || revision < 0 || ((identity == 0) != (revision == 0)))
        throw ssq::TypeException("Movement class needs both positive identities or both zero");
    return {MovementClassId(static_cast<std::uint64_t>(identity)), static_cast<std::uint64_t>(revision)};
}

InteractionPolicy parseInteractionPolicy(ssq::Table table) {
    const auto mask = [&](const char* key) {
        const auto value = integerField(table, key);
        if (value < 0 || value > std::numeric_limits<int>::max())
            throw ssq::TypeException("Interaction mask out of range");
        return static_cast<int>(value);
    };
    return {table.get<float>("pushability"),
            table.get<bool>("holdPosition"),
            mask("avoidanceLayer"),
            mask("avoidanceMask"),
            mask("contactLayer"),
            mask("contactMask")};
}

void exposeAgentBatches(ssq::Class& cls) {
    cls.addFunc("advanceTick", [vm = cls.getHandle()](Crowd* crowd, ssq::Table input) {
        CrowdStep step;
        try {
            const auto tick = integerField(input, "tick");
            if (tick < 0) throw ssq::TypeException("Tick must be nonnegative");
            step.tick  = SimulationTick(static_cast<std::uint64_t>(tick));
            step.delta = Duration::fromNanoseconds(integerField(input, "deltaNanoseconds"));
            if (auto budget = optionalField<ssq::Table>(input, "budget")) {
                const auto boundedInt = [&](const char* key, int& destination) {
                    if (budget->find(key).getType() != ssq::Type::INTEGER)
                        throw ssq::TypeException("Budget needs integer fields");
                    const auto value = integerField(*budget, key);
                    if (value < 0 || value > std::numeric_limits<int>::max())
                        throw ssq::TypeException("Budget out of range");
                    destination = static_cast<int>(value);
                };
                boundedInt("maxSubsteps", step.budget.maxSubsteps);
                boundedInt("maxContactPasses", step.budget.maxContactPasses);
                step.budget.maxGridCells        = integerField(*budget, "maxGridCells");
                step.budget.maxNeighborVisits   = integerField(*budget, "maxNeighborVisits");
                step.budget.maxPredictionChecks = integerField(*budget, "maxPredictionChecks");
                step.budget.maxContactChecks    = integerField(*budget, "maxContactChecks");
                try {
                    step.budget.maxSpatialVisits = integerField(*budget, "maxSpatialVisits");
                } catch (const ssq::NotFoundException&) {
                    // Absent extension retains the canonical unlimited query budget.
                }
                try {
                    step.budget.maxContactPairs = integerField(*budget, "maxContactPairs");
                } catch (const ssq::NotFoundException&) {
                }
                try {
                    step.budget.maxPairVisits = integerField(*budget, "maxPairVisits");
                } catch (const ssq::NotFoundException&) {
                }
                try {
                    step.budget.maxPreferredNeighbors = integerField(*budget, "maxPreferredNeighbors");
                } catch (const ssq::NotFoundException&) {
                }
                try {
                    step.budget.maxPreferredVisits = integerField(*budget, "maxPreferredVisits");
                } catch (const ssq::NotFoundException&) {
                }
            }
        } catch (const std::exception& error) {
            return script::projectResult(vm, malformed<StepReport>(error), [](StepReport) { return Value(); });
        }
        return script::projectResult(vm, crowd->advance(step), [](StepReport report) {
            return Value(Value::Object{{"tick", Value(std::to_string(report.tick.value()))},
                                       {"elapsedNanoseconds", Value(report.elapsed.nanoseconds())},
                                       {"quality", Value(static_cast<int>(report.quality))},
                                       {"navigationState", Value(static_cast<int>(report.navigation.state))},
                                       {"navigationRevision", Value(std::to_string(report.navigation.revision))},
                                       {"navigationClassMismatches", Value(report.navigationClassMismatches)},
                                       {"candidatesVisited", Value(report.candidatesVisited)},
                                       {"gridBuilds", Value(report.gridBuilds)},
                                       {"gridCellsPeak", Value(report.gridCellsPeak)},
                                       {"cellsVisited", Value(report.cellsVisited)},
                                       {"gridEntriesPeak", Value(report.gridEntriesPeak)},
                                       {"gridSortComparisons", Value(report.gridSortComparisons)},
                                       {"spatialVisits", Value(report.spatialVisits)},
                                       {"contactPairsPeak", Value(report.contactPairsPeak)},
                                       {"pairVisits", Value(report.pairVisits)},
                                       {"preferredNeighborsPeak", Value(report.preferredNeighborsPeak)},
                                       {"preferredVisits", Value(report.preferredVisits)},
                                       {"contactPairsChecked", Value(report.contactPairsChecked)},
                                       {"residualPairsChecked", Value(report.residualPairsChecked)},
                                       {"avoidanceChecks", Value(report.avoidanceChecks)},
                                       {"substeps", Value(report.substeps)}});
        });
    });
    cls.addFunc("setInteractionPolicy", [vm = cls.getHandle()](Crowd* crowd, ssq::Table reference, ssq::Table policy) {
        AgentRef          ref;
        InteractionPolicy settings;
        try {
            ref      = parseReference(reference);
            settings = parseInteractionPolicy(policy);
        } catch (const std::exception& error) {
            return script::projectResult(vm, malformed<void>(error));
        }
        return script::projectResult(vm, crowd->setInteractionPolicy(ref, settings));
    });
    cls.addFunc("readInteractionPolicy", [vm = cls.getHandle()](Crowd* crowd, ssq::Table reference) {
        const auto project = [](InteractionPolicy policy) {
            return Value(Value::Object{{"pushability", Value(policy.pushability)},
                                       {"holdPosition", Value(policy.holdPosition)},
                                       {"avoidanceLayer", Value(policy.avoidanceLayer)},
                                       {"avoidanceMask", Value(policy.avoidanceMask)},
                                       {"contactLayer", Value(policy.contactLayer)},
                                       {"contactMask", Value(policy.contactMask)}});
        };
        AgentRef ref;
        try {
            ref = parseReference(reference);
        } catch (const std::exception& error) {
            return script::projectResult(vm, malformed<InteractionPolicy>(error), project);
        }
        return script::projectResult(vm, crowd->readInteractionPolicy(ref), project);
    });
    cls.addFunc("agentRef", [vm = cls.getHandle()](Crowd* crowd, int slot) {
        return script::projectResult(vm, crowd->agentRef(slot), projectReference);
    });
    cls.addFunc("resolveNamedAgent", [vm = cls.getHandle()](Crowd* crowd, std::string name) {
        return script::projectResult(vm, crowd->resolveNamedAgent(name), projectReference);
    });
    cls.addFunc("isAgentAlive", [](Crowd* crowd, ssq::Table reference) {
        try {
            return crowd->isAgentAlive(parseReference(reference));
        } catch (const std::exception&) {
            return false;
        }
    });
    cls.addFunc("readAgent", [vm = cls.getHandle()](Crowd* crowd, ssq::Table reference) {
        try {
            return script::projectResult(vm, crowd->readAgent(parseReference(reference)), projectState);
        } catch (const std::exception& error) {
            return script::projectResult(vm, malformed<AgentState>(error), projectState);
        }
    });
    cls.addFunc("updateAgents", [vm = cls.getHandle()](Crowd* crowd, ssq::Array agents) {
        AgentUpdateBatch batch;
        try {
            batch.agents.reserve(agents.size());
            for (std::size_t index = 0; index < agents.size(); ++index)
                batch.agents.push_back(parseUpdate(*crowd, agents.get<ssq::Table>(index)));
        } catch (const std::exception& error) {
            return script::projectResult(vm, malformed<AgentBatchReceipt>(error), projectReceipt);
        }
        return script::projectResult(vm, crowd->updateAgents(batch), projectReceipt);
    });
    cls.addFunc("removeAgents", [vm = cls.getHandle()](Crowd* crowd, ssq::Array agents) {
        AgentRemovalBatch batch;
        try {
            batch.agents.reserve(agents.size());
            for (std::size_t index = 0; index < agents.size(); ++index)
                batch.agents.push_back(parseReference(agents.get<ssq::Table>(index)));
        } catch (const std::exception& error) {
            return script::projectResult(vm, malformed<AgentBatchReceipt>(error), projectReceipt);
        }
        return script::projectResult(vm, crowd->removeAgents(batch), projectReceipt);
    });
    cls.addFunc("relocateAgents", [vm = cls.getHandle()](Crowd* crowd, ssq::Array agents, int maxChecks) {
        AgentRelocationBatch batch;
        batch.maxChecks = maxChecks;
        try {
            if (agents.size() > 1024) throw ssq::TypeException("Relocation batches support at most 1024 agents");
            batch.agents.reserve(agents.size());
            for (std::size_t index = 0; index < agents.size(); ++index) {
                auto item = agents.get<ssq::Table>(index);
                batch.agents.push_back(
                    {parseReference(item.get<ssq::Table>("reference")), item.get<float>("x"), item.get<float>("y")});
            }
        } catch (const std::exception& error) {
            return script::projectResult(vm, malformed<AgentBatchReceipt>(error), projectReceipt);
        }
        return script::projectResult(vm, crowd->relocateAgents(batch), projectReceipt);
    });
    cls.addFunc("copyAgentFrame", [vm = cls.getHandle()](Crowd* crowd) {
        AgentFrame frame;
        auto       copied = crowd->copyAgentFrame(frame);
        if (!copied)
            return script::projectResult(vm, Result<Value>::failure(copied.status()),
                                         [](Value value) { return value; });
        return script::projectResult(vm, Result<Value>::success(projectFrame(frame)),
                                     [](Value value) { return value; });
    });
    cls.addFunc("copyAgentStates", [vm = cls.getHandle()](Crowd* crowd, ssq::Array agents) {
        try {
            std::vector<AgentRef> references;
            references.reserve(agents.size());
            for (std::size_t index = 0; index < agents.size(); ++index)
                references.push_back(parseReference(agents.get<ssq::Table>(index)));
            AgentFrame frame;
            auto       copied = crowd->copyAgentStates(references, frame);
            if (!copied)
                return script::projectResult(vm, Result<Value>::failure(copied.status()),
                                             [](Value value) { return value; });
            return script::projectResult(vm, Result<Value>::success(projectFrame(frame)),
                                         [](Value value) { return value; });
        } catch (const std::exception& error) {
            return script::projectResult(vm, malformed<Value>(error), [](Value value) { return value; });
        }
    });
}

}  // namespace eve::crowd
