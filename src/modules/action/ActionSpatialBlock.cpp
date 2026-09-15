#include "action/ActionSpatialBlock.h"

#include <cmath>
#include <limits>
#include <utility>

namespace eve::action {
namespace {

template <typename T>
Result<T> invalid(std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

Result<double> number(const Value& value, std::string path) {
    double result = 0.0;
    if (const auto* integer = value.getIf<std::int64_t>())
        result = static_cast<double>(*integer);
    else if (const auto* decimal = value.getIf<double>())
        result = *decimal;
    else
        return invalid<double>("spatial component must be numeric", std::move(path));
    if (!std::isfinite(result)) return invalid<double>("spatial component must be finite", std::move(path));
    return Result<double>::success(result);
}

Result<ActionSpatialVector3> vector(const Value::Object& payload, std::string_view key,
                                    ActionSpatialVector3 fallback) {
    const auto found = payload.find(std::string(key));
    if (found == payload.end()) return Result<ActionSpatialVector3>::success(fallback);
    const auto* values = found->second.getIf<Value::Array>();
    if (!values || values->size() != 3)
        return invalid<ActionSpatialVector3>("spatial vector must contain exactly three numbers", std::string(key));
    ActionSpatialVector3 result;
    double*              components[] = {&result.x, &result.y, &result.z};
    for (std::size_t index = 0; index < 3; ++index) {
        auto parsed = number((*values)[index], std::string(key) + "[" + std::to_string(index) + "]");
        if (!parsed) return Result<ActionSpatialVector3>::failure(parsed.status());
        *components[index] = std::move(parsed).takeValue();
    }
    return Result<ActionSpatialVector3>::success(result);
}

Result<std::string> text(const Value::Object& payload, std::string_view key, std::string fallback) {
    const auto found = payload.find(std::string(key));
    if (found == payload.end()) return Result<std::string>::success(std::move(fallback));
    const auto* value = found->second.getIf<std::string>();
    if (!value) return invalid<std::string>("spatial field must be text", std::string(key));
    return Result<std::string>::success(*value);
}

}  // namespace

std::string_view actionSpatialAttachmentModeName(ActionSpatialAttachmentMode mode) noexcept {
    switch (mode) {
        case ActionSpatialAttachmentMode::FollowTarget: return "follow_target";
        case ActionSpatialAttachmentMode::FollowPositionOnly: return "follow_position_only";
        case ActionSpatialAttachmentMode::WorldTransformAtStart: return "world_transform_at_start";
    }
    return "follow_target";
}

Result<ActionSpatialBinding> ActionSpatialBinding::fromPayload(const Value::Object& payload) {
    ActionSpatialBinding candidate;
    auto                 mode = text(payload, "attachment", "follow_target");
    if (!mode) return Result<ActionSpatialBinding>::failure(mode.status());
    if (mode.value() == "follow_target")
        candidate.mode = ActionSpatialAttachmentMode::FollowTarget;
    else if (mode.value() == "follow_position_only")
        candidate.mode = ActionSpatialAttachmentMode::FollowPositionOnly;
    else if (mode.value() == "world_transform_at_start")
        candidate.mode = ActionSpatialAttachmentMode::WorldTransformAtStart;
    else
        return invalid<ActionSpatialBinding>("unknown spatial attachment mode", "attachment");

    auto target = text(payload, "spatialTarget", "source");
    if (!target) return Result<ActionSpatialBinding>::failure(target.status());
    if (target.value() == "source")
        candidate.target = ActionSpatialTarget::Source;
    else if (target.value() == "target")
        candidate.target = ActionSpatialTarget::Target;
    else
        return invalid<ActionSpatialBinding>("unknown spatial target", "spatialTarget");

    if (const auto found = payload.find("targetIndex"); found != payload.end()) {
        const auto* index = found->second.getIf<std::int64_t>();
        if (!index || *index < 0 || static_cast<std::uint64_t>(*index) > std::numeric_limits<std::size_t>::max())
            return invalid<ActionSpatialBinding>("target index must be a non-negative integer", "targetIndex");
        candidate.targetIndex = static_cast<std::size_t>(*index);
    }
    auto bone = text(payload, "bone", {});
    if (!bone) return Result<ActionSpatialBinding>::failure(bone.status());
    candidate.bone = std::move(bone).takeValue();

    auto position = vector(payload, "positionOffset", {});
    if (!position) return Result<ActionSpatialBinding>::failure(position.status());
    candidate.positionOffset = std::move(position).takeValue();
    auto rotation = vector(payload, "rotationOffsetDegrees", {});
    if (!rotation) return Result<ActionSpatialBinding>::failure(rotation.status());
    candidate.rotationOffsetDegrees = std::move(rotation).takeValue();
    auto scale = vector(payload, "scale", {1.0, 1.0, 1.0});
    if (!scale) return Result<ActionSpatialBinding>::failure(scale.status());
    candidate.scale = std::move(scale).takeValue();
    if (candidate.scale.x <= 0.0 || candidate.scale.y <= 0.0 || candidate.scale.z <= 0.0)
        return invalid<ActionSpatialBinding>("spatial scale components must be positive", "scale");
    return Result<ActionSpatialBinding>::success(std::move(candidate));
}

}  // namespace eve::action
