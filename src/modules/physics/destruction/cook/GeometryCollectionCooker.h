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
 * P1 produces box-proxy bones and a connection graph from the fracture pattern.
 * Full triangle CSG with interior caps is deferred to graphics-backed cook; the
 * collision proxies and neighbor graph are bit-exact for a fixed recipe/seed.
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
