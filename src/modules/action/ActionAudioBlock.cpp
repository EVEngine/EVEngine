#include "action/ActionAudioBlock.h"

#include <cmath>
#include <set>
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
        return invalid<double>("audio field must be numeric", std::string(key));
    if (!std::isfinite(value)) return invalid<double>("audio field must be finite", std::string(key));
    return Result<double>::success(value);
}

}  // namespace

Result<ActionAudioBinding> ActionAudioBinding::fromPayload(const Value::Object& payload, ActionAudioShape shape) {
    ActionAudioBinding candidate;
    const auto         uri = payload.find("uri");
    if (uri == payload.end() || !uri->second.getIf<std::string>() || uri->second.getIf<std::string>()->empty())
        return invalid<ActionAudioBinding>("audio URI must be non-empty text", "uri");
    candidate.uri = *uri->second.getIf<std::string>();

    if (const auto randomUris = payload.find("randomUris"); randomUris != payload.end()) {
        const auto* values = randomUris->second.getIf<Value::Array>();
        if (!values) return invalid<ActionAudioBinding>("random audio URIs must be an array", "randomUris");
        std::set<std::string> unique;
        for (std::size_t index = 0; index < values->size(); ++index) {
            const auto* value = (*values)[index].getIf<std::string>();
            const auto path = "randomUris[" + std::to_string(index) + "]";
            if (!value || value->empty())
                return invalid<ActionAudioBinding>("random audio URI must be non-empty text", path);
            if (!unique.insert(*value).second)
                return invalid<ActionAudioBinding>("random audio URI must be unique", path);
            candidate.randomUris.push_back(*value);
        }
    }

    auto spatial = ActionSpatialBinding::fromPayload(payload);
    if (!spatial) return Result<ActionAudioBinding>::failure(spatial.status());
    candidate.spatial = std::move(spatial).takeValue();

    auto volume = numeric(payload, "volume", 1.0);
    if (!volume) return Result<ActionAudioBinding>::failure(volume.status());
    if (volume.value() < 0.0) return invalid<ActionAudioBinding>("audio volume must be non-negative", "volume");
    candidate.volume = volume.value();

    auto pitch = numeric(payload, "pitch", 1.0);
    if (!pitch) return Result<ActionAudioBinding>::failure(pitch.status());
    if (pitch.value() <= 0.0) return invalid<ActionAudioBinding>("audio pitch must be positive", "pitch");
    candidate.pitch = pitch.value();

    auto randomPitchOffset = numeric(payload, "randomPitchOffset", 0.0);
    if (!randomPitchOffset) return Result<ActionAudioBinding>::failure(randomPitchOffset.status());
    if (randomPitchOffset.value() < 0.0 || randomPitchOffset.value() > 0.5)
        return invalid<ActionAudioBinding>("random pitch offset must be between zero and 0.5",
                                           "randomPitchOffset");
    candidate.randomPitchOffset = randomPitchOffset.value();

    auto spatialBlend = numeric(payload, "spatialBlend", 1.0);
    if (!spatialBlend) return Result<ActionAudioBinding>::failure(spatialBlend.status());
    if (spatialBlend.value() < 0.0 || spatialBlend.value() > 1.0)
        return invalid<ActionAudioBinding>("audio spatial blend must be between zero and one", "spatialBlend");
    candidate.spatialBlend = spatialBlend.value();

    auto minDistance = numeric(payload, "minDistance", 1.0);
    if (!minDistance) return Result<ActionAudioBinding>::failure(minDistance.status());
    if (minDistance.value() <= 0.0)
        return invalid<ActionAudioBinding>("audio minimum distance must be positive", "minDistance");
    candidate.minDistance = minDistance.value();

    auto maxDistance = numeric(payload, "maxDistance", 35.0);
    if (!maxDistance) return Result<ActionAudioBinding>::failure(maxDistance.status());
    if (maxDistance.value() < candidate.minDistance)
        return invalid<ActionAudioBinding>("audio maximum distance must not be below minimum distance",
                                           "maxDistance");
    candidate.maxDistance = maxDistance.value();

    if (const auto looping = payload.find("looping"); looping != payload.end()) {
        const auto* enabled = looping->second.getIf<bool>();
        if (!enabled) return invalid<ActionAudioBinding>("audio looping must be boolean", "looping");
        candidate.looping = *enabled;
    }
    if (shape == ActionAudioShape::Instant && candidate.looping)
        return invalid<ActionAudioBinding>("instant audio cannot loop; use audio-state", "looping");
    return Result<ActionAudioBinding>::success(std::move(candidate));
}

}  // namespace eve::action
