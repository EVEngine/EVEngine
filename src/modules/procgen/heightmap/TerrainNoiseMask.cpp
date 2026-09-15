#include "procgen/heightmap/TerrainNoiseMask.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>
#include "procgen/heightmap/TerrainRasterInternal.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "procgen/texture/NoiseField.h"

namespace eve::procgen {
using namespace raster_detail;
namespace {
double baseNoise(const NoiseField& field, TerrainNoiseType type, double x, double z) {
    const double perlin = field.perlinNoise(float(x), float(z));
    switch (type) {
        case TerrainNoiseType::Perlin: return perlin;
        case TerrainNoiseType::Value: return field.valueNoise(float(x), float(z));
        case TerrainNoiseType::Billow: return std::abs(perlin * 2 - 1);
        case TerrainNoiseType::Ridge: return 1 - std::abs(perlin * 2 - 1);
        case TerrainNoiseType::Voronoi: {
            const int ix = int(std::floor(x)), iz = int(std::floor(z));
            double best = 1;
            for (int dz = -2; dz <= 2; ++dz)
                for (int dx = -2; dx <= 2; ++dx) {
                    const double px = dx + field.hash01(ix + dx, iz + dz) - (x - ix);
                    const double pz = dz + field.hash01(ix + dx + 7919, iz + dz) - (z - iz);
                    best = std::min(best, px * px + pz * pz);
                }
            return best;
        }
    }
    return 0;
}

double fractal(const NoiseField& field, TerrainNoiseType type, double x, double z,
               const TerrainNoiseMaskSettings& s) {
    const int whole = int(std::floor(s.octaves));
    const double fraction = s.octaves - whole;
    double sum = 0, amplitude = s.amplitude, frequency = s.frequency;
    for (int i = 0; i < whole; ++i) {
        sum += amplitude * baseNoise(field, type, x * frequency, z * frequency);
        amplitude *= s.persistence;
        frequency *= s.lacunarity;
    }
    if (fraction > 0) sum += fraction * amplitude * baseNoise(field, type, x * frequency, z * frequency);
    return sum;
}
}  // namespace

Result<int> generateTerrainNoiseMask(Heightmap& target, const Heightmap& input, const Heightmap& curve,
                                     const TerrainNoiseMaskSettings& s, TerrainMaskBlend mode) {
    if (!validRaster(target) || !validRaster(input) || !validRaster(curve) ||
        target.getWidth() != input.getWidth() || target.getHeight() != input.getHeight() || curve.getHeight() != 1 ||
        s.type < TerrainNoiseType::Perlin || s.type > TerrainNoiseType::Voronoi ||
        mode < TerrainMaskBlend::Multiply || mode > TerrainMaskBlend::Subtract || s.scaleX == 0 || s.scaleZ == 0 ||
        s.octaves < 0 || s.octaves > 16 || s.warpIterations < 0 || s.warpIterations > 16)
        return invalid("terrain.noiseMask: finite compatible rasters and valid noise controls required");
    for (float value : {s.translationX, s.translationZ, s.scaleX, s.scaleZ, s.rotation, s.octaves, s.amplitude,
                        s.frequency, s.persistence, s.lacunarity, s.warpIterations, s.warpStrength,
                        s.warpOffsetX, s.warpOffsetZ})
        if (!std::isfinite(value)) return invalid("terrain.noiseMask: finite controls required");
    NoiseField field;
    field.seed = s.seed;
    const double cosine = std::cos(s.rotation), sine = std::sin(s.rotation);
    std::vector<float> output(target.data().size());
    for (int z = 0; z < target.getHeight(); ++z)
        for (int x = 0; x < target.getWidth(); ++x) {
            const double ux = (x + 0.5) / target.getWidth() - 0.5;
            const double uz = (z + 0.5) / target.getHeight() - 0.5;
            double px = (cosine * ux - sine * uz) * s.scaleX + s.translationX;
            double pz = (sine * ux + cosine * uz) * s.scaleZ + s.translationZ;
            const int warpWhole = int(std::floor(s.warpIterations));
            const double warpFraction = s.warpIterations - warpWhole;
            for (int i = 0; i < warpWhole + (warpFraction > 0); ++i) {
                const double qx = fractal(field, s.type, px, pz, s);
                const double qz = fractal(field, s.type, px + s.warpOffsetX, pz + s.warpOffsetZ, s);
                const double weight = i == warpWhole ? warpFraction : 1;
                px += weight * s.warpStrength * qx;
                pz += weight * s.warpStrength * qz;
            }
            const double transformed = curveSample(curve, fractal(field, s.type, px, pz, s));
            const size_t index = size_t(z) * target.getWidth() + x;
            const double old = input.data()[index];
            double result = old;
            switch (mode) {
                case TerrainMaskBlend::Multiply: result = old * transformed; break;
                case TerrainMaskBlend::Maximum: result = std::max(old, transformed); break;
                case TerrainMaskBlend::Minimum: result = std::min(old, transformed); break;
                case TerrainMaskBlend::Add: result = old + transformed; break;
                case TerrainMaskBlend::Subtract: result = old - transformed; break;
            }
            if (!isRepresentable(result)) return invalid("terrain.noiseMask: output exceeds finite float range");
            output[index] = float(result);
        }
    return publish(target, std::move(output));
}
}  // namespace eve::procgen
