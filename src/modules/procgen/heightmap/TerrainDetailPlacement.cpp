#include "procgen/heightmap/TerrainDetailPlacement.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include "procgen/PointSet.h"
#include "procgen/heightmap/TerrainDetailLayer.h"
#include "procgen/heightmap/TerrainRasterInternal.h"
namespace eve::procgen {
namespace {
uint64_t mix(uint64_t x) {
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}
double unit(uint64_t identity, uint64_t stream, uint32_t seed) {
    return double(mix(identity ^ stream ^ (uint64_t(seed) << 32)) >> 40) / 16777216.0;
}
double smooth(double value) { return value * value * (3.0 - 2.0 * value); }
double lattice(int64_t x, int64_t z, int32_t seed) {
    const uint64_t key = uint64_t(x) * UINT64_C(0x9e3779b185ebca87) ^ uint64_t(z) * UINT64_C(0xc2b2ae3d27d4eb4f) ^
                         uint64_t(uint32_t(seed));
    return double(mix(key) >> 40) / 16777216.0;
}
double detailColorNoise(double u, double v, float spread, int32_t seed) {
    if (spread == 0) return 0.5;
    const double  frequency = 1.0 + 31.0 * double(spread);
    const double  x = u * frequency, z = v * frequency;
    const int64_t x0 = int64_t(std::floor(x)), z0 = int64_t(std::floor(z));
    const double  tx = smooth(x - x0), tz = smooth(z - z0);
    const double  a = std::lerp(lattice(x0, z0, seed), lattice(x0 + 1, z0, seed), tx);
    const double  b = std::lerp(lattice(x0, z0 + 1, seed), lattice(x0 + 1, z0 + 1, seed), tx);
    return std::lerp(a, b, tz);
}
bool normalized(float value) { return std::isfinite(value) && value >= 0 && value <= 1; }
}  // namespace
Result<int> exportTerrainDetailPoints(PointSet& output, const TerrainDetailLayer& layer, const Heightmap& heights,
                                      const TerrainDetailPlacementSettings& s) {
    using namespace raster_detail;
    if (layer.getWidth() <= 0 || layer.getHeight() <= 0 || !validRaster(heights) || !std::isfinite(s.originX) ||
        !std::isfinite(s.originZ) || !std::isfinite(s.width) || s.width <= 0 || !std::isfinite(s.depth) ||
        s.depth <= 0 || !std::isfinite(s.heightScale) || !std::isfinite(s.minimumScale) || s.minimumScale <= 0 ||
        !std::isfinite(s.maximumScale) || s.maximumScale < s.minimumScale || !std::isfinite(s.minimumWidth) ||
        s.minimumWidth <= 0 || !std::isfinite(s.maximumWidth) || s.maximumWidth < s.minimumWidth ||
        !std::isfinite(s.minimumHeight) || s.minimumHeight <= 0 || !std::isfinite(s.maximumHeight) ||
        s.maximumHeight < s.minimumHeight || !normalized(s.healthyR) || !normalized(s.healthyG) ||
        !normalized(s.healthyB) || !normalized(s.healthyA) || !normalized(s.dryR) || !normalized(s.dryG) ||
        !normalized(s.dryB) || !normalized(s.dryA) || !normalized(s.noiseSpread) || s.namespaceId == 0 ||
        s.asset.empty() || s.maxPoints < 0)
        return invalid("terrain.detailPoints: valid layer, heights, domain, resource and budget required");
    int total = 0;
    for (int z = 0; z < layer.getHeight(); ++z)
        for (int x = 0; x < layer.getWidth(); ++x) {
            auto count = layer.sampleResource(s.densityNamespaceId, x, z);
            if (!count.ok()) return Result<int>::failure(count.status());
            if (count.value() > s.maxPoints - total) return invalid("terrain.detailPoints: point budget exceeded");
            total += count.value();
        }
    PointSet next;
    next.reserve(size_t(total));
    for (int z = 0; z < layer.getHeight(); ++z)
        for (int x = 0; x < layer.getWidth(); ++x) {
            auto count = layer.sampleResource(s.densityNamespaceId, x, z);
            if (!count.ok()) return Result<int>::failure(count.status());
            const auto cell = uint64_t(z) * uint64_t(layer.getWidth()) + uint64_t(x);
            for (int ordinal = 0; ordinal < count.value(); ++ordinal) {
                ProcgenPoint p;
                p.id = mix(mix(s.namespaceId + UINT64_C(0x9e3779b97f4a7c15)) ^ ((cell << 32) | uint64_t(ordinal + 1)));
                if (p.id == 0) return invalid("terrain.detailPoints: reserved zero identity; choose another namespace");
                const double u  = (x + unit(p.id, UINT64_C(0x706f736974696f58), s.seed)) / layer.getWidth();
                const double v  = (z + unit(p.id, UINT64_C(0x706f736974696f5a), s.seed)) / layer.getHeight();
                const double px = double(s.originX) + u * s.width, pz = double(s.originZ) + v * s.depth;
                const double py = sample(heights, u, v) * double(s.heightScale);
                if (!isRepresentable(px) || !isRepresentable(py) || !isRepresentable(pz))
                    return invalid("terrain.detailPoints: unrepresentable world position");
                p.x      = float(px);
                p.y      = float(py);
                p.z      = float(pz);
                p.yaw    = float(360 * unit(p.id, UINT64_C(0x726f746174696f6e), s.seed));
                p.scaleX = p.scaleY = p.scaleZ =
                    float(double(s.minimumScale) +
                          (double(s.maximumScale) - s.minimumScale) * unit(p.id, UINT64_C(0x7363616c65), s.seed));
                const double horizontal =
                    double(p.scaleX) * (double(s.minimumWidth) + (double(s.maximumWidth) - s.minimumWidth) *
                                                                     unit(p.id, UINT64_C(0x7769647468), s.seed));
                const double vertical =
                    double(p.scaleY) * (double(s.minimumHeight) + (double(s.maximumHeight) - s.minimumHeight) *
                                                                      unit(p.id, UINT64_C(0x686569676874), s.seed));
                if (!isRepresentable(horizontal) || !isRepresentable(vertical) || float(horizontal) <= 0 ||
                    float(vertical) <= 0)
                    return invalid("terrain.detailPoints: unrepresentable instance dimensions");
                p.scaleX = p.scaleZ   = float(horizontal);
                p.scaleY              = float(vertical);
                const double colorMix = detailColorNoise(u, v, s.noiseSpread, s.noiseSeed);
                p.colorR              = float(std::lerp(double(s.dryR), double(s.healthyR), colorMix));
                p.colorG              = float(std::lerp(double(s.dryG), double(s.healthyG), colorMix));
                p.colorB              = float(std::lerp(double(s.dryB), double(s.healthyB), colorMix));
                p.colorA              = float(std::lerp(double(s.dryA), double(s.healthyA), colorMix));
                p.seed                = uint32_t(mix(p.id ^ s.seed));
                const int row         = next.appendPoint(p);
                auto      asset       = next.trySetStringAttribute(row, "asset", s.asset);
                if (!asset.ok()) return Result<int>::failure(asset.status());
                auto source = next.trySetStringAttribute(row, "spawnNamespace", std::to_string(s.namespaceId));
                if (!source.ok()) return Result<int>::failure(source.status());
                auto cellAttribute = next.trySetIntAttribute(row, "detailCell", int64_t(cell));
                if (!cellAttribute.ok()) return Result<int>::failure(cellAttribute.status());
                auto ordinalAttribute = next.trySetIntAttribute(row, "detailOrdinal", ordinal);
                if (!ordinalAttribute.ok()) return Result<int>::failure(ordinalAttribute.status());
            }
        }
    static_assert(std::is_nothrow_move_assignable_v<PointSet>);
    output = std::move(next);
    return Result<int>::success(total);
}
}  // namespace eve::procgen
