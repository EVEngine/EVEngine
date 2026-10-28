#include <numbers>
#include "procgen/heightmap/TerrainErosion.h"
#include "procgen/heightmap/TerrainRasterInternal.h"
#include "procgen/heightmap/TerrainWaterField.h"

namespace eve::procgen {
Result<int> applyTerrainSediment(Heightmap& heights, Heightmap& sediment, const Heightmap& velocityX,
                                 const Heightmap& velocityZ, const TerrainWaterSettings& water,
                                 const TerrainSedimentSettings& s) {
    using namespace raster_detail;
    auto sameShape = [&](const Heightmap& map) {
        return validRaster(map) && map.getWidth() == heights.getWidth() && map.getHeight() == heights.getHeight();
    };
    auto positive    = [](float v) { return std::isfinite(v) && v > 0; };
    auto nonnegative = [](float v) { return std::isfinite(v) && v >= 0; };
    if (&heights == &sediment || !validRaster(heights) || !sameShape(sediment) || !sameShape(velocityX) ||
        !sameShape(velocityZ) ||
        std::any_of(heights.data().begin(), heights.data().end(), [](float v) { return v < 0; }) ||
        !positive(water.spacingX) || !positive(water.spacingZ) || !nonnegative(water.dt) || !std::isfinite(s.effect) ||
        !nonnegative(s.depositRate) || !nonnegative(s.bankDeposit) || !nonnegative(s.bedDeposit))
        return invalid(
            "terrain.sediment: distinct matching finite outputs, nonnegative heights and valid coefficients required");
    const int          width = heights.getWidth(), height = heights.getHeight();
    std::vector<float> nextHeight(heights.data().size()), nextSediment(sediment.data().size());
    auto               sampleSediment = [&](int x, int z) -> double {
        return x < 0 || z < 0 || x >= width || z >= height ? 0 : sediment.height(x, z);
    };
    for (int z = 0; z < height; ++z)
        for (int x = 0; x < width; ++x) {
            const size_t i    = size_t(z) * width + x;
            const int    left = std::max(x - 1, 0), right = std::min(x + 1, width - 1);
            const int    top = std::max(z - 1, 0), bottom = std::min(z + 1, height - 1);
            const double dx =
                (double(heights.height(right, bottom)) + 2.0 * heights.height(right, z) + heights.height(right, top) -
                 heights.height(left, bottom) - 2.0 * heights.height(left, z) - heights.height(left, top)) /
                8;
            const double dz =
                (double(heights.height(left, top)) + 2.0 * heights.height(x, top) + heights.height(right, top) -
                 heights.height(left, bottom) - 2.0 * heights.height(x, bottom) - heights.height(right, bottom)) /
                8;
            // Source normal is normalize(dx,length(dx,dz),dz), giving this Y component for nonzero gradient.
            const double slope     = dx == 0 && dz == 0 ? 0 : std::numbers::sqrt2 / 2;
            const double speed     = std::hypot(double(velocityX.data()[i]), double(velocityZ.data()[i]));
            const double deposited = double(water.dt) * std::lerp(double(s.bankDeposit), double(s.bedDeposit), slope) *
                                     s.depositRate / std::max(speed, 0.05);
            const double reactedSediment = std::max(double(sediment.data()[i]) - s.effect * deposited, 0.0);
            const double newHeight       = std::max(double(heights.data()[i]) + s.effect * deposited, 0.0);
            const double bx =
                std::clamp(double(x) - double(water.dt) * velocityX.data()[i] * water.spacingX, 0.0, double(width - 1));
            const double bz = std::clamp(double(z) - double(water.dt) * velocityZ.data()[i] * water.spacingZ, 0.0,
                                         double(height - 1));
            const int    x0 = int(bx), z0 = int(bz), x1 = x0 + 1, z1 = z0 + 1;
            const double sx = (bx - x0) / width, tz = (bz - z0) / height;
            // Preserve the source shader's exact weights/indexing; this is not standard bilinear advection.
            const double transported = (1 - sx) * ((1 - tz) * sampleSediment(x0, z0) + tz * sampleSediment(x0, z1)) +
                                       sx * (tz * sampleSediment(x1, x0) + tz * sampleSediment(x1, z1));
            const double newSediment = reactedSediment + transported;
            if (!isRepresentable(newHeight) || !isRepresentable(newSediment))
                return invalid("terrain.sediment: output exceeds finite float range");
            nextHeight[i]   = float(newHeight);
            nextSediment[i] = float(newSediment);
        }
    int changed = 0;
    for (size_t i = 0; i < nextHeight.size(); ++i)
        changed += nextHeight[i] != heights.data()[i] || nextSediment[i] != sediment.data()[i];
    heights.data().swap(nextHeight);
    sediment.data().swap(nextSediment);
    return Result<int>::success(changed);
}
}  // namespace eve::procgen
