#include "climbing/Climbing.h"

#include "climbing/ClimbingCodec.h"
#include "physics/World3D.h"

#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace eve::climbing {
namespace {

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "climbing.snapshot"));
}

const eve::Value* field(const eve::Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

bool readString(const eve::Value::Object& object, std::string_view name, std::string& output) {
    const eve::Value* value = field(object, name);
    const auto*       text  = value ? value->getIf<std::string>() : nullptr;
    if (!text) return false;
    output = *text;
    return true;
}

bool readInt64(const eve::Value::Object& object, std::string_view name, std::int64_t& output) {
    const eve::Value* value  = field(object, name);
    const auto*       number = value ? value->getIf<std::int64_t>() : nullptr;
    if (!number) return false;
    output = *number;
    return true;
}

bool readBool(const eve::Value::Object& object, std::string_view name, bool& output, bool defaultValue, bool required) {
    const eve::Value* value = field(object, name);
    if (!value) {
        output = defaultValue;
        return !required;
    }
    const auto* boolean = value->getIf<bool>();
    if (!boolean) return false;
    output = *boolean;
    return true;
}

bool readUint64String(const eve::Value::Object& object, std::string_view name, std::uint64_t& output) {
    std::string text;
    if (!readString(object, name, text) || text.empty()) return false;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), output);
    return error == std::errc{} && end == text.data() + text.size();
}

eve::Value vecValue(Vec3 value) {
    return eve::Value::array({eve::Value(value.x), eve::Value(value.y), eve::Value(value.z)});
}

bool numericFloat(const eve::Value& value, float& output) {
    double number = 0.0;
    if (const auto* integer = value.getIf<std::int64_t>())
        number = static_cast<double>(*integer);
    else if (const auto* real = value.getIf<double>())
        number = *real;
    else
        return false;
    if (!std::isfinite(number) || number < -static_cast<double>(std::numeric_limits<float>::max()) ||
        number > static_cast<double>(std::numeric_limits<float>::max()))
        return false;
    output = static_cast<float>(number);
    return true;
}

bool readVec(const eve::Value::Object& object, std::string_view name, Vec3& output) {
    const eve::Value* value = field(object, name);
    const auto*       array = value ? value->getIf<eve::Value::Array>() : nullptr;
    return array && array->size() == 3 && numericFloat((*array)[0], output.x) && numericFloat((*array)[1], output.y) &&
           numericFloat((*array)[2], output.z);
}

bool readOptionalFloat(const eve::Value::Object& object, std::string_view name, float& output) {
    const eve::Value* value = field(object, name);
    return !value || numericFloat(*value, output);
}

std::string_view phaseName(ClimbingPhase phase) {
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
    return "unknown";
}

bool readPhase(std::string_view value, ClimbingPhase& output) {
    if (value == "idle")
        output = ClimbingPhase::Idle;
    else if (value == "requested")
        output = ClimbingPhase::Requested;
    else if (value == "aligning")
        output = ClimbingPhase::Aligning;
    else if (value == "launching")
        output = ClimbingPhase::Launching;
    else if (value == "climbing" || value == "traversing")
        // "traversing" is accepted only to migrate snapshots emitted before the
        // public climbing terminology was finalized.
        output = ClimbingPhase::Climbing;
    else if (value == "landing")
        output = ClimbingPhase::Landing;
    else if (value == "recovering")
        output = ClimbingPhase::Recovering;
    else if (value == "hanging")
        output = ClimbingPhase::Hanging;
    else if (value == "dropping")
        output = ClimbingPhase::Dropping;
    else if (value == "completed")
        output = ClimbingPhase::Completed;
    else if (value == "cancelled")
        output = ClimbingPhase::Cancelled;
    else if (value == "failed")
        output = ClimbingPhase::Failed;
    else if (value == "balanced")
        output = ClimbingPhase::Balanced;
    else if (value == "swinging")
        output = ClimbingPhase::Swinging;
    else
        return false;
    return true;
}

