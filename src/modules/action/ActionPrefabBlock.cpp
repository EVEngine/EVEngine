#include "action/ActionPrefabBlock.h"
#include <cmath>
#include <string_view>
#include <utility>
#include "action/ActionVfxBlock.h"

// ActionPrefabBlock.cpp
namespace eve::action {
namespace {

Result<std::string> requiredText(const Value::Object& payload, std::string_view key) {
    const auto found = payload.find(std::string(key));
    if (found == payload.end() || !found->second.getIf<std::string>())
        return Result<std::string>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "prefab field must be text", std::string(key)));
    if (found->second.getIf<std::string>()->empty())
        return Result<std::string>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "prefab field must not be empty", std::string(key)));
    return Result<std::string>::success(*found->second.getIf<std::string>());
}

Result<double> optionalNumber(const Value::Object& payload, std::string_view key, double fallback) {
    const auto found = payload.find(std::string(key));
    if (found == payload.end()) return Result<double>::success(fallback);
    double value = 0.0;
    if (const auto* decimal = found->second.getIf<double>())
        value = *decimal;
    else if (const auto* integer = found->second.getIf<std::int64_t>())
        value = static_cast<double>(*integer);
    else
        return Result<double>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "prefab duration must be numeric", std::string(key)));
    if (!std::isfinite(value))
        return Result<double>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "prefab duration must be finite", std::string(key)));
    return Result<double>::success(value);
}

}  // namespace

std::string_view prefabSpawnLifecycleName(PrefabSpawnLifecycle lifecycle) noexcept {
    switch (lifecycle) {
        case PrefabSpawnLifecycle::RecycleOnBlockExit: return "recycle_on_block_exit";
        case PrefabSpawnLifecycle::CustomDuration: return "custom_duration";
        case PrefabSpawnLifecycle::Independent: return "independent";
    }
    return "recycle_on_block_exit";
}

Result<ActionPrefabSpawnBinding> ActionPrefabSpawnBinding::fromPayload(const Value::Object& payload) {
    ActionPrefabSpawnBinding candidate;
    auto                     uri = requiredText(payload, "uri");
    if (!uri) return Result<ActionPrefabSpawnBinding>::failure(uri.status());
    candidate.uri = std::move(uri).takeValue();

    std::string lifecycle = "recycle_on_block_exit";
    if (const auto found = payload.find("lifecycle"); found != payload.end()) {
        const auto* value = found->second.getIf<std::string>();
        if (!value)
            return Result<ActionPrefabSpawnBinding>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "prefab lifecycle must be text", "lifecycle"));
        lifecycle = *value;
    }
    if (lifecycle == "recycle_on_block_exit")
        candidate.lifecycle = PrefabSpawnLifecycle::RecycleOnBlockExit;
    else if (lifecycle == "custom_duration")
        candidate.lifecycle = PrefabSpawnLifecycle::CustomDuration;
    else if (lifecycle == "independent")
        candidate.lifecycle = PrefabSpawnLifecycle::Independent;
    else
        return Result<ActionPrefabSpawnBinding>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "unknown prefab lifecycle", "lifecycle"));

    auto duration = optionalNumber(payload, "customDurationSeconds", 0.0);
    if (!duration) return Result<ActionPrefabSpawnBinding>::failure(duration.status());
    if (payload.contains("customDurationSeconds") && duration.value() <= 0.0)
        return Result<ActionPrefabSpawnBinding>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "custom prefab duration must be positive", "customDurationSeconds"));
    if (candidate.lifecycle == PrefabSpawnLifecycle::CustomDuration) {
        if (!payload.contains("customDurationSeconds"))
            return Result<ActionPrefabSpawnBinding>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "custom prefab duration must be positive", "customDurationSeconds"));
        auto converted = Duration::fromSeconds(duration.value());
        if (!converted) return Result<ActionPrefabSpawnBinding>::failure(converted.status());
        candidate.customDuration = std::move(converted).takeValue();
    }

    auto spatial = ActionSpatialBinding::fromPayload(payload);
    if (!spatial) return Result<ActionPrefabSpawnBinding>::failure(spatial.status());
    candidate.spatial = std::move(spatial).takeValue();
    return Result<ActionPrefabSpawnBinding>::success(std::move(candidate));
}

}  // namespace eve::action

