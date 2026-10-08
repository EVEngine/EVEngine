#pragma once

#include "common/Result.h"
#include "procgen/BuildLayerStack.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace eve::procgen {

/** @brief One cache mutation emitted by an incremental cluster rebuild. */
struct BuildClusterChange {
    int                 x       = 0;
    int                 z       = 0;
    bool                removed = false;
    BuildLayerExecution artifacts;
};

/** @brief Ordered changes required to synchronize a scene-side cluster cache. */
class EVENGINE_API_DOMAINS IncrementalBuildDelta {
public:
    [[nodiscard]] int  getCount() const noexcept;
    [[nodiscard]] int  getClusterX(int index) const noexcept;
    [[nodiscard]] int  getClusterZ(int index) const noexcept;
    [[nodiscard]] bool isRemoved(int index) const noexcept;
    [[nodiscard]] Result<BuildLayerExecution> getArtifacts(int index) const;

private:
    friend class IncrementalBuildExecutor;
    std::vector<BuildClusterChange> changes_;
};

/**
 * @brief Derived cluster cache that incrementally executes a BuildLayerStack.
 *
 * Grid hashes include a one-cell halo so tile-side geometry is invalidated when a
 * neighboring cluster changes. Point ownership follows the source point's half-open
 * world-space cluster. Updates are transactional and preserve the old cache on failure.
 * @thread Affine; update and query on the owning thread.
 */
class EVENGINE_API_DOMAINS IncrementalBuildExecutor {
public:
    /**
     * @brief Rebuild only clusters whose layer definition, grid halo, point rows or orientation changed.
     * @param stack Authoritative ordered layer definitions.
     * @param grid Authoritative tile source.
     * @param points Authoritative object placement source.
     * @param clusterSizeCells Positive square cluster extent in grid cells.
     * @param cellSizeWorld Positive world-space size of one grid cell.
     * @param orientation Optional object orientation layer; changes invalidate all active clusters.
     * @return Deterministically ordered upsert/removal delta. Empty means the cache was already current.
     * @cost O(grid cells + points + dirty cluster build cost); unchanged artifacts are copied only by cache ownership.
     */
    [[nodiscard]] Result<IncrementalBuildDelta> update(const BuildLayerStack& stack, const Grid2D& grid,
                                                        const PointSet& points, int clusterSizeCells,
                                                        float cellSizeWorld, const PointSet* orientation = nullptr);
    void                                    clear();
    [[nodiscard]] int                       getCachedClusterCount() const noexcept;
    [[nodiscard]] Result<BuildLayerExecution> getCachedArtifacts(int clusterX, int clusterZ) const;

private:
    struct Record {
        std::uint64_t       hash = 0;
        BuildLayerExecution artifacts;
    };
    std::unordered_map<std::uint64_t, Record> cache_;
};

}  // namespace eve::procgen
