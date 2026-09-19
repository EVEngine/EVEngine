#include "climbing/Climbing.h"
#include "climbing/ClimbingBindingInternal.h"

#include "animation/AnimClip.h"
#include "common/SquirrelBinding.h"
#include "physics/Body3D.h"
#include "physics/World3D.h"

#include <functional>
#include <optional>
#include <simplesquirrel/simplesquirrel.hpp>
#include <utility>

namespace eve::climbing {
namespace {

eve::Value vecValue(Vec3 value) { return eve::Value::object({{"x", value.x}, {"y", value.y}, {"z", value.z}}); }

eve::Value candidateValue(ClimbingCandidate candidate) {
    return eve::Value::object({
        {"actionId", std::move(candidate.actionId)},
        {"definitionGeneration", std::to_string(candidate.definitionGeneration)},
        {"world", static_cast<std::int64_t>(candidate.world.packed())},
        {"obstacleBody", static_cast<std::int64_t>(candidate.obstacleBody.packed())},
        {"obstacleShape", static_cast<std::int64_t>(candidate.obstacleShape.packed())},
        {"obstacleBodyId", candidate.obstacleBodyId},
        {"obstacleShapeId", candidate.obstacleShapeId},
        {"ignoredBodyId", candidate.ignoredBodyId},
        {"frontPoint", vecValue(candidate.frontPoint)},
        {"topPoint", vecValue(candidate.topPoint)},
        {"landingFeet", vecValue(candidate.landingFeet)},
        {"surfaceNormal", vecValue(candidate.surfaceNormal)},
        {"surfaceTangent", vecValue(candidate.surfaceTangent)},
        {"leftHandAnchor", vecValue(candidate.leftHandAnchor)},
        {"rightHandAnchor", vecValue(candidate.rightHandAnchor)},
        {"bodyLocalTop", vecValue(candidate.bodyLocalTop)},
        {"bodyLocalLanding", vecValue(candidate.bodyLocalLanding)},
        {"obstacleHeight", candidate.obstacleHeight},
        {"obstacleDepth", candidate.obstacleDepth},
        {"score", candidate.score},
        {"kind", static_cast<std::int64_t>(candidate.kind)},
        {"support", static_cast<std::int64_t>(candidate.support)},
    });
}

const char* phaseName(ClimbingPhase phase) {
    switch (phase) {
        case ClimbingPhase::Idle: return "idle";
        case ClimbingPhase::Requested: return "requested";
        case ClimbingPhase::Aligning: return "aligning";
        case ClimbingPhase::Launching: return "launching";
        case ClimbingPhase::Climbing: return "climbing";
        case ClimbingPhase::Landing: return "landing";
        case ClimbingPhase::Recovering: return "recovering";
        case ClimbingPhase::Hanging: return "hanging";
        case ClimbingPhase::Dropping: return "dropping";
        case ClimbingPhase::Completed: return "completed";
        case ClimbingPhase::Cancelled: return "cancelled";
        case ClimbingPhase::Failed: return "failed";
        case ClimbingPhase::Balanced: return "balanced";
        case ClimbingPhase::Swinging: return "swinging";
    }
    return "failed";
}

const char* eventKindName(ClimbingEventKind kind) {
    switch (kind) {
        case ClimbingEventKind::Started: return "started";
        case ClimbingEventKind::AnchorTransitionStarted: return "anchor_transition_started";
        case ClimbingEventKind::AnchorReached: return "anchor_reached";
        case ClimbingEventKind::ContactLeftHand: return "contact_left_hand";
        case ClimbingEventKind::ContactRightHand: return "contact_right_hand";
        case ClimbingEventKind::Landed: return "landed";
        case ClimbingEventKind::Hanging: return "hanging";
        case ClimbingEventKind::Dropped: return "dropped";
        case ClimbingEventKind::Completed: return "completed";
        case ClimbingEventKind::Cancelled: return "cancelled";
        case ClimbingEventKind::Failed: return "failed";
    }
    return "failed";
}

eve::Value eventValue(ClimbingEvent event) {
    eve::Value::Array metadata;
    metadata.reserve(event.metadata.size());
    for (std::string& value : event.metadata) metadata.emplace_back(std::move(value));
    return eve::Value::object({
        {"kind", eventKindName(event.kind)},
        {"actionId", std::move(event.actionId)},
        {"tick", std::to_string(event.tick.value())},
        {"executionId", std::to_string(event.executionId.value())},
        {"metadata", eve::Value(std::move(metadata))},
    });
}

eve::Value eventBatchValue(std::vector<ClimbingEvent> events) {
    eve::Value::Array result;
    result.reserve(events.size());
    for (auto& event : events) result.push_back(eventValue(std::move(event)));
    return eve::Value(std::move(result));
}

eve::Value advanceValue(ClimbingAdvance advance) {
    return eve::Value::object({
        {"phase", phaseName(advance.phase)},
        {"actionId", std::move(advance.actionId)},
        {"feet", vecValue(advance.feet)},
        {"desiredDelta", vecValue(advance.desiredDelta)},
        {"actualDelta", vecValue(advance.actualDelta)},
        {"warpResidual", vecValue(advance.warpResidual)},
        {"normalizedTime", advance.normalizedTime},
        {"constrained", advance.constrained},
        {"grounded", advance.grounded},
        {"support", static_cast<std::int64_t>(advance.support)},
        {"leftHandAnchor", vecValue(advance.leftHandAnchor)},
        {"rightHandAnchor", vecValue(advance.rightHandAnchor)},
        {"contactWeight", advance.contactWeight},
        {"leftHandWeight", advance.leftHandWeight},
        {"rightHandWeight", advance.rightHandWeight},
        {"leftFootWeight", advance.leftFootWeight},
        {"rightFootWeight", advance.rightFootWeight},
        {"pelvisWeight", advance.pelvisWeight},
        {"compactCollisionRequested", advance.compactCollisionRequested},
        {"compactCollisionActive", advance.compactCollisionActive},
        {"branchWindowOpen", advance.branchWindowOpen},
        {"branchComboTag", std::move(advance.branchComboTag)},
        {"executionId", std::to_string(advance.executionId.value())},
        {"appliedWarp", vecValue(advance.appliedWarp)},
        {"desiredYawDelta", advance.desiredYawDelta},
        {"cameraCueProfile", std::move(advance.cameraCueProfile)},
        {"cameraCue", std::move(advance.cameraCue)},
        {"animationClipId", std::move(advance.animationClipId)},
        {"animationGraphNodeId", std::move(advance.animationGraphNodeId)},
        {"animationMirrored", advance.animationMirrored},
        {"terminalVelocity", vecValue(advance.terminalVelocity)},
        {"hasTerminalVelocity", advance.hasTerminalVelocity},
    });
}

eve::Value debugValue(ClimbingDebugSnapshot snapshot) {
    eve::Value::Array candidates;
    candidates.reserve(snapshot.candidates.size());
    for (auto& candidate : snapshot.candidates) candidates.push_back(candidateValue(std::move(candidate)));
    eve::Value::Array queries;
    queries.reserve(snapshot.queries.size());
    for (auto& query : snapshot.queries)
        queries.push_back(eve::Value::object({{"actionId", std::move(query.actionId)},
                                              {"code", std::move(query.code)},
                                              {"start", vecValue(query.start)},
                                              {"end", vecValue(query.end)},
                                              {"radius", query.radius},
                                              {"height", query.height}}));
    eve::Value::Array evidence;
    evidence.reserve(snapshot.evidence.size());
    for (auto& item : snapshot.evidence)
        evidence.push_back(eve::Value::object({{"actionId", std::move(item.actionId)},
                                               {"code", std::move(item.code)},
                                               {"biasCost", item.biasCost},
                                               {"heightCost", item.heightCost},
                                               {"distanceCost", item.distanceCost},
                                               {"totalCost", item.totalCost}}));
    eve::Value::Array motion;
    motion.reserve(snapshot.motion.size());
    for (const auto& item : snapshot.motion)
        motion.push_back(eve::Value::object({{"tick", std::to_string(item.tick.value())},
                                             {"plannedFeet", vecValue(item.plannedFeet)},
                                             {"actualFeet", vecValue(item.actualFeet)},
                                             {"residual", vecValue(item.residual)},
                                             {"capsuleHeight", item.capsuleHeight},
                                             {"constrained", item.constrained}}));
    return eve::Value::object({
        {"phase", phaseName(snapshot.phase)},
        {"candidates", eve::Value(std::move(candidates))},
        {"accumulatedWarpResidual", vecValue(snapshot.accumulatedWarpResidual)},
        {"broadPhaseQueryCount", static_cast<std::int64_t>(snapshot.broadPhaseQueryCount)},
        {"broadPhaseHitCount", static_cast<std::int64_t>(snapshot.broadPhaseHitCount)},
        {"queryCount", static_cast<std::int64_t>(snapshot.queryCount)},
        {"terminalCode", std::move(snapshot.terminalCode)},
        {"executionId", static_cast<std::int64_t>(snapshot.executionId.value())},
        {"queries", eve::Value(std::move(queries))},
        {"evidence", eve::Value(std::move(evidence))},
        {"motion", eve::Value(std::move(motion))},
    });
}

std::optional<ClimbingActionKind> parseActionKind(std::string_view value) {
    if (value == "vault") return ClimbingActionKind::Vault;
    if (value == "mantle") return ClimbingActionKind::Mantle;
    if (value == "ledge_grab") return ClimbingActionKind::LedgeGrab;
    if (value == "climb_up") return ClimbingActionKind::ClimbUp;
    if (value == "shimmy") return ClimbingActionKind::Shimmy;
    if (value == "corner_inner") return ClimbingActionKind::CornerInner;
    if (value == "corner_outer") return ClimbingActionKind::CornerOuter;
    if (value == "ledge_jump") return ClimbingActionKind::LedgeJump;
    if (value == "climb_down") return ClimbingActionKind::ClimbDown;
    if (value == "ladder_mount") return ClimbingActionKind::LadderMount;
    if (value == "ladder_climb") return ClimbingActionKind::LadderClimb;
    if (value == "ladder_dismount") return ClimbingActionKind::LadderDismount;
    if (value == "wall_run") return ClimbingActionKind::WallRun;
    if (value == "slide") return ClimbingActionKind::Slide;
    if (value == "beam_balance") return ClimbingActionKind::BeamBalance;
    if (value == "pole_swing") return ClimbingActionKind::PoleSwing;
    if (value == "bar_swing") return ClimbingActionKind::BarSwing;
    return std::nullopt;
}

std::optional<ClimbingAnchorEdgeKind> parseAnchorEdgeKind(std::string_view value) {
    if (value == "shimmy") return ClimbingAnchorEdgeKind::Shimmy;
    if (value == "corner") return ClimbingAnchorEdgeKind::Corner;
    if (value == "jump") return ClimbingAnchorEdgeKind::Jump;
    if (value == "drop") return ClimbingAnchorEdgeKind::Drop;
    if (value == "mount") return ClimbingAnchorEdgeKind::Mount;
    if (value == "dismount") return ClimbingAnchorEdgeKind::Dismount;
    if (value == "climb") return ClimbingAnchorEdgeKind::Climb;
    if (value == "balance") return ClimbingAnchorEdgeKind::Balance;
    if (value == "swing") return ClimbingAnchorEdgeKind::Swing;
    return std::nullopt;
}

eve::Value anchorNodeValue(ClimbingAnchorNodeRef reference) {
    return eve::Value::object({
        {"graphId", std::move(reference.graphId)},
        {"nodeId", std::move(reference.nodeId)},
        {"graphGeneration", std::to_string(reference.graphGeneration)},
    });
}

eve::Value graphReloadValue(ClimbingAnchorGraphReload reload) {
    eve::Value::Array invalidated;
    invalidated.reserve(reload.invalidatedOccupants.size());
    for (const auto& occupant : reload.invalidatedOccupants)
        invalidated.push_back(eve::Value::object({
            {"agentId", std::to_string(occupant.agentId.value())},
            {"executionId", std::to_string(occupant.executionId.value())},
        }));
    return eve::Value::object({
        {"oldGeneration", std::to_string(reload.oldGeneration)},
        {"newGeneration", std::to_string(reload.newGeneration)},
        {"invalidatedOccupants", eve::Value(std::move(invalidated))},
    });
}

const char* anchorEdgeKindName(ClimbingAnchorEdgeKind kind);

eve::Value anchorRouteValue(ClimbingAnchorRoute route) {
    eve::Value::Array nodes;
    nodes.reserve(route.nodes.size());
    for (auto& node : route.nodes) nodes.push_back(anchorNodeValue(std::move(node)));
    eve::Value::Array steps;
    steps.reserve(route.steps.size());
    for (auto& step : route.steps)
        steps.push_back(eve::Value::object({
            {"from", anchorNodeValue(std::move(step.from))},
            {"to", anchorNodeValue(std::move(step.to))},
            {"kind", anchorEdgeKindName(step.kind)},
        }));
    return eve::Value::object({
        {"graphId", std::move(route.graphId)},
        {"graphGeneration", std::to_string(route.graphGeneration)},
        {"nodes", eve::Value(std::move(nodes))},
        {"steps", eve::Value(std::move(steps))},
    });
}

const char* anchorKindName(ClimbingAnchorKind kind) {
    switch (kind) {
        case ClimbingAnchorKind::Ledge: return "ledge";
        case ClimbingAnchorKind::CornerInner: return "corner_inner";
        case ClimbingAnchorKind::CornerOuter: return "corner_outer";
        case ClimbingAnchorKind::LadderRung: return "ladder_rung";
        case ClimbingAnchorKind::Pole: return "pole";
        case ClimbingAnchorKind::Beam: return "beam";
        case ClimbingAnchorKind::Bar: return "bar";
    }
    return "unknown";
}

const char* anchorEdgeKindName(ClimbingAnchorEdgeKind kind) {
    switch (kind) {
        case ClimbingAnchorEdgeKind::Shimmy: return "shimmy";
        case ClimbingAnchorEdgeKind::Corner: return "corner";
        case ClimbingAnchorEdgeKind::Jump: return "jump";
        case ClimbingAnchorEdgeKind::Drop: return "drop";
        case ClimbingAnchorEdgeKind::Mount: return "mount";
        case ClimbingAnchorEdgeKind::Dismount: return "dismount";
        case ClimbingAnchorEdgeKind::Climb: return "climb";
        case ClimbingAnchorEdgeKind::Balance: return "balance";
        case ClimbingAnchorEdgeKind::Swing: return "swing";
    }
    return "unknown";
}

eve::Value anchorOverlayValue(ClimbingAnchorAuthoringOverlay overlay) {
    eve::Value::Array nodes;
    nodes.reserve(overlay.nodes.size());
    for (auto& node : overlay.nodes)
        nodes.push_back(eve::Value::object({
            {"id", std::move(node.id)},
            {"kind", anchorKindName(node.kind)},
            {"position", vecValue(node.position)},
            {"normal", vecValue(node.normal)},
            {"tangent", vecValue(node.tangent)},
            {"leftHandSocket", vecValue(node.leftHandSocket)},
            {"rightHandSocket", vecValue(node.rightHandSocket)},
            {"feetSocket", vecValue(node.feetSocket)},
            {"occupancySlots", static_cast<std::int64_t>(node.occupancySlots)},
        }));
    eve::Value::Array edges;
    edges.reserve(overlay.edges.size());
    for (auto& edge : overlay.edges)
        edges.push_back(eve::Value::object({
            {"from", std::move(edge.from)},
            {"to", std::move(edge.to)},
            {"kind", anchorEdgeKindName(edge.kind)},
            {"bidirectional", edge.bidirectional},
        }));
    return eve::Value::object({
        {"graphId", std::move(overlay.graphId)},
        {"buildSettingsHash", std::move(overlay.buildSettingsHash)},
        {"nodes", eve::Value(std::move(nodes))},
        {"edges", eve::Value(std::move(edges))},
    });
}

eve::Result<eve::Value> anchorBakeResultValue(ClimbingAnchorBakeResult result) {
    auto encoded = encodeClimbingAnchorGraphDefinition(result.graph);
    if (!encoded) return eve::Result<eve::Value>::failure(encoded.status());
    auto json = encoded.value().toJson();
    if (!json) return eve::Result<eve::Value>::failure(json.status());
    return eve::Result<eve::Value>::success(eve::Value::object({
        {"graphJson", std::move(json).takeValue()},
        {"buildSettingsHash", std::move(result.graph.buildSettingsHash)},
        {"ledgeNodeCount", static_cast<std::int64_t>(result.ledgeNodeCount)},
        {"ladderNodeCount", static_cast<std::int64_t>(result.ladderNodeCount)},
    }));
}

eve::Value identityValue(eve::Value value) { return value; }

std::vector<std::string> requiredNotifiesForKind(ClimbingActionKind kind) {
    switch (kind) {
        case ClimbingActionKind::Vault: return {"contact.left_hand", "land"};
        case ClimbingActionKind::Mantle:
        case ClimbingActionKind::ClimbUp: return {"contact.left_hand", "contact.right_hand", "land"};
        case ClimbingActionKind::LedgeGrab: return {"contact.left_hand", "contact.right_hand"};
        case ClimbingActionKind::Shimmy:
        case ClimbingActionKind::CornerInner:
        case ClimbingActionKind::CornerOuter:
        case ClimbingActionKind::LedgeJump:
        case ClimbingActionKind::ClimbDown:
        case ClimbingActionKind::LadderMount:
        case ClimbingActionKind::LadderClimb: return {"contact.left_hand", "contact.right_hand"};
        case ClimbingActionKind::LadderDismount:
            return {"contact.left_hand", "contact.right_hand", "land"};
        case ClimbingActionKind::Slide: return {"collision.compact", "land"};
        case ClimbingActionKind::PoleSwing:
        case ClimbingActionKind::BarSwing: return {"contact.left_hand", "contact.right_hand"};
        case ClimbingActionKind::WallRun:
        case ClimbingActionKind::BeamBalance: return {};
    }
    return {};
}

std::optional<ClimbingCancelReason> parseCancelReason(std::string_view value) {
    if (value == "player_request") return ClimbingCancelReason::PlayerRequest;
    if (value == "drop_requested") return ClimbingCancelReason::DropRequested;
    if (value == "link_stale") return ClimbingCancelReason::LinkStale;
    if (value == "anchor_stale") return ClimbingCancelReason::AnchorStale;
    if (value == "motion_blocked") return ClimbingCancelReason::MotionBlocked;
    if (value == "warp_budget_exceeded") return ClimbingCancelReason::WarpBudgetExceeded;
    if (value == "definition_reloaded") return ClimbingCancelReason::DefinitionReloaded;
    return std::nullopt;
}

template <class Ref, class Proxy, class Release>
ssq::Table makeOwnedProxy(HSQUIRRELVM vm, eve::Result<Ref>&& reference, Release&& release) {
    if (!reference) return eve::script::projectStatusResult(vm, reference.status(), false, false);
    const Ref ref    = std::move(reference).takeValue();
    auto      object = eve::script::makeOwnedSquirrelInstance<Proxy>(vm, std::make_unique<Proxy>(ref));
    if (!object) {
        const eve::Status status = object.status();
        object.ignore("failed to create owned climbing proxy");
        std::invoke(std::forward<Release>(release), ref).ignore("rollback failed climbing allocation");
        return eve::script::projectStatusResult(vm, status, false, false);
    }
    ssq::Object owned = std::move(object).takeValue();
    auto result = eve::script::projectStatusResult(vm, eve::Status::success(eve::StatusCode::Applied), true, false);
    result.set("value", owned);
    result.set("ownership", std::string("owned"));
    result.set("ownerEpoch", static_cast<std::int64_t>(ref.ownerEpoch));
    result.set("handle", static_cast<std::int64_t>(ref.packed()));
    return result;
}


struct ScriptClimbingAnchorGraph {
    explicit ScriptClimbingAnchorGraph(ClimbingAnchorGraphHandleRef value) : reference(value) {}
    ~ScriptClimbingAnchorGraph() noexcept {
        Climbing::releaseAnchorGraph(reference).ignore("script climbing anchor graph proxy destruction");
    }
    ClimbingAnchorGraphHandleRef reference;
};

}  // namespace

eve::Value projectClimbingAdvance(ClimbingAdvance value) { return advanceValue(std::move(value)); }

Module_IMPL(Climbing, new Climbing());

void Climbing::expose(ssq::Table& table) {
    const HSQUIRRELVM vm      = table.getHandle();
    auto graph = table.addClass<ScriptClimbingAnchorGraph>(
        "ClimbingAnchorGraph", std::function<ScriptClimbingAnchorGraph*()>([]() { return nullptr; }), false);
    graph.addFunc("ownership", [](ScriptClimbingAnchorGraph*) { return std::string("owned"); });
    graph.addFunc("isStale", [](ScriptClimbingAnchorGraph* value) {
        return !value || Climbing::isAnchorGraphStale(value->reference);
    });
    graph.addFunc("release", [vm](ScriptClimbingAnchorGraph* value) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "climbing anchor graph proxy must not be null", "anchorGraph", {}, "climbing.binding")));
        return eve::script::projectResult(vm, Climbing::releaseAnchorGraph(value->reference));
    });
    graph.addFunc("node", [vm](ScriptClimbingAnchorGraph* value, const std::string& nodeId) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<ClimbingAnchorNodeRef>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "climbing anchor graph proxy must not be null", "anchorGraph", {}, "climbing.binding")),
                anchorNodeValue);
        auto resolved = Climbing::resolveAnchorGraph(value->reference);
        return eve::script::projectResult(
            vm,
            resolved.isBound()
                ? resolved->nodeRef(nodeId)
                : eve::Result<ClimbingAnchorNodeRef>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "climbing anchor graph handle is stale", "anchorGraph", {}, "climbing.binding")),
            anchorNodeValue);
    });
    graph.addFunc("reloadJson", [vm](ScriptClimbingAnchorGraph* value, const std::string& json) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<ClimbingAnchorGraphReload>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "climbing anchor graph proxy must not be null", "anchorGraph", {}, "climbing.binding")),
                graphReloadValue);
        auto parsed = eve::Value::fromJson(json);
        if (!parsed)
            return eve::script::projectResult(
                vm, eve::Result<ClimbingAnchorGraphReload>::failure(parsed.status()), graphReloadValue);
        auto definition = decodeClimbingAnchorGraphDefinition(parsed.value());
        if (!definition)
            return eve::script::projectResult(
                vm, eve::Result<ClimbingAnchorGraphReload>::failure(definition.status()), graphReloadValue);
        auto resolved = Climbing::resolveAnchorGraph(value->reference);
        return eve::script::projectResult(
            vm,
            resolved.isBound()
                ? resolved->reload(std::move(definition).takeValue())
                : eve::Result<ClimbingAnchorGraphReload>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "climbing anchor graph handle is stale", "anchorGraph", {}, "climbing.binding")),
            graphReloadValue);
    });
    graph.addFunc("planRoute", [vm](ScriptClimbingAnchorGraph* value, const std::string& startNodeId,
                                     const std::string& goalNodeId, bool avoidFull, std::int64_t agentId,
                                     std::int64_t executionId, std::int64_t maxVisitedNodes) {
        if (!value || agentId < 0 || executionId < 0 || (agentId == 0) != (executionId == 0) ||
            maxVisitedNodes <= 0 || maxVisitedNodes > 65536)
            return eve::script::projectResult(
                vm, eve::Result<ClimbingAnchorRoute>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "graph, paired non-negative owner ids, and route bound are required", "planRoute", {}, "climbing.binding")),
                anchorRouteValue);
        auto resolved = Climbing::resolveAnchorGraph(value->reference);
        if (!resolved.isBound())
            return eve::script::projectResult(
                vm, eve::Result<ClimbingAnchorRoute>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "climbing anchor graph handle is stale", "anchorGraph", {}, "climbing.binding")),
                anchorRouteValue);
        auto start = resolved->nodeRef(startNodeId);
        if (!start)
            return eve::script::projectResult(vm, eve::Result<ClimbingAnchorRoute>::failure(start.status()),
                                              anchorRouteValue);
        auto goal = resolved->nodeRef(goalNodeId);
        if (!goal)
            return eve::script::projectResult(vm, eve::Result<ClimbingAnchorRoute>::failure(goal.status()),
                                              anchorRouteValue);
        ClimbingAnchorRouteRequest request;
        request.start = std::move(start).takeValue();
        request.goal = std::move(goal).takeValue();
        request.occupancyPolicy = avoidFull ? ClimbingRouteOccupancyPolicy::AvoidFull
                                            : ClimbingRouteOccupancyPolicy::Ignore;
        request.requester = {
            ClimbingAnchorAgentId(static_cast<std::uint64_t>(agentId)),
            ClimbingExecutionId(static_cast<std::uint64_t>(executionId)),
        };
        request.maxVisitedNodes = static_cast<std::uint32_t>(maxVisitedNodes);
        return eve::script::projectResult(vm, resolved->planRoute(request), anchorRouteValue);
    });
    graph.addFunc("generation", [](ScriptClimbingAnchorGraph* value) -> std::string {
        if (!value) return {};
        auto resolved = Climbing::resolveAnchorGraph(value->reference);
        return resolved.isBound() ? std::to_string(resolved->generation()) : std::string{};
    });
    graph.addFunc("reservationCount", [](ScriptClimbingAnchorGraph* value) -> std::int64_t {
        if (!value) return 0;
        auto resolved = Climbing::resolveAnchorGraph(value->reference);
        return resolved.isBound() ? static_cast<std::int64_t>(resolved->reservationCount()) : 0;
    });
    auto              runtime = table.addClass<ScriptClimbingRuntime>(
        "ClimbingRuntime", std::function<ScriptClimbingRuntime*()>([]() { return nullptr; }), false);
    runtime.addFunc("ownership", [](ScriptClimbingRuntime*) { return std::string("owned"); });
    runtime.addFunc("isStale",
                    [](ScriptClimbingRuntime* value) { return !value || Climbing::isStale(value->reference); });
    runtime.addFunc("release", [vm](ScriptClimbingRuntime* value) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "climbing runtime proxy must not be null", "runtime", {}, "climbing.binding")));
        return eve::script::projectResult(vm, Climbing::release(value->reference));
    });
    runtime.addFunc("setProfileJson", [vm](ScriptClimbingRuntime* value, const std::string& json) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "climbing runtime proxy must not be null", "runtime", {}, "climbing.binding")));
        auto resolved = Climbing::resolve(value->reference);
        return eve::script::projectResult(
            vm, resolved.isBound()
                    ? resolved->setProfileJson(json)
                    : eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "climbing runtime handle is stale", "runtime", {}, "climbing.binding")));
    });
    runtime.addFunc("reloadProfileJson", [vm](ScriptClimbingRuntime* value, const std::string& json) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "climbing runtime proxy must not be null", "runtime", {}, "climbing.binding")));
        auto resolved = Climbing::resolve(value->reference);
        return eve::script::projectResult(
            vm, resolved.isBound()
                    ? resolved->reloadProfileJson(json)
                    : eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "climbing runtime handle is stale", "runtime", {}, "climbing.binding")));
    });
    runtime.addFunc("setProfile", [vm](ScriptClimbingRuntime* value, float radius, float height, float skin,
                                       float probeDistance, float obstacleHeight, float minTopNormalY,
                                       float maxWarpResidual, int maskBits) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "climbing runtime proxy must not be null", "runtime", {}, "climbing.binding")));
        auto resolved = Climbing::resolve(value->reference);
        if (!resolved.isBound())
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "climbing runtime handle is stale", "runtime", {}, "climbing.binding")));
        ClimbingProfile profile;
        profile.capsuleRadius        = radius;
        profile.capsuleHeight        = height;
        profile.skin                 = skin;
        profile.maxProbeDistance     = probeDistance;
        profile.maxObstacleHeight    = obstacleHeight;
        profile.minTopNormalY        = minTopNormalY;
        profile.maxWarpResidual      = maxWarpResidual;
        profile.queryFilter.maskBits = static_cast<std::uint32_t>(maskBits);
        return eve::script::projectResult(vm, resolved->setProfile(std::move(profile)));
    });
    runtime.addFunc("upsertAction", [vm](ScriptClimbingRuntime* value, const std::string& id, float minHeight,
                                         float maxHeight, float minSpeed, float durationSeconds, float landingForward,
                                         float apexHeight, int selectionBias) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "climbing runtime proxy must not be null", "runtime", {}, "climbing.binding")));
        auto resolved = Climbing::resolve(value->reference);
        if (!resolved.isBound())
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "climbing runtime handle is stale", "runtime", {}, "climbing.binding")));
        auto duration = eve::Duration::fromSeconds(durationSeconds);
        if (!duration) return eve::script::projectStatusResult(vm, duration.status(), false, false);
        return eve::script::projectResult(
            vm, resolved->upsertAction({id, minHeight, maxHeight, minSpeed, std::move(duration).takeValue(),
                                        landingForward, apexHeight, selectionBias}));
    });
    runtime.addFunc("upsertActionKind", [vm](ScriptClimbingRuntime* value, const std::string& id,
                                             const std::string& kind, float minHeight, float maxHeight, float minSpeed,
                                             float durationSeconds, float landingForward, float apexHeight,
                                             int selectionBias, float hangBodyOffset, float hangFeetBelowLedge,
                                             float handSpacing, float cancelStart, float cancelEnd) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "climbing runtime proxy must not be null", "runtime", {}, "climbing.binding")));
        auto resolved = Climbing::resolve(value->reference);
        if (!resolved.isBound())
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "climbing runtime handle is stale", "runtime", {}, "climbing.binding")));
        const auto parsedKind = parseActionKind(kind);
        if (!parsedKind)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "unknown climbing action kind", "kind", {}, "climbing.binding")));
        auto duration = eve::Duration::fromSeconds(durationSeconds);
        if (!duration) return eve::script::projectStatusResult(vm, duration.status(), false, false);
        ClimbingActionDefinition action{
            id,         minHeight,    maxHeight, minSpeed, std::move(duration).takeValue(), landingForward,
            apexHeight, selectionBias};
        action.kind               = *parsedKind;
        action.hangBodyOffset     = hangBodyOffset;
        action.hangFeetBelowLedge = hangFeetBelowLedge;
        action.handSpacing        = handSpacing;
        action.cancelWindowStart  = cancelStart;
        action.cancelWindowEnd    = cancelEnd;
        action.requiredNotifies   = requiredNotifiesForKind(*parsedKind);
        return eve::script::projectResult(vm, resolved->upsertAction(std::move(action)));
    });
    runtime.addFunc("validateActionClip", [vm](ScriptClimbingRuntime* value, const std::string& actionId,
                                               animation::AnimClip* clip) {
        if (!value || !clip)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "runtime and animation clip are required", "validateActionClip", {}, "climbing.binding")));
        auto resolved = Climbing::resolve(value->reference);
        return eve::script::projectResult(
            vm, resolved.isBound()
                    ? resolved->validateAnimationBinding(actionId, *clip)
                    : eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "climbing runtime handle is stale", "runtime", {}, "climbing.binding")));
    });
    runtime.addFunc("probeMode", [vm](ScriptClimbingRuntime* value, physics::World3D* world, float x, float y, float z,
                                      float forwardX, float forwardZ, float speed, float verticalSpeed, bool grounded) {
        if (!value || !world)
            return eve::script::projectResult(
                vm,
                eve::Result<ClimbingCandidate>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "runtime and world are required", "probeMode", {}, "climbing.binding")),
                candidateValue);
        auto resolved = Climbing::resolve(value->reference);
        if (!resolved.isBound())
            return eve::script::projectResult(
                vm,
                eve::Result<ClimbingCandidate>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "climbing runtime handle is stale", "runtime", {}, "climbing.binding")),
                candidateValue);
        auto candidates =
            resolved->probe(*world, {{x, y, z}, {forwardX, 0.f, forwardZ}, speed, -1, verticalSpeed, grounded});
        if (!candidates.ok())
            return eve::script::projectResult(vm, eve::Result<ClimbingCandidate>::failure(candidates.status()),
                                              candidateValue);
        if (candidates.value().empty())
            return eve::script::projectResult(vm,
                                              eve::Result<ClimbingCandidate>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "no traversable obstacle", "probeMode", {}, "climbing.binding")),
                                              candidateValue);
        return eve::script::projectResult(vm, eve::Result<ClimbingCandidate>::success(candidates.value()[0]),
                                          candidateValue);
    });
    runtime.addFunc("tryBegin", [vm](ScriptClimbingRuntime* value, physics::World3D* world, float x, float y, float z,
                                     float forwardX, float forwardZ, float speed, int ignoredBodyId,
                                     std::int64_t tick) {
        if (!value || !world || tick < 0)
            return eve::script::projectResult(
                vm,
                eve::Result<ClimbingCandidate>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "runtime, world, and non-negative tick are required", "tryBegin", {}, "climbing.binding")),
                candidateValue);
        auto resolved = Climbing::resolve(value->reference);
        if (!resolved.isBound())
            return eve::script::projectResult(
                vm, eve::Result<ClimbingCandidate>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "climbing runtime handle is stale", "runtime", {}, "climbing.binding")),
                candidateValue);
        return eve::script::projectResult(
            vm,
            resolved->tryBegin(*world, {{x, y, z}, {forwardX, 0.f, forwardZ}, speed, ignoredBodyId},
                               eve::SimulationTick(static_cast<std::uint64_t>(tick))),
            candidateValue);
    });
    runtime.addFunc("tryBeginMode", [vm](ScriptClimbingRuntime* value, physics::World3D* world, float x, float y,
                                         float z, float forwardX, float forwardZ, float speed, int ignoredBodyId,
                                         float verticalSpeed, bool grounded, std::int64_t tick) {
        if (!value || !world || tick < 0)
            return eve::script::projectResult(vm,
                                              eve::Result<ClimbingCandidate>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "runtime, world, and non-negative tick are required", "tryBeginMode", {}, "climbing.binding")),
                                              candidateValue);
        auto resolved = Climbing::resolve(value->reference);
        if (!resolved.isBound())
            return eve::script::projectResult(
                vm, eve::Result<ClimbingCandidate>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "climbing runtime handle is stale", "runtime", {}, "climbing.binding")),
                candidateValue);
        return eve::script::projectResult(
            vm,
            resolved->tryBegin(*world,
                               {{x, y, z}, {forwardX, 0.f, forwardZ}, speed, ignoredBodyId, verticalSpeed, grounded},
                               eve::SimulationTick(static_cast<std::uint64_t>(tick))),
            candidateValue);
    });
    runtime.addFunc("tryBeginAnchor", [vm](ScriptClimbingRuntime* value, ScriptClimbingAnchorGraph* graphValue,
                                            physics::World3D* world, const std::string& nodeId,
                                            std::int64_t agentId, const std::string& actionId, float x, float y,
                                            float z, float forwardX, float forwardZ, float speed, int ignoredBodyId,
                                            float verticalSpeed, bool grounded, std::int64_t tick) {
        if (!value || !graphValue || !world || agentId <= 0 || tick < 0)
            return eve::script::projectResult(
                vm, eve::Result<ClimbingCandidate>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "runtime, graph, world, positive agent id, and tick are required", "tryBeginAnchor", {}, "climbing.binding")),
                candidateValue);
        auto runtimeResolved = Climbing::resolve(value->reference);
        auto graphResolved   = Climbing::resolveAnchorGraph(graphValue->reference);
        if (!runtimeResolved.isBound() || !graphResolved.isBound())
            return eve::script::projectResult(
                vm, eve::Result<ClimbingCandidate>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "climbing runtime or anchor graph handle is stale", "runtime", {}, "climbing.binding")),
                candidateValue);
        auto node = graphResolved->nodeRef(nodeId);
        if (!node)
            return eve::script::projectResult(vm, eve::Result<ClimbingCandidate>::failure(node.status()),
                                              candidateValue);
        return eve::script::projectResult(
            vm,
            runtimeResolved->tryBeginAnchor(
                *world, graphValue->reference, node.value(),
                ClimbingAnchorAgentId(static_cast<std::uint64_t>(agentId)), actionId,
                {{x, y, z}, {forwardX, 0.f, forwardZ}, speed, ignoredBodyId, verticalSpeed, grounded},
                eve::SimulationTick(static_cast<std::uint64_t>(tick))),
            candidateValue);
    });
    runtime.addFunc("transitionAnchor", [vm](ScriptClimbingRuntime* value,
                                              ScriptClimbingAnchorGraph* graphValue,
                                              physics::World3D* world, const std::string& targetNodeId,
                                              const std::string& edgeKind, const std::string& actionId,
                                              std::int64_t tick) {
        if (!value || !graphValue || !world || tick < 0)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "runtime, graph, world, and non-negative tick are required", "transitionAnchor", {}, "climbing.binding")));
        const auto parsedEdge = parseAnchorEdgeKind(edgeKind);
        if (!parsedEdge)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "unknown climbing anchor edge kind", "edgeKind", {}, "climbing.binding")));
        auto runtimeResolved = Climbing::resolve(value->reference);
        auto graphResolved   = Climbing::resolveAnchorGraph(graphValue->reference);
        if (!runtimeResolved.isBound() || !graphResolved.isBound())
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "climbing runtime or anchor graph handle is stale", "runtime", {}, "climbing.binding")));
        auto node = graphResolved->nodeRef(targetNodeId);
        if (!node) return eve::script::projectResult(vm, eve::Result<void>::failure(node.status()));
        return eve::script::projectResult(
            vm, runtimeResolved->transitionAnchor(*world, node.value(), *parsedEdge, actionId,
                                                  eve::SimulationTick(static_cast<std::uint64_t>(tick))));
    });
    runtime.addFunc("currentAnchor", [vm](ScriptClimbingRuntime* value) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<ClimbingAnchorNodeRef>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "climbing runtime proxy must not be null", "runtime", {}, "climbing.binding")),
                anchorNodeValue);
        auto resolved = Climbing::resolve(value->reference);
        return eve::script::projectResult(
            vm,
            resolved.isBound()
                ? resolved->currentAnchor()
                : eve::Result<ClimbingAnchorNodeRef>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "climbing runtime handle is stale", "runtime", {}, "climbing.binding")),
            anchorNodeValue);
    });
    exposeClimbingMotionBindings(runtime, vm);
    runtime.addFunc("cancel", [vm](ScriptClimbingRuntime* value, const std::string& reason, std::int64_t tick) {
        if (!value || tick < 0)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "runtime and non-negative tick are required", "cancel", {}, "climbing.binding")));
        auto resolved = Climbing::resolve(value->reference);
        if (!resolved.isBound())
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "climbing runtime handle is stale", "runtime", {}, "climbing.binding")));
        const auto parsed = parseCancelReason(reason);
        if (!parsed)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "unknown climbing cancellation reason", "reason", {}, "climbing.binding")));
        return eve::script::projectResult(
            vm, resolved->cancel(*parsed, eve::SimulationTick(static_cast<std::uint64_t>(tick))));
    });
    runtime.addFunc("drainEvents", [vm](ScriptClimbingRuntime* value) {
        if (!value)
            return eve::script::projectResult(
                vm,
                eve::Result<std::vector<ClimbingEvent>>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "climbing runtime proxy must not be null", "runtime", {}, "climbing.binding")),
                eventBatchValue);
        auto resolved = Climbing::resolve(value->reference);
        return eve::script::projectResult(
            vm,
            resolved.isBound()
                ? resolved->drainEvents()
                : eve::Result<std::vector<ClimbingEvent>>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "climbing runtime handle is stale", "runtime", {}, "climbing.binding")),
            eventBatchValue);
    });
    runtime.addFunc("drop", [vm](ScriptClimbingRuntime* value, std::int64_t tick) {
        if (!value || tick < 0)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "runtime and non-negative tick are required", "drop", {}, "climbing.binding")));
        auto resolved = Climbing::resolve(value->reference);
        return eve::script::projectResult(
            vm, resolved.isBound()
                    ? resolved->drop(eve::SimulationTick(static_cast<std::uint64_t>(tick)))
                    : eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "climbing runtime handle is stale", "runtime", {}, "climbing.binding")));
    });
    runtime.addFunc("climbUp", [vm](ScriptClimbingRuntime* value, std::int64_t tick) {
        if (!value || tick < 0)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "runtime and non-negative tick are required", "climbUp", {}, "climbing.binding")));
        auto resolved = Climbing::resolve(value->reference);
        return eve::script::projectResult(
            vm, resolved.isBound()
                    ? resolved->climbUp(eve::SimulationTick(static_cast<std::uint64_t>(tick)))
                    : eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "climbing runtime handle is stale", "runtime", {}, "climbing.binding")));
    });
    runtime.addFunc("snapshotJson", [vm](ScriptClimbingRuntime* value) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<std::string>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "climbing runtime proxy must not be null", "runtime", {}, "climbing.binding")),
                [](std::string text) { return eve::Value(std::move(text)); });
        auto resolved = Climbing::resolve(value->reference);
        return eve::script::projectResult(
            vm, resolved.isBound()
                    ? resolved->snapshotJson()
                    : eve::Result<std::string>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "climbing runtime handle is stale", "runtime", {}, "climbing.binding")),
            [](std::string text) { return eve::Value(std::move(text)); });
    });
    runtime.addFunc("restoreJson", [vm](ScriptClimbingRuntime* value, const std::string& json,
                                        physics::World3D* world) {
        if (!value || !world)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "runtime and world are required", "restoreJson", {}, "climbing.binding")));
        auto resolved = Climbing::resolve(value->reference);
        return eve::script::projectResult(
            vm, resolved.isBound()
                    ? resolved->restoreJson(json, *world)
                    : eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "climbing runtime handle is stale", "runtime", {}, "climbing.binding")));
    });
    runtime.addFunc("definitionGeneration", [](ScriptClimbingRuntime* value) -> std::string {
        if (!value) return {};
        auto resolved = Climbing::resolve(value->reference);
        return resolved.isBound() ? std::to_string(resolved->definitionGeneration()) : std::string{};
    });
    runtime.addFunc("setDebugCapture", [vm](ScriptClimbingRuntime* value, bool enabled) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "climbing runtime proxy must not be null", "runtime", {}, "climbing.binding")));
        auto resolved = Climbing::resolve(value->reference);
        if (!resolved.isBound())
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "climbing runtime handle is stale", "runtime", {}, "climbing.binding")));
        resolved->setDebugCapture(enabled ? ClimbingDebugCapture::Enabled : ClimbingDebugCapture::Disabled);
        return eve::script::projectResult(vm, eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)));
    });
    runtime.addFunc("inspect", [vm](ScriptClimbingRuntime* value) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<ClimbingDebugSnapshot>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "climbing runtime proxy must not be null", "runtime", {}, "climbing.binding")),
                debugValue);
        auto resolved = Climbing::resolve(value->reference);
        if (!resolved.isBound())
            return eve::script::projectResult(
                vm, eve::Result<ClimbingDebugSnapshot>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "climbing runtime handle is stale", "runtime", {}, "climbing.binding")),
                debugValue);
        return eve::script::projectResult(vm, eve::Result<ClimbingDebugSnapshot>::success(resolved->inspect()),
                                          debugValue);
    });

    auto module = table.addClass(name, Climbing::create, false);
    expose(module);
}

