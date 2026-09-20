#pragma once

#include "common/Result.h"
#include "procgen/MeshBuild.h"
#include <memory>
#include <vector>

namespace eve::procgen {

/** @brief Pivot translation applied independently to each GTS split mesh. */
enum class GtsMeshPivot { None=0, MinimumXZ=1, CenterXZ=2 };

/** @brief One row-major GTS mesh cell and the world offset restoring its source position. */
struct GtsMeshSplitTile {
    MeshBuild mesh;
    float offsetX=0.f,offsetZ=0.f;
};

/** @brief Owned row-major result of a GTS X/Z plane split. */
class EVENGINE_API_DOMAINS GtsMeshSplitResult {
public:
    /** @brief Return the number of output columns. */
    int getColumnCount() const noexcept { return columns_; }
    /** @brief Return the number of output rows. */
    int getRowCount() const noexcept { return rows_; }
    /** @brief Return the number of retained row-major cells. */
    int getTileCount() const noexcept { return static_cast<int>(tiles_.size()); }
    /**
     * @brief Borrow one indexed tile.
     * @ownership This result retains ownership; the caller must not delete the pointer.
     * @lifetime Valid until this result is destroyed or replaced by another split.
     */
    const GtsMeshSplitTile* tileAt(int index) const noexcept;
    /** @brief Copy one tile mesh for independent ownership, or null for an invalid index. */
    [[nodiscard]] std::unique_ptr<MeshBuild> copyTileMesh(int index) const;
    /** @brief Return the X offset that restores one translated tile to world space. */
    float getTileOffsetX(int index) const noexcept;
    /** @brief Return the Z offset that restores one translated tile to world space. */
    float getTileOffsetZ(int index) const noexcept;
private:
    friend EVENGINE_API_DOMAINS Result<GtsMeshSplitResult> splitGtsMesh(const MeshBuild&,int,int,GtsMeshPivot);
    friend Result<void> splitGtsMeshInto(GtsMeshSplitResult&,const MeshBuild&,int,int,GtsMeshPivot);
    int columns_=0,rows_=0;
    std::vector<GtsMeshSplitTile> tiles_;
};

/**
 * @brief Split every source triangle against a regular X/Z grid like GTSMeshSplitter.
 * @param source Immutable triangle mesh with aligned position, normal and UV streams.
 * @param xSplits Number of vertical split planes; output columns equal xSplits plus one.
 * @param zSplits Number of horizontal split planes; output rows equal zSplits plus one.
 * @param pivot Optional per-cell vertex translation with a matching restoration offset.
 * @return Owned row-major cells; empty cells are retained.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<GtsMeshSplitResult> splitGtsMesh(
    const MeshBuild& source,int xSplits,int zSplits,GtsMeshPivot pivot=GtsMeshPivot::None);
/** @brief Atomically replace an owned result with a GTS mesh split. */
[[nodiscard]] Result<void> splitGtsMeshInto(GtsMeshSplitResult& output,const MeshBuild& source,
                                            int xSplits,int zSplits,GtsMeshPivot pivot=GtsMeshPivot::None);
}
