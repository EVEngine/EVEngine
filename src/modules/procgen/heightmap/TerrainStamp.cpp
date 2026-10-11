#include "procgen/heightmap/TerrainStamp.h"

#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainRasterInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace eve::procgen {
using namespace raster_detail;

Result<int> blendTerrainMask(Heightmap& target, const Heightmap& source, TerrainMaskBlend mode, float strength,
                             bool invert) {
    if (!validRaster(target) || !validRaster(source) || target.getWidth() != source.getWidth() ||
        target.getHeight() != source.getHeight())
        return invalid("terrain.mask: finite nonempty rasters with matching dimensions required");
    if (!std::isfinite(strength) || strength < 0 || strength > 1 || mode < TerrainMaskBlend::Multiply ||
        mode > TerrainMaskBlend::Subtract)
        return invalid("terrain.mask: invalid blend mode or strength");
    std::vector<float> candidate(target.data().size());
    for (size_t i = 0; i < candidate.size(); ++i) {
        const double a = target.data()[i], b = invert ? 1.0 - source.data()[i] : source.data()[i];
        double       value = a;
        switch (mode) {
            case TerrainMaskBlend::Multiply: value = a * b; break;
            case TerrainMaskBlend::Maximum: value = std::max(a, b); break;
            case TerrainMaskBlend::Minimum: value = std::min(a, b); break;
            case TerrainMaskBlend::Add: value = a + b; break;
            case TerrainMaskBlend::Subtract: value = a - b; break;
        }
        value = std::lerp(a, value, double(strength));
        if (!isRepresentable(value)) return invalid("terrain.mask: result exceeds finite height range");
        candidate[i] = float(value);
    }
    return publish(target, std::move(candidate));
}

Result<int> applyTerrainStamp(Heightmap& target, const Heightmap& stamp, const TerrainStampSettings& s,
                              const Heightmap& localMask, const Heightmap& globalMask) {
    if (!validRaster(target) || !validRaster(stamp) || !validRaster(localMask) || !validRaster(globalMask))
        return invalid("terrain.stamp: finite nonempty rasters required");
    for (double v : {s.originX, s.originZ, s.spacingX, s.spacingZ, s.centerX, s.centerZ, s.width, s.depth, s.rotation,
                     double(s.amplitude), double(s.baseHeight), double(s.blendStrength)})
        if (!std::isfinite(v)) return invalid("terrain.stamp: settings must be finite");
    if (s.spacingX <= 0 || s.spacingZ <= 0 || s.width <= 0 || s.depth <= 0 || s.blendStrength < 0 ||
        s.blendStrength > 1 || s.operation < TerrainStampOperation::Raise ||
        s.operation > TerrainStampOperation::Subtract)
        return invalid("terrain.stamp: invalid operation, extent, spacing or blend strength");

    std::vector<float> candidate = target.data();
    const double       c = std::cos(s.rotation), sn = std::sin(s.rotation);
    for (int y = 0; y < target.getHeight(); ++y) {
        for (int x = 0; x < target.getWidth(); ++x) {
            const double dx = (s.originX + x * s.spacingX) - s.centerX;
            const double dz = (s.originZ + y * s.spacingZ) - s.centerZ;
            double       u  = (c * dx + sn * dz) / s.width + 0.5;
            double       v  = (-sn * dx + c * dz) / s.depth + 0.5;
            if (!std::isfinite(u) || !std::isfinite(v))
                return invalid("terrain.stamp: world-to-stamp coordinates overflow");
            // A quarter-turn must not lose an edge sample to trigonometric roundoff.
            constexpr double edgeTolerance = 8 * std::numeric_limits<double>::epsilon();
            if (u < -edgeTolerance || u > 1 + edgeTolerance || v < -edgeTolerance || v > 1 + edgeTolerance) continue;
            u                   = std::clamp(u, 0.0, 1.0);
            v                   = std::clamp(v, 0.0, 1.0);
            const double weight = std::clamp(sample(globalMask, u, v), 0.0, 1.0);
            if (weight == 0) continue;
            const double old   = target.height(x, y);
            const double level = s.baseHeight + s.amplitude * sample(stamp, u, v) * sample(localMask, u, v);
            double       next  = old;
            switch (s.operation) {
                case TerrainStampOperation::Raise: next = std::max(old, level); break;
                case TerrainStampOperation::Lower: next = std::min(old, level); break;
                case TerrainStampOperation::Set: next = level; break;
                case TerrainStampOperation::Blend: next = std::lerp(old, level, double(s.blendStrength)); break;
                case TerrainStampOperation::Add: next = old + level; break;
                case TerrainStampOperation::Subtract: next = old - level; break;
            }
            next = std::lerp(old, next, weight);
            if (!isRepresentable(next)) return invalid("terrain.stamp: result exceeds finite height range");
            candidate[size_t(y) * size_t(target.getWidth()) + size_t(x)] = float(next);
        }
    }
    return publish(target, std::move(candidate));
}
}  // namespace eve::procgen
