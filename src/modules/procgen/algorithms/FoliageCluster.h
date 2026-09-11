#pragma once

#include "procgen/MeshBuild.h"

#include <cstdint>

namespace eve::procgen {

/**
 * @brief Recipe for one stylized leaf cluster ("foliage card bundle").
 *
 * A cluster is a spherical volume filled with a handful of leaf planes. Each
 * plane is a disk whose leaf centres come from a blue-noise (Poisson-disk)
 * sampler, so the leaves stay evenly spread instead of clumping the way an
 * independent per-leaf random draw does. Only leaf quads are emitted, which is
 * the geometric equivalent of "leaves opaque, everything else transparent":
 * the gaps between leaves cost no geometry, no blending and no sort order.
 *
 * The planes are placed at stratified yaw angles around the cluster's Y axis
 * with a small random tilt and offset, and vertex normals are transferred from
 * the cluster sphere rather than taken from the leaf's own plane, so a bundle
 * of flat cards still shades like a rounded mass of foliage.
 */
struct FoliageClusterDesc {
    /** @brief RNG stream seed; the same seed and inputs reproduce the same leaves. */
    std::uint32_t seed = 0;
    /** @brief Cluster centre on X, in world units. */
    float centerX = 0.f;
    /** @brief Cluster centre on Y, in world units. */
    float centerY = 0.f;
    /** @brief Cluster centre on Z, in world units. */
    float centerZ = 0.f;
    /** @brief Cluster radius in world units; clamped to at least 1e-3. */
    float radius = 1.f;
    /** @brief Leaf length in world units; clamped to at least 1e-3. */
    float leafSize = 0.2f;
    /**
     * @brief Blue-noise centre distance, in `leafSize` units.
     * Larger values spread the leaves out (sparser, more see-through foliage).
     * Clamped to at least 0.05.
     */
    float leafSpacing = 0.9f;
    /** @brief Leaf planes rotated around the cluster's Y axis; clamped to `1..24`. */
    int planes = 9;
    /**
     * @brief Near-horizontal planes that close the cluster's top and bottom.
     * A pure ring of vertical planes leaves the poles empty. Clamped to `0..8`.
     */
    int capPlanes = 2;
    /** @brief Maximum ring-plane tilt away from vertical, in degrees; clamped to `0..80`. */
    float tiltDegrees = 26.f;
    /** @brief Per-plane translation inside the cluster, in `radius` units; clamped to `0..0.6`. */
    float planeOffset = 0.18f;
    /** @brief Per-plane radius variation; clamped to `0..0.6`. */
    float planeScaleVariation = 0.14f;
    /** @brief Per-leaf offset along the plane normal, in `radius` units; clamped to `0..0.5`. */
    float leafJitter = 0.16f;
    /** @brief Leaf size variation as a fraction of `leafSize`; clamped to `0..0.6`. */
    float leafScaleVariation = 0.25f;
    /**
     * @brief Hard cap on leaves per plane; clamped to `1..256`.
     * When it binds, the sampler widens the blue-noise distance instead of
     * truncating, so the retained leaves stay evenly distributed.
     */
    int maxLeavesPerPlane = 24;
    /** @brief Blend from the flat plane normal (0) to the cluster sphere normal (1). */
    float normalRounding = 1.f;
    /** @brief Start of the foliage band in the shared tree/bush atlas, on U. */
    float uvMin = 0.55f;
    /** @brief End of the foliage band in the shared tree/bush atlas, on U. */
    float uvMax = 1.f;
    /**
     * @brief Emit both windings per leaf.
     * The engine's mesh3d pipelines cull back faces unless a `Material` opts
     * into two-sided rasterization, so foliage has to carry both windings.
     */
    bool doubleSided = true;
};

/**
 * @brief Append one blue-noise leaf cluster to a mesh.
 *
 * The call is deterministic for a given @ref FoliageClusterDesc and appends
 * leaves in a stable order. Positions, normals and UVs follow the usual
 * `MeshBuild` layout; UVs stay inside `[uvMin, uvMax]` on U.
 *
 * @param out Destination mesh, appended to in place.
 * @param desc Cluster description; out-of-range fields are clamped, never rejected.
 * @return Number of leaves appended (0 when the description degenerates).
 */
int addFoliageCluster(MeshBuild &out, const FoliageClusterDesc &desc);

}  // namespace eve::procgen
