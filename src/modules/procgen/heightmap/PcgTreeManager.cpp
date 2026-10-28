#include "procgen/heightmap/PcgTreeManager.h"

#include "procgen/PointSet.h"

#include <cmath>

namespace eve::procgen {
namespace {
bool finite(float value) { return std::isfinite(value); }
}  // namespace

Result<void> PcgTreeManager::reset(float minimumX, float minimumZ, float width, float depth) {
    if (!finite(minimumX) || !finite(minimumZ) || !finite(width) || !finite(depth) ||
        width <= 0 || depth <= 0 || !finite(minimumX + width) || !finite(minimumZ + depth))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "finite positive world bounds are required",
                                                "procgen.pcgTreeManager"));
    minimumX_ = minimumX; minimumZ_ = minimumZ; width_ = width; depth_ = depth;
    trees_.clear();
    configured_ = true;
    return Result<void>::success();
}

Result<int> PcgTreeManager::addTree(float worldX, float worldZ, int prototypeIndex) {
    if (!configured_) return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "reset must configure bounds before insertion",
                                                "procgen.pcgTreeManager"));
    if (!finite(worldX) || !finite(worldZ) || prototypeIndex < 0)
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "finite position and non-negative prototype index are required",
                                                "procgen.pcgTreeManager"));
    if (worldX < minimumX_ || worldZ < minimumZ_ || worldX >= minimumX_ + width_ ||
        worldZ >= minimumZ_ + depth_)
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "tree position lies outside manager bounds",
                                                "procgen.pcgTreeManager"));
    trees_.push_back({worldX, worldZ, prototypeIndex});
    return Result<int>::success(static_cast<int>(trees_.size()));
}

Result<int> PcgTreeManager::addTrees(const PointSet& points, int prototypeIndex) {
    if (!configured_) return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "reset must configure bounds before insertion",
                                                "procgen.pcgTreeManager"));
    if (prototypeIndex < 0) return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "prototype index must be non-negative",
                                                "procgen.pcgTreeManager"));
    std::vector<Tree> candidate = trees_;
    candidate.reserve(candidate.size() + static_cast<std::size_t>(points.getCount()));
    for (const auto& point : points.points()) {
        if (!finite(point.x) || !finite(point.z) || point.x < minimumX_ || point.z < minimumZ_ ||
            point.x >= minimumX_ + width_ || point.z >= minimumZ_ + depth_)
            return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "all PointSet trees must lie inside manager bounds",
                                                "procgen.pcgTreeManager"));
        candidate.push_back({point.x, point.z, prototypeIndex});
    }
    trees_.swap(candidate);
    return Result<int>::success(static_cast<int>(trees_.size()));
}

Result<int> PcgTreeManager::countInRange(float worldX, float worldZ, float range) const {
    if (!configured_) return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "reset must configure bounds before queries",
                                                "procgen.pcgTreeManager"));
    if (!finite(worldX) || !finite(worldZ) || !finite(range) || range < 0)
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "finite position and non-negative range are required",
                                                "procgen.pcgTreeManager"));
    int count = 0;
    for (const auto& tree : trees_)
        if (tree.x >= worldX - range && tree.x <= worldX + range &&
            tree.z >= worldZ - range && tree.z <= worldZ + range)
            ++count;
    return Result<int>::success(count);
}
int PcgTreeManager::getCount() const noexcept { return static_cast<int>(trees_.size()); }
}  // namespace eve::procgen
