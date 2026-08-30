#include "climbing/ClimbingCodec.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

namespace eve::climbing {
namespace {

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "climbing.codec"));
}

const eve::Value* field(const eve::Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

bool finite(float value) { return std::isfinite(value); }

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

bool readBool(const eve::Value::Object& object, std::string_view name, bool& output) {
    const eve::Value* value   = field(object, name);
    const auto*       boolean = value ? value->getIf<bool>() : nullptr;
    if (!boolean) return false;
    output = *boolean;
    return true;
}

bool readFloat(const eve::Value::Object& object, std::string_view name, float& output) {
    const eve::Value* value = field(object, name);
    if (!value) return false;
    double number = 0.0;
    if (const auto* integer = value->getIf<std::int64_t>())
        number = static_cast<double>(*integer);
    else if (const auto* real = value->getIf<double>())
        number = *real;
    else
        return false;
    if (!std::isfinite(number) || number < -static_cast<double>(std::numeric_limits<float>::max()) ||
        number > static_cast<double>(std::numeric_limits<float>::max()))
        return false;
    output = static_cast<float>(number);
    return true;
}

bool readOptionalFloat(const eve::Value::Object& object, std::string_view name, float& output) {
    return !field(object, name) || readFloat(object, name, output);
}

bool readOptionalInt64(const eve::Value::Object& object, std::string_view name, std::int64_t& output) {
    return !field(object, name) || readInt64(object, name, output);
}

template <class Enum>
bool readOptionalEnum(const eve::Value::Object& object, std::string_view name, Enum& output,
                      std::int64_t maximumInclusive) {
    std::int64_t value = static_cast<std::int64_t>(output);
    if (!readOptionalInt64(object, name, value) || value < 0 || value > maximumInclusive) return false;
    output = static_cast<Enum>(value);
    return true;
}

eve::Value stringArray(const std::vector<std::string>& values) {
    eve::Value::Array result;
    result.reserve(values.size());
    for (const std::string& value : values) result.emplace_back(value);
    return eve::Value(std::move(result));
}

bool readOptionalStringArray(const eve::Value::Object& object, std::string_view name,
                             std::vector<std::string>& output) {
    const eve::Value* value = field(object, name);
    if (!value) return true;
    const auto* array = value->getIf<eve::Value::Array>();
    if (!array) return false;
    std::vector<std::string> candidate;
    candidate.reserve(array->size());
    for (const eve::Value& item : *array) {
        const auto* text = item.getIf<std::string>();
        if (!text) return false;
        candidate.push_back(*text);
    }
    output = std::move(candidate);
    return true;
}

bool readOptionalString(const eve::Value::Object& object, std::string_view name, std::string& output) {
    return !field(object, name) || readString(object, name, output);
}

bool uniqueNonEmpty(const std::vector<std::string>& values) {
    std::vector<std::string_view> sorted;
    sorted.reserve(values.size());
    for (const std::string& value : values) {
        if (value.empty()) return false;
        sorted.push_back(value);
    }
    std::sort(sorted.begin(), sorted.end());
    return std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end();
}

std::string_view kindName(ClimbingActionKind kind) {
    switch (kind) {
        case ClimbingActionKind::Vault: return "vault";
        case ClimbingActionKind::Mantle: return "mantle";
        case ClimbingActionKind::LedgeGrab: return "ledge_grab";
        case ClimbingActionKind::ClimbUp: return "climb_up";
        case ClimbingActionKind::Shimmy: return "shimmy";
        case ClimbingActionKind::CornerInner: return "corner_inner";
        case ClimbingActionKind::CornerOuter: return "corner_outer";
        case ClimbingActionKind::LedgeJump: return "ledge_jump";
        case ClimbingActionKind::ClimbDown: return "climb_down";
        case ClimbingActionKind::LadderMount: return "ladder_mount";
        case ClimbingActionKind::LadderClimb: return "ladder_climb";
        case ClimbingActionKind::LadderDismount: return "ladder_dismount";
        case ClimbingActionKind::WallRun: return "wall_run";
        case ClimbingActionKind::Slide: return "slide";
        case ClimbingActionKind::BeamBalance: return "beam_balance";
        case ClimbingActionKind::PoleSwing: return "pole_swing";
        case ClimbingActionKind::BarSwing: return "bar_swing";
    }
    return "unknown";
}

bool readKind(std::string_view text, ClimbingActionKind& output) {
    if (text == "vault")
        output = ClimbingActionKind::Vault;
    else if (text == "mantle")
        output = ClimbingActionKind::Mantle;
    else if (text == "ledge_grab")
        output = ClimbingActionKind::LedgeGrab;
    else if (text == "climb_up")
        output = ClimbingActionKind::ClimbUp;
    else if (text == "shimmy")
        output = ClimbingActionKind::Shimmy;
    else if (text == "corner_inner")
        output = ClimbingActionKind::CornerInner;
    else if (text == "corner_outer")
        output = ClimbingActionKind::CornerOuter;
    else if (text == "ledge_jump")
        output = ClimbingActionKind::LedgeJump;
    else if (text == "climb_down")
        output = ClimbingActionKind::ClimbDown;
    else if (text == "ladder_mount")
        output = ClimbingActionKind::LadderMount;
    else if (text == "ladder_climb")
        output = ClimbingActionKind::LadderClimb;
    else if (text == "ladder_dismount")
        output = ClimbingActionKind::LadderDismount;
    else if (text == "wall_run")
        output = ClimbingActionKind::WallRun;
    else if (text == "slide")
        output = ClimbingActionKind::Slide;
    else if (text == "beam_balance")
        output = ClimbingActionKind::BeamBalance;
    else if (text == "pole_swing")
        output = ClimbingActionKind::PoleSwing;
    else if (text == "bar_swing")
        output = ClimbingActionKind::BarSwing;
    else
        return false;
    return true;
}

eve::Result<const eve::Value::Object*> readRoot(const eve::Value& value, std::string_view schemaId,
                                                std::int64_t schemaVersion) {
    const auto* object = value.getIf<eve::Value::Object>();
    if (!object)
        return failure<const eve::Value::Object*>(eve::DiagnosticCode::ParseError,
                                                  "climbing definition must be an object");
    std::string  actualSchema;
    std::int64_t actualVersion = 0;
    if (!readString(*object, "schemaId", actualSchema) || actualSchema != schemaId)
        return failure<const eve::Value::Object*>(eve::DiagnosticCode::ParseError,
                                                  "climbing definition schemaId is missing or mismatched", "schemaId");
    if (!readInt64(*object, "schemaVersion", actualVersion))
        return failure<const eve::Value::Object*>(
            eve::DiagnosticCode::ParseError, "climbing definition schemaVersion must be an integer", "schemaVersion");
    if (actualVersion != schemaVersion && actualVersion != schemaVersion - 1)
        return failure<const eve::Value::Object*>(eve::DiagnosticCode::UnknownVersion,
                                                  "climbing definition schema version is unsupported", "schemaVersion");
    return eve::Result<const eve::Value::Object*>::success(object);
}

eve::Value::Object unknownFields(const eve::Value::Object& object, std::initializer_list<std::string_view> knownNames) {
    std::unordered_set<std::string_view> known(knownNames.begin(), knownNames.end());
    eve::Value::Object                   result;
    for (const auto& [name, value] : object)
        if (!known.contains(name)) result.emplace(name, value);
    return result;
}

void writeActionFields(eve::Value::Object& object, const ClimbingActionDefinition& action) {
    eve::Value::Array notifyValues;
    notifyValues.reserve(action.requiredNotifies.size());
    for (const std::string& notify : action.requiredNotifies) notifyValues.emplace_back(notify);
    eve::Value::Array warpWindowValues;
    warpWindowValues.reserve(action.warpWindows.size());
    for (const ClimbingWarpWindow& window : action.warpWindows) {
        warpWindowValues.push_back(eve::Value::object({{"start", window.start},
                                                       {"end", window.end},
                                                       {"horizontal", window.horizontal},
                                                       {"vertical", window.vertical},
                                                       {"facing", window.facing}}));
    }
    object["schemaId"]                  = eve::Value(std::string(ClimbingActionDefinition::SchemaId));
    object["schemaVersion"]             = eve::Value(ClimbingActionDefinition::SchemaVersion);
    object["id"]                        = eve::Value(action.id);
    object["kind"]                      = eve::Value(std::string(kindName(action.kind)));
    object["minHeight"]                 = eve::Value(action.minHeight);
    object["maxHeight"]                 = eve::Value(action.maxHeight);
    object["minSpeed"]                  = eve::Value(action.minSpeed);
    object["durationNs"]                = eve::Value(action.duration.nanoseconds());
    object["landingForward"]            = eve::Value(action.landingForward);
    object["apexHeight"]                = eve::Value(action.apexHeight);
    object["selectionBias"]             = eve::Value(action.selectionBias);
    object["hangBodyOffset"]            = eve::Value(action.hangBodyOffset);
    object["hangFeetBelowLedge"]        = eve::Value(action.hangFeetBelowLedge);
    object["handSpacing"]               = eve::Value(action.handSpacing);
    object["cancelWindowStart"]         = eve::Value(action.cancelWindowStart);
    object["cancelWindowEnd"]           = eve::Value(action.cancelWindowEnd);
    object["rootMotionScaleMin"]        = eve::Value(action.rootMotionScaleMin);
    object["rootMotionScaleMax"]        = eve::Value(action.rootMotionScaleMax);
    object["maxTranslationWarpPerTick"] = eve::Value(action.maxTranslationWarpPerTick);
    object["maxYawWarpRadiansPerTick"]  = eve::Value(action.maxYawWarpRadiansPerTick);
    object["horizontalWarpBudget"]      = eve::Value(action.horizontalWarpBudget);
    object["verticalWarpBudget"]        = eve::Value(action.verticalWarpBudget);
    object["facingWarpBudgetRadians"]   = eve::Value(action.facingWarpBudgetRadians);
    object["warpWindows"]               = eve::Value(std::move(warpWindowValues));
    object["requiredNotifies"]          = eve::Value(std::move(notifyValues));
    object["tags"]                      = stringArray(action.tags);
    object["sourceModes"]               = eve::Value(static_cast<std::int64_t>(action.sourceModes));
    object["requiredCommand"]           = eve::Value(static_cast<std::int64_t>(action.requiredCommand));
    object["probeRecipe"]               = eve::Value(static_cast<std::int64_t>(action.probeRecipe));
    object["minDepth"]                  = eve::Value(action.minDepth);
    object["maxDepth"]                  = eve::Value(action.maxDepth);
    object["minDistance"]               = eve::Value(action.minDistance);
    object["maxDistance"]               = eve::Value(action.maxDistance);
    object["minSurfaceNormalY"]         = eve::Value(action.minSurfaceNormalY);
    object["maxSlopeRadians"]           = eve::Value(action.maxSlopeRadians);
    object["maxCurvature"]              = eve::Value(action.maxCurvature);
    object["requiredSupportTags"]       = stringArray(action.requiredSupportTags);
    object["trajectory"]                = eve::Value(static_cast<std::int64_t>(action.trajectory));
    object["animation"] = eve::Value::object({{"clipId", action.animation.clipId},
                                                {"graphNodeId", action.animation.graphNodeId},
                                                {"rootBone", action.animation.rootBone},
                                                {"mirrored", action.animation.mirrored}});
    eve::Value::Array branchValues;
    branchValues.reserve(action.branchWindows.size());
    for (const ClimbingBranchWindow& window : action.branchWindows)
        branchValues.push_back(eve::Value::object(
            {{"start", window.start}, {"end", window.end}, {"comboTag", window.comboTag}}));
    object["branchWindows"] = eve::Value(std::move(branchValues));
    eve::Value::Array contactValues;
    contactValues.reserve(action.contactConstraints.size());
    for (const ClimbingContactConstraint& contact : action.contactConstraints)
        contactValues.push_back(eve::Value::object({{"target", static_cast<std::int64_t>(contact.target)},
                                                    {"start", contact.start},
                                                    {"end", contact.end},
                                                    {"maxWeight", contact.maxWeight}}));
    object["contactConstraints"]        = eve::Value(std::move(contactValues));
    object["landingPolicy"]             = eve::Value(static_cast<std::int64_t>(action.landingPolicy));
    object["terminalVelocityPolicy"]    = eve::Value(static_cast<std::int64_t>(action.terminalVelocityPolicy));
    object["comboTags"]                 = stringArray(action.comboTags);
    object["repetitionPenalty"]         = eve::Value(static_cast<std::int64_t>(action.repetitionPenalty));
    object["requiredConditionTags"]     = stringArray(action.requiredConditionTags);
    object["staminaCost"]               = eve::Value(action.staminaCost);
    object["cameraCue"]                 = eve::Value(action.cameraCue);
    object["eventMetadata"]             = stringArray(action.eventMetadata);
}

}  // namespace

