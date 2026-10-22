#pragma once
#include <memory>
#include "common/Export.h"
#include "common/Result.h"
#include "physics/destruction/FractureGeometry.h"
#include "physics/destruction/GeometryCollectionAsset.h"
namespace eve::asset {
struct CanonicalMeshData;
}
namespace eve::physics {
struct FractureRecipe;
}
namespace eve::physics::destruction_cook {
struct AssemblyFractureSettings;
/** @brief Complete owning fracture asset: graph, body origins and exact convex geometry. */
struct FracturedCollection {
    GeometryCollectionAsset collection;
    FractureGeometry        geometry;
};
/** @brief Intersect a closed convex source mesh with seeded Voronoi half-spaces.
 * @param mesh Borrowed only for the call. Reject open, nonconvex, degenerate or excessive input.
 * @param recipe Uniform/clustered Voronoi recipe; explicit seed and named stream, no clock.
 * @return Complete immutable-ready data or failure, without partial publication.
 * @cost Offline polynomial clipping in sites and source faces; cook once, reuse the asset.
 * @thread Worker-safe, reentrant; no IO, locks, callbacks, services or global RNG.
 * @lifetime Returned data owns every vertex; source can be destroyed immediately.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<FracturedCollection> cookVoronoi(const eve::asset::CanonicalMeshData& mesh,
                                                                           const FractureRecipe&                recipe);
/** @brief Prefracture each box in an authored assembly and preserve declared parent supports.
 * Adjacent child AABBs within 0.04m are welded only across an authored parent edge.
 * @cost Offline repeated convex clipping; amortize once over all animation instances.
 * @return Complete owning assembly or checked failure. Borrows inputs only during call.
 * @thread Worker-safe; same seed/stream and input give the same ordering within float tolerance.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<FracturedCollection> cookVoronoiAssembly(
    const GeometryCollectionAsset& assembly, const FractureRecipe& recipe);
/** @brief Parameterized assembly cook with per-group/per-component fracture and joint policies.
 * @cost Offline clipping and graph construction; cook once, reuse every saved playback.
 * @return Complete asset or failure without partial publication; borrows inputs only during call.
 * @thread Worker-safe, no callbacks, IO or global RNG. Policy lookup never runs in playback.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<FracturedCollection> cookVoronoiAssembly(
    const GeometryCollectionAsset& assembly, const AssemblyFractureSettings& settings);
}  // namespace eve::physics::destruction_cook
