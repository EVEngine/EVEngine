#include "procgen/IncrementalBuild.h"

#include "common/Diagnostic.h"
#include "procgen/Semantic.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string_view>

namespace eve::procgen {
namespace {

template <class T>
Result<T> fail(DiagnosticCode code, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), {}, {}, "procgen.incrementalBuild"));
}

std::uint64_t keyFor(int x, int z) {
    return (std::uint64_t(std::uint32_t(x)) << 32U) | std::uint32_t(z);
}

int keyX(std::uint64_t key) { return std::int32_t(key >> 32U); }
int keyZ(std::uint64_t key) { return std::int32_t(key & 0xffffffffU); }

void hashBytes(std::uint64_t& hash, const void* bytes, std::size_t count) {
    const auto* data = static_cast<const unsigned char*>(bytes);
    for (std::size_t index = 0; index < count; ++index) {
        hash ^= data[index];
        hash *= 1099511628211ULL;
    }
}

template <class T>
void hashValue(std::uint64_t& hash, const T& value) {
    hashBytes(hash, &value, sizeof(value));
}

void hashString(std::uint64_t& hash, std::string_view value) {
    hashBytes(hash, value.data(), value.size());
    const unsigned char separator = 0xff;
    hashBytes(hash, &separator, 1);
}

void hashPoint(std::uint64_t& hash, const PointSet& points, std::size_t row) {
    const auto& point = points.points()[row];
    hashValue(hash, point.id);
    hashValue(hash, point.x);
    hashValue(hash, point.y);
    hashValue(hash, point.z);
    hashValue(hash, point.normalX);
    hashValue(hash, point.normalY);
    hashValue(hash, point.normalZ);
    hashValue(hash, point.pitch);
    hashValue(hash, point.yaw);
    hashValue(hash, point.roll);
    hashValue(hash, point.scaleX);
    hashValue(hash, point.scaleY);
    hashValue(hash, point.scaleZ);
    hashValue(hash, point.density);
    hashValue(hash, point.seed);
    hashValue(hash, point.boundsMinX);
    hashValue(hash, point.boundsMinY);
    hashValue(hash, point.boundsMinZ);
    hashValue(hash, point.boundsMaxX);
    hashValue(hash, point.boundsMaxY);
    hashValue(hash, point.boundsMaxZ);
    hashValue(hash, point.colorR);
    hashValue(hash, point.colorG);
    hashValue(hash, point.colorB);
    hashValue(hash, point.colorA);
    hashValue(hash, point.steepness);
    const auto& attributes = points.attributes();
    for (std::size_t column = 0; column < attributes.columnCount(); ++column) {
        const auto name = attributes.columnName(column);
        hashString(hash, name);
        const auto type = attributes.typeOf(name);
        if (!type || !attributes.has(row, name)) {
            const std::uint8_t absent = 0;
            hashValue(hash, absent);
            continue;
        }
        const auto typeValue = std::uint8_t(*type) + 1;
        hashValue(hash, typeValue);
        switch (*type) {
            case ProcgenAttributeType::Float: hashValue(hash, *attributes.getFloat(row, name)); break;
            case ProcgenAttributeType::Int: hashValue(hash, *attributes.getInt(row, name)); break;
            case ProcgenAttributeType::Bool: hashValue(hash, *attributes.getBool(row, name)); break;
            case ProcgenAttributeType::Vector: {
                const auto value = *attributes.getVector(row, name);
                hashValue(hash, value.x);
                hashValue(hash, value.y);
                hashValue(hash, value.z);
                break;
            }
            case ProcgenAttributeType::String: hashString(hash, *attributes.getString(row, name)); break;
        }
    }
}

int floorDivision(float value, float divisor) { return int(std::floor(value / divisor)); }

bool gridContent(const Grid2D& grid, int clusterX, int clusterZ, int size) {
    const int minX = std::max(0, clusterX * size);
    const int minY = std::max(0, clusterZ * size);
    const int maxX = std::min(grid.getWidth(), (clusterX + 1) * size);
    const int maxY = std::min(grid.getHeight(), (clusterZ + 1) * size);
    for (int y = minY; y < maxY; ++y)
        for (int x = minX; x < maxX; ++x)
            if (grid.getCell(x, y) != int(Semantic::Empty)) return true;
    return false;
}

std::uint64_t clusterHash(const BuildLayerStack& stack, const Grid2D& grid, const PointSet& points,
                          const PointSet* orientation, int clusterX, int clusterZ, int size, float worldSize) {
    std::uint64_t hash = 1469598103934665603ULL;
    const auto definition = stack.serializeDefinition();
    hashString(hash, definition);
    const int minX = std::max(0, clusterX * size - 1);
    const int minY = std::max(0, clusterZ * size - 1);
    const int maxX = std::min(grid.getWidth(), (clusterX + 1) * size + 1);
    const int maxY = std::min(grid.getHeight(), (clusterZ + 1) * size + 1);
    for (int y = minY; y < maxY; ++y) {
        for (int x = minX; x < maxX; ++x) {
            const int cell = grid.getCell(x, y);
            const int detail = grid.getDetail(x, y);
            hashValue(hash, cell);
            hashValue(hash, detail);
        }
    }
    for (std::size_t index = 0; index < points.points().size(); ++index) {
        const auto& point = points.points()[index];
        if (floorDivision(point.x, worldSize) == clusterX && floorDivision(point.z, worldSize) == clusterZ)
            hashPoint(hash, points, index);
    }
    if (orientation) {
        for (std::size_t index = 0; index < orientation->points().size(); ++index) hashPoint(hash, *orientation, index);
    }
    return hash;
}

}  // namespace

