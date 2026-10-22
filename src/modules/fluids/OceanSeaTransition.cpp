#include "fluids/OceanSeaTransition.h"
#include <cmath>
#include "fluids/OceanSpectrumInternal.h"

namespace eve::fluids {
Result<OceanSeaTransition> OceanSeaTransitionBuilder::build() const {
    auto invalid = [] {
        return Result<OceanSeaTransition>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "Sea transition requires finite time, positive duration and compatible tile/current/gravity settings",
            "fluids.ocean.transition"));
    };
    if (!std::isfinite(start) || !std::isfinite(duration) || std::abs(start) > 1e9 || duration < 1.0 / 30.0 ||
        duration > 1e7 || std::abs(start + duration) > 1e9)
        return invalid();
    const auto& a = from_.data_->settings;
    const auto& b = to_.data_->settings;
    if (a.current != b.current || a.gravity != b.gravity || a.bands.size() != b.bands.size()) return invalid();
    for (std::size_t i = 0; i < a.bands.size(); ++i)
        if (a.bands[i].resolution != b.bands[i].resolution || a.bands[i].length != b.bands[i].length) return invalid();
    return Result<OceanSeaTransition>::success(OceanSeaTransition(from_, to_, start, duration));
}
OceanSeaTransitionFrameBuilder OceanSeaTransition::frameBuilder(double time) const {
    return {from_, to_, start_, duration_, time};
}
Result<OceanSpectrumFrame> OceanSeaTransitionFrameBuilder::build() const {
    if (!std::isfinite(time_) || std::abs(time_) > 1e9)
        return Result<OceanSpectrumFrame>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Invalid sea transition timestamp", "fluids.ocean.transition"));
    if (time_ <= start_) return from_.frameBuilder(time_).build();
    if (time_ >= start_ + duration_) return to_.frameBuilder(time_).build();
    auto first = from_.frameBuilder(time_).build();
    if (!first) return first;
    auto second = to_.frameBuilder(time_).build();
    if (!second) return second;
    auto         out           = std::move(first).takeValue();
    const auto&  target        = second.value();
    const double u             = (time_ - start_) / duration_;
    const double weight        = u * u * u * (10.0 + u * (-15.0 + 6.0 * u));
    const double rate          = 30.0 * u * u * (1.0 - u) * (1.0 - u) / duration_;
    const double levelVelocity = (target.seaLevel_ - out.seaLevel_) * rate;
    out.seaLevel_ += weight * (target.seaLevel_ - out.seaLevel_);
    for (std::size_t band = 0; band < out.grids_.size(); ++band) {
        auto&       grid  = out.grids_[band];
        const auto& other = target.grids_[band];
        for (std::size_t i = 0; i < grid.displacement.size(); ++i) {
            const auto delta = other.displacement[i] - grid.displacement[i];
            grid.velocity[i] += weight * (other.velocity[i] - grid.velocity[i]) + rate * delta;
            if (band == 0) grid.velocity[i].y += levelVelocity;
            grid.displacement[i] += weight * delta;
        }
    }
    return Result<OceanSpectrumFrame>::success(std::move(out));
}
}  // namespace eve::fluids
