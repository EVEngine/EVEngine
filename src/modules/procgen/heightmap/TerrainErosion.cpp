#include "procgen/heightmap/TerrainErosion.h"
#include "procgen/heightmap/TerrainRasterInternal.h"

#include <array>

namespace eve::procgen {
Result<int> applyTerrainThermal(Heightmap& heights, Heightmap& sediment, const TerrainThermalSettings& settings) {
    using namespace raster_detail;
    if (&heights == &sediment || !validRaster(heights) || !validRaster(sediment) ||
        heights.getWidth() != sediment.getWidth() || heights.getHeight() != sediment.getHeight() ||
        std::any_of(heights.data().begin(), heights.data().end(), [](float h) { return h < 0; }) ||
        !std::isfinite(settings.spacingX) || settings.spacingX <= 0 || !std::isfinite(settings.spacingZ) ||
        settings.spacingZ <= 0 || !std::isfinite(settings.heightScale) || settings.heightScale <= 0 ||
        !std::isfinite(settings.reposeSlope) || settings.reposeSlope < 0 || !std::isfinite(settings.dt) ||
        settings.dt < 0 || settings.iterations < 0)
        return invalid(
            "terrain.thermal: distinct matching rasters, nonnegative heights, positive scales and nonnegative "
            "simulation controls required");
    if (settings.iterations == 0 || settings.dt == 0) return Result<int>::success(0);
    const int          width = heights.getWidth(), height = heights.getHeight();
    const double       diagonal       = std::hypot(double(settings.spacingX), double(settings.spacingZ));
    Heightmap          current        = heights;
    auto               sedimentResult = sediment.data();
    std::vector<float> next(heights.data().size());
    for (int iteration = 0; iteration < settings.iterations; ++iteration) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int                  left = std::max(x - 1, 0), right = std::min(x + 1, width - 1);
                const int                  top = std::min(y + 1, height - 1), bottom = std::max(y - 1, 0);
                const size_t               index     = size_t(y) * width + x;
                const double               original  = current.data()[index];
                const std::array<float, 8> neighbors = {current.height(left, y),      current.height(right, y),
                                                        current.height(x, top),       current.height(x, bottom),
                                                        current.height(left, top),    current.height(right, top),
                                                        current.height(left, bottom), current.height(right, bottom)};
                double                     sum       = 0;
                for (int n = 0; n < 8; ++n) {
                    const double dh       = double(settings.heightScale) * (original - neighbors[n]);
                    const double distance = n < 2 ? settings.spacingX : n < 4 ? settings.spacingZ : diagonal;
                    if (std::abs(dh / distance) > settings.reposeSlope) sum += dh * (n < 4 ? 1.0 : 0.707);
                }
                const double movement  = std::clamp(0.0625 * settings.dt * sum, -0.5 * original, 0.5 * original);
                const double newHeight = original - movement, newSediment = double(sedimentResult[index]) + movement;
                if (!isRepresentable(newHeight) || !isRepresentable(newSediment))
                    return invalid("terrain.thermal: output exceeds finite float range");
                next[index]           = float(newHeight);
                sedimentResult[index] = float(newSediment);
            }
        }
        current.data().swap(next);
    }
    int changed = 0;
    for (size_t i = 0; i < sedimentResult.size(); ++i)
        changed += heights.data()[i] != current.data()[i] || sediment.data()[i] != sedimentResult[i];
    heights.data().swap(current.data());
    sediment.data().swap(sedimentResult);
    return Result<int>::success(changed);
}
}  // namespace eve::procgen