// ActionVfxBlock.cpp
namespace eve::action {
namespace {

Result<double> numeric(const Value::Object& payload, std::string_view key, double fallback) {
    const auto found = payload.find(std::string(key));
    if (found == payload.end()) return Result<double>::success(fallback);
    double value = 0.0;
    if (const auto* integer = found->second.getIf<std::int64_t>())
        value = static_cast<double>(*integer);
    else if (const auto* decimal = found->second.getIf<double>())
        value = *decimal;
    else
        return Result<double>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "VFX field must be numeric", std::string(key)));
    if (!std::isfinite(value))
        return Result<double>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "VFX field must be finite", std::string(key)));
    return Result<double>::success(value);
}

}  // namespace

Result<ActionVfxBinding> ActionVfxBinding::fromPayload(const Value::Object& payload, ActionVfxShape shape) {
    ActionVfxBinding candidate;
    const auto       uri = payload.find("uri");
    if (uri == payload.end() || !uri->second.getIf<std::string>() || uri->second.getIf<std::string>()->empty())
        return Result<ActionVfxBinding>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "VFX URI must be non-empty text", "uri"));
    candidate.uri = *uri->second.getIf<std::string>();

    auto spatial = ActionSpatialBinding::fromPayload(payload);
    if (!spatial) return Result<ActionVfxBinding>::failure(spatial.status());
    candidate.spatial = std::move(spatial).takeValue();

    if (const auto stop = payload.find("stopBehavior"); stop != payload.end()) {
        const auto* value = stop->second.getIf<std::string>();
        if (!value)
            return Result<ActionVfxBinding>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "VFX stopBehavior must be text", "stopBehavior"));
        if (*value == "stop_emitting")
            candidate.stopBehavior = ActionVfxStopBehavior::StopEmitting;
        else if (*value == "clear_immediately")
            candidate.stopBehavior = ActionVfxStopBehavior::ClearImmediately;
        else
            return Result<ActionVfxBinding>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument,
                                  "VFX stopBehavior must be stop_emitting or clear_immediately", "stopBehavior"));
    }
    if (const auto synced = payload.find("playbackRateSynced"); synced != payload.end()) {
        const auto* value = synced->second.getIf<bool>();
        if (!value)
            return Result<ActionVfxBinding>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "VFX playbackRateSynced must be boolean", "playbackRateSynced"));
        candidate.playbackRateSynced = *value;
    }

    auto clipStart = numeric(payload, "clipStartTime", 0.0);
    if (!clipStart) return Result<ActionVfxBinding>::failure(clipStart.status());
    auto clipEnd = numeric(payload, "clipEndTime", 0.5);
    if (!clipEnd) return Result<ActionVfxBinding>::failure(clipEnd.status());
    if (clipStart.value() < 0.0)
        return Result<ActionVfxBinding>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "VFX clipStartTime must be non-negative", "clipStartTime"));
    if (clipEnd.value() <= clipStart.value())
        return Result<ActionVfxBinding>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "VFX clipEndTime must be greater than clipStartTime", "clipEndTime"));
    candidate.clipStartTime = clipStart.value();
    candidate.clipEndTime   = clipEnd.value();

    auto lifetime = numeric(payload, "lifetimeSeconds", 0.0);
    if (!lifetime) return Result<ActionVfxBinding>::failure(lifetime.status());
    if (shape == ActionVfxShape::Instant && lifetime.value() <= 0.0)
        return Result<ActionVfxBinding>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "instant VFX lifetimeSeconds must be positive", "lifetimeSeconds"));
    if (shape == ActionVfxShape::State && lifetime.value() < 0.0)
        return Result<ActionVfxBinding>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "VFX lifetimeSeconds must be non-negative", "lifetimeSeconds"));
    candidate.lifetimeSeconds = lifetime.value();
    return Result<ActionVfxBinding>::success(std::move(candidate));
}

}  // namespace eve::action
