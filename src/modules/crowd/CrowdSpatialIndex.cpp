#include "crowd/CrowdSpatialIndex.h"
namespace eve::crowd {
std::int64_t CrowdSpatialIndex::coordinate(double value, double cell) {
    const double key = std::floor(value / cell);
    if (!std::isfinite(key) || key < -0x1p63 || key >= 0x1p63)
        throw WorkLimit{"Crowd spatial coordinate exceeds signed 64-bit range"};
    return static_cast<std::int64_t>(key);
}
std::int64_t CrowdSpatialIndex::queryBound(double value, double cell) noexcept {
    const double key = std::floor(value / cell);
    if (key < -0x1p63) return std::numeric_limits<std::int64_t>::min();
    if (key >= 0x1p63) return std::numeric_limits<std::int64_t>::max();
    return static_cast<std::int64_t>(key);
}
void CrowdSpatialIndex::rebuild(std::span<const float> xs, std::span<const float> ys, std::span<const CrowdTieKey> ties,
                                double cellSize, std::int64_t maxCells, std::int64_t& comparisons) {
    if (xs.size() != ys.size() || xs.size() != ties.size() || xs.size() > std::numeric_limits<int>::max() ||
        !std::isfinite(cellSize) || cellSize < 1.0 || maxCells < 1)
        throw WorkLimit{"Invalid Crowd spatial input or cell budget"};
    clear();
    cellSize_ = cellSize;
    members_.reserve(xs.size());
    for (std::size_t i = 0; i < xs.size(); ++i)
        members_.push_back({coordinate(xs[i], cellSize_), coordinate(ys[i], cellSize_), ties[i], static_cast<int>(i)});
    std::sort(members_.begin(), members_.end(), [&](const Member& a, const Member& b) {
        ++comparisons;
        if (a.y != b.y) return a.y < b.y;
        if (a.x != b.x) return a.x < b.x;
        return a.tie < b.tie;
    });
    for (std::size_t begin = 0; begin < members_.size();) {
        std::size_t end = begin + 1;
        while (end < members_.size() && members_[end].x == members_[begin].x && members_[end].y == members_[begin].y)
            ++end;
        if (cells_.size() >= static_cast<std::uint64_t>(maxCells))
            throw WorkLimit{"Crowd occupied cell budget exhausted"};
        cells_.push_back({members_[begin].x, members_[begin].y, begin, end});
        begin = end;
    }
    for (std::size_t begin = 0; begin < cells_.size();) {
        std::size_t end = begin + 1;
        while (end < cells_.size() && cells_[end].y == cells_[begin].y) ++end;
        rows_.push_back({cells_[begin].y, begin, end});
        begin = end;
    }
}
}  // namespace eve::crowd
