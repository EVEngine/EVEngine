#include "procgen/heightmap/TerrainTerraceRemover.h"
#include "procgen/heightmap/TerrainRasterInternal.h"
#include "procgen/texture/NoiseField.h"

#include <array>

namespace eve::procgen {
namespace {
bool valid(const TerrainTerraceRemovalSettings& s) {
    const std::array<float, 7> values = {s.perlinScale, s.perlinStrength, s.slopeTerraceThreshold,
                                         s.flatThreshold, s.verticalGradientThreshold,
                                         s.minimumTerraceThreshold, s.maximumTerraceThreshold};
    return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value) && value >= 0; }) &&
           s.minimumTerraceThreshold <= s.maximumTerraceThreshold && s.noiseSeed >= 0;
}
bool matching(const Heightmap& a, const Heightmap& b) {
    return raster_detail::validRaster(a) && raster_detail::validRaster(b) && a.getWidth() == b.getWidth() &&
           a.getHeight() == b.getHeight();
}
}

Result<int> analyzeTerrainTerraces(Heightmap& target, const Heightmap& source,
                                    const TerrainTerraceRemovalSettings& s) {
    using namespace raster_detail;
    if (&target == &source || !matching(target, source) || !valid(s))
        return invalid("terrain.terraceRemoval.analyze: distinct matching finite rasters and valid thresholds required");
    const int width = source.getWidth(), height = source.getHeight();
    std::vector<float> output(size_t(width) * height, float(TerrainTerraceClass::Black));
    auto at = [width](int x, int y) { return size_t(y) * width + x; };
    for (int y = 1; y < height - 1; ++y)
        for (int x = 1; x < width - 1; ++x) {
            const float current = source.data()[at(x, y)];
            const float gradientX = std::abs(current - source.data()[at(x - 1, y)]) +
                                    std::abs(current - source.data()[at(x + 1, y)]);
            const float gradientY = std::abs(current - source.data()[at(x, y - 1)]) +
                                    std::abs(current - source.data()[at(x, y + 1)]);
            const float gradient = gradientX + gradientY;
            const float vertical = std::abs(source.data()[at(x, y - 1)] - source.data()[at(x, y + 1)]);
            bool slope = false;
            if (gradient < s.slopeTerraceThreshold && vertical < s.verticalGradientThreshold)
                slope = std::abs(source.data()[at(x - 1, y)] - current) < s.slopeTerraceThreshold &&
                        std::abs(source.data()[at(x + 1, y)] - current) < s.slopeTerraceThreshold &&
                        std::abs(source.data()[at(x, y - 1)] - current) < s.slopeTerraceThreshold &&
                        std::abs(source.data()[at(x, y + 1)] - current) < s.slopeTerraceThreshold;
            const bool flat = gradient < s.flatThreshold;
            const bool mountain = vertical > s.verticalGradientThreshold;
            const bool terrace = vertical >= s.minimumTerraceThreshold && vertical <= s.maximumTerraceThreshold;
            output[at(x, y)] = float(terrace ? TerrainTerraceClass::Green
                                             : flat ? TerrainTerraceClass::Black
                                                    : mountain ? TerrainTerraceClass::Red
                                                               : slope ? TerrainTerraceClass::Blue
                                                                       : TerrainTerraceClass::Black);
        }
    return publish(target, std::move(output));
}

Result<int> removeTerrainTerraces(Heightmap& target, const Heightmap& source, const Heightmap& classes,
                                   const TerrainTerraceRemovalSettings& s) {
    using namespace raster_detail;
    if (&target == &classes || !matching(target, source) || !matching(source, classes) || !valid(s))
        return invalid("terrain.terraceRemoval.apply: matching finite rasters, distinct class output and valid settings required");
    const int width = source.getWidth(), height = source.getHeight();
    const auto original = source.data();
    auto index = [width](int x, int y) { return size_t(y) * width + x; };
    std::vector<unsigned char> mask(original.size(), 0);
    for (int y = 1; y < height - 1; ++y)
        for (int x = 1; x < width - 1; ++x) {
            const float category = classes.data()[index(x, y)];
            if ((s.excludeRed && category == float(TerrainTerraceClass::Red)) ||
                (s.excludeBlack && category == float(TerrainTerraceClass::Black)))
                continue;
            const float current = original[index(x, y)];
            mask[index(x, y)] = std::abs(current - original[index(x + 1, y)]) < 0.01F &&
                                std::abs(current - original[index(x, y + 1)]) < 0.01F;
        }
    auto smooth = original;
    for (int y = 1; y < height - 1; ++y)
        for (int x = 1; x < width - 1; ++x)
            if (mask[index(x, y)]) {
                double sum = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) sum += original[index(x + dx, y + dy)];
                smooth[index(x, y)] = float(sum / 9.0);
            }
    auto noisy = smooth;
    NoiseField noise{static_cast<unsigned int>(s.noiseSeed)};
    const float workflowScale = s.terrainWorkflow ? 0.00042F / 0.0035F : 1.F;
    for (int y = 1; y < height - 1; ++y)
        for (int x = 1; x < width - 1; ++x)
            if (mask[index(x, y)]) {
                const float dx = std::abs(smooth[index(x + 1, y)] - smooth[index(x - 1, y)]);
                const float dy = std::abs(smooth[index(x, y + 1)] - smooth[index(x, y - 1)]);
                const float gradient = std::sqrt(dx * dx + dy * dy);
                const float strength = s.perlinStrength * workflowScale * (1.F - std::clamp(gradient * 10.F, 0.F, 1.F));
                const double value = double(smooth[index(x, y)]) + noise.perlinNoise(x * s.perlinScale, y * s.perlinScale) * strength;
                if (!isRepresentable(value)) return invalid("terrain.terraceRemoval.apply: noise result exceeds float range");
                noisy[index(x, y)] = float(value);
            }
    auto directional = noisy;
    constexpr int weights[3][3] = {{1, 2, 1}, {2, 4, 2}, {1, 2, 1}};
    for (int y = 1; y < height - 1; ++y)
        for (int x = 1; x < width - 1; ++x) {
            double sum = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) sum += noisy[index(x + dx, y + dy)] * weights[dy + 1][dx + 1];
            directional[index(x, y)] = float(sum / 16.0);
        }
    std::array<double, 25> kernel{};
    double kernelSum = 0;
    for (int ky = -2; ky <= 2; ++ky)
        for (int kx = -2; kx <= 2; ++kx) {
            const double weight = std::exp(-(kx * kx + ky * ky) / 2.0);
            kernel[size_t(ky + 2) * 5 + kx + 2] = weight; kernelSum += weight;
        }
    auto output = directional;
    for (int y = 2; y < height - 2; ++y)
        for (int x = 2; x < width - 2; ++x) {
            double sum = 0;
            for (int ky = -2; ky <= 2; ++ky)
                for (int kx = -2; kx <= 2; ++kx)
                    sum += directional[index(x + kx, y + ky)] * kernel[size_t(ky + 2) * 5 + kx + 2];
            const double value = sum / kernelSum;
            if (!isRepresentable(value)) return invalid("terrain.terraceRemoval.apply: filtered result exceeds float range");
            output[index(x, y)] = float(value);
        }
    return publish(target, std::move(output));
}
}
