#include "procgen/heightmap/TerrainPolygonMask.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>
#include "procgen/heightmap/TerrainMask.h"
#include "procgen/heightmap/TerrainRasterInternal.h"

namespace eve::procgen {
using namespace raster_detail;

struct TerrainPolygonMask::Impl {
    struct Node {
        double x, z;
        float  radius, strength;
    };
    std::vector<Node> nodes;
};

TerrainPolygonMask::TerrainPolygonMask() : impl_(std::make_unique<Impl>()) {}
TerrainPolygonMask::~TerrainPolygonMask() = default;
TerrainPolygonMask::TerrainPolygonMask(TerrainPolygonMask&&) noexcept = default;
TerrainPolygonMask& TerrainPolygonMask::operator=(TerrainPolygonMask&&) noexcept = default;

Result<int> TerrainPolygonMask::addNode(double worldX, double worldZ, float radius, float strength) {
    if (!impl_ || !std::isfinite(worldX) || !std::isfinite(worldZ) || !std::isfinite(radius) || radius <= 0 ||
        !std::isfinite(strength))
        return invalid("terrain.polyMask: finite position/strength and positive radius required");
    auto candidate = impl_->nodes;
    candidate.push_back({worldX, worldZ, radius, strength});
    impl_->nodes.swap(candidate);
    return Result<int>::success(static_cast<int>(impl_->nodes.size()));
}

void TerrainPolygonMask::clear() {
    if (impl_) impl_->nodes.clear();
}

int TerrainPolygonMask::getNodeCount() const noexcept {
    return impl_ ? static_cast<int>(impl_->nodes.size()) : 0;
}

Result<int> TerrainPolygonMask::rasterize(Heightmap& target, const Heightmap& brush, const TerrainSampleGrid& grid,
                                          TerrainPolygonMaskType type) const {
    if (!impl_ || !validRaster(target) || !validRaster(brush) || target.getWidth() != grid.width ||
        target.getHeight() != grid.height || !std::isfinite(grid.originX) || !std::isfinite(grid.originZ) ||
        !std::isfinite(grid.spacingX) || !std::isfinite(grid.spacingZ) || grid.spacingX <= 0 || grid.spacingZ <= 0 ||
        type < TerrainPolygonMaskType::Open || type > TerrainPolygonMaskType::Closed)
        return invalid("terrain.polyMask.rasterize: finite compatible rasters, grid and topology required");

    auto inside = [&](double x, double z) {
        bool hit = false;
        for (size_t i = 0, j = impl_->nodes.size() - 1; i < impl_->nodes.size(); j = i++) {
            const auto& a = impl_->nodes[i];
            const auto& b = impl_->nodes[j];
            if ((a.z > z) != (b.z > z) && x < (b.x - a.x) * (z - a.z) / (b.z - a.z) + a.x) hit = !hit;
        }
        return hit;
    };

    std::vector<float> output(target.data().size(), 0.0F);
    for (int z = 0; z < target.getHeight(); ++z)
        for (int x = 0; x < target.getWidth(); ++x) {
            const double wx = grid.originX + x * grid.spacingX;
            const double wz = grid.originZ + z * grid.spacingZ;
            double value = type == TerrainPolygonMaskType::Closed && impl_->nodes.size() > 2 && inside(wx, wz) ? 1 : 0;
            for (const auto& node : impl_->nodes) {
                const double u = (wx - (node.x - node.radius)) / (2 * node.radius);
                const double v = (wz - (node.z - node.radius)) / (2 * node.radius);
                if (u >= 0 && u <= 1 && v >= 0 && v <= 1) value += textureSample(brush, u, v);
                value = std::min(value, 1.0);
                if (value < 0.25) value = 0;
            }
            output[size_t(z) * target.getWidth() + x] = static_cast<float>(value);
        }
    return publish(target, std::move(output));
}
}  // namespace eve::procgen
