#include "procgen/heightmap/TerrainErosion.h"
#include "procgen/heightmap/TerrainRasterInternal.h"

#include <array>

namespace eve::procgen {
Result<int> applyTerrainLegacyDistributedErosion(
    Heightmap& heights, const TerrainLegacyDistributedErosionSettings& settings) {
    using namespace raster_detail;
    if (!validRaster(heights) || !std::isfinite(settings.minimumThreshold) ||
        !std::isfinite(settings.maximumThreshold) || settings.minimumThreshold < 0 ||
        settings.maximumThreshold > 1 || settings.minimumThreshold >= settings.maximumThreshold ||
        settings.iterations < 0)
        return invalid("terrain.legacyDistributedErosion: finite thresholds in [0,1], minimum below maximum, "
                       "nonnegative iterations and finite terrain required");
    if (settings.iterations == 0 || heights.getWidth() < 3 || heights.getHeight() < 3)
        return Result<int>::success(0);
    const int width = heights.getWidth(), depth = heights.getHeight();
    auto current = heights.data();
    std::vector<double> diff(current.size(), 0);
    const auto at = [width](int x, int z) { return size_t(z) * size_t(width) + size_t(x); };
    for (int iteration = 0; iteration < settings.iterations; ++iteration) {
        for (int x = 1; x < width - 1; ++x)
            for (int z = 1; z < depth - 1; ++z) {
                const size_t source = at(x, z);
                const std::array<size_t, 4> neighbor = {at(x, z + 1), at(x - 1, z), at(x + 1, z), at(x, z - 1)};
                std::array<double, 4> drop{};
                double maximum = -std::numeric_limits<float>::max(), total = 0;
                for (int i = 0; i < 4; ++i) {
                    drop[i] = double(current[source]) - current[neighbor[i]];
                    if (drop[i] > 0) {
                        total += drop[i];
                        maximum = std::max(maximum, drop[i]);
                    }
                }
                if (maximum < settings.minimumThreshold || maximum > settings.maximumThreshold) continue;
                const double movement = maximum * 0.5, factor = movement / total;
                diff[source] -= movement;
                for (int i = 0; i < 4; ++i)
                    if (drop[i] > 0) diff[neighbor[i]] += factor * drop[i];
            }
        for (size_t i = 0; i < current.size(); ++i) {
            const double value = double(current[i]) + diff[i];
            if (!isRepresentable(value))
                return invalid("terrain.legacyDistributedErosion: output exceeds finite float range");
            current[i] = float(value);
            diff[i] = 0;
        }
    }
    return publish(heights, std::move(current));
}

namespace {
double pcgNormalizedSample(const Heightmap& map, double u, double v) {
    const double x = u * map.getWidth(), z = v * map.getHeight();
    const int x0 = std::min(int(x), map.getWidth() - 1), z0 = std::min(int(z), map.getHeight() - 1);
    const int x1 = std::min(x0 + 1, map.getWidth() - 1), z1 = std::min(z0 + 1, map.getHeight() - 1);
    const double tx = x - x0, tz = z - z0;
    return (1 - tx) * (1 - tz) * map.height(x0, z0) + (1 - tx) * tz * map.height(x0, z1) +
           tx * (1 - tz) * map.height(x1, z0) + tx * tz * map.height(x1, z1);
}
}  // namespace

Result<int> applyTerrainLegacySteepestErosion(Heightmap& heights, const Heightmap& hardness,
                                               const TerrainLegacySteepestErosionSettings& settings) {
    using namespace raster_detail;
    const auto unit = [](float value) { return value >= 0 && value <= 1; };
    if (!validRaster(heights) || !validRaster(hardness) ||
        !std::all_of(hardness.data().begin(), hardness.data().end(), unit) ||
        !std::isfinite(settings.talusMinimum) || !std::isfinite(settings.talusMaximum) ||
        settings.talusMinimum < 0 || settings.talusMaximum > 1 ||
        settings.talusMinimum > settings.talusMaximum || settings.iterations < 0)
        return invalid("terrain.legacySteepestErosion: finite terrain, normalized hardness/talus bounds and "
                       "nonnegative iterations required");
    if (settings.iterations == 0 || heights.getWidth() < 3 || heights.getHeight() < 3)
        return Result<int>::success(0);
    const int width = heights.getWidth(), depth = heights.getHeight();
    auto result = heights.data();
    const Heightmap hardnessInput = hardness;
    const auto at = [width](int x, int z) { return size_t(z) * size_t(width) + size_t(x); };
    for (int iteration = 0; iteration < settings.iterations; ++iteration)
        for (int x = 1; x < width - 1; ++x)
            for (int z = 1; z < depth - 1; ++z) {
                const size_t source = at(x, z);
                const std::array<size_t, 4> neighbor = {at(x, z + 1), at(x - 1, z), at(x + 1, z), at(x, z - 1)};
                double maximum = 0;
                int selected = -1;
                for (int i = 0; i < 4; ++i) {
                    const double drop = double(result[source]) - result[neighbor[i]];
                    if (drop > maximum) maximum = drop, selected = i;
                }
                if (maximum < settings.talusMinimum || maximum > settings.talusMaximum || selected < 0) continue;
                const double resistance = pcgNormalizedSample(hardnessInput, double(x) / width, double(z) / depth);
                const double movement = maximum * (1 - resistance) * 0.5;
                const double sourceValue = double(result[source]) - movement;
                const double targetValue = double(result[neighbor[selected]]) + movement;
                if (!isRepresentable(sourceValue) || !isRepresentable(targetValue))
                    return invalid("terrain.legacySteepestErosion: output exceeds finite float range");
                result[source] = float(sourceValue);
                result[neighbor[selected]] = float(targetValue);
            }
    return publish(heights, std::move(result));
}
}  // namespace eve::procgen