eve::Result<void> validateClimbingActionDefinition(const ClimbingActionDefinition& action) {
    if (action.id.empty())
        return failure<void>(eve::DiagnosticCode::InvalidArgument, "action id must not be empty", "id");
    if (!finite(action.minHeight) || !finite(action.maxHeight) || action.minHeight < 0.f ||
        action.maxHeight < action.minHeight)
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "action height range must be finite, non-negative, and ordered", "height");
    if (!finite(action.minSpeed) || action.minSpeed < 0.f || !finite(action.landingForward) ||
        action.landingForward < 0.f || !finite(action.apexHeight) || action.apexHeight < 0.f)
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "action speed, landing distance, and apex must be finite and non-negative");
    if (action.duration.nanoseconds() <= 0)
        return failure<void>(eve::DiagnosticCode::InvalidArgument, "action duration must be positive", "durationNs");
    if (!finite(action.hangBodyOffset) || action.hangBodyOffset < 0.f || !finite(action.hangFeetBelowLedge) ||
        action.hangFeetBelowLedge <= 0.f || !finite(action.handSpacing) || action.handSpacing <= 0.f ||
        !finite(action.cancelWindowStart) || !finite(action.cancelWindowEnd) || action.cancelWindowStart < 0.f ||
        action.cancelWindowEnd > 1.f || action.cancelWindowEnd < action.cancelWindowStart)
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "hang geometry and cancel window must be finite and ordered");
    if (!finite(action.rootMotionScaleMin) || !finite(action.rootMotionScaleMax) || action.rootMotionScaleMin <= 0.f ||
        action.rootMotionScaleMax < action.rootMotionScaleMin || action.rootMotionScaleMax > 4.f ||
        !finite(action.maxTranslationWarpPerTick) || action.maxTranslationWarpPerTick <= 0.f ||
        !finite(action.maxYawWarpRadiansPerTick) || action.maxYawWarpRadiansPerTick <= 0.f ||
        !finite(action.horizontalWarpBudget) || action.horizontalWarpBudget < 0.f ||
        !finite(action.verticalWarpBudget) || action.verticalWarpBudget < 0.f ||
        !finite(action.facingWarpBudgetRadians) || action.facingWarpBudgetRadians < 0.f)
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "root-motion scale and warp budgets must be finite, positive, and bounded", "warp");
    float previousEnd = 0.f;
    for (std::size_t index = 0; index < action.warpWindows.size(); ++index) {
        const ClimbingWarpWindow& window = action.warpWindows[index];
        if (!finite(window.start) || !finite(window.end) || window.start < 0.f || window.end > 1.f ||
            window.end <= window.start || window.start < previousEnd ||
            (!window.horizontal && !window.vertical && !window.facing))
            return failure<void>(eve::DiagnosticCode::InvalidArgument,
                                 "warp windows must be ordered, non-overlapping, and enable a channel",
                                 "warpWindows." + std::to_string(index));
        previousEnd = window.end;
    }
    std::vector<std::string> notifyNames = action.requiredNotifies;
    std::sort(notifyNames.begin(), notifyNames.end());
    if (std::any_of(notifyNames.begin(), notifyNames.end(), [](const std::string& name) { return name.empty(); }) ||
        std::adjacent_find(notifyNames.begin(), notifyNames.end()) != notifyNames.end())
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "required animation notifies must be non-empty and unique", "requiredNotifies");
    const auto sourceBits = static_cast<std::uint8_t>(action.sourceModes);
    if (sourceBits == 0 || (sourceBits & ~static_cast<std::uint8_t>(ClimbingSourceMode::Any)) != 0)
        return failure<void>(eve::DiagnosticCode::InvalidArgument, "sourceModes must contain a known mode",
                             "sourceModes");
    if (!finite(action.minDepth) || !finite(action.maxDepth) || action.minDepth < 0.f ||
        action.maxDepth < action.minDepth || !finite(action.minDistance) || !finite(action.maxDistance) ||
        action.minDistance < 0.f || action.maxDistance < action.minDistance ||
        !finite(action.minSurfaceNormalY) || action.minSurfaceNormalY < -1.f || action.minSurfaceNormalY > 1.f ||
        !finite(action.maxSlopeRadians) || action.maxSlopeRadians < 0.f || action.maxSlopeRadians > 3.1415927f ||
        !finite(action.maxCurvature) || action.maxCurvature < 0.f)
        return failure<void>(eve::DiagnosticCode::InvalidArgument, "action geometry constraints are invalid",
                             "geometry");
    if (!uniqueNonEmpty(action.tags) || !uniqueNonEmpty(action.requiredSupportTags) ||
        !uniqueNonEmpty(action.comboTags) || !uniqueNonEmpty(action.requiredConditionTags) ||
        !uniqueNonEmpty(action.eventMetadata))
        return failure<void>(eve::DiagnosticCode::InvalidArgument, "action tags and event metadata must be unique",
                             "metadata");
    if (action.animation.rootBone.empty())
        return failure<void>(eve::DiagnosticCode::InvalidArgument, "animation rootBone must not be empty",
                             "animation.rootBone");
    for (std::size_t index = 0; index < action.branchWindows.size(); ++index) {
        const auto& window = action.branchWindows[index];
        if (!finite(window.start) || !finite(window.end) || window.start < 0.f || window.end > 1.f ||
            window.end <= window.start || window.comboTag.empty())
            return failure<void>(eve::DiagnosticCode::InvalidArgument, "branch window is invalid",
                                 "branchWindows." + std::to_string(index));
        if (std::find(action.comboTags.begin(), action.comboTags.end(), window.comboTag) == action.comboTags.end())
            return failure<void>(eve::DiagnosticCode::InvalidArgument,
                                 "branch window comboTag must be declared by the action",
                                 "branchWindows." + std::to_string(index) + ".comboTag");
    }
    for (std::size_t index = 0; index < action.contactConstraints.size(); ++index) {
        const auto& contact = action.contactConstraints[index];
        if (!finite(contact.start) || !finite(contact.end) || !finite(contact.maxWeight) || contact.start < 0.f ||
            contact.end > 1.f || contact.end <= contact.start || contact.maxWeight < 0.f || contact.maxWeight > 1.f)
            return failure<void>(eve::DiagnosticCode::InvalidArgument, "contact constraint is invalid",
                                 "contactConstraints." + std::to_string(index));
    }
    if (action.repetitionPenalty < 0 || !finite(action.staminaCost) || action.staminaCost < 0.f)
        return failure<void>(eve::DiagnosticCode::InvalidArgument, "action cost metadata is invalid", "cost");
    return eve::Result<void>::success();
}

