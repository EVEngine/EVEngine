// eve-hot-path
#include "graphics/multiscale/FarFlux.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <numbers>
#include <utility>
#include "graphics/multiscale/AngularProjectionInternal.h"

namespace eve::graphics::multiscale {
namespace {
Diagnostic failure(DiagnosticCode code, const char* message) {
    return Diagnostic::error(code, message, "graphics.multiscale.farFlux");
}
}

FarFluxFrame::FarFluxFrame() = default;
FarFluxFrame::~FarFluxFrame() = default;
FarFluxFrame::FarFluxFrame(FarFluxFrame&&) noexcept = default;
FarFluxFrame& FarFluxFrame::operator=(FarFluxFrame&&) noexcept = default;

Result<FarFluxFrame> FarFluxFrame::build(std::span<const FarEmitter> emitters, const spatial::CellPosition& camera,
                                        const AngularView& view, FarFluxPolicy policy) {
    if (policy.width == 0 || policy.height == 0 || policy.width > policy.maxPixels ||
        policy.height > policy.maxPixels / policy.width || emitters.size() > policy.maxEmitters ||
        !std::isfinite(policy.maxPositionErrorCells) || policy.maxPositionErrorCells <= 0)
        return Result<FarFluxFrame>::failure(failure(DiagnosticCode::InvalidArgument, "Invalid flux budget or dimensions"));
    auto prepared = detail::prepareAngularView(view);
    if (!prepared.ok()) return Result<FarFluxFrame>::failure(prepared.status());
    auto cameraCheck = spatial::projectCellRelative(
        camera, camera, {view.cellExponent, view.cellExponent, policy.maxPositionErrorCells});
    if (!cameraCheck.ok()) return Result<FarFluxFrame>::failure(cameraCheck.status());
    const auto basis = std::move(prepared).takeValue().basis;
    const double tangent[2]{view.tanHalfVerticalFov * view.aspect, view.tanHalfVerticalFov};
    if (!std::isfinite(tangent[0]) || tangent[0] <= 0)
        return Result<FarFluxFrame>::failure(failure(DiagnosticCode::Unsupported, "Flux perspective scale exceeds range"));
    try {
        FarFluxFrame frame;
        frame.width_  = policy.width;
        frame.height_ = policy.height;
        frame.pixels_.resize(policy.width * policy.height);
        constexpr double epsilon = std::numeric_limits<double>::epsilon();
        for (const auto& emitter : emitters) {
            if (!std::isfinite(emitter.powerMantissa) || emitter.powerMantissa <= 0 ||
                emitter.powerExponent < -3000 || emitter.powerExponent > 3000 ||
                !std::all_of(emitter.rgbWeights.begin(), emitter.rgbWeights.end(),
                             [](double weight) { return std::isfinite(weight) && weight >= 0; }))
                return Result<FarFluxFrame>::failure(failure(DiagnosticCode::InvalidArgument, "Invalid radiant source"));
            auto relative = spatial::projectCellRelative(
                emitter.position, camera,
                {view.cellExponent, view.cellExponent, policy.maxPositionErrorCells});
            if (!relative.ok()) return Result<FarFluxFrame>::failure(relative.status());
            const auto offset = std::move(relative).takeValue();
            std::array<double, 3> coordinate{}, error{}, low{}, high{};
            for (unsigned axis = 0; axis < 3; ++axis) {
                double magnitude = 0;
                for (unsigned sourceAxis = 0; sourceAxis < 3; ++sourceAxis) {
                    const double component = basis[axis][sourceAxis];
                    const double term      = component * offset.offset[sourceAxis];
                    coordinate[axis] += term;
                    magnitude += std::abs(term);
                    error[axis] += std::abs(component) * offset.absoluteErrorBound[sourceAxis];
                }
                error[axis] += 16 * epsilon * magnitude;
                low[axis] = std::nextafter(coordinate[axis] - error[axis], -INFINITY);
                high[axis] = std::nextafter(coordinate[axis] + error[axis], INFINITY);
            }
            if (high[2] <= 0) continue;
            if (low[2] <= 0 || !std::isfinite(high[2]))
                return Result<FarFluxFrame>::failure(failure(DiagnosticCode::Unsupported, "Emitter crosses camera plane"));
            std::array<double, 2> ndcLow{}, ndcHigh{};
            for (unsigned axis = 0; axis < 2; ++axis) {
                const double lo = axis == 0 ? low[0] : -high[1];
                const double hi = axis == 0 ? high[0] : -low[1];
                const std::array<double, 4> corners{lo / low[2] / tangent[axis], lo / high[2] / tangent[axis],
                                                    hi / low[2] / tangent[axis], hi / high[2] / tangent[axis]};
                ndcLow[axis]  = std::nextafter(*std::min_element(corners.begin(), corners.end()), -INFINITY);
                ndcHigh[axis] = std::nextafter(*std::max_element(corners.begin(), corners.end()), INFINITY);
            }
            if (ndcHigh[0] < -1 || ndcLow[0] >= 1 || ndcHigh[1] < -1 || ndcLow[1] >= 1) continue;
            if (!std::isfinite(ndcLow[0]) || !std::isfinite(ndcHigh[0]) || !std::isfinite(ndcLow[1]) ||
                !std::isfinite(ndcHigh[1]))
                return Result<FarFluxFrame>::failure(failure(DiagnosticCode::Unsupported, "Emitter direction exceeds numeric range"));
            if (ndcLow[0] < -1 || ndcHigh[0] >= 1 || ndcLow[1] < -1 || ndcHigh[1] >= 1)
                return Result<FarFluxFrame>::failure(failure(DiagnosticCode::Unsupported, "Emitter direction crosses viewport edge"));
            const auto x0 = static_cast<std::size_t>(std::floor((ndcLow[0] + 1) * 0.5 * policy.width));
            const auto x1 = static_cast<std::size_t>(std::floor((ndcHigh[0] + 1) * 0.5 * policy.width));
            const auto y0 = static_cast<std::size_t>(std::floor((ndcLow[1] + 1) * 0.5 * policy.height));
            const auto y1 = static_cast<std::size_t>(std::floor((ndcHigh[1] + 1) * 0.5 * policy.height));
            if (x1 > x0 + 1 || y1 > y0 + 1)
                return Result<FarFluxFrame>::failure(failure(DiagnosticCode::Unsupported, "Emitter direction spans too many pixels"));
            const double distance = std::hypot(offset.offset[0], offset.offset[1], offset.offset[2]);
            if (!std::isfinite(distance) || distance <= 0)
                return Result<FarFluxFrame>::failure(failure(DiagnosticCode::Unsupported, "Emitter distance is singular"));
            int          distanceExponent = 0;
            const double distanceMantissa = std::frexp(distance, &distanceExponent);
            const int    fluxExponent = emitter.powerExponent - 2 * (distanceExponent + view.cellExponent);
            const double flux = std::ldexp(emitter.powerMantissa /
                                               (4 * std::numbers::pi_v<double> * distanceMantissa * distanceMantissa),
                                           fluxExponent);
            if (!std::isfinite(flux) || flux == 0)
                return Result<FarFluxFrame>::failure(failure(DiagnosticCode::Unsupported, "Emitter flux exceeds numeric range"));
            const double share = flux / static_cast<double>((x1 - x0 + 1) * (y1 - y0 + 1));
            if (share == 0)
                return Result<FarFluxFrame>::failure(failure(DiagnosticCode::Unsupported, "Per-pixel flux underflows"));
            for (std::size_t y = y0; y <= y1; ++y)
                for (std::size_t x = x0; x <= x1; ++x)
                    for (unsigned channel = 0; channel < 3; ++channel) {
                        const double contribution = share * emitter.rgbWeights[channel];
                        if (emitter.rgbWeights[channel] > 0 && contribution == 0)
                            return Result<FarFluxFrame>::failure(
                                failure(DiagnosticCode::Unsupported, "Spectral flux underflows"));
                        auto& pixel = frame.pixels_[y * policy.width + x][channel];
                        pixel += contribution;
                        if (!std::isfinite(pixel))
                            return Result<FarFluxFrame>::failure(
                                failure(DiagnosticCode::Unsupported, "Accumulated flux exceeds numeric range"));
                    }
        }
        return Result<FarFluxFrame>::success(std::move(frame));
    } catch (const std::bad_alloc&) {
        return Result<FarFluxFrame>::failure(failure(DiagnosticCode::Failed, "Flux frame allocation failed"));
    }
}
}  // namespace eve::graphics::multiscale
