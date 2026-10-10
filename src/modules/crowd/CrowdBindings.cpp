#include "common/SquirrelBinding.h"
#include "crowd/CrowdInternal.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::crowd {
namespace {
Result<SpawnBatch> readSpawnBatch(ssq::Array agents, ssq::Table options) {
    try {
        if (agents.size() > 1024)
            return Result<SpawnBatch>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "Spawn batches support at most 1024 agents", "agents"));
        SpawnBatch batch;
        const auto mode = options.get<std::string>("policy");
        if (mode == "reject")
            batch.policy = SpawnPolicy::RejectOverlap;
        else if (mode == "nearestFree")
            batch.policy = SpawnPolicy::NearestFree;
        else if (mode == "pushNeighbors")
            batch.policy = SpawnPolicy::PushNeighbors;
        else
            return Result<SpawnBatch>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Unknown spawn policy", "policy"));
        batch.maxDistance   = options.get<float>("maxDistance");
        batch.searchSpacing = options.get<float>("searchSpacing");
        batch.maxPasses     = options.get<int>("maxPasses");
        batch.maxChecks     = options.get<int>("maxChecks");
        for (size_t i = 0; i < agents.size(); ++i) {
            auto         item = agents.get<ssq::Table>(i);
            SpawnRequest request;
            request.stableId    = item.get<std::string>("stableId");
            request.x           = item.get<float>("x");
            request.y           = item.get<float>("y");
            request.heading     = item.get<float>("heading");
            request.radius      = item.get<float>("radius");
            request.interaction = {item.get<float>("pushability"), item.get<bool>("holdPosition"),
                                   item.get<int>("layer"), item.get<int>("mask")};
            batch.agents.push_back(std::move(request));
        }
        return Result<SpawnBatch>::success(std::move(batch));
    } catch (const std::exception &error) {
        return Result<SpawnBatch>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, error.what(), "spawnBatch"));
    }
}
}  // namespace

// --- 脚本绑定 ---

void Crowd::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Crowd::create, false);
    expose(cls);

    auto state = table.addClass<AgentState>("CrowdAgentState", ssq::Class::Ctor<AgentState()>());
    state.addVar("x", &AgentState::x);
    state.addVar("y", &AgentState::y);
    state.addVar("heading", &AgentState::heading);
    state.addVar("speed", &AgentState::speed);
    state.addVar("vx", &AgentState::vx);
    state.addVar("vy", &AgentState::vy);
    state.addVar("action", &AgentState::action);
    state.addVar("data", &AgentState::data);
    state.addVar("avoidancePriority", &AgentState::avoidancePriority);

    auto flow = table.addClass<FlowVec>("CrowdFlowVec", ssq::Class::Ctor<FlowVec()>());
    flow.addVar("x", &FlowVec::x);
    flow.addVar("y", &FlowVec::y);
}