eve::Result<void> validateClimbingProfileDefinition(const ClimbingProfileDefinition& profile) {
    if (!finite(profile.capsuleRadius) || !finite(profile.capsuleHeight) || profile.capsuleRadius <= 0.f ||
        profile.capsuleHeight <= profile.capsuleRadius * 2.f)
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "capsule height must exceed twice its positive radius", "capsule");
    if (!finite(profile.compactCapsuleHeight) ||
        profile.compactCapsuleHeight <= profile.capsuleRadius * 2.f ||
        profile.compactCapsuleHeight > profile.capsuleHeight)
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "compact capsule height must exceed the diameter and not exceed standing height",
                             "compactCapsuleHeight");
    if (!finite(profile.skin) || profile.skin < 0.f || !finite(profile.maxProbeDistance) ||
        profile.maxProbeDistance <= 0.f || !finite(profile.maxObstacleHeight) || profile.maxObstacleHeight <= 0.f ||
        !finite(profile.minTopNormalY) || profile.minTopNormalY < 0.f || profile.minTopNormalY > 1.f ||
        !finite(profile.maxWarpResidual) || profile.maxWarpResidual < 0.f)
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "profile probe, surface, skin, and warp limits are invalid");
    if (!finite(profile.maxPlatformSpeed) || profile.maxPlatformSpeed <= 0.f || !finite(profile.dropInitialSpeed) ||
        profile.dropInitialSpeed < 0.f)
        return failure<void>(eve::DiagnosticCode::InvalidArgument, "platform and drop speed limits must be finite");
    if (profile.inputBufferTicks == 0 || profile.inputBufferTicks > 1000000 || profile.coyoteTicks > 1000000)
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "input buffer must be non-zero and tick windows must be bounded", "inputTicks");
    if (profile.pathValidationSegments < 2 || profile.pathValidationSegments > 64 ||
        !finite(profile.maxPelvisDeviation) || profile.maxPelvisDeviation < 0.f)
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "path validation samples and pelvis deviation are invalid", "motionValidation");
    if (!finite(profile.groundAcceleration) || profile.groundAcceleration < 0.f || !finite(profile.groundBraking) ||
        profile.groundBraking < 0.f || !finite(profile.airControl) || profile.airControl < 0.f ||
        profile.airControl > 1.f || !finite(profile.gravity) || profile.gravity <= 0.f ||
        !finite(profile.jumpSpeed) || profile.jumpSpeed <= 0.f || profile.probeSectors == 0 ||
        profile.probeSectors > 64 || profile.maxCandidates == 0 || profile.maxCandidates > 8 ||
        !finite(profile.autoAssistStrength) || profile.autoAssistStrength < 0.f || profile.autoAssistStrength > 1.f ||
        !finite(profile.maxTotalWarpBudget) || profile.maxTotalWarpBudget < 0.f)
        return failure<void>(eve::DiagnosticCode::InvalidArgument, "profile locomotion and probe policies are invalid",
                             "policies");
    if (!uniqueNonEmpty(profile.defaultActionIds) || !uniqueNonEmpty(profile.allowedActionTags) ||
        !uniqueNonEmpty(profile.deniedActionTags))
        return failure<void>(eve::DiagnosticCode::InvalidArgument, "profile action ids and tags must be unique",
                             "actionPolicy");
    const ClimbingScoreWeights& weights = profile.scoreWeights;
    if (weights.direction < 0 || weights.approachSpeed < 0 || weights.height < 0 || weights.distance < 0 ||
        weights.warpTranslation < 0 || weights.warpRotation < 0 || weights.intentMismatch < 0)
        return failure<void>(eve::DiagnosticCode::InvalidArgument, "score weights must be non-negative",
                             "scoreWeights");
    if (profile.staminaPolicy == ClimbingStaminaPolicy::RequireProvider && profile.staminaAdapter.empty())
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "RequireProvider stamina policy needs a stable adapter id", "staminaAdapter");
    std::unordered_set<std::string> ids;
    for (const ClimbingActionDefinition& action : profile.actions) {
        auto valid = validateClimbingActionDefinition(action);
        if (!valid) return eve::Result<void>::failure(valid.status());
        if (std::max(action.horizontalWarpBudget, action.verticalWarpBudget) >
            profile.maxTotalWarpBudget + 0.0001f)
            return failure<void>(eve::DiagnosticCode::InvalidArgument,
                                 "action translation warp budgets exceed profile maximum", "actions." + action.id);
        if (!ids.emplace(action.id).second)
            return failure<void>(eve::DiagnosticCode::AlreadyExists, "profile contains duplicate action id",
                                  "actions." + action.id);
    }
    for (const std::string& id : profile.defaultActionIds)
        if (!ids.contains(id))
            return failure<void>(eve::DiagnosticCode::NotFound,
                                 "default action id is not registered in the profile", "defaultActionIds." + id);
    return eve::Result<void>::success();
}