std::string_view eventKindName(ClimbingEventKind kind) {
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

bool readEventKind(std::string_view value, ClimbingEventKind& output) {
    if (value == "started")
        output = ClimbingEventKind::Started;
    else if (value == "anchor_transition_started")
        output = ClimbingEventKind::AnchorTransitionStarted;
    else if (value == "anchor_reached")
        output = ClimbingEventKind::AnchorReached;
    else if (value == "contact_left_hand")
        output = ClimbingEventKind::ContactLeftHand;
    else if (value == "contact_right_hand")
        output = ClimbingEventKind::ContactRightHand;
    else if (value == "landed")
        output = ClimbingEventKind::Landed;
    else if (value == "hanging")
        output = ClimbingEventKind::Hanging;
    else if (value == "dropped")
        output = ClimbingEventKind::Dropped;
    else if (value == "completed")
        output = ClimbingEventKind::Completed;
    else if (value == "cancelled")
        output = ClimbingEventKind::Cancelled;
    else if (value == "failed")
        output = ClimbingEventKind::Failed;
    else
        return false;
    return true;
}

eve::Value eventValue(const ClimbingEvent& event) {
    eve::Value::Array metadata;
    metadata.reserve(event.metadata.size());
    for (const std::string& value : event.metadata) metadata.emplace_back(value);
    return eve::Value::object({
        {"kind", std::string(eventKindName(event.kind))},
        {"actionId", event.actionId},
        {"tick", std::to_string(event.tick.value())},
        {"executionId", std::to_string(event.executionId.value())},
        {"metadata", eve::Value(std::move(metadata))},
    });
}

eve::Value eventArrayValue(const std::vector<ClimbingEvent>& events) {
    eve::Value::Array values;
    values.reserve(events.size());
    for (const auto& event : events) values.push_back(eventValue(event));
    return eve::Value(std::move(values));
}

eve::Result<std::vector<ClimbingEvent>> readEvents(const eve::Value& value, std::uint64_t nextExecutionId) {
    const auto* array = value.getIf<eve::Value::Array>();
    if (!array || array->size() > ClimbingRuntime::PendingEventCapacity)
        return failure<std::vector<ClimbingEvent>>(eve::DiagnosticCode::ParseError,
                                                   "pending climbing events must be a bounded array",
                                                   "pendingEvents");
    std::vector<ClimbingEvent> events;
    events.reserve(array->size());
    eve::SimulationTick previousTick = eve::SimulationTick::zero();
    bool                first = true;
    for (std::size_t index = 0; index < array->size(); ++index) {
        const auto* object = (*array)[index].getIf<eve::Value::Object>();
        ClimbingEvent event;
        std::string   kind;
        std::uint64_t tick = 0;
        std::uint64_t executionId = 0;
        if (!object || !readString(*object, "kind", kind) || !readEventKind(kind, event.kind) ||
            !readString(*object, "actionId", event.actionId) || event.actionId.empty() ||
            !readUint64String(*object, "tick", tick) || !readUint64String(*object, "executionId", executionId) ||
            executionId == 0 || executionId >= nextExecutionId)
            return failure<std::vector<ClimbingEvent>>(eve::DiagnosticCode::ParseError,
                                                       "pending climbing event is missing or inconsistent",
                                                       "pendingEvents." + std::to_string(index));
        if (const eve::Value* metadataValue = field(*object, "metadata")) {
            const auto* metadata = metadataValue->getIf<eve::Value::Array>();
            if (!metadata)
                return failure<std::vector<ClimbingEvent>>(eve::DiagnosticCode::ParseError,
                                                           "climbing event metadata must be an array",
                                                           "pendingEvents." + std::to_string(index) + ".metadata");
            event.metadata.reserve(metadata->size());
            for (const eve::Value& item : *metadata) {
                const auto* text = item.getIf<std::string>();
                if (!text || text->empty())
                    return failure<std::vector<ClimbingEvent>>(
                        eve::DiagnosticCode::ParseError, "climbing event metadata must contain non-empty strings",
                        "pendingEvents." + std::to_string(index) + ".metadata");
                event.metadata.push_back(*text);
            }
        }
        event.tick        = eve::SimulationTick(tick);
        event.executionId = ClimbingExecutionId(executionId);
        if (!first && event.tick < previousTick)
            return failure<std::vector<ClimbingEvent>>(eve::DiagnosticCode::InvariantViolation,
                                                       "pending climbing events must be ordered by tick",
                                                       "pendingEvents." + std::to_string(index) + ".tick");
        first        = false;
        previousTick = event.tick;
        events.push_back(std::move(event));
    }
    return eve::Result<std::vector<ClimbingEvent>>::success(std::move(events));
}

eve::Value candidateValue(const ClimbingCandidate& value) {
    return eve::Value::object({
        {"actionId", value.actionId},
        {"definitionGeneration", std::to_string(value.definitionGeneration)},
        {"world", std::to_string(value.world.packed())},
        {"obstacleBody", std::to_string(value.obstacleBody.packed())},
        {"obstacleShape", std::to_string(value.obstacleShape.packed())},
        {"obstacleBodyId", value.obstacleBodyId},
        {"obstacleShapeId", value.obstacleShapeId},
        {"ignoredBodyId", value.ignoredBodyId},
        {"frontPoint", vecValue(value.frontPoint)},
        {"topPoint", vecValue(value.topPoint)},
        {"landingFeet", vecValue(value.landingFeet)},
        {"surfaceNormal", vecValue(value.surfaceNormal)},
        {"surfaceTangent", vecValue(value.surfaceTangent)},
        {"leftHandAnchor", vecValue(value.leftHandAnchor)},
        {"rightHandAnchor", vecValue(value.rightHandAnchor)},
        {"bodyLocalTop", vecValue(value.bodyLocalTop)},
        {"bodyLocalLanding", vecValue(value.bodyLocalLanding)},
        {"obstacleHeight", value.obstacleHeight},
        {"obstacleDepth", value.obstacleDepth},
        {"gapDistance", value.gapDistance},
        {"clearanceHeight", value.clearanceHeight},
        {"slopeRadians", value.slopeRadians},
        {"curvature", value.curvature},
        {"supportShapeTag", value.supportShapeTag},
        {"supportMaterialId", value.supportMaterialId},
        {"probeRecipe", static_cast<std::int64_t>(value.probeRecipe)},
        {"score", value.score},
        {"kind", static_cast<std::int64_t>(value.kind)},
        {"support", static_cast<std::int64_t>(value.support)},
    });
}

eve::Value anchorValue(ClimbingAnchorGraphHandleRef graph, const ClimbingAnchorNodeRef& node,
                       const ClimbingAnchorReservation& reservation) {
    if (!graph.isValid() || reservation.id.isZero()) return eve::Value();
    return eve::Value::object({
        {"graphHandle", std::to_string(graph.packed())},
        {"graphOwnerEpoch", std::to_string(graph.ownerEpoch)},
        {"graphId", node.graphId},
        {"nodeId", node.nodeId},
        {"nodeGraphGeneration", std::to_string(node.graphGeneration)},
        {"reservationId", std::to_string(reservation.id.value())},
        {"reservationGraphGeneration", std::to_string(reservation.graphGeneration)},
        {"claimGeneration", std::to_string(reservation.claimGeneration)},
        {"slot", static_cast<std::int64_t>(reservation.slot)},
        {"agentId", std::to_string(reservation.occupant.agentId.value())},
        {"executionId", std::to_string(reservation.occupant.executionId.value())},
    });
}

eve::Result<ClimbingCandidate> readCandidate(const eve::Value& value, std::int64_t snapshotVersion) {
    const auto* object = value.getIf<eve::Value::Object>();
    if (!object)
        return failure<ClimbingCandidate>(eve::DiagnosticCode::ParseError, "runtime candidate must be an object",
                                          "execution.candidate");
    ClimbingCandidate result;
    std::uint64_t     world = 0, body = 0, shape = 0, definitionGeneration = 1;
    std::int64_t      bodyId = 0, shapeId = 0, ignoredBodyId = 0, score = 0, kind = 0, support = 0;
    std::int64_t      supportShapeTag = 0, supportMaterialId = 0, probeRecipe = 0;
    const eve::Value* heightValue = field(*object, "obstacleHeight");
    const eve::Value* depthValue = field(*object, "obstacleDepth");
    const eve::Value* gapValue = field(*object, "gapDistance");
    const eve::Value* clearanceValue = field(*object, "clearanceHeight");
    const eve::Value* slopeValue = field(*object, "slopeRadians");
    const eve::Value* curvatureValue = field(*object, "curvature");
    if (!readString(*object, "actionId", result.actionId) ||
        (field(*object, "definitionGeneration") &&
         !readUint64String(*object, "definitionGeneration", definitionGeneration)) ||
        definitionGeneration == 0 || !readUint64String(*object, "world", world) ||
        !readUint64String(*object, "obstacleBody", body) || !readUint64String(*object, "obstacleShape", shape) ||
        !readInt64(*object, "obstacleBodyId", bodyId) || bodyId < std::numeric_limits<int>::min() ||
        bodyId > std::numeric_limits<int>::max() || !readInt64(*object, "obstacleShapeId", shapeId) ||
        shapeId < std::numeric_limits<int>::min() || shapeId > std::numeric_limits<int>::max() ||
        !readInt64(*object, "ignoredBodyId", ignoredBodyId) || ignoredBodyId < std::numeric_limits<int>::min() ||
        ignoredBodyId > std::numeric_limits<int>::max() || !readVec(*object, "frontPoint", result.frontPoint) ||
        !readVec(*object, "topPoint", result.topPoint) || !readVec(*object, "landingFeet", result.landingFeet) ||
        !readVec(*object, "surfaceNormal", result.surfaceNormal) ||
        !readVec(*object, "surfaceTangent", result.surfaceTangent) ||
        !readVec(*object, "leftHandAnchor", result.leftHandAnchor) ||
        !readVec(*object, "rightHandAnchor", result.rightHandAnchor) ||
        !readVec(*object, "bodyLocalTop", result.bodyLocalTop) ||
        !readVec(*object, "bodyLocalLanding", result.bodyLocalLanding) || !heightValue ||
        !numericFloat(*heightValue, result.obstacleHeight) || !readInt64(*object, "score", score) ||
        !readInt64(*object, "kind", kind) || kind < 0 ||
        kind > static_cast<std::int64_t>(ClimbingActionKind::BarSwing) ||
        !readInt64(*object, "support", support) ||
        support < 0 || support > static_cast<std::int64_t>(HangSupport::Free))
        return failure<ClimbingCandidate>(eve::DiagnosticCode::ParseError,
                                          "runtime candidate has missing or invalid fields", "execution.candidate");
    if ((snapshotVersion >= 4 && (!depthValue || !gapValue || !clearanceValue || !slopeValue || !curvatureValue ||
                                  !field(*object, "supportShapeTag") || !field(*object, "supportMaterialId") ||
                                  !field(*object, "probeRecipe"))) ||
        (depthValue && !numericFloat(*depthValue, result.obstacleDepth)) ||
        (gapValue && !numericFloat(*gapValue, result.gapDistance)) ||
        (clearanceValue && !numericFloat(*clearanceValue, result.clearanceHeight)) ||
        (slopeValue && !numericFloat(*slopeValue, result.slopeRadians)) ||
        (curvatureValue && !numericFloat(*curvatureValue, result.curvature)) ||
        (field(*object, "supportShapeTag") && !readInt64(*object, "supportShapeTag", supportShapeTag)) ||
        supportShapeTag < std::numeric_limits<int>::min() || supportShapeTag > std::numeric_limits<int>::max() ||
        (field(*object, "supportMaterialId") && !readInt64(*object, "supportMaterialId", supportMaterialId)) ||
        supportMaterialId < std::numeric_limits<int>::min() ||
        supportMaterialId > std::numeric_limits<int>::max() ||
        (field(*object, "probeRecipe") && !readInt64(*object, "probeRecipe", probeRecipe)) || probeRecipe < 0 ||
        probeRecipe > static_cast<std::int64_t>(ClimbingProbeRecipe::AnchorGraph))
        return failure<ClimbingCandidate>(eve::DiagnosticCode::ParseError,
                                          "runtime candidate metrics are missing or invalid", "execution.candidate");
    result.definitionGeneration = definitionGeneration;
    result.world           = physics::PhysicsWorldHandle::fromPacked(world);
    result.obstacleBody    = physics::PhysicsBodyHandle::fromPacked(body);
    result.obstacleShape   = physics::PhysicsShapeHandle::fromPacked(shape);
    result.obstacleBodyId  = static_cast<int>(bodyId);
    result.obstacleShapeId = static_cast<int>(shapeId);
    result.ignoredBodyId   = static_cast<int>(ignoredBodyId);
    result.score           = score;
    result.kind            = static_cast<ClimbingActionKind>(kind);
    result.supportShapeTag = static_cast<int>(supportShapeTag);
    result.supportMaterialId = static_cast<int>(supportMaterialId);
    result.probeRecipe = static_cast<ClimbingProbeRecipe>(probeRecipe);
    result.support         = static_cast<HangSupport>(support);
    return eve::Result<ClimbingCandidate>::success(std::move(result));
}

bool activePhase(ClimbingPhase phase) {
    return phase != ClimbingPhase::Idle && phase != ClimbingPhase::Completed && phase != ClimbingPhase::Cancelled &&
           phase != ClimbingPhase::Failed;
}

}  // namespace

