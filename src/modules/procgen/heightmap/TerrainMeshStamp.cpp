#include "procgen/heightmap/TerrainMeshStamp.h"

#include "procgen/MeshBuild.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainRasterInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eve::procgen {
using namespace raster_detail;

Result<int> TerrainMeshStampBuilder::setSource(const MeshBuild& mesh) {
    const auto&           positions       = mesh.positions();
    const auto&           indices         = mesh.indices();
    constexpr std::size_t maximumElements = 3U * 1024U * 1024U;
    if (positions.empty() || positions.size() % 3 != 0 || positions.size() > maximumElements || indices.empty() ||
        indices.size() % 3 != 0 || indices.size() > maximumElements ||
        !std::all_of(positions.begin(), positions.end(), [](float v) { return std::isfinite(v); }) ||
        !std::all_of(indices.begin(), indices.end(), [&](std::uint32_t i) { return i < positions.size() / 3; }))
        return invalid("terrain.meshStamp: finite triangle positions and in-range indices required (1M limit)");
    auto candidatePositions = positions;
    auto candidateIndices   = indices;
    positions_.swap(candidatePositions);
    indices_.swap(candidateIndices);
    return Result<int>::success(static_cast<int>(indices_.size() / 3));
}

Result<int> TerrainMeshStampBuilder::bake(Heightmap& heights, Heightmap& coverage, double minX, double minZ,
                                          double width, double depth, double feather) const {
    if (&heights == &coverage || !validRaster(heights) || !validRaster(coverage) || heights.getWidth() < 2 ||
        heights.getHeight() < 2 || heights.getWidth() != coverage.getWidth() ||
        heights.getHeight() != coverage.getHeight() || heights.data().size() > 4U * 1024U * 1024U || indices_.empty() ||
        !std::isfinite(minX) || !std::isfinite(minZ) || !std::isfinite(width) || !std::isfinite(depth) ||
        !std::isfinite(feather) || width <= 0 || depth <= 0 || feather < 0 || !std::isfinite(minX + width) ||
        !std::isfinite(minZ + depth))
        return invalid("terrain.meshStamp: source and distinct matching finite rasters/bake bounds required");
    const int    columns = heights.getWidth(), rows = heights.getHeight();
    const double dx = width / (columns - 1), dz = depth / (rows - 1);
    if (dx <= 0 || dz <= 0 || minX + dx == minX || minZ + dz == minZ)
        return invalid("terrain.meshStamp: grid spacing is not representable");
    std::vector<float> candidate(heights.data().size(), 0.f), mask(candidate.size(), 0.f);
    std::uint64_t      work    = 0;
    int                covered = 0;
    struct Projection {
        std::size_t a, b, c;
        double      determinant;
        int         x0, x1, z0, z1;
    };
    std::vector<Projection> projections;
    projections.reserve(indices_.size() / 3);
    for (std::size_t triangle = 0; triangle < indices_.size(); triangle += 3) {
        const auto   a  = static_cast<std::size_t>(indices_[triangle]) * 3;
        const auto   b  = static_cast<std::size_t>(indices_[triangle + 1]) * 3;
        const auto   c  = static_cast<std::size_t>(indices_[triangle + 2]) * 3;
        const double ax = positions_[a], az = positions_[a + 2];
        const double bx = positions_[b], bz = positions_[b + 2];
        const double cx = positions_[c], cz = positions_[c + 2];
        const double determinant = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
        if (determinant == 0) continue;
        const double left   = std::max(minX, std::min({ax, bx, cx}));
        const double right  = std::min(minX + width, std::max({ax, bx, cx}));
        const double bottom = std::max(minZ, std::min({az, bz, cz}));
        const double top    = std::min(minZ + depth, std::max({az, bz, cz}));
        if (left > right || bottom > top) continue;
        // Expand by one sample to avoid losing a shared edge through rounding.
        const int x0 = static_cast<int>(std::clamp(std::floor((left - minX) / dx) - 1, 0.0, double(columns - 1)));
        const int x1 = static_cast<int>(std::clamp(std::ceil((right - minX) / dx) + 1, 0.0, double(columns - 1)));
        const int z0 = static_cast<int>(std::clamp(std::floor((bottom - minZ) / dz) - 1, 0.0, double(rows - 1)));
        const int z1 = static_cast<int>(std::clamp(std::ceil((top - minZ) / dz) + 1, 0.0, double(rows - 1)));
        work += std::uint64_t(x1 - x0 + 1) * std::uint64_t(z1 - z0 + 1);
        if (work > 128U * 1024U * 1024U)
            return invalid("terrain.meshStamp: projected triangle work exceeds 128M sample budget");
        projections.push_back({a, b, c, determinant, x0, x1, z0, z1});
    }
    for (const auto& projection : projections) {
        const auto [a, b, c, determinant, x0, x1, z0, z1] = projection;
        const double ax = positions_[a], az = positions_[a + 2], bx = positions_[b], bz = positions_[b + 2],
                     cx = positions_[c], cz = positions_[c + 2];
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                const double     px = minX + x * dx, pz = minZ + z * dz;
                double           wa        = ((bz - cz) * (px - cx) + (cx - bx) * (pz - cz)) / determinant;
                double           wb        = ((cz - az) * (px - cx) + (ax - cx) * (pz - cz)) / determinant;
                double           wc        = 1.0 - wa - wb;
                constexpr double tolerance = 1e-10;
                if (wa < -tolerance || wb < -tolerance || wc < -tolerance) continue;
                wa = std::max(0.0, wa);
                wb = std::max(0.0, wb);
                wc = std::max(0.0, wc);
                const double y =
                    (wa * positions_[a + 1] + wb * positions_[b + 1] + wc * positions_[c + 1]) / (wa + wb + wc);
                if (!isRepresentable(y)) return invalid("terrain.meshStamp: nonfinite projected height");
                const auto index = static_cast<std::size_t>(z) * columns + x;
                if (mask[index] == 0 || y > candidate[index]) candidate[index] = static_cast<float>(y);
                if (mask[index] == 0) ++covered;
                mask[index] = 1;
            }
        }
    }
    if (feather > 0) {
        // Distance to uncovered samples or the rectangle edge in the bake coordinate system.
        std::vector<double> distances(mask.size(), 0.0);
        for (int z = 0; z < rows; ++z)
            for (int x = 0; x < columns; ++x) {
                const auto i = static_cast<std::size_t>(z) * columns + x;
                if (mask[i] == 0) continue;
                distances[i] = std::min({x * dx, (columns - 1 - x) * dx, z * dz, (rows - 1 - z) * dz, feather});
                if (x > 0) distances[i] = std::min(distances[i], distances[i - 1] + dx);
                if (z > 0) distances[i] = std::min(distances[i], distances[i - columns] + dz);
            }
        for (int z = rows - 1; z >= 0; --z)
            for (int x = columns - 1; x >= 0; --x) {
                const auto i = static_cast<std::size_t>(z) * columns + x;
                if (x + 1 < columns) distances[i] = std::min(distances[i], distances[i + 1] + dx);
                if (z + 1 < rows) distances[i] = std::min(distances[i], distances[i + columns] + dz);
                const double t = std::clamp(distances[i] / feather, 0.0, 1.0);
                mask[i]        = static_cast<float>(t * t * (3.0 - 2.0 * t));
            }
    }
    heights.data().swap(candidate);
    coverage.data().swap(mask);
    return Result<int>::success(covered);
}
}  // namespace eve::procgen
