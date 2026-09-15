#include "action/ActionPrefabBlock.h"

#include <cmath>
#include <utility>

namespace eve::action {
namespace {

template <typename T>
Result<T> invalid(std::string message, std::string path) {
    return Result<T>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

Result<std::string> requiredText(const Value::Object& payload, std::string_view key) {
    const auto found = payload.find(std::string(key));
    if (found == payload.end() || !found->second.getIf<std::string>())
        return invalid<std::string>("prefab field must be text", std::string(key));
    if (found->second.getIf<std::string>()->empty())
        return invalid<std::string>("prefab field must not be empty", std::string(key));
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
        return invalid<double>("prefab duration must be numeric", std::string(key));
    if (!std::isfinite(value))
        return invalid<double>("prefab duration must be finite", std::string(key));
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
        if (!value) return invalid<ActionPrefabSpawnBinding>("prefab lifecycle must be text", "lifecycle");
        lifecycle = *value;
    }
    if (lifecycle == "recycle_on_block_exit")
        candidate.lifecycle = PrefabSpawnLifecycle::RecycleOnBlockExit;
    else if (lifecycle == "custom_duration")
        candidate.lifecycle = PrefabSpawnLifecycle::CustomDuration;
    else if (lifecycle == "independent")
        candidate.lifecycle = PrefabSpawnLifecycle::Independent;
    else
        return invalid<ActionPrefabSpawnBinding>("unknown prefab lifecycle", "lifecycle");

    auto duration = optionalNumber(payload, "customDurationSeconds", 0.0);
    if (!duration) return Result<ActionPrefabSpawnBinding>::failure(duration.status());
    if (payload.contains("customDurationSeconds") && duration.value() <= 0.0)
        return invalid<ActionPrefabSpawnBinding>("custom prefab duration must be positive",
                                                 "customDurationSeconds");
    if (candidate.lifecycle == PrefabSpawnLifecycle::CustomDuration) {
        if (!payload.contains("customDurationSeconds"))
            return invalid<ActionPrefabSpawnBinding>("custom prefab duration must be positive",
                                                     "customDurationSeconds");
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