eve::Result<eve::Value> ClimbingRuntime::snapshot() const {
    auto profile = encodeClimbingProfileDefinition(profile_);
    if (!profile) return eve::Result<eve::Value>::failure(profile.status());
    eve::Value::Object root{{"schemaId", eve::Value(std::string(SnapshotSchemaId))},
                            {"schemaVersion", eve::Value(SnapshotSchemaVersion)},
                            {"phase", eve::Value(std::string(phaseName(phase_)))},
                            {"terminalCode", eve::Value(terminalCode_)},
                            {"definitionGeneration", eve::Value(std::to_string(definitionGeneration_))},
                            {"profile", std::move(profile).takeValue()},
                            {"previousActionId", eve::Value(previousActionId_)},
                            {"nextExecutionId", eve::Value(std::to_string(nextExecutionId_))},
                            {"pendingEvents", eventArrayValue(pendingEvents_)}};
    if (!execution_) {
        root.emplace("execution", eve::Value());
        return eve::Result<eve::Value>::success(eve::Value(std::move(root)));
    }
    auto action = encodeClimbingActionDefinition(execution_->action);
    if (!action) return eve::Result<eve::Value>::failure(action.status());
    eve::Value::Object state{
        {"executionId", std::to_string(execution_->executionId.value())},
        {"definitionGeneration", std::to_string(execution_->definitionGeneration)},
        {"action", std::move(action).takeValue()},
        {"candidate", candidateValue(execution_->candidate)},
        {"startFeet", vecValue(execution_->startFeet)},
        {"currentFeet", vecValue(execution_->currentFeet)},
        {"lastPlannedFeet", vecValue(execution_->lastPlannedFeet)},
        {"elapsedNs", execution_->elapsed.nanoseconds()},
        {"durationNs", execution_->duration.nanoseconds()},
        {"lastTick", std::to_string(execution_->lastTick.value())},
        {"velocity", vecValue(execution_->velocity)},
        {"accumulatedResidual", vecValue(execution_->accumulatedResidual)},
        {"horizontalWarpUsed", execution_->horizontalWarpUsed},
        {"verticalWarpUsed", execution_->verticalWarpUsed},
        {"facingWarpUsed", execution_->facingWarpUsed},
        {"leftContactEmitted", execution_->leftContactEmitted},
        {"rightContactEmitted", execution_->rightContactEmitted},
        {"landContactReleased", execution_->landContactReleased},
        {"compactCollisionActive", execution_->compactCollisionActive},
        {"branchWindowOpen", execution_->branchWindowOpen},
        {"anchor", anchorValue(execution_->anchorGraph, execution_->anchorNode,
                                execution_->anchorReservation)},
    };
    root.emplace("execution", eve::Value(std::move(state)));
    return eve::Result<eve::Value>::success(eve::Value(std::move(root)));
}

