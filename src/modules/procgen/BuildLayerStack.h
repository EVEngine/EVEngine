#pragma once

#include "common/Result.h"
#include "procgen/Grid2D.h"
#include "procgen/MeshBuild.h"
#include "procgen/ObjectBuildLayer.h"
#include "procgen/PointSet.h"

#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace eve::procgen {

/** @brief One named build-layer artifact in configured execution order. */
struct BuildLayerArtifact {
    std::string                       id;
    std::variant<MeshBuild, PointSet> value;
};

/** @brief Half-open grid and world-space bounds used for one incremental cluster build. */
struct BuildLayerRegion {
    int   minCellX = 0;
    int   minCellY = 0;
    int   maxCellX = 0;
    int   maxCellY = 0;
    float minWorldX = 0.f;
    float minWorldZ = 0.f;
    float maxWorldX = 0.f;
    float maxWorldZ = 0.f;
};

/** @brief Immutable-by-convention result of one complete build-stack execution. */
class EVENGINE_API_DOMAINS BuildLayerExecution {
public:
    [[nodiscard]] int         getCount() const noexcept;
    [[nodiscard]] std::string getId(int index) const;
    [[nodiscard]] std::string getType(int index) const;
    /** @brief Copy a mesh artifact, or return a typed failure for a bad index/type. */
    [[nodiscard]] Result<MeshBuild> getMesh(int index) const;
    /** @brief Copy a point artifact, or return a typed failure for a bad index/type. */
    [[nodiscard]] Result<PointSet> getPoints(int index) const;

private:
    friend class BuildLayerStack;
    std::vector<BuildLayerArtifact> artifacts_;
};

/**
 * @brief Ordered, versioned executor for procedural tile and object build layers.
 *
 * The stack owns all definitions. Execution creates a new result and publishes no
 * partial artifacts if a layer fails. No scene, renderer or physics state is owned.
 * @thread Affine; mutate and execute on the owning thread.
 */
class EVENGINE_API_DOMAINS BuildLayerStack {
public:
    /** @brief Append a grid-to-mesh tile layer. Layer ids are unique. */
    [[nodiscard]] Result<void> addTileLayer(std::string id, bool enabled, float cellSize, float height,
                                            std::string group);
    /** @brief Append an object layer by value. Layer ids are unique. */
    [[nodiscard]] Result<void> addObjectLayer(std::string id, bool enabled, const ObjectBuildLayer& layer);
    [[nodiscard]] Result<void> setEnabled(std::string_view id, bool enabled);
    void                       clear();
    [[nodiscard]] int          getLayerCount() const noexcept;
    [[nodiscard]] std::string  getLayerId(int index) const;
    [[nodiscard]] std::string  getLayerType(int index) const;
    [[nodiscard]] bool         isLayerEnabled(int index) const noexcept;

    /**
     * @brief Execute every enabled layer in stable insertion order.
     * @param grid Source for tile layers.
     * @param points Source for object layers.
     * @param orientation Optional orientation layer shared by object definitions that enable orientation.
     * @return Complete ordered artifacts, or the first structured failure with no visible partial result.
     * @cost Sum of enabled tile mesh construction and object layer costs.
     */
    [[nodiscard]] Result<BuildLayerExecution> execute(const Grid2D& grid, const PointSet& points,
                                                       const PointSet* orientation = nullptr) const;
    /** @brief Execute enabled layers for one half-open cluster region. */
    [[nodiscard]] Result<BuildLayerExecution> executeRegion(const Grid2D& grid, const PointSet& points,
                                                             const BuildLayerRegion& region,
                                                             const PointSet* orientation = nullptr) const;

    /** @brief Serialize ordered definitions using EVPCG_BUILD_LAYERS schema version 1. */
    [[nodiscard]] std::string serializeDefinition() const;
    /** @brief Atomically restore version 1; unknown fields and malformed embedded layers are rejected. */
    [[nodiscard]] Result<void> deserializeDefinition(std::string_view definition);

private:
    struct TileLayer {
        float       cellSize = 1.f;
        float       height   = 0.f;
        std::string group;
    };
    struct Layer {
        std::string                               id;
        bool                                      enabled = true;
        std::variant<TileLayer, ObjectBuildLayer> definition;
    };
    [[nodiscard]] bool contains(std::string_view id) const;
    std::vector<Layer> layers_;
};

}  // namespace eve::procgen
