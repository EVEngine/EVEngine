#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
#include "common/Export.h"
#include "common/Result.h"
#include "common/Revision.h"

namespace eve::procgen {
class MeshBuild;
namespace recursive {
class RecursiveInstance;
struct NodeKey;
}  // namespace recursive
namespace recursive_graphics {
/** @brief Explicit root-local to render-local projection and generation admission limits. */
struct FrontierMeshPolicy {
    std::array<double, 3> origin{};
    /** @brief Render position is (root-local position - origin) * 2^scaleExponent. */
    int         scaleExponent = 0;
    std::size_t maxNodes      = 100000;
    /** @brief Absolute coordinate quantization tolerance in render-local units. */
    double maxCoordinateError = 1e-5;
};
/**
 * @brief Owning conservative box mesh for one complete edited frontier, with stable triangle identity.
 * @ownership Owns the mesh and node paths; no source instance or renderer pointers survive the build.
 * @thread Move/destruction require exclusive access; no callbacks. Rebuild after edits or restore.
 * @remarks Each emitted box has 12 consecutive triangles. boxNodePaths maps triangle/12 to a node.
 * Empty/deleted nodes emit no box. Geometry is a bound proxy, not detailed leaf geometry.
 */
struct EVENGINE_API_ORCHESTRATION FrontierMesh {
    /** @brief Create empty owning output storage. */
    FrontierMesh();
    /** @brief Release mesh storage with its complete provider type. */
    ~FrontierMesh();
    /** @brief Transfer all derived mesh data. */
    FrontierMesh(FrontierMesh&&) noexcept;
    /** @brief Replace derived data by ownership transfer. */
    FrontierMesh& operator=(FrontierMesh&&) noexcept;
    FrontierMesh(const FrontierMesh&)                         = delete;
    FrontierMesh&              operator=(const FrontierMesh&) = delete;
    std::unique_ptr<MeshBuild> mesh;
    std::vector<std::uint64_t> boxNodePaths;
    Revision                   revision;
    int                        lengthExponent = 0;
    FrontierMeshPolicy         projection;
    double                     maxObservedCoordinateError = 0;
};
/**
 * @brief Build renderable conservative bounds for a complete, non-overlapping source frontier.
 * @param instance Borrowed for this synchronous owner-thread call; must not mutate concurrently.
 * @param keys Borrowed until return; the instance validates source coverage before geometry allocation.
 * @param policy Explicit budget and projection; physical lengthExponent is retained, never cast to float.
 * @return Owning candidate or structured failure; never mutates the instance or any existing mesh.
 * @cost O(K*D+K log K+K log S) validation plus O(K) mesh work/storage per explicit rebuild;
 * reuse the result until model revision or render-local projection changes.
 * @remarks No implicit publication, GPU calls, RNG, simulation time or callbacks. Quantization is
 * measured against binary64 projected bounds, not a certified error bound on all preceding arithmetic.
 */
[[nodiscard]] EVENGINE_API_ORCHESTRATION Result<FrontierMesh> buildFrontierMesh(const recursive::RecursiveInstance& instance,
                                                                          std::span<const recursive::NodeKey> keys,
                                                                          const FrontierMeshPolicy&           policy);
}  // namespace recursive_graphics
}  // namespace eve::procgen
