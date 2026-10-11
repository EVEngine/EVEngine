#include "procgen/heightmap/TerrainImageMask.h"
#include <array>
#include "procgen/heightmap/TerrainRasterInternal.h"
#include "procgen/heightmap/TerrainStamp.h"

namespace eve::procgen {
namespace {
std::array<double, 3> lab(double r, double g, double b) {
    auto linear    = [](double v) { return 100 * (v > 0.04045 ? std::pow((v + 0.055) / 1.055, 2.4) : v / 12.92); };
    r              = linear(r);
    g              = linear(g);
    b              = linear(b);
    auto         f = [](double v) { return v > 0.008856 ? std::pow(v, 1.0 / 3) : 7.787 * v + 16.0 / 116; };
    const double x = f((r * 0.4124 + g * 0.3576 + b * 0.1805) / 95.047);
    const double y = f((r * 0.2126 + g * 0.7152 + b * 0.0722) / 100);
    const double z = f((r * 0.0193 + g * 0.1192 + b * 0.9505) / 108.883);
    return {116 * y - 16, 500 * (x - y), 200 * (y - z)};
}
}  // namespace
Result<int> generateTerrainImageMask(Heightmap& target, const Heightmap& input, const Heightmap& red,
                                     const Heightmap& green, const Heightmap& blue, const Heightmap& alpha,
                                     const Heightmap& curve, const TerrainImageMaskSettings& s, TerrainMaskBlend mode) {
    using namespace raster_detail;
    auto same = [](const Heightmap& a, const Heightmap& b) {
        return a.getWidth() == b.getWidth() && a.getHeight() == b.getHeight();
    };
    if (!validRaster(target) || !validRaster(input) || !validRaster(red) || !validRaster(green) || !validRaster(blue) ||
        !validRaster(alpha) || !validRaster(curve) || !same(target, input) || !same(red, green) || !same(red, blue) ||
        !same(red, alpha) || curve.getHeight() != 1 || s.scaleX == 0 || s.scaleZ == 0 || s.accuracy < 0 ||
        s.accuracy > 1 || s.filter < TerrainImageFilter::MaximumRgb || s.filter > TerrainImageFilter::Alpha ||
        mode < TerrainMaskBlend::Multiply || mode > TerrainMaskBlend::Subtract)
        return invalid("terrain.imageMask: finite compatible planes, curve and valid settings required");
    for (float v : {s.offsetX, s.offsetZ, s.scaleX, s.scaleZ, s.rotation, s.red, s.green, s.blue, s.accuracy})
        if (!std::isfinite(v)) return invalid("terrain.imageMask: finite settings required");
    const auto         selected = lab(s.red, s.green, s.blue);
    const double       cosine = std::cos(double(s.rotation)), sine = std::sin(double(s.rotation));
    std::vector<float> output(target.data().size());
    for (int z = 0; z < target.getHeight(); ++z)
        for (int x = 0; x < target.getWidth(); ++x) {
            const double px = ((x + 0.5) / target.getWidth() - 0.5 - s.offsetX) / s.scaleX;
            const double pz = ((z + 0.5) / target.getHeight() - 0.5 - s.offsetZ) / s.scaleZ;
            double       u = cosine * px + sine * pz + 0.5, v = -sine * px + cosine * pz + 0.5;
            if (s.tiling) {
                auto wrap = [](double q) { return q > 0 ? std::fmod(q, 1.0) : 1 + std::fmod(q, 1.0); };
                u         = wrap(u);
                v         = wrap(v);
            }
            double filter = 0;
            if (u >= 0 && u <= 1 && v >= 0 && v <= 1) {
                const double r = textureSample(red, u, v), g = textureSample(green, u, v),
                             b = textureSample(blue, u, v);
                switch (s.filter) {
                    case TerrainImageFilter::MaximumRgb: filter = std::max({r, g, b}); break;
                    case TerrainImageFilter::Red: filter = r; break;
                    case TerrainImageFilter::Green: filter = g; break;
                    case TerrainImageFilter::Blue: filter = b; break;
                    case TerrainImageFilter::Alpha: filter = textureSample(alpha, u, v); break;
                    case TerrainImageFilter::ColorSelection: {
                        double difference = 0;
                        if (!(std::abs(r - s.red) < 0.00001 && std::abs(g - s.green) < 0.00001 &&
                              std::abs(b - s.blue) < 0.00001)) {
                            const auto color = lab(r, g, b);
                            difference       = std::clamp(
                                std::hypot(color[0] - selected[0], color[1] - selected[1], color[2] - selected[2]), 0.0,
                                100.0);
                        }
                        if (difference < (1 - double(s.accuracy)) * 100) filter = 1 - difference / 100 * s.accuracy;
                        break;
                    }
                }
            }
            const double transformed = curveSample(curve, filter);
            const size_t i           = size_t(z) * target.getWidth() + x;
            const double old         = input.data()[i];
            double       result      = old;
            switch (mode) {
                case TerrainMaskBlend::Multiply: result = old * transformed; break;
                case TerrainMaskBlend::Maximum: result = std::max(old, transformed); break;
                case TerrainMaskBlend::Minimum: result = std::min(old, transformed); break;
                case TerrainMaskBlend::Add: result = old + transformed; break;
                case TerrainMaskBlend::Subtract: result = old - transformed; break;
            }
            if (!isRepresentable(result)) return invalid("terrain.imageMask: output exceeds finite float range");
            output[i] = float(result);
        }
    return publish(target, std::move(output));
}

Result<int> applyTerrainGlobalSpawnerMask(Heightmap& target, const Heightmap& input, const Heightmap& source,
                                          const Heightmap& curve, const TerrainImageMaskSettings& settings,
                                          TerrainMaskBlend mode) {
    auto scalarSettings   = settings;
    scalarSettings.filter = TerrainImageFilter::Red;
    return generateTerrainImageMask(target, input, source, source, source, source, curve, scalarSettings, mode);
}
}  // namespace eve::procgen