void Crowd::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Crowd::getName);

    // 流场
    cls.addFunc("resizeField", &Crowd::resizeField);
    cls.addFunc("setBlocked", &Crowd::setBlocked);
    cls.addFunc("setCellCost", &Crowd::setCellCost);
    cls.addFunc("getCellCost", &Crowd::getCellCost);
    cls.addFunc("buildFlowField", &Crowd::buildFlowField);
    cls.addFunc("addFlowGoal", &Crowd::addFlowGoal);
    cls.addFunc("clearFlowGoals", &Crowd::clearFlowGoals);
    cls.addFunc("build", &Crowd::build);
    cls.addFunc("isFieldBuilt", &Crowd::isFieldBuilt);
    cls.addFunc("isReachable", &Crowd::isReachable);
    cls.addFunc("getFieldWidth", &Crowd::getFieldWidth);
    cls.addFunc("getFieldHeight", &Crowd::getFieldHeight);
    cls.addFunc("getCellSize", &Crowd::getCellSize);
    cls.addFunc("getFieldOriginX", &Crowd::getFieldOriginX);
    cls.addFunc("getFieldOriginY", &Crowd::getFieldOriginY);
    cls.addFunc("flowAtWorld", &Crowd::flowAtWorld);
    cls.addFunc("costAtWorld", &Crowd::costAtWorld);
    cls.addFunc("flowAtCell", &Crowd::flowAtCell);

    // 单位
    cls.addFunc("addAgent", &Crowd::addAgent);
    cls.addFunc("addNamedAgent", &Crowd::addNamedAgent);
    cls.addFunc("hasNamedAgent", &Crowd::hasNamedAgent);
    cls.addFunc("getNamedAgentIndex", &Crowd::getNamedAgentIndex);
    cls.addFunc("getAgentStableId", &Crowd::getAgentStableId);
    cls.addFunc("removeNamedAgent", &Crowd::removeNamedAgent);
    cls.addFunc("removeAgent", &Crowd::removeAgent);
    cls.addFunc("clearAgents", &Crowd::clearAgents);
    cls.addFunc("getAgentCount", &Crowd::getAgentCount);
    cls.addFunc("setMaxAgents", &Crowd::setMaxAgents);
    cls.addFunc("getMaxAgents", &Crowd::getMaxAgents);
    cls.addFunc("setAgentAction", &Crowd::setAgentAction);
    cls.addFunc("getAgentAction", &Crowd::getAgentAction);
    cls.addFunc("setAgentTarget", &Crowd::setAgentTarget);
    cls.addFunc("clearAgentTarget", &Crowd::clearAgentTarget);
    cls.addFunc("setAgentSpeed", &Crowd::setAgentSpeed);
    cls.addFunc("setAgentAccel", &Crowd::setAgentAccel);
    cls.addFunc("setAgentTurnRate", &Crowd::setAgentTurnRate);
    cls.addFunc("setAgentRadius", &Crowd::setAgentRadius);
    cls.addFunc("setAgentData", &Crowd::setAgentData);
    cls.addFunc("getAgentData", &Crowd::getAgentData);
    cls.addFunc("setAgentAvoidancePriority",
                [](Crowd *crowd, int id, int priority) { return crowd->setAgentAvoidancePriority(id, priority).ok(); });
    cls.addFunc("getAgentAvoidancePriority", &Crowd::getAgentAvoidancePriority);
    cls.addFunc("setAgentInteraction", [vm = cls.getHandle()](Crowd *crowd, int id, float pushability,
                                                              bool holdPosition, int layer, int mask) {
        return script::projectResult(vm, crowd->setAgentInteraction(id, {pushability, holdPosition, layer, mask}));
    });
    cls.addFunc("getAgentInteraction", [vm = cls.getHandle()](Crowd *crowd, int id) {
        return script::projectResult(vm, crowd->getAgentInteraction(id), [](AgentInteraction policy) {
            return Value(Value::Object{{"pushability", Value(policy.pushability)},
                                       {"holdPosition", Value(policy.holdPosition)},
                                       {"layer", Value(policy.layer)},
                                       {"mask", Value(policy.mask)}});
        });
    });
    cls.addFunc("setAgentPosition", &Crowd::setAgentPosition);
    cls.addFunc("applySpawnBatch", [vm = cls.getHandle()](Crowd *crowd, ssq::Array agents, ssq::Table options) {
        auto       batch   = readSpawnBatch(std::move(agents), std::move(options));
        const auto project = [](const SpawnReceipt &receipt) {
            Value::Array created;
            for (const auto &item : receipt.created)
                created.emplace_back(
                    Value::Object{{"stableId", Value(item.stableId)}, {"x", Value(item.x)}, {"y", Value(item.y)}});
            return Value(Value::Object{{"created", Value(std::move(created))},
                                       {"displacedAgents", Value(receipt.displacedAgents)},
                                       {"checks", Value(receipt.checks)}});
        };
        if (!batch) return script::projectResult(vm, Result<SpawnReceipt>::failure(batch.status()), project);
        return script::projectResult(vm, crowd->applySpawnBatch(batch.value()), project);
    });
    cls.addFunc("getAgentState", &Crowd::getAgentState);

    // 群体参数
    cls.addFunc("setDefaultSpeed", &Crowd::setDefaultSpeed);
    cls.addFunc("setDefaultRadius", &Crowd::setDefaultRadius);
    cls.addFunc("setDefaultTurnRate", &Crowd::setDefaultTurnRate);
    cls.addFunc("setArriveRadius", &Crowd::setArriveRadius);
    cls.addFunc("setSeparationRadius", &Crowd::setSeparationRadius);
    cls.addFunc("setPerceptionRadius", &Crowd::setPerceptionRadius);
    cls.addFunc("setSeparationWeight", &Crowd::setSeparationWeight);
    cls.addFunc("setAlignmentWeight", &Crowd::setAlignmentWeight);
    cls.addFunc("setCohesionWeight", &Crowd::setCohesionWeight);
    cls.addFunc("setWanderWeight", &Crowd::setWanderWeight);
    cls.addFunc("setGoalWeight", &Crowd::setGoalWeight);
    cls.addFunc("setResolveOverlaps", &Crowd::setResolveOverlaps);
    cls.addFunc("setClampToField", &Crowd::setClampToField);
    cls.addFunc("configureAvoidance", [vm = cls.getHandle()](Crowd *crowd, bool enabled, float horizon, float margin,
                                                             int maxNeighbors) {
        return script::projectResult(vm, crowd->configureAvoidance({enabled, horizon, margin, maxNeighbors}));
    });
    cls.addFunc("getAvoidanceSettings", [vm = cls.getHandle()](Crowd *crowd) {
        const auto settings = crowd->getAvoidanceSettings();
        ssq::Table result(vm);
        result.set("enabled", settings.enabled);
        result.set("horizon", settings.horizon);
        result.set("margin", settings.margin);
        result.set("maxNeighbors", settings.maxNeighbors);
        return result;
    });

    // 批量读取（脚本预分配数组）
    cls.addFunc("getPositions", [](Crowd *c, ssq::Array xs, ssq::Array ys) {
        if (!c) return;
        const size_t n = std::min<size_t>({xs.size(), ys.size(), c->impl_->xs.size()});
        for (size_t i = 0; i < n; ++i) {
            xs.set(i, c->impl_->xs[i]);
            ys.set(i, c->impl_->ys[i]);
        }
    });
    cls.addFunc("getHeadings", [](Crowd *c, ssq::Array hs) {
        if (!c) return;
        const size_t n = std::min<size_t>(hs.size(), c->impl_->headings.size());
        for (size_t i = 0; i < n; ++i) hs.set(i, c->impl_->headings[i]);
    });
    cls.addFunc("step", &Crowd::step);
    cls.addFunc("advance", [vm = cls.getHandle()](Crowd *crowd, float dt) {
        return script::projectResult(vm, crowd->advance(dt), [](StepReport report) {
            return Value(Value::Object{{"avoidanceChecks", Value(report.avoidanceChecks)},
                                       {"avoidanceTruncations", Value(report.avoidanceTruncations)},
                                       {"substeps", Value(report.substeps)},
                                       {"unresolvedContacts", Value(report.unresolvedContacts)},
                                       {"unresolvedWalls", Value(report.unresolvedWalls)},
                                       {"maxPenetration", Value(report.maxPenetration)}});
        });
    });
}

}  // namespace eve::crowd