void Climbing::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Climbing::getName);
    cls.addFunc("newRuntime", [vm = cls.getHandle()](Climbing*) {
        return makeOwnedProxy<ClimbingRuntimeHandleRef, ScriptClimbingRuntime>(
            vm, Climbing::newRuntime(),
            [](ClimbingRuntimeHandleRef reference) { return Climbing::release(reference); });
    });
    cls.addFunc("newAnchorGraphJson", [vm = cls.getHandle()](Climbing*, const std::string& json,
                                                              physics::World3D* world, physics::Body3D* body) {
        if (!world || !body)
            return makeOwnedProxy<ClimbingAnchorGraphHandleRef, ScriptClimbingAnchorGraph>(
                vm,
                eve::Result<ClimbingAnchorGraphHandleRef>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "world and body are required", "newAnchorGraphJson", {}, "climbing.binding")),
                [](ClimbingAnchorGraphHandleRef reference) { return Climbing::releaseAnchorGraph(reference); });
        auto parsed = eve::Value::fromJson(json);
        if (!parsed)
            return makeOwnedProxy<ClimbingAnchorGraphHandleRef, ScriptClimbingAnchorGraph>(
                vm, eve::Result<ClimbingAnchorGraphHandleRef>::failure(parsed.status()),
                [](ClimbingAnchorGraphHandleRef reference) { return Climbing::releaseAnchorGraph(reference); });
        auto definition = decodeClimbingAnchorGraphDefinition(parsed.value());
        if (!definition)
            return makeOwnedProxy<ClimbingAnchorGraphHandleRef, ScriptClimbingAnchorGraph>(
                vm, eve::Result<ClimbingAnchorGraphHandleRef>::failure(definition.status()),
                [](ClimbingAnchorGraphHandleRef reference) { return Climbing::releaseAnchorGraph(reference); });
        return makeOwnedProxy<ClimbingAnchorGraphHandleRef, ScriptClimbingAnchorGraph>(
            vm, Climbing::newAnchorGraph(std::move(definition).takeValue(), *world, body->runtimeHandle()),
            [](ClimbingAnchorGraphHandleRef reference) { return Climbing::releaseAnchorGraph(reference); });
    });
    cls.addFunc("bakeAnchorGraphJson", [vm = cls.getHandle()](Climbing*, const std::string& json) {
        auto parsed = eve::Value::fromJson(json);
        if (!parsed)
            return eve::script::projectResult(
                vm, eve::Result<eve::Value>::failure(parsed.status()), identityValue);
        auto request = decodeClimbingAnchorBakeRequest(parsed.value());
        if (!request)
            return eve::script::projectResult(
                vm, eve::Result<eve::Value>::failure(request.status()), identityValue);
        auto baked = bakeClimbingAnchorGraph(std::move(request).takeValue());
        if (!baked)
            return eve::script::projectResult(
                vm, eve::Result<eve::Value>::failure(baked.status()), identityValue);
        return eve::script::projectResult(vm, anchorBakeResultValue(std::move(baked).takeValue()), identityValue);
    });
    cls.addFunc("inspectAnchorGraphJson", [vm = cls.getHandle()](Climbing*, const std::string& json) {
        auto parsed = eve::Value::fromJson(json);
        if (!parsed)
            return eve::script::projectResult(
                vm, eve::Result<ClimbingAnchorAuthoringOverlay>::failure(parsed.status()), anchorOverlayValue);
        auto graph = decodeClimbingAnchorGraphDefinition(parsed.value());
        if (!graph)
            return eve::script::projectResult(
                vm, eve::Result<ClimbingAnchorAuthoringOverlay>::failure(graph.status()), anchorOverlayValue);
        return eve::script::projectResult(
            vm, inspectClimbingAnchorGraphAuthoring(graph.value()), anchorOverlayValue);
    });
}

}  // namespace eve::climbing
