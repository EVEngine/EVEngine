#include "daynight/sky/SkyRuntime.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include "daynight/sky/SkySolarOrbit.h"
#include "graphics/Graphics.h"
#include "graphics/sky/SkyAtmospherePass.h"
#include "graphics/sky/SkyWispsLayer.h"

namespace eve::daynight {
namespace {
using Light = graphics::SkyCelestialLight;
Result<Light> solarLight(const SkySolarOrbit& orbit, double hour, const std::array<float, 3>& base) {
    auto direction = orbit.direction(hour);
    if (!direction) return Result<Light>::failure(direction.status());
    // UDS Current Sun Light Intensity / Using Sky Atmosphere branch:
    // cached light-forward Z [0.157,0.113] -> [0,base]. Our Y points towards the sun.
    const double fraction = std::clamp((direction.value()[1] + .157) / (.157 - .113), 0.0, 1.0);
    Light        light;
    for (int axis = 0; axis < 3; ++axis) {
        light.direction[axis]  = static_cast<float>(direction.value()[axis]);
        light.irradiance[axis] = static_cast<float>(base[axis] * fraction);
    }
    return Result<Light>::success(light);
}
float      dayPhase(double hour) { return std::min(static_cast<float>(hour / 24.0), std::nextafter(1.f, 0.f)); }
Diagnostic retired() {
    return Diagnostic::error(DiagnosticCode::StaleHandle, "Sky graphics provider retired", "sky.runtime");
}
}  // namespace
Result<void> SkyCloudMotion::validate() const {
    const auto multiplier = [](double value) { return std::isfinite(value) && value >= 0 && value <= 1000; };
    if (!std::isfinite(phase) || std::abs(phase) > 1e6 || !multiplier(speed) || !multiplier(timeScale) ||
        !multiplier(windMultiplier))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid cloud motion controls", "sky.motion"));
    return Result<void>::success();
}
struct SkyRuntime::Impl {
    SkyTimeline                      timeline;
    SkySolarOrbit                    orbit;
    std::array<float, 3>             base;
    Light                            light;
    SkyCloudMotion                   motion;
    double                           sourceTime = 0, morphRate = 0, morphPeriod = 1, cloudTime = 0;
    float                            wispsMorphPhase = 0;
    Result<std::pair<double, float>> cloudSample(double seconds) const {
        const double time =
            sourceTime + motion.phase + seconds * motion.timeScale * motion.speed * motion.windMultiplier;
        if (!std::isfinite(time))
            return Result<std::pair<double, float>>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Cloud time overflow", "sky.motion"));
        double phase = std::fmod(time * morphRate / morphPeriod, 1.0);
        if (phase < 0) phase += 1;
        float gpuPhase = static_cast<float>(phase);
        if (gpuPhase >= 1) gpuPhase = 0;
        return Result<std::pair<double, float>>::success({time, gpuPhase});
    }
    std::weak_ptr<const void>                    lifetime;
    std::unique_ptr<graphics::SkyAtmospherePass> atmosphere;
    Impl(SkyTimeline time, SkySolarOrbit solar, std::array<float, 3> irradiance)
        : timeline(std::move(time)), orbit(std::move(solar)), base(irradiance) {}
};
SkyRuntime::SkyRuntime()  = default;
SkyRuntime::~SkyRuntime() = default;

Result<std::unique_ptr<SkyRuntime>> SkyRuntime::prepare(graphics::Graphics&                      graphics,
                                                        const SkyRuntimeSettings&                settings,
                                                        const graphics::SkyAtmosphereParameters& atmosphere,
                                                        const graphics::SkyWispsLayer*           wisps) {
    using Prepared   = Result<std::unique_ptr<SkyRuntime>>;
    auto motionValid = settings.cloudMotion.validate();
    if (!motionValid) return Prepared::failure(motionValid.status());
    auto timeline = SkyTimeline::create(settings.clock, settings.weather);
    if (!timeline) return Prepared::failure(timeline.status());
    auto orbit = SkySolarOrbit::create(settings.sunPitchDegrees, settings.sunYawDegrees);
    if (!orbit) return Prepared::failure(orbit.status());
    for (const float value : settings.sunIrradiance)
        if (!std::isfinite(value) || value < 0)
            return Prepared::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "Solar irradiance must be finite and nonnegative", "sky.runtime"));
    auto light = solarLight(orbit.value(), timeline.value().frame().hour, settings.sunIrradiance);
    if (!light) return Prepared::failure(light.status());
    auto pass = graphics::SkyAtmospherePass::prepare(graphics, atmosphere, wisps);
    if (!pass) return Prepared::failure(pass.status());
    auto result = std::unique_ptr<SkyRuntime>(new SkyRuntime());
    result->impl_ =
        std::make_unique<Impl>(std::move(timeline).takeValue(), std::move(orbit).takeValue(), settings.sunIrradiance);
    result->impl_->motion = settings.cloudMotion;
    if (wisps) {
        result->impl_->sourceTime  = wisps->cloudTime;
        result->impl_->morphRate   = wisps->morphRate;
        result->impl_->morphPeriod = wisps->morphPeriod;
    }
    auto cloud = result->impl_->cloudSample(0);
    if (!cloud) return Prepared::failure(cloud.status());
    auto applied =
        pass.value()->setFrame({light.value(), cloud.value().second, dayPhase(result->impl_->timeline.frame().hour)});
    if (!applied) return Prepared::failure(applied.status());
    result->impl_->cloudTime       = cloud.value().first;
    result->impl_->wispsMorphPhase = cloud.value().second;
    result->impl_->light           = light.value();
    result->impl_->lifetime        = graphics.resourceLifetime();
    result->impl_->atmosphere      = std::move(pass).takeValue();
    return Prepared::success(std::move(result));
}