eve::Result<void> ClimbingRuntime::restore(const eve::Value& value, physics::World3D& world) {
    const auto* root = value.getIf<eve::Value::Object>();
    if (!root) return failure<void>(eve::DiagnosticCode::ParseError, "runtime snapshot must be an object");
    std::string   schemaId;
    std::int64_t  version = -1;
    std::string   phaseText;
    std::string   terminalCode;
    std::string   candidatePreviousActionId;
    std::uint64_t candidateNextExecutionId = 1;
    std::uint64_t candidateDefinitionGeneration = definitionGeneration_;
    if (!readString(*root, "schemaId", schemaId) || schemaId != SnapshotSchemaId ||
        !readInt64(*root, "schemaVersion", version) || version < 0 || version > SnapshotSchemaVersion ||
        !readString(*root, "phase", phaseText) ||
        (version >= 1 && !readString(*root, "terminalCode", terminalCode))) {
        if (version > SnapshotSchemaVersion)
            return failure<void>(eve::DiagnosticCode::UnknownVersion, "climbing.restore.version_unsupported",
                                 "schemaVersion");
        return failure<void>(eve::DiagnosticCode::ParseError, "invalid climbing runtime snapshot envelope");
    }
    if (version >= 4 && !readString(*root, "previousActionId", candidatePreviousActionId))
        return failure<void>(eve::DiagnosticCode::ParseError, "invalid previous climbing action id",
                             "previousActionId");
    if (field(*root, "nextExecutionId") && !readUint64String(*root, "nextExecutionId", candidateNextExecutionId))
        return failure<void>(eve::DiagnosticCode::ParseError, "invalid next climbing execution id", "nextExecutionId");
    if (field(*root, "definitionGeneration") &&
        (!readUint64String(*root, "definitionGeneration", candidateDefinitionGeneration) ||
         candidateDefinitionGeneration == 0))
        return failure<void>(eve::DiagnosticCode::ParseError, "invalid climbing definition generation",
                             "definitionGeneration");
    ClimbingProfile candidateProfile = profile_;
    if (const eve::Value* profileValue = field(*root, "profile")) {
        auto decodedProfile = decodeClimbingProfileDefinition(*profileValue);
        if (!decodedProfile) return eve::Result<void>::failure(decodedProfile.status());
        candidateProfile = std::move(decodedProfile).takeValue();
    }
    ClimbingPhase candidatePhase;
    if (!readPhase(phaseText, candidatePhase))
        return failure<void>(eve::DiagnosticCode::ParseError, "invalid climbing runtime phase", "phase");
    const eve::Value* executionValue = field(*root, "execution");
    if (!executionValue)
        return failure<void>(eve::DiagnosticCode::ParseError, "runtime snapshot execution field is required",
                             "execution");
    std::optional<Execution> candidateExecution;
    if (!executionValue->isNull()) {
        const auto*       state               = executionValue->getIf<eve::Value::Object>();
        const eve::Value* actionValue         = state ? field(*state, "action") : nullptr;
        const eve::Value* candidateValueField = state ? field(*state, "candidate") : nullptr;
        if (!state || !actionValue || !candidateValueField)
            return failure<void>(eve::DiagnosticCode::ParseError, "invalid climbing execution object", "execution");
        auto action = decodeClimbingActionDefinition(*actionValue);
        if (!action) return eve::Result<void>::failure(action.status());
        auto candidate = readCandidate(*candidateValueField, version);
        if (!candidate) return eve::Result<void>::failure(candidate.status());
        Execution     parsed;
        std::uint64_t executionId = 1;
        std::uint64_t executionDefinitionGeneration = candidateDefinitionGeneration;
        if (field(*state, "executionId") && !readUint64String(*state, "executionId", executionId))
            return failure<void>(eve::DiagnosticCode::ParseError, "invalid climbing execution id",
                                 "execution.executionId");
        parsed.executionId      = ClimbingExecutionId(executionId);
        if (field(*state, "definitionGeneration") &&
            (!readUint64String(*state, "definitionGeneration", executionDefinitionGeneration) ||
             executionDefinitionGeneration == 0))
            return failure<void>(eve::DiagnosticCode::ParseError, "invalid execution definition generation",
                                 "execution.definitionGeneration");
        parsed.definitionGeneration = executionDefinitionGeneration;
        parsed.action           = std::move(action).takeValue();
        parsed.candidate        = std::move(candidate).takeValue();
        std::int64_t  elapsedNs = 0, durationNs = 0;
        std::uint64_t lastTick = 0;
        if (parsed.action.id != parsed.candidate.actionId ||
            (field(*state, "definitionGeneration") &&
             parsed.candidate.definitionGeneration != parsed.definitionGeneration) ||
            !readVec(*state, "startFeet", parsed.startFeet) ||
            !readVec(*state, "currentFeet", parsed.currentFeet) ||
            !readVec(*state, "lastPlannedFeet", parsed.lastPlannedFeet) || !readInt64(*state, "elapsedNs", elapsedNs) ||
            elapsedNs < 0 || !readInt64(*state, "durationNs", durationNs) || durationNs <= 0 ||
            !readUint64String(*state, "lastTick", lastTick) || !readVec(*state, "velocity", parsed.velocity) ||
            !readVec(*state, "accumulatedResidual", parsed.accumulatedResidual) ||
            !readOptionalFloat(*state, "horizontalWarpUsed", parsed.horizontalWarpUsed) ||
            !readOptionalFloat(*state, "verticalWarpUsed", parsed.verticalWarpUsed) ||
            !readOptionalFloat(*state, "facingWarpUsed", parsed.facingWarpUsed) || parsed.horizontalWarpUsed < 0.f ||
            parsed.verticalWarpUsed < 0.f || parsed.facingWarpUsed < 0.f ||
            !readBool(*state, "leftContactEmitted", parsed.leftContactEmitted, false, version != 0) ||
            !readBool(*state, "rightContactEmitted", parsed.rightContactEmitted, false, version != 0) ||
            !readBool(*state, "landContactReleased", parsed.landContactReleased, false, false) ||
            !readBool(*state, "compactCollisionActive", parsed.compactCollisionActive, false, false) ||
            !readBool(*state, "branchWindowOpen", parsed.branchWindowOpen, false, false))
            return failure<void>(eve::DiagnosticCode::ParseError,
                                 "climbing execution has missing or inconsistent fields", "execution");
        const eve::Value* anchorField = field(*state, "anchor");
        if (version >= 3 && !anchorField)
            return failure<void>(eve::DiagnosticCode::ParseError,
                                 "runtime snapshot anchor field is required", "execution.anchor");
        if (anchorField && !anchorField->isNull()) {
            if (version < 3)
                return failure<void>(eve::DiagnosticCode::ParseError,
                                     "legacy runtime snapshot cannot contain an anchor claim", "execution.anchor");
            const auto* anchor = anchorField->getIf<eve::Value::Object>();
            std::uint64_t graphHandle = 0, graphOwnerEpoch = 0, nodeGraphGeneration = 0;
            std::uint64_t reservationId = 0, reservationGraphGeneration = 0, claimGeneration = 0;
            std::uint64_t agentId = 0, occupantExecutionId = 0;
            std::int64_t  slot = -1;
            std::string   graphId, nodeId;
            if (!anchor || !readUint64String(*anchor, "graphHandle", graphHandle) ||
                !readUint64String(*anchor, "graphOwnerEpoch", graphOwnerEpoch) || graphOwnerEpoch == 0 ||
                !readString(*anchor, "graphId", graphId) || graphId.empty() ||
                !readString(*anchor, "nodeId", nodeId) || nodeId.empty() ||
                !readUint64String(*anchor, "nodeGraphGeneration", nodeGraphGeneration) ||
                nodeGraphGeneration == 0 || !readUint64String(*anchor, "reservationId", reservationId) ||
                reservationId == 0 ||
                !readUint64String(*anchor, "reservationGraphGeneration", reservationGraphGeneration) ||
                reservationGraphGeneration == 0 ||
                !readUint64String(*anchor, "claimGeneration", claimGeneration) || claimGeneration == 0 ||
                !readInt64(*anchor, "slot", slot) || slot < 0 ||
                slot > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()) ||
                !readUint64String(*anchor, "agentId", agentId) || agentId == 0 ||
                !readUint64String(*anchor, "executionId", occupantExecutionId) || occupantExecutionId == 0)
                return failure<void>(eve::DiagnosticCode::ParseError,
                                     "runtime anchor claim has missing or invalid fields", "execution.anchor");
            parsed.anchorGraph.handle = eve::RuntimeHandle<ClimbingAnchorGraphHandleTag>::fromPacked(graphHandle);
            parsed.anchorGraph.ownerEpoch = graphOwnerEpoch;
            parsed.anchorNode = {std::move(graphId), std::move(nodeId), nodeGraphGeneration};
            parsed.anchorReservation = {ClimbingAnchorReservationId(reservationId), reservationGraphGeneration,
                                        claimGeneration, parsed.anchorNode.nodeId, static_cast<std::uint32_t>(slot),
                                        {ClimbingAnchorAgentId(agentId), ClimbingExecutionId(occupantExecutionId)}};
            if (!parsed.anchorGraph.isValid() || nodeGraphGeneration != reservationGraphGeneration ||
                parsed.executionId != parsed.anchorReservation.occupant.executionId)
                return failure<void>(eve::DiagnosticCode::InvariantViolation,
                                     "runtime anchor identities are inconsistent", "execution.anchor");
        }
        parsed.elapsed  = eve::Duration::fromNanoseconds(elapsedNs);
        parsed.duration = eve::Duration::fromNanoseconds(durationNs);
        parsed.lastTick = eve::SimulationTick(lastTick);
        if (parsed.duration != parsed.action.duration || parsed.elapsed > parsed.duration)
            return failure<void>(eve::DiagnosticCode::InvariantViolation,
                                 "execution timing disagrees with pinned action", "execution.durationNs");
        const bool graphBound = parsed.anchorGraph.isValid();
        if (world.runtimeHandle() != parsed.candidate.world || !world.findBody(parsed.candidate.obstacleBody) ||
            (!graphBound && !world.findShape(parsed.candidate.obstacleShape)))
            return failure<void>(eve::DiagnosticCode::StaleHandle, "climbing restore target link is stale",
                                 "execution.candidate");
        if (graphBound) {
            auto graph = Climbing::resolveAnchorGraph(parsed.anchorGraph);
            if (!graph.isBound() || graph->body() != parsed.candidate.obstacleBody)
                return failure<void>(eve::DiagnosticCode::StaleHandle,
                                     "climbing restore anchor graph link is stale", "execution.anchor.graph");
            auto resolved = graph->resolveNode(world, parsed.anchorNode);
            if (!resolved)
                return eve::Result<void>::failure(resolved.status());
            auto reservation = graph->validateReservation(parsed.anchorReservation);
            if (!reservation && (!reservation.error() || reservation.error()->code() != eve::DiagnosticCode::NotFound))
                return eve::Result<void>::failure(reservation.status());
        }
        candidateExecution = std::move(parsed);
    }
    if (activePhase(candidatePhase) != candidateExecution.has_value())
        return failure<void>(eve::DiagnosticCode::InvariantViolation, "active phase and execution presence disagree",
                             "execution");
    if (!field(*root, "nextExecutionId")) {
        candidateNextExecutionId = candidateExecution ? candidateExecution->executionId.value() + 1 : 1;
    }
    if (candidateNextExecutionId == 0 ||
        (candidateExecution && (candidateExecution->executionId.isZero() ||
                                candidateNextExecutionId <= candidateExecution->executionId.value())))
        return failure<void>(eve::DiagnosticCode::InvariantViolation,
                             "next execution id must follow the restored execution", "nextExecutionId");
    std::vector<ClimbingEvent> candidateEvents;
    if (const eve::Value* eventValue = field(*root, "pendingEvents")) {
        auto parsedEvents = readEvents(*eventValue, candidateNextExecutionId);
        if (!parsedEvents) return eve::Result<void>::failure(parsedEvents.status());
        candidateEvents = std::move(parsedEvents).takeValue();
    } else if (version >= 2) {
        return failure<void>(eve::DiagnosticCode::ParseError, "runtime snapshot pendingEvents field is required",
                             "pendingEvents");
    }

    if (execution_ && execution_->anchorGraph.isValid() &&
        (!candidateExecution || execution_->anchorGraph != candidateExecution->anchorGraph ||
         execution_->anchorReservation.id != candidateExecution->anchorReservation.id ||
         execution_->anchorReservation.claimGeneration != candidateExecution->anchorReservation.claimGeneration))
        return failure<void>(eve::DiagnosticCode::Conflict,
                             "restore cannot overwrite a different live anchor claim", "execution.anchor");
    if (candidateExecution && candidateExecution->anchorGraph.isValid()) {
        const bool alreadyOwned = execution_ && execution_->anchorGraph == candidateExecution->anchorGraph &&
                                  execution_->anchorReservation.id == candidateExecution->anchorReservation.id &&
                                  execution_->anchorReservation.claimGeneration ==
                                      candidateExecution->anchorReservation.claimGeneration;
        if (!alreadyOwned) {
            auto graph = Climbing::resolveAnchorGraph(candidateExecution->anchorGraph);
            if (!graph.isBound())
                return failure<void>(eve::DiagnosticCode::StaleHandle,
                                     "climbing restore anchor graph link became stale", "execution.anchor.graph");
            auto claimed = graph->restoreReservation(candidateExecution->anchorReservation);
            if (!claimed) return eve::Result<void>::failure(claimed.status());
            candidateExecution->anchorReservation = std::move(claimed).takeValue();
        }
    }

    // Publish only after the whole owning candidate and all cross-domain links have passed validation.
    phase_           = candidatePhase;
    profile_         = std::move(candidateProfile);
    terminalCode_    = std::move(terminalCode);
    execution_       = std::move(candidateExecution);
    previousActionId_ = std::move(candidatePreviousActionId);
    nextExecutionId_ = candidateNextExecutionId;
    definitionGeneration_ = candidateDefinitionGeneration;
    pendingEvents_ = std::move(candidateEvents);
    validatedAnimationActions_.clear();
    lastCandidates_.clear();
    lastDebugQueries_.clear();
    lastEvidence_.clear();
    motionEvidence_.clear();
    lastQueryCount_ = 0;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::climbing
