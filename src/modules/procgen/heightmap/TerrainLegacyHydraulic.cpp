#include "procgen/heightmap/TerrainErosion.h"
#include "procgen/heightmap/TerrainRasterInternal.h"

#include <array>

namespace eve::procgen {
Result<int> applyTerrainLegacyHydraulic(Heightmap& heights, Heightmap& sediment, const Heightmap& hardness,
                                        const Heightmap& rain, const TerrainLegacyHydraulicSettings& settings) {
    using namespace raster_detail;
    const auto sameShape = [&](const Heightmap& map) {
        return validRaster(map) && map.getWidth() == heights.getWidth() && map.getHeight() == heights.getHeight();
    };
    const auto unit = [](float value) { return value >= 0 && value <= 1; };
    if (&heights == &sediment || !validRaster(heights) || !sameShape(sediment) || !sameShape(hardness) ||
        !sameShape(rain) || settings.iterations < 0 || settings.rainFrequency <= 0 ||
        !std::isfinite(settings.sedimentDissolveRate) || !unit(settings.sedimentDissolveRate) ||
        !std::all_of(heights.data().begin(), heights.data().end(), unit) ||
        !std::all_of(hardness.data().begin(), hardness.data().end(), unit) ||
        std::any_of(rain.data().begin(), rain.data().end(), [](float value) { return value < 0; }))
        return invalid("terrain.legacyHydraulic: distinct matching rasters, normalized heights/hardness, "
                       "nonnegative rain/iterations, positive rain frequency and dissolve rate in [0,1] required");
    if (settings.iterations == 0) return Result<int>::success(0);

    constexpr double time = 0.2;
    const int width = heights.getWidth(), depth = heights.getHeight();
    const size_t count = heights.data().size();
    auto terrain = heights.data(), sedimentResult = sediment.data();
    const auto hardnessInput = hardness.data(), rainInput = rain.data();
    std::vector<double> water(count, 0), sedimentDiff(count, 0);
    std::vector<std::array<double, 4>> outflow(count);
    const auto index = [width](int x, int z) { return size_t(z) * size_t(width) + size_t(x); };

    for (int iteration = 0; iteration < settings.iterations; ++iteration) {
        if (iteration % settings.rainFrequency == 0)
            for (size_t i = 0; i < count; ++i) {
                water[i] += rainInput[i];
                if (!std::isfinite(water[i])) return invalid("terrain.legacyHydraulic: water exceeds finite range");
            }

        for (int z = 0; z < depth; ++z)
            for (int x = 0; x < width; ++x) {
                const size_t i = index(x, z);
                const std::array<size_t, 4> neighbor = {
                    index(std::max(x - 1, 0), z), index(std::min(x + 1, width - 1), z),
                    index(x, std::max(z - 1, 0)), index(x, std::min(z + 1, depth - 1))};
                double sum = 0;
                for (int direction = 0; direction < 4; ++direction) {
                    const double difference = water[i] + terrain[i] - water[neighbor[direction]] - terrain[neighbor[direction]];
                    outflow[i][direction] = std::max(0.0, outflow[i][direction] + difference);
                    sum += outflow[i][direction];
                }
                if (sum > 0) {
                    const double scale = std::clamp(water[i] / (sum * time), 0.0, 1.0);
                    for (double& flow : outflow[i]) flow *= scale;
                } else {
                    outflow[i].fill(0);
                }
            }

        auto nextWater = water;
        for (int z = 0; z < depth; ++z)
            for (int x = 0; x < width; ++x) {
                const size_t i = index(x, z);
                const double flowOut = outflow[i][0] + outflow[i][1] + outflow[i][2] + outflow[i][3];
                double flowIn = 0;
                if (x > 0) flowIn += outflow[index(x - 1, z)][1];
                if (x + 1 < width) flowIn += outflow[index(x + 1, z)][0];
                if (z > 0) flowIn += outflow[index(x, z - 1)][3];
                if (z + 1 < depth) flowIn += outflow[index(x, z + 1)][2];
                nextWater[i] = std::max(0.0, water[i] + (flowIn - flowOut) * time);
                if (!std::isfinite(nextWater[i])) return invalid("terrain.legacyHydraulic: water exceeds finite range");
            }
        water.swap(nextWater);

        for (int z = 0; z < depth; ++z)
            for (int x = 0; x < width; ++x) {
                const size_t i = index(x, z);
                double totalDrop = 0;
                for (int nz = std::max(z - 1, 0); nz <= std::min(z + 1, depth - 1); ++nz)
                    for (int nx = std::max(x - 1, 0); nx <= std::min(x + 1, width - 1); ++nx)
                        totalDrop += std::max(0.0, double(terrain[i]) - terrain[index(nx, nz)]);
                if (totalDrop == 0) continue;
                const double moved = water[i] * totalDrop * settings.sedimentDissolveRate * (1.0 - hardnessInput[i]);
                sedimentDiff[i] -= moved;
                for (int nz = std::max(z - 1, 0); nz <= std::min(z + 1, depth - 1); ++nz)
                    for (int nx = std::max(x - 1, 0); nx <= std::min(x + 1, width - 1); ++nx) {
                        const size_t n = index(nx, nz);
                        const double drop = double(terrain[i]) - terrain[n];
                        if (drop > 0) sedimentDiff[n] += moved * drop / totalDrop;
                    }
            }

        for (size_t i = 0; i < count; ++i) {
            const double nextSediment = double(sedimentResult[i]) + sedimentDiff[i];
            if (!isRepresentable(nextSediment)) return invalid("terrain.legacyHydraulic: sediment exceeds finite float range");
            sedimentResult[i] = float(nextSediment);
            terrain[i] = float(std::clamp(double(terrain[i]) + sedimentDiff[i], 0.0, 1.0));
            water[i] = std::clamp(water[i] - 1.0 / settings.rainFrequency, 0.0, 1.0);
            sedimentDiff[i] = 0;
        }
    }
    int changed = 0;
    for (size_t i = 0; i < count; ++i)
        changed += terrain[i] != heights.data()[i] || sedimentResult[i] != sediment.data()[i];
    heights.data().swap(terrain);
    sediment.data().swap(sedimentResult);
    return Result<int>::success(changed);
}
}  // namespace eve::procgen
