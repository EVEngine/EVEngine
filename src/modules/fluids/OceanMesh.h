#pragma once

#include "common/Export.h"

#include "common/Result.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace eve::fluids {
class OceanWaterView;

/** @brief Camera-centered concentric square rings with shared vertices and continuous distance grading. */
struct OceanMeshSettings {
    unsigned segments    = 256;
    unsigned rings       = 128;
    double   firstRadius = 0.5;
    double   outerRadius = 20000.0;
};

/**
 * @brief Owning immutable topology projection; XZ offsets are relative to the render origin.
 * All rings share their neighboring edges. No skirts, overlapping tiles or independent patch seams.
 * Const reads are reentrant. Consumers sample waves at double-precision center + parameterOffsets,
 * then subtract their floating origin before converting to GPU floats. Never translate wave phases
 * with the camera. This mesh covers a square, not the entire curved planet.
 */
struct OceanMeshData {
    std::vector<glm::dvec2>    parameterOffsets;
    std::vector<std::uint32_t> indices;
    std::vector<double>        radii;
};

/** @brief Owning topology preparation; settings are copied at build, with no retained references. */
class OceanMeshBuilder {
public:
    OceanMeshSettings settings;
    /**
     * @brief Build positively oriented triangles on XZ, graded geometrically from near water to horizon.
     * @return Complete owning mesh or InvalidArgument; no partial publication or callbacks.
     * @cost O(segments * rings) allocation and work; amortize over renderer quality changes.
     * @thread Caller-owned builder; reentrant const reads, no graphics context required.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<OceanMeshData> build() const;
};

/** @brief Owning CPU reference projection, ready for a mesh upload; simulation time and origin identify the frame. */
struct OceanMeshFrame {
    std::vector<float> positions;
    std::vector<float> normals;
    /** @brief Packed foam coverage and horizontal Jacobian, one pair per vertex. */
    std::vector<float> surface;
    glm::dvec3         origin{0.0};
    double             time = 0.0;
};

/**
 * @brief Prepare CPU reference vertices from the same water view used by rigid bodies.
 * Borrows mesh and water until build returns. They and the view's owners must remain alive and
 * unmodified; do not queue this builder asynchronously or retain across simulation steps.
 * No graphics, physics or resource pointer is retained in the published frame.
 */
class OceanMeshFrameBuilder {
public:
    /** @brief Borrow immutable topology and synchronized water for this synchronous build. */
    OceanMeshFrameBuilder(const OceanMeshData& mesh, const OceanWaterView& water) : mesh_(mesh), water_(water) {}
    /** @brief Double-precision world XZ center; continuous camera movement does not change wave phase. */
    glm::dvec2 center{0.0};
    /** @brief Render origin subtracted before float conversion; use the same origin for camera and scene. */
    glm::dvec3 origin{0.0};
    /**
     * @brief Build an atomic owning vertex projection; invalid queries or degenerate tangents reject the whole frame.
     * @return Positions and unit normals, or an input/query diagnostic without external mutation.
     * @cost O(vertices * bands); CPU reference/low-quality path, once per render tick, without inverse queries.
     * @thread Const synchronous operation; no callbacks, backend synchronization or global lookups.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<OceanMeshFrame> build() const;

private:
    const OceanMeshData&  mesh_;
    const OceanWaterView& water_;
};
}  // namespace eve::fluids
