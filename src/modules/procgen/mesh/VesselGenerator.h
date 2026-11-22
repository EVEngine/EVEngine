#pragma once
#include <vector>
#include "common/Export.h"
#include "common/Result.h"
#include "procgen/MeshBuild.h"
namespace eve::procgen {
class MeshRecipeRegistry;
/** @brief Radial/vertical section point in engine units (Y up). */
struct VesselSectionPoint {
    float radius = 0.f;
    float height = 0.f;
};
/**
 * @brief Revolve a solid section: underside axis, outside, rim, inside, floor axis.
 * @param section Borrowed during this call; 3..1024 finite points forming a simple
 * counterclockwise polygon in (radius,height), closed along the axis. Only endpoints touch the axis.
 * @param radialSegments Circumference resolution, 3..512.
 * @return Independently owned CPU mesh or VesselSectionInvalid; failure publishes nothing.
 * @ownership Caller owns the mesh. No input references or GPU resources are retained.
 * @thread Any thread with immutable inputs; reentrant, no callbacks or global state.
 * @cost Linear in section points times radialSegments after quadratic section validation;
 * generate once, then reuse or upload separately.
 * @remarks Section corners sharper than acos(0.8) are preserved; gentler joins use smooth normals. Same-build
 * deterministic; cross-platform float tolerance applies.
 */
[[nodiscard]] EVENGINE_API_DOMAINS eve::Result<MeshBuild> revolveVesselSection(
    const std::vector<VesselSectionPoint>& section, int radialSegments);
/** @brief Register pottery recipes at initialization on the registry owner thread. */
EVENGINE_API_DOMAINS void registerVesselRecipes(MeshRecipeRegistry& registry);
}  // namespace eve::procgen
