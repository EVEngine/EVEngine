#include <algorithm>
#include <cmath>
#include "map/Pathfinder.h"
#include "rts/RTSCrowdSurfaceInternal.h"
#include "rts/RTSSystems.h"
namespace eve::rts {
Result<crowd::NavigationSurfaceSnapshot> projectCrowdSurface(const map::NavigationGridSnapshot& source,
                                                             crowd::NavigationSurfaceStamp      stamp,
                                                             const NavigationGrid&              geometry,
                                                             std::int64_t                       maxChecks) try {
    const auto fail = [](DiagnosticCode code, const char* message) {
        return Result<crowd::NavigationSurfaceSnapshot>::failure(
            Diagnostic::error(code, message, "crowdSurfaceAdapter"));
    };
    if (!source.valid() || source.topology() == map::NavigationTopology::Hex)
        return fail(DiagnosticCode::PreconditionViolation, "Crowd adapter requires a captured orthogonal map grid");
    if (!std::isfinite(geometry.cellSize) || geometry.cellSize <= 0.f || !std::isfinite(geometry.originX) ||
        !std::isfinite(geometry.originY) || !std::isfinite(stamp.clearanceRadius) || stamp.clearanceRadius < 0.f ||
        maxChecks < 0)
        return fail(DiagnosticCode::InvalidArgument, "Invalid adapter geometry or work budget");
    crowd::NavigationSurfaceSnapshot result;
    result.stamp                = stamp;
    result.width                = source.width();
    result.height               = source.height();
    result.cellSize             = geometry.cellSize;
    result.originX              = geometry.originX - geometry.cellSize * 0.5f;
    result.originY              = geometry.originY - geometry.cellSize * 0.5f;
    result.diagonal             = source.topology() == map::NavigationTopology::Ortho8;
    result.destinationEntryCost = true;
    result.terrainCosts.assign(source.costs().begin(), source.costs().end());
    for (std::size_t index = 0; index < result.terrainCosts.size(); ++index) {
        auto& cost = result.terrainCosts[index];
        if (cost <= 0.f)
            cost = 0.f;
        else if (cost < 1.f || (source.enterMasks()[index] & 15) != 15 || (source.exitMasks()[index] & 15) != 15)
            return fail(DiagnosticCode::PreconditionViolation,
                        "Adapter does not support fractional costs or directional gates");
    }
    result.navigationCosts = result.terrainCosts;
    const double radius    = double(stamp.clearanceRadius) / geometry.cellSize;
    std::int64_t checks    = 0;
    for (int y = 0; y < result.height; ++y)
        for (int x = 0; x < result.width; ++x) {
            const auto index = static_cast<std::size_t>(y * result.width + x);
            if (result.navigationCosts[index] == 0.f || radius == 0.0) continue;
            const double cx = x + 0.5, cy = y + 0.5;
            if (cx < radius || cy < radius || result.width - cx < radius || result.height - cy < radius) {
                result.navigationCosts[index] = 0.f;
                continue;
            }
            const int x0      = int(std::clamp(std::floor(cx - radius), 0.0, double(result.width - 1)));
            const int x1      = int(std::clamp(std::floor(cx + radius), 0.0, double(result.width - 1)));
            const int y0      = int(std::clamp(std::floor(cy - radius), 0.0, double(result.height - 1)));
            const int y1      = int(std::clamp(std::floor(cy + radius), 0.0, double(result.height - 1)));
            bool      blocked = false;
            for (int wy = y0; wy <= y1 && !blocked; ++wy)
                for (int wx = x0; wx <= x1; ++wx) {
                    if (checks == maxChecks)
                        return fail(DiagnosticCode::PreconditionViolation, "Clearance work budget exhausted");
                    ++checks;
                    if (result.terrainCosts[static_cast<std::size_t>(wy * result.width + wx)] != 0.f) continue;
                    const double dx = cx - std::clamp(cx, double(wx), double(wx + 1));
                    const double dy = cy - std::clamp(cy, double(wy), double(wy + 1));
                    if (dx * dx + dy * dy < radius * radius) {
                        blocked = true;
                        break;
                    }
                }
            if (blocked) result.navigationCosts[index] = 0.f;
        }
    return Result<crowd::NavigationSurfaceSnapshot>::success(std::move(result));
} catch (const std::exception& error) {
    return Result<crowd::NavigationSurfaceSnapshot>::failure(
        Diagnostic::error(DiagnosticCode::Failed, error.what(), "crowdSurfaceAdapter"));
}
}  // namespace eve::rts
