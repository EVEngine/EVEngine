#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include "common/Export.h"
#include "common/Result.h"
namespace eve::procgen {
class MeshBuild;
}
namespace eve::procgen::road {
/** @brief Source category, scoped to one immutable road bake rather than a live object handle. */
enum class RoadGeometrySourceKind : std::uint8_t { Edge = 0, Junction = 1, NetworkDerived = 2 };
/** @brief Contiguous triangle interval; nonzero ids name author edges/nodes, NetworkDerived uses id zero. */
struct RoadGeometrySourceSpan {
    RoadGeometrySourceKind kind = RoadGeometrySourceKind::Edge;
    std::uint32_t          id = 0, firstTriangle = 0, triangleCount = 0;
};
/** @brief Owning validated source snapshot. No mesh pointer survives preparation.
 * Schema eve.procgen.road.geometrySources v1 ignores unknown fields and rejects unsupported versions.
 * The little-endian FNV-1a digest detects stale mesh streams; it is not an authentication mechanism.
 * Source ids are local to the producing bake, not globally qualified author identities.
 * Caller-thread affine, synchronous and callback-free. Re-prepare before applying intervals to a changed mesh.
 */
class EVENGINE_API_DOMAINS PreparedRoadGeometrySources {
public:
    /** @brief Release the owned interval snapshot. */
    ~PreparedRoadGeometrySources();
    /** @brief Transfer the snapshot; invalidates old borrowed spans. */
    PreparedRoadGeometrySources(PreparedRoadGeometrySources&&) noexcept;
    /** @brief Replace the snapshot; invalidates old borrowed spans. */
    PreparedRoadGeometrySources& operator=(PreparedRoadGeometrySources&&) noexcept;
    PreparedRoadGeometrySources(const PreparedRoadGeometrySources&)            = delete;
    PreparedRoadGeometrySources& operator=(const PreparedRoadGeometrySources&) = delete;
    /** @brief Validate mesh layout, fingerprint and complete ordered source partition before returning an owned token.
     * @param mesh Borrowed until return; missing old/chunk metadata returns Unsupported.
     * @return Prepared snapshot or diagnostic, without changing the mesh.
     * @cost Linear in all mesh streams and bounded metadata, amortized once per bake/load before source queries.
     */
    [[nodiscard]] static Result<PreparedRoadGeometrySources> prepare(const MeshBuild& mesh);
    /** @brief Read prepared intervals without rebuilding. Borrowed until token move, replacement or destruction. */
    [[nodiscard]] std::span<const RoadGeometrySourceSpan> spans() const noexcept;

private:
    explicit PreparedRoadGeometrySources(std::vector<RoadGeometrySourceSpan> spans);
    std::vector<RoadGeometrySourceSpan> spans_;
};
}  // namespace eve::procgen::road
