#pragma once

#include <memory>
#include "common/Result.h"

namespace eve::procgen {
class Heightmap;
struct TerrainSampleGrid;

/** @brief Pcg PolyMask topology: open strokes only, or closed flood fill plus strokes. */
enum class TerrainPolygonMaskType { Open, Closed };

/**
 * @brief Own ordered Pcg-style PolyMask nodes and rasterize them into a caller-owned scalar grid.
 * Nodes are value-copied and no terrain, texture, scene object, callback, clock, or RNG is retained.
 */
class TerrainPolygonMask {
public:
    TerrainPolygonMask();
    ~TerrainPolygonMask();
    TerrainPolygonMask(TerrainPolygonMask&&) noexcept;
    TerrainPolygonMask& operator=(TerrainPolygonMask&&) noexcept;
    TerrainPolygonMask(const TerrainPolygonMask&) = delete;
    TerrainPolygonMask& operator=(const TerrainPolygonMask&) = delete;

    /** @brief Append a finite world-X/Z node with positive radius and retained Pcg strength metadata. */
    [[nodiscard]] Result<int> addNode(double worldX, double worldZ, float radius, float strength);
    /** @brief Remove every owned node. */
    void clear();
    /** @brief Return the current node count. */
    [[nodiscard]] int getNodeCount() const noexcept;
    /**
     * @brief Rasterize closed fill and ordered additive brush strokes atomically.
     * @param target Exclusively borrowed finite destination; dimensions must match grid.
     * @param brush Borrowed finite scalar brush. Samples outside each node's square footprint are zero.
     * @param grid Positive finite sample spacing and finite world origin.
     * @param type Open draws strokes only; Closed first fills the polygon when at least three nodes exist.
     * @return Changed sample count or InvalidArgument; failure leaves target unchanged.
     * @throws std::bad_alloc Target remains unchanged.
     * @thread Synchronous exclusive target access; immutable brush and owned-node snapshot, no callbacks.
     * Pcg 4.2.2 passes node strength to ApplyBrushStroke but its shader path does not bind or consume it;
     * this implementation retains the field for asset fidelity and likewise does not scale the brush by it.
     */
    [[nodiscard]] Result<int> rasterize(Heightmap& target, const Heightmap& brush, const TerrainSampleGrid& grid,
                                        TerrainPolygonMaskType type) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::procgen
