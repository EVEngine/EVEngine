#include "action/ActionVfxBlock.h"

#include <cmath>
#include <string_view>
#include <utility>

namespace eve::action {
namespace {

template <typename T>
Result<T> invalid(std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

Result<double> numeric(const Value::Object& payload, std::string_view key, double fallback) {
    const auto found = payload.find(std::string(key));
    if (found == payload.end()) return Result<double>::success(fallback);
    double value = 0.0;
    if (const auto* integer = found->second.getIf<std::int64_t>())
        value = static_cast<double>(*integer);
    else if (const auto* decimal = found->second.getIf<double>())
        value = *decimal;
    else
        return invalid<double>("VFX field must be numeric", std::string(key));
    if (!std::isfinite(value)) return invalid<double>("VFX field must be finite", std::string(key));
    return Result<double>::success(value);
}

}  // namespace

Result<ActionVfxBinding> ActionVfxBinding::fromPayload(const Value::Object& payload, ActionVfxShape shape) {
    ActionVfxBinding candidate;
    const auto       uri = payload.find("uri");
    if (uri == payload.end() || !uri->second.getIf<std::string>() || uri->second.getIf<std::string>()->empty())
        return invalid<ActionVfxBinding>("VFX URI must be non-empty text", "uri");
    candidate.uri = *uri->second.getIf<std::string>();

    auto spatial = ActionSpatialBinding::fromPayload(payload);
    if (!spatial) return Result<ActionVfxBinding>::failure(spatial.status());
    candidate.spatial = std::move(spatial).takeValue();

    if (const auto stop = payload.find("stopBehavior"); stop != payload.end()) {
        const auto* value = stop->second.getIf<std::string>();
        if (!value)
            return invalid<ActionVfxBinding>("VFX stopBehavior must be text", "stopBehavior");
        if (*value == "stop_emitting")
            candidate.stopBehavior = ActionVfxStopBehavior::StopEmitting;
        else if (*value == "clear_immediately")
            candidate.stopBehavior = ActionVfxStopBehavior::ClearImmediately;
        else
            return invalid<ActionVfxBinding>(
                "VFX stopBehavior must be stop_emitting or clear_immediately", "stopBehavior");
    }
    if (const auto synced = payload.find("playbackRateSynced"); synced != payload.end()) {
        const auto* value = synced->second.getIf<bool>();
        if (!value)
            return invalid<ActionVfxBinding>("VFX playbackRateSynced must be boolean", "playbackRateSynced");
        candidate.playbackRateSynced = *value;
    }

    auto clipStart = numeric(payload, "clipStartTime", 0.0);
    if (!clipStart) return Result<ActionVfxBinding>::failure(clipStart.status());
    auto clipEnd = numeric(payload, "clipEndTime", 0.5);
    if (!clipEnd) return Result<ActionVfxBinding>::failure(clipEnd.status());
    if (clipStart.value() < 0.0)
        return invalid<ActionVfxBinding>("VFX clipStartTime must be non-negative", "clipStartTime");
    if (clipEnd.value() <= clipStart.value())
        return invalid<ActionVfxBinding>("VFX clipEndTime must be greater than clipStartTime", "clipEndTime");
    candidate.clipStartTime = clipStart.value();
    candidate.clipEndTime   = clipEnd.value();

    auto lifetime = numeric(payload, "lifetimeSeconds", 0.0);
    if (!lifetime) return Result<ActionVfxBinding>::failure(lifetime.status());
    if (shape == ActionVfxShape::Instant && lifetime.value() <= 0.0)
        return invalid<ActionVfxBinding>(
            "instant VFX lifetimeSeconds must be positive", "lifetimeSeconds");
    if (shape == ActionVfxShape::State && lifetime.value() < 0.0)
        return invalid<ActionVfxBinding>("VFX lifetimeSeconds must be non-negative", "lifetimeSeconds");
    candidate.lifetimeSeconds = lifetime.value();
    return Result<ActionVfxBinding>::success(std::move(candidate));
}

}  // namespace eve::action
