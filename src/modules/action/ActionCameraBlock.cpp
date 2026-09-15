#include "action/ActionCameraBlock.h"

#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace eve::action {
namespace {

template <typename T>
Result<T> invalid(std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

Result<double> optionalNumber(const Value::Object& payload, std::string_view field, double fallback) {
    const auto found = payload.find(std::string(field));
    if (found == payload.end()) return Result<double>::success(fallback);
    double value = 0.0;
    if (const auto* integer = found->second.getIf<std::int64_t>())
        value = static_cast<double>(*integer);
    else if (const auto* decimal = found->second.getIf<double>())
        value = *decimal;
    else
        return invalid<double>("camera cue field must be numeric", std::string(field));
    if (!std::isfinite(value)) return invalid<double>("camera cue field must be finite", std::string(field));
    return Result<double>::success(value);
}

}  // namespace

Result<ActionCameraCueBinding> ActionCameraCueBinding::fromPayload(const Value::Object& payload) {
    ActionCameraCueBinding candidate;
    const auto foundCue = payload.find("cue");
    const auto* cue = foundCue == payload.end() ? nullptr : foundCue->second.getIf<std::string>();
    if (!cue) return invalid<ActionCameraCueBinding>("camera cue must be text", "cue");
    auto parsedCue = LogicalId::parse(*cue);
    if (!parsedCue) return invalid<ActionCameraCueBinding>("camera cue must be a LogicalId", "cue");
    candidate.cue = std::move(*parsedCue);

    auto position = optionalNumber(payload, "positionAmplitude", candidate.positionAmplitude);
    if (!position) return Result<ActionCameraCueBinding>::failure(position.status());
    auto rotation = optionalNumber(payload, "rotationAmplitude", candidate.rotationAmplitude);
    if (!rotation) return Result<ActionCameraCueBinding>::failure(rotation.status());
    auto fov = optionalNumber(payload, "fovAmplitude", candidate.fovAmplitude);
    if (!fov) return Result<ActionCameraCueBinding>::failure(fov.status());
    auto duration = optionalNumber(payload, "durationSeconds", candidate.duration.seconds());
    if (!duration) return Result<ActionCameraCueBinding>::failure(duration.status());
    if (position.value() < 0.0)
        return invalid<ActionCameraCueBinding>("camera position amplitude must be non-negative", "positionAmplitude");
    if (rotation.value() < 0.0)
        return invalid<ActionCameraCueBinding>("camera rotation amplitude must be non-negative", "rotationAmplitude");
    const double maximumFloat = static_cast<double>(std::numeric_limits<float>::max());
    if (position.value() > maximumFloat)
        return invalid<ActionCameraCueBinding>("camera position amplitude exceeds runtime range", "positionAmplitude");
    if (rotation.value() > maximumFloat)
        return invalid<ActionCameraCueBinding>("camera rotation amplitude exceeds runtime range", "rotationAmplitude");
    if (std::abs(fov.value()) > maximumFloat)
        return invalid<ActionCameraCueBinding>("camera FOV amplitude exceeds runtime range", "fovAmplitude");
    if (duration.value() <= 0.0)
        return invalid<ActionCameraCueBinding>("camera cue duration must be positive", "durationSeconds");
    if (duration.value() > maximumFloat)
        return invalid<ActionCameraCueBinding>("camera cue duration exceeds runtime range", "durationSeconds");
    auto parsedDuration = Duration::fromSeconds(duration.value());
    if (!parsedDuration) return Result<ActionCameraCueBinding>::failure(parsedDuration.status());
    candidate.positionAmplitude = position.value();
    candidate.rotationAmplitude = rotation.value();
    candidate.fovAmplitude = fov.value();
    candidate.duration = std::move(parsedDuration).takeValue();

    if (const auto foundSeed = payload.find("seed"); foundSeed != payload.end()) {
        const auto* seed = foundSeed->second.getIf<std::int64_t>();
        if (!seed || *seed < 0 || static_cast<std::uint64_t>(*seed) > std::numeric_limits<std::uint32_t>::max())
            return invalid<ActionCameraCueBinding>("camera cue seed must be an unsigned 32-bit integer", "seed");
        candidate.seed = static_cast<std::uint32_t>(*seed);
    }
    return Result<ActionCameraCueBinding>::success(std::move(candidate));
}

}  // namespace eve::action