int IncrementalBuildDelta::getCount() const noexcept { return int(changes_.size()); }
int IncrementalBuildDelta::getClusterX(int index) const noexcept {
    return index >= 0 && index < int(changes_.size()) ? changes_[std::size_t(index)].x : 0;
}
int IncrementalBuildDelta::getClusterZ(int index) const noexcept {
    return index >= 0 && index < int(changes_.size()) ? changes_[std::size_t(index)].z : 0;
}
bool IncrementalBuildDelta::isRemoved(int index) const noexcept {
    return index >= 0 && index < int(changes_.size()) && changes_[std::size_t(index)].removed;
}
Result<BuildLayerExecution> IncrementalBuildDelta::getArtifacts(int index) const {
    if (index < 0 || index >= int(changes_.size()))
        return fail<BuildLayerExecution>(DiagnosticCode::InvalidArgument, "delta index is out of range");
    if (changes_[std::size_t(index)].removed)
        return fail<BuildLayerExecution>(DiagnosticCode::NotFound, "removed cluster has no artifacts");
    return Result<BuildLayerExecution>::success(changes_[std::size_t(index)].artifacts);
}

Result<IncrementalBuildDelta> IncrementalBuildExecutor::update(const BuildLayerStack& stack, const Grid2D& grid,
                                                                const PointSet& points, int clusterSizeCells,
                                                                float cellSizeWorld, const PointSet* orientation) {
    if (clusterSizeCells <= 0 || !std::isfinite(cellSizeWorld) || cellSizeWorld <= 0.f)
        return fail<IncrementalBuildDelta>(DiagnosticCode::InvalidArgument, "cluster and cell sizes must be positive");
    const float worldSize = float(clusterSizeCells) * cellSizeWorld;
    std::set<std::pair<int, int>> candidates;
    for (const auto& [key, record] : cache_) candidates.emplace(keyX(key), keyZ(key));
    for (int y = 0; y < grid.getHeight(); ++y)
        for (int x = 0; x < grid.getWidth(); ++x)
            if (grid.getCell(x, y) != int(Semantic::Empty)) candidates.emplace(x / clusterSizeCells, y / clusterSizeCells);
    for (const auto& point : points.points())
        candidates.emplace(floorDivision(point.x, worldSize), floorDivision(point.z, worldSize));

    IncrementalBuildDelta delta;
    std::vector<std::uint64_t> removals;
    std::vector<std::pair<std::uint64_t, Record>> upserts;
    for (const auto& [clusterX, clusterZ] : candidates) {
        const auto key = keyFor(clusterX, clusterZ);
        bool active = gridContent(grid, clusterX, clusterZ, clusterSizeCells);
        if (!active) {
            for (const auto& point : points.points()) {
                if (floorDivision(point.x, worldSize) == clusterX && floorDivision(point.z, worldSize) == clusterZ) {
                    active = true;
                    break;
                }
            }
        }
        const auto previous = cache_.find(key);
        if (!active) {
            if (previous != cache_.end()) {
                removals.push_back(key);
                delta.changes_.push_back({clusterX, clusterZ, true, {}});
            }
            continue;
        }
        const auto hash = clusterHash(stack, grid, points, orientation, clusterX, clusterZ, clusterSizeCells, worldSize);
        if (previous != cache_.end() && previous->second.hash == hash) continue;
        const int minCellX = std::clamp(clusterX * clusterSizeCells, 0, grid.getWidth());
        const int minCellY = std::clamp(clusterZ * clusterSizeCells, 0, grid.getHeight());
        const int maxCellX = std::clamp((clusterX + 1) * clusterSizeCells, minCellX, grid.getWidth());
        const int maxCellY = std::clamp((clusterZ + 1) * clusterSizeCells, minCellY, grid.getHeight());
        BuildLayerRegion region{minCellX, minCellY, maxCellX, maxCellY, float(clusterX) * worldSize,
                                float(clusterZ) * worldSize, float(clusterX + 1) * worldSize,
                                float(clusterZ + 1) * worldSize};
        auto built = stack.executeRegion(grid, points, region, orientation);
        if (!built.ok()) return Result<IncrementalBuildDelta>::failure(built.status());
        Record record{hash, std::move(built).takeValue()};
        delta.changes_.push_back({clusterX, clusterZ, false, record.artifacts});
        upserts.emplace_back(key, std::move(record));
    }
    for (const auto key : removals) cache_.erase(key);
    for (auto& [key, record] : upserts) cache_.insert_or_assign(key, std::move(record));
    return Result<IncrementalBuildDelta>::success(std::move(delta));
}

void IncrementalBuildExecutor::clear() { cache_.clear(); }
int IncrementalBuildExecutor::getCachedClusterCount() const noexcept { return int(cache_.size()); }

Result<BuildLayerExecution> IncrementalBuildExecutor::getCachedArtifacts(int clusterX, int clusterZ) const {
    const auto found = cache_.find(keyFor(clusterX, clusterZ));
    if (found == cache_.end()) return fail<BuildLayerExecution>(DiagnosticCode::NotFound, "cluster is not cached");
    return Result<BuildLayerExecution>::success(found->second.artifacts);
}

}  // namespace eve::procgen
