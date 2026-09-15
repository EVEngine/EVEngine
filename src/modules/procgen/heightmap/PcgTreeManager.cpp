#include "procgen/heightmap/PcgTreeManager.h"

#include "procgen/PointSet.h"

#include <cmath>

namespace eve::procgen {
namespace {
template <class T> Result<T> treeManagerFailure(const char* message) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message,
                                                "procgen.pcgTreeManager"));
}
bool finite(float value) { return std::isfinite(value); }
}  // namespace

Result<void> PcgTreeManager::reset(float minimumX, float minimumZ, float width, float depth) {
    if (!finite(minimumX) || !finite(minimumZ) || !finite(width) || !finite(depth) ||
        width <= 0 || depth <= 0 || !finite(minimumX + width) || !finite(minimumZ + depth))
        return treeManagerFailure<void>("finite positive world bounds are required");
    minimumX_ = minimumX; minimumZ_ = minimumZ; width_ = width; depth_ = depth;
    trees_.clear();
    configured_ = true;
    return Result<void>::success();
}

Result<int> PcgTreeManager::addTree(float worldX, float worldZ, int prototypeIndex) {
    if (!configured_) return treeManagerFailure<int>("reset must configure bounds before insertion");
    if (!finite(worldX) || !finite(worldZ) || prototypeIndex < 0)
        return treeManagerFailure<int>("finite position and non-negative prototype index are required");
    if (worldX < minimumX_ || worldZ < minimumZ_ || worldX >= minimumX_ + width_ ||
        worldZ >= minimumZ_ + depth_)
        return treeManagerFailure<int>("tree position lies outside manager bounds");
    trees_.push_back({worldX, worldZ, prototypeIndex});
    return Result<int>::success(static_cast<int>(trees_.size()));
}

Result<int> PcgTreeManager::addTrees(const PointSet& points, int prototypeIndex) {
    if (!configured_) return treeManagerFailure<int>("reset must configure bounds before insertion");
    if (prototypeIndex < 0) return treeManagerFailure<int>("prototype index must be non-negative");
    std::vector<Tree> candidate = trees_;
    candidate.reserve(candidate.size() + static_cast<std::size_t>(points.getCount()));
    for (const auto& point : points.points()) {
        if (!finite(point.x) || !finite(point.z) || point.x < minimumX_ || point.z < minimumZ_ ||
            point.x >= minimumX_ + width_ || point.z >= minimumZ_ + depth_)
            return treeManagerFailure<int>("all PointSet trees must lie inside manager bounds");
        candidate.push_back({point.x, point.z, prototypeIndex});
    }
    trees_.swap(candidate);
    return Result<int>::success(static_cast<int>(trees_.size()));
}

Result<int> PcgTreeManager::countInRange(float worldX, float worldZ, float range) const {
    if (!configured_) return treeManagerFailure<int>("reset must configure bounds before queries");
    if (!finite(worldX) || !finite(worldZ) || !finite(range) || range < 0)
        return treeManagerFailure<int>("finite position and non-negative range are required");
    int count = 0;
    for (const auto& tree : trees_)
        if (tree.x >= worldX - range && tree.x <= worldX + range &&
            tree.z >= worldZ - range && tree.z <= worldZ + range)
            ++count;
    return Result<int>::success(count);
}
int PcgTreeManager::getCount() const noexcept { return static_cast<int>(trees_.size()); }
}  // namespace eve::procgen
