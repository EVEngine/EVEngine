#pragma once

#include "common/Export.h"
#include "common/Result.h"

#include <cstdint>

namespace eve::procgen {
class Heightmap;
class MeshBuild;

/**
 * @brief Settings for Pegasus/Ubisoft-style cliff extraction from a heightfield.
 *
 * Steps follow the GDC/Houdini cliff tool and Project Pegasus:
 * 1. Slope-threshold steep heightfield quads
 * 2. Subdivide those surface patches
 * 3. Stratify with per-bed XZ extrusion and Worley rock displace
 * 4. Laplacian blur, then rebuild normals
 *
 * Slope uses the same metric as deriveTerrainSlope (`1 - run/hypot(run, relief)`).
 * When cliffFitness is supplied it must match the heightmap size; otherwise slope is
 * derived inline and used as a soft gate alongside geometric steepness.
 */
struct EVENGINE_API_DOMAINS CliffBandMeshSettings {
    float         originX          = 0.f;
    float         originZ          = 0.f;
    float         spacingX         = 1.f;
    float         spacingZ         = 1.f;
    float         heightScale      = 1.f;
    float         slopeMin         = 0.42f;
    float         slopeMax         = 1.f;
    float         minRelief        = 0.5f;   ///< Skip quads whose world-space max edge |Δh| is below this.
    int           subdivisions     = 2;      ///< Extra splits per cell edge in [0, 4].
    float         strataAmplitude  = 0.28f;  ///< Per-bed outward extrusion scale (Ubisoft strata).
    float         strataScale      = 0.55f;  ///< World frequency for Worley/rock macro noise.
    float         strataVertical   = 2.4f;   ///< Bedding frequency along world Y (beds ≈ 1/f).
    float         ledgeHeight      = 0.f;    ///< >0 overrides bed height in world meters.
    float         outwardBias      = 0.85f;  ///< Blend of horizontal outward vs surface-normal displace.
    float         claddingOffset   = 0.15f;  ///< Uniform push along outward so cladding clears terrain.
    float         shellThickness   = 0.f;    ///< >0 extrudes a back face (-outward) so the wall is not a wafer.
    float         strataTilt       = 0.04f;  ///< Soft world tilt of bedding planes (Houdini strata tilt).
    float         macroAmplitude   = 0.35f;  ///< Worley F2-F1 rock block displace (Pegasus detail).
    int           smoothIterations = 2;      ///< Laplacian blur passes after displace (shape blur).
    float         uvScaleU         = 0.12f;  ///< World XZ → albedo U (triplanar-ish along wall).
    float         uvScaleV         = 0.14f;  ///< World Y → albedo V (beds).
    std::uint32_t seed             = 1;
    int           maxTriangles     = 500000;
};

/**
 * @brief Extract steep heightfield patches, subdivide, stratify, blur → cliff mesh.
 *
 * Follows Pegasus steep-heightfield-to-mesh displace and the Ubisoft GDC cliff
 * sequence: slope threshold, remesh, stratification, extrusion, blur. Shared verts
 * across adjacent steep quads; no dual-edge fence wafers.
 * @param out Cleared then filled; published only after a complete candidate succeeds.
 * @param heights Finite heightmap with both dimensions at least 2.
 * @param settings Finite positive spacing/scale, slope range, subdivision and triangle budget.
 * @param cliffFitness Optional matching fitness raster in [0,1]; null derives slope inline.
 * @ownership `out` is exclusively borrowed and fully replaced on success. `heights` and
 *            `cliffFitness` are non-owning borrows read only for the call duration.
 * @lifetime Callers must keep `heights` / `cliffFitness` alive through the call; the
 *           function retains no pointers afterward. `cliffFitness` may be null.
 * @return Emitted triangle count, or InvalidArgument / Failed (budget) without mutating out.
 * @cost Linear in steep-quad count times subdivision^2; keep maxTriangles bounded for gameplay meshes.
 * @thread Synchronous exclusive out; immutable inputs; deterministic for a fixed seed.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<int> generateCliffBandMesh(MeshBuild& out, const Heightmap& heights,
                                                                     const CliffBandMeshSettings& settings,
                                                                     const Heightmap* cliffFitness = nullptr);

/**
 * @brief Build a cliff fitness mask from height slopes (1-normal.y) and a smoothstep range.
 * @param target Matching-size destination; may alias heights.
 * @return Changed sample count or InvalidArgument.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<int> generateCliffFitnessMask(Heightmap& target, const Heightmap& heights,
                                                                        float spacingX, float spacingZ,
                                                                        float heightScale, float slopeMin,
                                                                        float slopeMax);

}  // namespace eve::procgen
