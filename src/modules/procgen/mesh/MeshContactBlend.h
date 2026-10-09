#pragma once

#include "common/Export.h"
#include "common/Result.h"
#include "procgen/MeshBuild.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace eve::procgen {

/**
 * @brief Parameters for contact-band edge and material fusion.
 *
 * Pure value object. Callers serialize mutation. Defaults produce no effect until radii and
 * strengths are set positive (static merge keeps enablement separate and off by default).
 *
 * @thread Copied by value into blend evaluation; no retained pointers.
 */
struct EVENGINE_API_DOMAINS MeshContactBlendParams {
    float           edgeRadius        = 0.f;
    float           materialRadius    = 0.f;
    float           strength          = 1.f;
    float           normalsBlend      = 1.f;
    float           materialBlend     = 1.f;
    float           maxQueryDistance  = 0.f;  ///< 0 ⇒ max(edgeRadius, materialRadius)
    float           surfaceOffset     = 0.f;
    bool            softSnapPositions = false;
    std::string_view falloff          = "smooth";  ///< smooth|linear|sharp|sphere
};

/**
 * @brief Fuse edge normals / optional positions and material blend weights across source slices.
 *
 * @param mesh Owning input is not retained; vertex attributes are copied into the result.
 * @param vertexSourceIds One stable source id per vertex; size must equal `mesh.getVertexCount()`.
 * @param params Falloff and radius controls; invalid values return a diagnostic.
 * @return Owning blended mesh, or a `procgen.mesh.blend.*` diagnostic without mutating `mesh`.
 *
 * Material fusion writes vertex-color alpha as `contactBlend` in \[0,1\] (RGB preserved or filled
 * with 1). Topology and UVs are unchanged unless a later merge weld runs. When
 * `softSnapPositions` is true, hit-normal lerp is skipped; triangle normals are
 * recalculated from the deformed positions and then 1-ring-averaged so contact necks
 * shade continuously even when the soft-snap silhouette still pinches.
 * Deterministic for equal inputs; main-thread or worker safe when inputs are immutable for the
 * call duration. No callbacks.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<MeshBuild> meshContactBlendResult(
    const MeshBuild& mesh, const std::vector<std::int32_t>& vertexSourceIds,
    const MeshContactBlendParams& params);

/**
 * @brief Deform `movable` toward `surface` only (dynamic adhere / one-way fusion).
 * @param movable Source mesh A; topology preserved in the result.
 * @param surface Target mesh B; never modified or copied into the output.
 * @param params Contact-band controls; `softSnapPositions` typically true for adhere.
 * @return Owning deformed A, or a `procgen.mesh.blend.*` diagnostic.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<MeshBuild> meshContactBlendAgainstSurfaceResult(
    const MeshBuild& movable, const MeshBuild& surface, const MeshContactBlendParams& params);

/**
 * @brief Rebuild triangle normals then 1-ring-average them (soft-snap / post-weld lighting).
 * @param mesh Mesh to mutate; no-op when empty.
 * @thread Caller serializes mutation; no retained pointers.
 */
EVENGINE_API_DOMAINS void rebuildSoftSnapContactNormals(MeshBuild& mesh);

}  // namespace eve::procgen
