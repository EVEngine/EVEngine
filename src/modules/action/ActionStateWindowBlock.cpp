#include "action/ActionStateWindowBlock.h"

#include <limits>
#include <utility>

namespace eve::action {
namespace {

template <typename T>
Result<T> invalid(std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

Result<std::optional<std::size_t>> parseTargetIndex(const Value::Object& payload) {
    const auto found = payload.find("targetIndex");
    if (found == payload.end()) return Result<std::optional<std::size_t>>::success(std::nullopt);
    const auto* value = found->second.getIf<std::int64_t>();
    if (!value || *value < 0 || static_cast<std::uint64_t>(*value) > std::numeric_limits<std::uint32_t>::max())
        return invalid<std::optional<std::size_t>>(
            "state window target index must be an unsigned 32-bit integer", "targetIndex");
    return Result<std::optional<std::size_t>>::success(static_cast<std::size_t>(*value));
}

Result<std::string> requiredString(const Value::Object& payload, const char* field) {
    const auto found = payload.find(field);
    const auto* value = found == payload.end() ? nullptr : found->second.getIf<std::string>();
    if (!value || value->empty()) return invalid<std::string>("state window resource must be non-empty", field);
    return Result<std::string>::success(*value);
}

}  // namespace

Result<ActionStateWindowBinding> ActionStateWindowBinding::fromPayload(std::string_view type,
                                                                       const Value::Object& payload) {
    ActionStateWindowBinding candidate;
    auto target = parseTargetIndex(payload);
    if (!target) return Result<ActionStateWindowBinding>::failure(target.status());
    candidate.targetIndex = target.value();
    const char* field = nullptr;
    if (type == "combat:hitbox-window") {
        candidate.kind = ActionStateWindowKind::Hitbox;
        field = "hitbox";
    } else if (type == "combat:invulnerability-window") {
        candidate.kind = ActionStateWindowKind::Invulnerability;
    } else if (type == "input:combo-window") {
        candidate.kind = ActionStateWindowKind::Combo;
        field = "input";
    } else if (type == "collision:ignore-window") {
        candidate.kind = ActionStateWindowKind::CollisionIgnore;
        field = "channel";
    } else {
        return invalid<ActionStateWindowBinding>("unknown built-in state window type", "type");
    }
    if (field) {
        auto resource = requiredString(payload, field);
        if (!resource) return Result<ActionStateWindowBinding>::failure(resource.status());
        candidate.resource = std::move(resource).takeValue();
    }
    return Result<ActionStateWindowBinding>::success(std::move(candidate));
}

}  // namespace eve::action
