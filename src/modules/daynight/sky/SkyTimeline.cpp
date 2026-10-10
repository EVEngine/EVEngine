#include "daynight/sky/SkyTimeline.h"

#include <algorithm>
#include <cmath>

namespace eve::daynight {
namespace {
bool inRange(double value, double low, double high) { return std::isfinite(value) && value >= low && value <= high; }
bool validWeather(const SkyWeather& weather) {
    return inRange(weather.cloudCoverage, 0, 10) && inRange(weather.fog, 0, 1) && inRange(weather.rain, 0, 1) &&
           inRange(weather.snow, 0, 1) && inRange(weather.windXZ[0], -1000, 1000) &&
           inRange(weather.windXZ[1], -1000, 1000);
}
Diagnostic invalid(const char* message) {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, message, "sky.timeline");
}
}  // namespace

SkyTimeline::SkyTimeline(const SkyClock& clock, const SkyWeather& weather)
    : clock_(clock), from_(weather), to_(weather) {}

Result<SkyTimeline> SkyTimeline::create(const SkyClock& clock, const SkyWeather& weather) {
    if (!inRange(clock.initialHour, 0, 24) || clock.initialHour == 24 || !inRange(clock.hoursPerSecond, 0, 24) ||
        !validWeather(weather))
        return Result<SkyTimeline>::failure(invalid("Invalid initial sky clock or weather"));
    return Result<SkyTimeline>::success(SkyTimeline(clock, weather));
}

Result<void> SkyTimeline::advance(Duration duration) {
    if (duration.nanoseconds() < 0) return Result<void>::failure(invalid("Sky time step must be nonnegative"));
    auto next = elapsed_.tryAdd(duration);
    if (!next) return Result<void>::failure(next.status());
    elapsed_ = std::move(next).takeValue();
    return Result<void>::success();
}

Result<void> SkyTimeline::transitionTo(const SkyWeather& target, Duration duration) {
    if (!validWeather(target) || duration.nanoseconds() < 0)
        return Result<void>::failure(invalid("Invalid sky weather or transition duration"));
    const auto current  = frame();
    from_               = current.weather;
    to_                 = target;
    travelOrigin_       = current.cloudTravelXZ;
    transitionStart_    = elapsed_;
    transitionDuration_ = duration;
    return Result<void>::success();
}

SkyFrame SkyTimeline::frame() const noexcept {
    SkyFrame result;
    result.elapsedSeconds = elapsed_.seconds();
    // Reduce before scaling so long-running clocks retain sub-day precision.
    result.hour      = clock_.hoursPerSecond == 0
                           ? clock_.initialHour
                           : std::fmod(clock_.initialHour +
                                           std::fmod(elapsed_.seconds(), 24 / clock_.hoursPerSecond) * clock_.hoursPerSecond,
                                       24);
    const double age = Duration::fromNanoseconds(elapsed_.nanoseconds() - transitionStart_.nanoseconds()).seconds();
    const double duration        = transitionDuration_.seconds();
    const double ramp            = std::min(age, duration);
    const double fraction        = duration > 0 ? ramp / duration : 1;
    const auto   mix             = [fraction](double a, double b) { return std::lerp(a, b, fraction); };
    result.weather.cloudCoverage = mix(from_.cloudCoverage, to_.cloudCoverage);
    result.weather.fog           = mix(from_.fog, to_.fog);
    result.weather.rain          = mix(from_.rain, to_.rain);
    result.weather.snow          = mix(from_.snow, to_.snow);
    for (size_t axis = 0; axis < 2; ++axis) {
        result.weather.windXZ[axis] = mix(from_.windXZ[axis], to_.windXZ[axis]);
        // Integrate the linear ramp plus constant-speed tail analytically. A large
        // frame crossing the transition endpoint must equal many smaller frames.
        result.cloudTravelXZ[axis] = travelOrigin_[axis] +
                                     ramp * (from_.windXZ[axis] + result.weather.windXZ[axis]) * .5 +
                                     (age - ramp) * to_.windXZ[axis];
    }
    return result;
}
}  // namespace eve::daynight
