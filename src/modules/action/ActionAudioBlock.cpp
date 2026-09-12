#include "action/ActionAudioBlock.h"

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
