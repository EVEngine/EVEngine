#pragma once

#include "building/PlacementSystem.h"
#include "common/Result.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace eve::building {

/** @brief Immutable world-space triangle mesh accelerated for placement-surface projection. */
class StaticMeshSurface final {
public:
    /** @brief Deterministic policy for multiple triangles under one plane coordinate. */
    enum class HitSelection { Highest, Lowest, ClosestToReference };

    /** @brief Immutable identity, hit policy and metadata for one static mesh snapshot. */
    struct Config {
        std::string surfaceId;
        uint64_t surfaceRevision = 0;
        std::vector<std::string> tags;
        HitSelection hitSelection = HitSelection::Highest;
        float referenceHeight = 0.f;
        bool orientNormalsToGridUp = true;
        uint32_t leafTriangleCount = 8;
    };

    /**
     * @brief Validate world-space triangle data and build an owning median-split BVH.
     * @param config Stable identity, hit selection and build settings.
     * @param vertices Packed finite XYZ positions.
     * @param indices Three valid vertex indices per non-degenerate triangle.
     * @param normals Optional packed finite XYZ vertex normals matching vertices exactly.
     * @return An immutable shareable surface, or a structured failure with no publication.
     */
    [[nodiscard]] static eve::Result<std::shared_ptr<const StaticMeshSurface>>
    create(Config config, std::vector<float> vertices, std::vector<uint32_t> indices,
           std::vector<float> normals = {});

    /**
     * @brief Project one grid-plane coordinate through the BVH and sample the selected triangle.
     * @param world Grid-plane configuration used to choose XY or XZ projection.
     * @param planeX First coordinate on the configured grid plane.
     * @param planeY Second coordinate on the configured grid plane.
     * @return A hit with stable source triangle id, or NotFound when no projected face exists.
     * @thread Safe to call concurrently after construction. No callbacks are invoked.
     */
    [[nodiscard]] eve::Result<PlacementSystem::PlacementHit>
    sample(const PlacementWorld &world, float planeX, float planeY) const;

    /** @brief Return the immutable construction configuration. */
    const Config &config() const { return config_; }
    /** @brief Number of source triangles indexed by the BVH. */
    size_t triangleCount() const { return indices_.size() / 3; }
    /** @brief Number of BVH nodes, including leaves. */
    size_t nodeCount() const { return nodes_.size(); }

private:
    struct Bounds {
        float minX;
        float minY;
        float minZ;
        float maxX;
        float maxY;
        float maxZ;
    };
    struct Node {
        Bounds bounds;
        uint32_t first = 0;
        uint32_t count = 0;
        uint32_t left = 0;
        uint32_t right = 0;
        bool leaf = false;
    };

    StaticMeshSurface(Config config, std::vector<float> vertices,
                      std::vector<uint32_t> indices, std::vector<float> normals);
    uint32_t buildNode(uint32_t first, uint32_t count);

    Config config_;
    std::vector<float> vertices_;
    std::vector<uint32_t> indices_;
    std::vector<float> normals_;
    std::vector<uint32_t> triangleOrder_;
    std::vector<Bounds> triangleBounds_;
    std::vector<Node> nodes_;
};

}  // namespace eve::building