eve::Result<eve::Value> encodeClimbingActionDefinition(const ClimbingActionDefinition& action) {
    auto valid = validateClimbingActionDefinition(action);
    if (!valid) return eve::Result<eve::Value>::failure(valid.status());
    eve::Value::Object object = action.extensionMetadata;
    writeActionFields(object, action);
    return eve::Result<eve::Value>::success(eve::Value(std::move(object)));
}

eve::Result<ClimbingActionDefinition> decodeClimbingActionDefinition(const eve::Value& value) {
    auto root = readRoot(value, ClimbingActionDefinition::SchemaId, ClimbingActionDefinition::SchemaVersion);
    if (!root) return eve::Result<ClimbingActionDefinition>::failure(root.status());
    const eve::Value::Object& object = *root.value();
    ClimbingActionDefinition  candidate;
    std::string               kind;
    std::int64_t              durationNs       = 0;
    std::int64_t              selectionBias    = 0;
    const eve::Value*         notifiesValue    = field(object, "requiredNotifies");
    const auto*               notifies         = notifiesValue ? notifiesValue->getIf<eve::Value::Array>() : nullptr;
    const eve::Value*         warpWindowsValue = field(object, "warpWindows");
    const auto*               warpWindows = warpWindowsValue ? warpWindowsValue->getIf<eve::Value::Array>() : nullptr;
    if (!readString(object, "id", candidate.id) || !readString(object, "kind", kind) ||
        !readKind(kind, candidate.kind) || !readFloat(object, "minHeight", candidate.minHeight) ||
        !readFloat(object, "maxHeight", candidate.maxHeight) || !readFloat(object, "minSpeed", candidate.minSpeed) ||
        !readInt64(object, "durationNs", durationNs) ||
        !readFloat(object, "landingForward", candidate.landingForward) ||
        !readFloat(object, "apexHeight", candidate.apexHeight) || !readInt64(object, "selectionBias", selectionBias) ||
        selectionBias < std::numeric_limits<int>::min() || selectionBias > std::numeric_limits<int>::max() ||
        !readFloat(object, "hangBodyOffset", candidate.hangBodyOffset) ||
        !readFloat(object, "hangFeetBelowLedge", candidate.hangFeetBelowLedge) ||
        !readFloat(object, "handSpacing", candidate.handSpacing) ||
        !readFloat(object, "cancelWindowStart", candidate.cancelWindowStart) ||
        !readFloat(object, "cancelWindowEnd", candidate.cancelWindowEnd) ||
        !readOptionalFloat(object, "rootMotionScaleMin", candidate.rootMotionScaleMin) ||
        !readOptionalFloat(object, "rootMotionScaleMax", candidate.rootMotionScaleMax) ||
        !readOptionalFloat(object, "maxTranslationWarpPerTick", candidate.maxTranslationWarpPerTick) ||
        !readOptionalFloat(object, "maxYawWarpRadiansPerTick", candidate.maxYawWarpRadiansPerTick) ||
        !readOptionalFloat(object, "horizontalWarpBudget", candidate.horizontalWarpBudget) ||
        !readOptionalFloat(object, "verticalWarpBudget", candidate.verticalWarpBudget) ||
        !readOptionalFloat(object, "facingWarpBudgetRadians", candidate.facingWarpBudgetRadians) ||
        (warpWindowsValue && !warpWindows) || !notifies)
        return failure<ClimbingActionDefinition>(eve::DiagnosticCode::ParseError,
                                                 "climbing action has missing or invalid known fields");
    candidate.duration      = eve::Duration::fromNanoseconds(durationNs);
    candidate.selectionBias = static_cast<int>(selectionBias);
    candidate.requiredNotifies.reserve(notifies->size());
    for (std::size_t index = 0; index < notifies->size(); ++index) {
        const auto* notify = (*notifies)[index].getIf<std::string>();
        if (!notify)
            return failure<ClimbingActionDefinition>(eve::DiagnosticCode::ParseError,
                                                     "animation notify must be a string",
                                                     "requiredNotifies." + std::to_string(index));
        candidate.requiredNotifies.push_back(*notify);
    }
    if (warpWindows) candidate.warpWindows.reserve(warpWindows->size());
    for (std::size_t index = 0; warpWindows && index < warpWindows->size(); ++index) {
        const auto*        valueObject = (*warpWindows)[index].getIf<eve::Value::Object>();
        ClimbingWarpWindow window;
        if (!valueObject || !readFloat(*valueObject, "start", window.start) ||
            !readFloat(*valueObject, "end", window.end) || !readBool(*valueObject, "horizontal", window.horizontal) ||
            !readBool(*valueObject, "vertical", window.vertical) || !readBool(*valueObject, "facing", window.facing))
            return failure<ClimbingActionDefinition>(eve::DiagnosticCode::ParseError,
                                                     "warp window has missing or invalid fields",
                                                     "warpWindows." + std::to_string(index));
        candidate.warpWindows.push_back(window);
    }
    std::int64_t repetitionPenalty = candidate.repetitionPenalty;
    if (!readOptionalStringArray(object, "tags", candidate.tags) ||
        !readOptionalEnum(object, "sourceModes", candidate.sourceModes,
                          static_cast<std::int64_t>(ClimbingSourceMode::Any)) ||
        !readOptionalEnum(object, "requiredCommand", candidate.requiredCommand,
                          static_cast<std::int64_t>(ClimbingCommandRequirement::Crouch)) ||
        !readOptionalEnum(object, "probeRecipe", candidate.probeRecipe,
                          static_cast<std::int64_t>(ClimbingProbeRecipe::AnchorGraph)) ||
        !readOptionalFloat(object, "minDepth", candidate.minDepth) ||
        !readOptionalFloat(object, "maxDepth", candidate.maxDepth) ||
        !readOptionalFloat(object, "minDistance", candidate.minDistance) ||
        !readOptionalFloat(object, "maxDistance", candidate.maxDistance) ||
        !readOptionalFloat(object, "minSurfaceNormalY", candidate.minSurfaceNormalY) ||
        !readOptionalFloat(object, "maxSlopeRadians", candidate.maxSlopeRadians) ||
        !readOptionalFloat(object, "maxCurvature", candidate.maxCurvature) ||
        !readOptionalStringArray(object, "requiredSupportTags", candidate.requiredSupportTags) ||
        !readOptionalEnum(object, "trajectory", candidate.trajectory,
                          static_cast<std::int64_t>(ClimbingTrajectoryKind::AnchorToAnchor)) ||
        !readOptionalEnum(object, "landingPolicy", candidate.landingPolicy,
                          static_cast<std::int64_t>(ClimbingLandingPolicy::Stop)) ||
        !readOptionalEnum(object, "terminalVelocityPolicy", candidate.terminalVelocityPolicy,
                          static_cast<std::int64_t>(ClimbingTerminalVelocityPolicy::Zero)) ||
        !readOptionalStringArray(object, "comboTags", candidate.comboTags) ||
        !readOptionalInt64(object, "repetitionPenalty", repetitionPenalty) ||
        repetitionPenalty < std::numeric_limits<std::int32_t>::min() ||
        repetitionPenalty > std::numeric_limits<std::int32_t>::max() ||
        !readOptionalStringArray(object, "requiredConditionTags", candidate.requiredConditionTags) ||
        !readOptionalFloat(object, "staminaCost", candidate.staminaCost) ||
        !readOptionalString(object, "cameraCue", candidate.cameraCue) ||
        !readOptionalStringArray(object, "eventMetadata", candidate.eventMetadata))
        return failure<ClimbingActionDefinition>(eve::DiagnosticCode::ParseError,
                                                 "climbing action has invalid v4 metadata");
    candidate.repetitionPenalty = static_cast<std::int32_t>(repetitionPenalty);
    if (const eve::Value* animationValue = field(object, "animation")) {
        const auto* animation = animationValue->getIf<eve::Value::Object>();
        if (!animation || !readString(*animation, "clipId", candidate.animation.clipId) ||
            !readString(*animation, "graphNodeId", candidate.animation.graphNodeId) ||
            !readString(*animation, "rootBone", candidate.animation.rootBone) ||
            !readBool(*animation, "mirrored", candidate.animation.mirrored))
            return failure<ClimbingActionDefinition>(eve::DiagnosticCode::ParseError,
                                                     "animation binding has missing or invalid fields", "animation");
    }
    if (const eve::Value* branchValue = field(object, "branchWindows")) {
        const auto* branches = branchValue->getIf<eve::Value::Array>();
        if (!branches)
            return failure<ClimbingActionDefinition>(eve::DiagnosticCode::ParseError,
                                                     "branchWindows must be an array", "branchWindows");
        candidate.branchWindows.reserve(branches->size());
        for (std::size_t index = 0; index < branches->size(); ++index) {
            const auto* valueObject = (*branches)[index].getIf<eve::Value::Object>();
            ClimbingBranchWindow window;
            if (!valueObject || !readFloat(*valueObject, "start", window.start) ||
                !readFloat(*valueObject, "end", window.end) ||
                !readString(*valueObject, "comboTag", window.comboTag))
                return failure<ClimbingActionDefinition>(eve::DiagnosticCode::ParseError,
                                                         "branch window has missing or invalid fields",
                                                         "branchWindows." + std::to_string(index));
            candidate.branchWindows.push_back(std::move(window));
        }
    }
    if (const eve::Value* contactsValue = field(object, "contactConstraints")) {
        const auto* contacts = contactsValue->getIf<eve::Value::Array>();
        if (!contacts)
            return failure<ClimbingActionDefinition>(eve::DiagnosticCode::ParseError,
                                                     "contactConstraints must be an array", "contactConstraints");
        candidate.contactConstraints.reserve(contacts->size());
        for (std::size_t index = 0; index < contacts->size(); ++index) {
            const auto* valueObject = (*contacts)[index].getIf<eve::Value::Object>();
            ClimbingContactConstraint contact;
            if (!valueObject ||
                !readOptionalEnum(*valueObject, "target", contact.target,
                                  static_cast<std::int64_t>(ClimbingContactTarget::Pelvis)) ||
                !field(*valueObject, "target") || !readFloat(*valueObject, "start", contact.start) ||
                !readFloat(*valueObject, "end", contact.end) ||
                !readFloat(*valueObject, "maxWeight", contact.maxWeight))
                return failure<ClimbingActionDefinition>(eve::DiagnosticCode::ParseError,
                                                         "contact constraint has missing or invalid fields",
                                                         "contactConstraints." + std::to_string(index));
            candidate.contactConstraints.push_back(contact);
        }
    }
    candidate.extensionMetadata = unknownFields(object, {"schemaId",
                                                         "schemaVersion",
                                                         "id",
                                                         "kind",
                                                         "minHeight",
                                                         "maxHeight",
                                                         "minSpeed",
                                                         "durationNs",
                                                         "landingForward",
                                                         "apexHeight",
                                                         "selectionBias",
                                                         "hangBodyOffset",
                                                         "hangFeetBelowLedge",
                                                         "handSpacing",
                                                         "cancelWindowStart",
                                                         "cancelWindowEnd",
                                                         "rootMotionScaleMin",
                                                         "rootMotionScaleMax",
                                                         "maxTranslationWarpPerTick",
                                                         "maxYawWarpRadiansPerTick",
                                                         "horizontalWarpBudget",
                                                         "verticalWarpBudget",
                                                         "facingWarpBudgetRadians",
                                                         "warpWindows",
                                                         "requiredNotifies",
                                                         "tags",
                                                         "sourceModes",
                                                         "requiredCommand",
                                                         "probeRecipe",
                                                         "minDepth",
                                                         "maxDepth",
                                                         "minDistance",
                                                         "maxDistance",
                                                         "minSurfaceNormalY",
                                                         "maxSlopeRadians",
                                                         "maxCurvature",
                                                         "requiredSupportTags",
                                                         "trajectory",
                                                         "animation",
                                                         "branchWindows",
                                                         "contactConstraints",
                                                         "landingPolicy",
                                                         "terminalVelocityPolicy",
                                                         "comboTags",
                                                         "repetitionPenalty",
                                                         "requiredConditionTags",
                                                         "staminaCost",
                                                         "cameraCue",
                                                         "eventMetadata"});
    auto valid                  = validateClimbingActionDefinition(candidate);
    if (!valid) return eve::Result<ClimbingActionDefinition>::failure(valid.status());
    return eve::Result<ClimbingActionDefinition>::success(std::move(candidate));
}