Result<void> SkyRuntime::advance(Duration duration) {
    if (impl_->lifetime.expired()) return Result<void>::failure(retired());
    auto candidate = impl_->timeline;
    auto advanced  = candidate.advance(duration);
    if (!advanced) return advanced;
    auto light = solarLight(impl_->orbit, candidate.frame().hour, impl_->base);
    if (!light) return Result<void>::failure(light.status());
    auto cloud = impl_->cloudSample(candidate.frame().elapsedSeconds);
    if (!cloud) return Result<void>::failure(cloud.status());
    auto applied = impl_->atmosphere->setFrame({light.value(), cloud.value().second, dayPhase(candidate.frame().hour)});
    if (!applied) return applied;
    impl_->timeline        = std::move(candidate);
    impl_->light           = light.value();
    impl_->cloudTime       = cloud.value().first;
    impl_->wispsMorphPhase = cloud.value().second;
    return Result<void>::success();
}
Result<void> SkyRuntime::transitionTo(const SkyWeather& target, Duration duration) {
    if (impl_->lifetime.expired()) return Result<void>::failure(retired());
    return impl_->timeline.transitionTo(target, duration);
}
SkyRuntimeFrame SkyRuntime::frame() const noexcept {
    SkyRuntimeFrame result;
    result.simulation      = impl_->timeline.frame();
    result.cloudTime       = impl_->cloudTime;
    result.wispsMorphPhase = impl_->wispsMorphPhase;
    for (int axis = 0; axis < 3; ++axis) {
        result.sunDirection[axis]  = impl_->light.direction[axis];
        result.sunIrradiance[axis] = impl_->light.irradiance[axis];
    }
    return result;
}
Result<void> SkyRuntime::attach() {
    if (impl_->lifetime.expired()) return Result<void>::failure(retired());
    return impl_->atmosphere->attach();
}
void SkyRuntime::detach() noexcept { impl_->atmosphere->detach(); }
}  // namespace eve::daynight
