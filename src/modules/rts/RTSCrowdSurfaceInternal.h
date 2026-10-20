#pragma once
#include "common/Export.h"
#include "common/Result.h"
#include "crowd/CrowdTypes.h"
#include "map/Pathfinder.h"
#include "rts/RTS.h"
namespace eve::map {
class NavigationGridSnapshot;
}
namespace eve::rts {
struct RTS::CrowdSurfaceRuntime {
    CrowdSurfaceTracking          settings;
    map::Pathfinder*              source = nullptr;
    map::NavigationChangeStamp    change;
    NavigationGrid                geometry;
    crowd::NavigationSurfaceStamp publication;
    std::uint64_t                 captureRevision = 0;
};
struct NavigationGrid;
/** @brief Upper-domain adapter; owns output and never retains map spans.
 * @return Projection or explicit unsupported/invalid/budget failure. No live world is mutated.
 * @thread Owner thread only. @reentrancy No callbacks.
 * @cost Copies cells and performs bounded clearance checks; amortize at surface revision boundaries.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<crowd::NavigationSurfaceSnapshot> projectCrowdSurface(
    const map::NavigationGridSnapshot& source, crowd::NavigationSurfaceStamp stamp, const NavigationGrid& geometry,
    std::int64_t maxChecks);
}  // namespace eve::rts
