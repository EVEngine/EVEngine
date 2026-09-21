#include "graphics/VegetationWind.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace eve::graphics {
namespace {
bool finite(glm::dvec3 value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
bool representable(glm::dvec3 value) {
    const double limit = std::numeric_limits<float>::max();
    return finite(value) && std::abs(value.x) <= limit && std::abs(value.y) <= limit && std::abs(value.z) <= limit;
}
bool valid(const VegetationWindState& state) {
    return finite(state.direction) && std::isfinite(state.strength) && state.strength >= 0 && state.strength <= 1.2F &&
           std::isfinite(state.phase) && state.phase >= 0 && std::isfinite(state.updatePhase) && state.updatePhase >= 0;
}
double saturate(double x) { return std::clamp(x, 0.0, 1.0); }
}  // namespace
Result<std::array<float, 14>> packVegetationWind(const VegetationWindState& state, const VegetationWindProfile& profile,
                                                 double seconds) {
    if (!valid(state) || !finite(profile.flex) || !finite(profile.frequency) ||
        !std::isfinite(profile.maximumDistance) || profile.maximumDistance <= 0 || !std::isfinite(seconds))
        return Result<std::array<float, 14>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "vegetation.wind: finite valid globals, dimensions and transform required"));
    const float distance =
        !profile.enabled ? 0 : (profile.billboard ? -profile.maximumDistance : profile.maximumDistance);
    return Result<std::array<float, 14>>::success(
        {state.direction.x, state.direction.y, state.direction.z, state.strength, state.phase, distance, profile.flex.x,
         profile.flex.y, profile.alphaTest ? profile.flex.z : 0, profile.frequency.x, profile.frequency.y,
         profile.frequency.z, float(std::sin(seconds * 0.25)), float(std::sin(seconds))});
}
Result<void> initializeVegetationWind(VegetationWindState& state, glm::vec3 forward, float strength) {
    if (!finite(forward) || !std::isfinite(strength))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "vegetation.wind: finite valid globals, dimensions and transform required"));
    const double     main = std::clamp(double(strength), 0.0, double(1.2F));
    const glm::dvec3 direction{
        forward.x, -std::max(double(forward.y), std::hypot(double(forward.x), double(forward.z)) * 0.5), forward.z};
    const double phase = std::pow(main * 0.5 + 0.5, 3) * 0.1;
    if (!representable(direction) || !std::isfinite(phase))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "vegetation.wind: finite valid globals, dimensions and transform required"));
    state = {glm::vec3(direction), float(main), float(phase), state.updatePhase};
    return Result<void>::success();
}
Result<void> advanceVegetationWind(VegetationWindState& state, glm::vec3 forward, float strength, float dt) {
    if (!valid(state) || !finite(forward) || !std::isfinite(strength) || !std::isfinite(dt) || dt < 0)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "vegetation.wind: finite valid globals, dimensions and transform required"));
    const double     alpha = saturate(double(dt) * 0.25);
    const glm::dvec3 target{
        forward.x, -std::max(double(forward.y), std::hypot(double(forward.x), double(forward.z)) * 0.5), forward.z};
    const auto   direction = glm::mix(glm::dvec3(state.direction), target, alpha);
    const double main =
        double(state.strength) + (std::clamp(double(strength), 0.0, double(1.2F)) - state.strength) * alpha;
    double phase = double(state.updatePhase) + double(dt) * std::pow(main * 0.5 + 0.5, 3) * 0.1;
    if (phase > 100) phase -= 100;
    if (!representable(direction) || !std::isfinite(phase) || phase > std::numeric_limits<float>::max())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "vegetation.wind: finite valid globals, dimensions and transform required"));
    state = {glm::vec3(direction), float(main), float(phase), float(phase)};
    return Result<void>::success();
}
Result<void> advanceVegetationWindAudio(VegetationWindAudioState& state, float windStrength, float transitionTime,
                                        float dt, bool enabled, bool clipAvailable) {
    if (!std::isfinite(state.currentWindSpeed) || !std::isfinite(state.anchorVolume) ||
        !std::isfinite(state.volume) || state.volume < 0 || !std::isfinite(windStrength) ||
        !std::isfinite(transitionTime) || transitionTime <= 0 || !std::isfinite(dt) || dt < 0)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "vegetation.wind: finite valid globals, dimensions and transform required"));
    if (!enabled) return Result<void>::success();
    auto next = state;
    if (windStrength != next.currentWindSpeed) {
        next.anchorVolume = next.volume;
        next.processing = true;
    }
    if (next.processing && clipAvailable) {
        next.playing = true;
        const double target = saturate(windStrength);
        const double alpha = saturate(double(dt) / transitionTime);
        next.volume = float(double(next.anchorVolume) + (target - next.anchorVolume) * alpha);
        if (next.anchorVolume >= target) {
            if (next.volume <= target - 0.05) next.volume = float(target), next.processing = false;
        } else if (next.volume >= target - 0.05) {
            next.volume = float(target);
            next.processing = false;
        }
    }
    state = next;
    return Result<void>::success();
}
Result<glm::vec3> evaluateVegetationWind(const VegetationWindInput& in, const VegetationWindState& state) {
    if (!valid(state) || !finite(in.position) || !finite(in.worldOffset) || !finite(in.cameraPosition) ||
        !finite(in.flex) || !finite(in.frequency) || !std::isfinite(in.widthHeight.x) || in.widthHeight.x <= 0 ||
        !std::isfinite(in.widthHeight.y) || in.widthHeight.y <= 0 || !std::isfinite(in.maximumDistance) ||
        in.maximumDistance <= 0 || !std::isfinite(in.sinTimeQuarter) || std::abs(in.sinTimeQuarter) > 1 ||
        !std::isfinite(in.sinTimeFull) || std::abs(in.sinTimeFull) > 1)
        return Result<glm::vec3>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "vegetation.wind: finite valid globals, dimensions and transform required"));
    glm::dmat3 matrix(in.objectToWorld);
    for (int c = 0; c < 3; ++c)
        if (!finite(matrix[c]))
            return Result<glm::vec3>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument,
                                  "vegetation.wind: finite valid globals, dimensions and transform required"));
    const double determinant = glm::determinant(matrix);
    if (!std::isfinite(determinant) || determinant == 0)
        return Result<glm::vec3>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "vegetation.wind: finite valid globals, dimensions and transform required"));
    if (!in.enabled) return Result<glm::vec3>::success(in.position);
    const auto distance    = (glm::dvec3(in.worldOffset) - glm::dvec3(in.cameraPosition)) / double(in.maximumDistance);
    double     attenuation = 1 - saturate(glm::dot(distance, distance));
    attenuation *= attenuation;
    if (attenuation == 0 || state.strength == 0) return Result<glm::vec3>::success(in.position);
    const double main = state.strength;
    glm::dvec3 flex = glm::dvec3(in.flex) * glm::dvec3(saturate(main * 3), saturate(main * 2), 1 - main * main * 0.5) *
                      main * attenuation;
    const auto   up     = matrix * glm::dvec3(0, 1, 0);
    const double length = glm::length(up), scale = std::max(length, 0.4), upDot = saturate(up.y / length);
    const auto   dimensions =
        glm::mix(glm::dvec2(double(in.widthHeight.y) * 2), glm::dvec2(in.widthHeight), upDot * upDot) * scale;
    flex *= scale;
    const auto   frequency = glm::dvec3(in.frequency) * scale;
    auto         local     = matrix * glm::dvec3(in.position);
    const auto   world     = local + glm::dvec3(in.worldOffset);
    const double phase = double(state.phase) * 6, time = -(phase - std::floor(phase)) * 6.283185;
    const auto   norm   = local / glm::dvec3(dimensions.x, dimensions.y, dimensions.x);
    const double branch = norm.x * norm.x + norm.z * norm.z, stem = norm.y, lengthA = glm::dot(local, local);
    double       gust =
        ((std::sin(time + frequency.x * (double(in.worldOffset.x) + in.worldOffset.y + in.worldOffset.z)) * 0.3 +
          main * 0.5) +
         (double(in.sinTimeQuarter) * 0.4 + main) * main) *
        (double(in.sinTimeFull) * 0.3 + 0.7);
    const auto direction = glm::dvec3(state.direction);
    glm::dvec3 tally{direction.x * stem * stem * gust * flex.x, 0, direction.z * stem * stem * gust * flex.x};
    gust = gust * 0.7 + 0.3;
    if (!in.billboard) {
        const auto displaced = world + tally * 0.25;
        tally += direction * stem * stem *
                 (std::sin(time * 2 + (displaced.x + displaced.y + displaced.z) * frequency.y) * branch * 0.7 + 0.3) *
                 gust * flex.y;
    }
    const auto   offset        = local + tally;
    const double denominator   = glm::dot(offset, offset);
    const double normalization = denominator == 0 ? 0 : saturate(lengthA / denominator);
    tally                      = offset * normalization;
    local                      = tally;
    if (!in.billboard && in.alphaTest) {
        const auto offsets = (world + tally) * frequency.z;
        glm::dvec3 wave{std::sin(time * 5 + offsets.x), std::sin(time * 5 + offsets.y), std::sin(time * 5 + offsets.z)};
        local += (wave * direction + direction) * glm::dvec3(branch, branch * 0.75, branch) * (stem * gust * flex.z) *
                 (normalization + 0.5);
    }
    const auto result = glm::inverse(matrix) * local;
    if (!representable(result))
        return Result<glm::vec3>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "vegetation.wind: finite valid globals, dimensions and transform required"));
    return Result<glm::vec3>::success(glm::vec3(result));
}
}  // namespace eve::graphics