eve::Result<eve::Value> encodeClimbingProfileDefinition(const ClimbingProfileDefinition& profile) {
    auto valid = validateClimbingProfileDefinition(profile);
    if (!valid) return eve::Result<eve::Value>::failure(valid.status());
    eve::Value::Array actions;
    actions.reserve(profile.actions.size());
    for (const ClimbingActionDefinition& action : profile.actions) {
        auto encoded = encodeClimbingActionDefinition(action);
        if (!encoded) return eve::Result<eve::Value>::failure(encoded.status());
        actions.push_back(std::move(encoded).takeValue());
    }
    eve::Value::Object filter{{"categoryBits", eve::Value(static_cast<std::int64_t>(profile.queryFilter.categoryBits))},
                              {"maskBits", eve::Value(static_cast<std::int64_t>(profile.queryFilter.maskBits))},
                              {"ignoredBodyId", eve::Value(profile.queryFilter.ignoredBodyId)},
                              {"ignoredShapeId", eve::Value(profile.queryFilter.ignoredShapeId)}};
    eve::Value::Object object        = profile.extensionMetadata;
    object["schemaId"]               = eve::Value(std::string(ClimbingProfileDefinition::SchemaId));
    object["schemaVersion"]          = eve::Value(ClimbingProfileDefinition::SchemaVersion);
    object["capsuleRadius"]          = eve::Value(profile.capsuleRadius);
    object["capsuleHeight"]          = eve::Value(profile.capsuleHeight);
    object["compactCapsuleHeight"]   = eve::Value(profile.compactCapsuleHeight);
    object["skin"]                   = eve::Value(profile.skin);
    object["maxProbeDistance"]       = eve::Value(profile.maxProbeDistance);
    object["maxObstacleHeight"]      = eve::Value(profile.maxObstacleHeight);
    object["minTopNormalY"]          = eve::Value(profile.minTopNormalY);
    object["maxWarpResidual"]        = eve::Value(profile.maxWarpResidual);
    object["maxPlatformSpeed"]       = eve::Value(profile.maxPlatformSpeed);
    object["dropInitialSpeed"]       = eve::Value(profile.dropInitialSpeed);
    object["inputBufferTicks"]       = eve::Value(static_cast<std::int64_t>(profile.inputBufferTicks));
    object["coyoteTicks"]            = eve::Value(static_cast<std::int64_t>(profile.coyoteTicks));
    object["pathValidationSegments"] = eve::Value(static_cast<std::int64_t>(profile.pathValidationSegments));
    object["maxPelvisDeviation"]     = eve::Value(profile.maxPelvisDeviation);
    object["groundAcceleration"]     = eve::Value(profile.groundAcceleration);
    object["groundBraking"]          = eve::Value(profile.groundBraking);
    object["airControl"]             = eve::Value(profile.airControl);
    object["gravity"]                = eve::Value(profile.gravity);
    object["jumpSpeed"]              = eve::Value(profile.jumpSpeed);
    object["probeSectors"]           = eve::Value(static_cast<std::int64_t>(profile.probeSectors));
    object["maxCandidates"]          = eve::Value(static_cast<std::int64_t>(profile.maxCandidates));
    object["autoAssistStrength"]     = eve::Value(profile.autoAssistStrength);
    object["scoreWeights"] = eve::Value::object({{"direction", profile.scoreWeights.direction},
                                                   {"approachSpeed", profile.scoreWeights.approachSpeed},
                                                   {"height", profile.scoreWeights.height},
                                                   {"distance", profile.scoreWeights.distance},
                                                   {"warpTranslation", profile.scoreWeights.warpTranslation},
                                                   {"warpRotation", profile.scoreWeights.warpRotation},
                                                   {"intentMismatch", profile.scoreWeights.intentMismatch}});
    object["maxTotalWarpBudget"]     = eve::Value(profile.maxTotalWarpBudget);
    object["defaultActionIds"]       = stringArray(profile.defaultActionIds);
    object["allowedActionTags"]      = stringArray(profile.allowedActionTags);
    object["deniedActionTags"]       = stringArray(profile.deniedActionTags);
    object["staminaPolicy"]          = eve::Value(static_cast<std::int64_t>(profile.staminaPolicy));
    object["staminaAdapter"]         = eve::Value(profile.staminaAdapter);
    object["cameraCueProfile"]       = eve::Value(profile.cameraCueProfile);
    object["queryFilter"]            = eve::Value(std::move(filter));
    object["actions"]                = eve::Value(std::move(actions));
    return eve::Result<eve::Value>::success(eve::Value(std::move(object)));
}

