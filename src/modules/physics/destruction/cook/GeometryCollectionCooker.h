#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "physics/destruction/FractureRecipe.h"
#include "physics/destruction/GeometryCollectionAsset.h"

namespace eve::asset {
struct CanonicalMeshData;
}

namespace eve::physics::destruction_cook {

/**
 * @brief Cook an owning canonical mesh into a geometry-collection asset.
 *
 * Compatibility-only proxy cook: Voronoi mode names here describe site placement,
 * not a tessellation. Use cookVoronoi from VoronoiCook.h for actual closed convex
 * fragments, shared-face adjacency, interior caps and exact convex collision.
 * The legacy proxy shape and seed behavior remain unchanged.
 *
 * @param mesh Borrowed immutable mesh retained only for this synchronous call.
 * @param recipe Validated fracture recipe; randomness uses only seed+stream name.
 * @return Complete validated asset, or a checked diagnostic with no partial publish.
 * @thread Worker-safe and reentrant; no IO, callbacks, or global mutation.
 */
[[nodiscard("check geometry-collection cooking")]]
EVENGINE_API_DOMAINS eve::Result<GeometryCollectionAsset> cookGeometryCollection(
    const eve::asset::CanonicalMeshData& mesh, const FractureRecipe& recipe);

}  // namespace eve::physics::destruction_cook
