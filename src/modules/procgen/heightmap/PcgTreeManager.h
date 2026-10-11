#pragma once

#include "common/Result.h"

#include <vector>

namespace eve::procgen {
class PointSet;

/**
 * @brief Caller-owned world-space tree index matching Pcg TreeManager's rectangular queries.
 * @ownership Copies positions and prototype indices; PointSet inputs are borrowed only during calls.
 * @thread Caller-thread affine; methods invoke no callbacks and read no hidden time or RNG.
 */
class PcgTreeManager {
public:
    /** @brief Atomically reset the index to finite positive world XZ bounds. */
    [[nodiscard]] Result<void> reset(float minimumX, float minimumZ, float width, float depth);
    /** @brief Add one tree when its finite position is within the configured half-open world bounds. */
    [[nodiscard]] Result<int> addTree(float worldX, float worldZ, int prototypeIndex);
    /** @brief Atomically append every world-space XZ position from a PointSet under one prototype index. */
    [[nodiscard]] Result<int> addTrees(const PointSet& trees, int prototypeIndex);
    /** @brief Count trees in the inclusive axis-aligned square centered at XZ with the supplied range. */
    [[nodiscard]] Result<int> countInRange(float worldX, float worldZ, float range) const;
    int getCount() const noexcept;

private:
    struct Tree { float x, z; int prototype; };
    bool configured_ = false;
    float minimumX_ = 0, minimumZ_ = 0, width_ = 0, depth_ = 0;
    std::vector<Tree> trees_;
};
}  // namespace eve::procgen