eve::Result<ClimbingProfileDefinition> decodeClimbingProfileDefinition(const eve::Value& value) {
    auto root = readRoot(value, ClimbingProfileDefinition::SchemaId, ClimbingProfileDefinition::SchemaVersion);
    if (!root) return eve::Result<ClimbingProfileDefinition>::failure(root.status());
    const eve::Value::Object& object = *root.value();
    ClimbingProfileDefinition candidate;
    const eve::Value*         actionsValue     = field(object, "actions");
    const auto*               actions          = actionsValue ? actionsValue->getIf<eve::Value::Array>() : nullptr;
    const eve::Value*         filterValue      = field(object, "queryFilter");
    const auto*               filter           = filterValue ? filterValue->getIf<eve::Value::Object>() : nullptr;
    std::int64_t              categoryBits     = 0;
    std::int64_t              maskBits         = 0;
    std::int64_t              ignoredBodyId    = 0;
    std::int64_t              ignoredShapeId   = 0;
    std::int64_t              inputBufferTicks = 0;
    std::int64_t              coyoteTicks      = 0;
    std::int64_t              pathValidationSegments = candidate.pathValidationSegments;
    std::int64_t              probeSectors = candidate.probeSectors;
    std::int64_t              maxCandidates = candidate.maxCandidates;
    if (!readFloat(object, "capsuleRadius", candidate.capsuleRadius) ||
        !readFloat(object, "capsuleHeight", candidate.capsuleHeight) ||
        !readOptionalFloat(object, "compactCapsuleHeight", candidate.compactCapsuleHeight) ||
        !readFloat(object, "skin", candidate.skin) ||
        !readFloat(object, "maxProbeDistance", candidate.maxProbeDistance) ||
        !readFloat(object, "maxObstacleHeight", candidate.maxObstacleHeight) ||
        !readFloat(object, "minTopNormalY", candidate.minTopNormalY) ||
        !readFloat(object, "maxWarpResidual", candidate.maxWarpResidual) ||
        !readFloat(object, "maxPlatformSpeed", candidate.maxPlatformSpeed) ||
        !readFloat(object, "dropInitialSpeed", candidate.dropInitialSpeed) ||
        !readInt64(object, "inputBufferTicks", inputBufferTicks) || inputBufferTicks <= 0 ||
        !readInt64(object, "coyoteTicks", coyoteTicks) || coyoteTicks < 0 ||
        !readOptionalInt64(object, "pathValidationSegments", pathValidationSegments) || pathValidationSegments < 0 ||
        pathValidationSegments > std::numeric_limits<std::uint32_t>::max() ||
        !readOptionalFloat(object, "maxPelvisDeviation", candidate.maxPelvisDeviation) ||
        !readOptionalFloat(object, "groundAcceleration", candidate.groundAcceleration) ||
        !readOptionalFloat(object, "groundBraking", candidate.groundBraking) ||
        !readOptionalFloat(object, "airControl", candidate.airControl) ||
        !readOptionalFloat(object, "gravity", candidate.gravity) ||
        !readOptionalFloat(object, "jumpSpeed", candidate.jumpSpeed) ||
        !readOptionalInt64(object, "probeSectors", probeSectors) || probeSectors < 0 ||
        probeSectors > std::numeric_limits<std::uint32_t>::max() ||
        !readOptionalInt64(object, "maxCandidates", maxCandidates) || maxCandidates < 0 ||
        maxCandidates > std::numeric_limits<std::uint32_t>::max() ||
        !readOptionalFloat(object, "autoAssistStrength", candidate.autoAssistStrength) ||
        !readOptionalFloat(object, "maxTotalWarpBudget", candidate.maxTotalWarpBudget) ||
        !readOptionalStringArray(object, "defaultActionIds", candidate.defaultActionIds) ||
        !readOptionalStringArray(object, "allowedActionTags", candidate.allowedActionTags) ||
        !readOptionalStringArray(object, "deniedActionTags", candidate.deniedActionTags) ||
        !readOptionalEnum(object, "staminaPolicy", candidate.staminaPolicy,
                          static_cast<std::int64_t>(ClimbingStaminaPolicy::UseWhenPresent)) ||
        !readOptionalString(object, "staminaAdapter", candidate.staminaAdapter) ||
        !readOptionalString(object, "cameraCueProfile", candidate.cameraCueProfile) || !actions || !filter ||
        !readInt64(*filter, "categoryBits", categoryBits) || !readInt64(*filter, "maskBits", maskBits) ||
        categoryBits < 0 || categoryBits > std::numeric_limits<std::uint32_t>::max() || maskBits < 0 ||
        maskBits > std::numeric_limits<std::uint32_t>::max() || !readInt64(*filter, "ignoredBodyId", ignoredBodyId) ||
        ignoredBodyId < std::numeric_limits<int>::min() || ignoredBodyId > std::numeric_limits<int>::max() ||
        !readInt64(*filter, "ignoredShapeId", ignoredShapeId) || ignoredShapeId < std::numeric_limits<int>::min() ||
        ignoredShapeId > std::numeric_limits<int>::max())
        return failure<ClimbingProfileDefinition>(eve::DiagnosticCode::ParseError,
                                                  "climbing profile has missing or invalid known fields");
    candidate.queryFilter.categoryBits   = static_cast<std::uint32_t>(categoryBits);
    candidate.queryFilter.maskBits       = static_cast<std::uint32_t>(maskBits);
    candidate.queryFilter.ignoredBodyId  = static_cast<int>(ignoredBodyId);
    candidate.queryFilter.ignoredShapeId = static_cast<int>(ignoredShapeId);
    candidate.inputBufferTicks           = static_cast<std::uint64_t>(inputBufferTicks);
    candidate.coyoteTicks                = static_cast<std::uint64_t>(coyoteTicks);
    candidate.pathValidationSegments     = static_cast<std::uint32_t>(pathValidationSegments);
    candidate.probeSectors               = static_cast<std::uint32_t>(probeSectors);
    candidate.maxCandidates              = static_cast<std::uint32_t>(maxCandidates);
    if (const eve::Value* weightsValue = field(object, "scoreWeights")) {
        const auto* weights = weightsValue->getIf<eve::Value::Object>();
        std::int64_t direction = 0, approachSpeed = 0, height = 0, distance = 0;
        std::int64_t warpTranslation = 0, warpRotation = 0, intentMismatch = 0;
        if (!weights || !readInt64(*weights, "direction", direction) ||
            !readInt64(*weights, "approachSpeed", approachSpeed) || !readInt64(*weights, "height", height) ||
            !readInt64(*weights, "distance", distance) ||
            !readInt64(*weights, "warpTranslation", warpTranslation) ||
            !readInt64(*weights, "warpRotation", warpRotation) ||
            !readInt64(*weights, "intentMismatch", intentMismatch))
            return failure<ClimbingProfileDefinition>(eve::DiagnosticCode::ParseError,
                                                       "scoreWeights has missing or invalid fields", "scoreWeights");
        const auto inRange = [](std::int64_t value) {
            return value >= std::numeric_limits<std::int32_t>::min() &&
                   value <= std::numeric_limits<std::int32_t>::max();
        };
        if (!inRange(direction) || !inRange(approachSpeed) || !inRange(height) || !inRange(distance) ||
            !inRange(warpTranslation) || !inRange(warpRotation) || !inRange(intentMismatch))
            return failure<ClimbingProfileDefinition>(eve::DiagnosticCode::ParseError,
                                                       "scoreWeights exceeds int32 range", "scoreWeights");
        candidate.scoreWeights = {static_cast<std::int32_t>(direction),
                                  static_cast<std::int32_t>(approachSpeed),
                                  static_cast<std::int32_t>(height),
                                  static_cast<std::int32_t>(distance),
                                  static_cast<std::int32_t>(warpTranslation),
                                  static_cast<std::int32_t>(warpRotation),
                                  static_cast<std::int32_t>(intentMismatch)};
    }
    candidate.actions.reserve(actions->size());
    for (std::size_t index = 0; index < actions->size(); ++index) {
        auto decoded = decodeClimbingActionDefinition((*actions)[index]);
        if (!decoded)
            return failure<ClimbingProfileDefinition>(
                decoded.error()->code(), decoded.error()->message(),
                "actions." + std::to_string(index) + "." + decoded.error()->path());
        candidate.actions.push_back(std::move(decoded).takeValue());
    }
    candidate.extensionMetadata =
        unknownFields(object, {"schemaId", "schemaVersion", "capsuleRadius", "capsuleHeight",
                               "compactCapsuleHeight", "skin",
                               "maxProbeDistance", "maxObstacleHeight", "minTopNormalY", "maxWarpResidual",
                               "maxPlatformSpeed", "dropInitialSpeed", "inputBufferTicks", "coyoteTicks",
                               "pathValidationSegments", "maxPelvisDeviation", "groundAcceleration",
                               "groundBraking", "airControl", "gravity", "jumpSpeed", "probeSectors",
                               "maxCandidates", "autoAssistStrength", "scoreWeights", "maxTotalWarpBudget",
                               "defaultActionIds", "allowedActionTags", "deniedActionTags", "staminaPolicy",
                               "staminaAdapter", "cameraCueProfile", "queryFilter", "actions"});
    auto valid = validateClimbingProfileDefinition(candidate);
    if (!valid) return eve::Result<ClimbingProfileDefinition>::failure(valid.status());
    return eve::Result<ClimbingProfileDefinition>::success(std::move(candidate));
}

}  // namespace eve::climbing
