#pragma once

#include "common/Result.h"
#include "procgen/heightmap/PcgTerrainStreaming.h"

namespace eve::procgen {

/** @brief Pcg TerrainLoader activation modes retained by a biome controller. */
enum class PcgBiomeLoadMode { Disabled = 0, EditorSelected = 1, EditorAlways = 2, RuntimeAlways = 3 };

/** @brief Axis-aligned world-space loading bounds owned by one biome controller. */
struct PcgBiomeLoadingBounds {
    double centerX = 0.0;
    double centerY = 0.0;
    double centerZ = 0.0;
    double sizeX = 0.0;
    double sizeY = 0.0;
    double sizeZ = 0.0;
};

/**
 * @brief Native runtime state for Pcg BiomeController placement and automatic terrain loading bounds.
 * @details State updates validate a complete candidate before publishing it. The class owns no scene objects and
 *          invokes no callbacks; callers use tierAt() to feed their terrain residency scheduler.
 * @thread Game thread only.
 */
class EVENGINE_API_DOMAINS PcgBiomeController {
public:
    /** @brief Configure the controller and reproduce Pcg's automatic regular/impostor bound calculation. */
    [[nodiscard]] Result<void> configure(double x, double y, double z, double range,
                                         double impostorLoadingRange, PcgBiomeLoadMode mode);
    /** @brief Fit to one terrain: use its X/Z center, origin Y, and X half-size as the biome range. */
    [[nodiscard]] Result<void> fitToTerrain(double minX, double originY, double minZ,
                                            double sizeX, double sizeY, double sizeZ);
    /** @brief Fit to aggregate terrain bounds: use the full bounds center and X half-size. */
    [[nodiscard]] Result<void> fitToAllTerrains(double minX, double minY, double minZ,
                                                double sizeX, double sizeY, double sizeZ);
    /** @brief Return the representation requested for a world-space point. */
    [[nodiscard]] PcgTerrainStreamingTier tierAt(double x, double y, double z) const noexcept;
    /** @brief Return the regular terrain loading bounds. */
    [[nodiscard]] const PcgBiomeLoadingBounds& regularBounds() const noexcept { return regular_; }
    /** @brief Return the impostor terrain loading bounds. */
    [[nodiscard]] const PcgBiomeLoadingBounds& impostorBounds() const noexcept { return impostor_; }
    /** @brief Return the current biome radius. */
    [[nodiscard]] double range() const noexcept { return range_; }
    /** @brief Return the current TerrainLoader mode. */
    [[nodiscard]] PcgBiomeLoadMode loadMode() const noexcept { return mode_; }

private:
    [[nodiscard]] Result<void> publish(double x, double y, double z, double range,
                                       double impostorLoadingRange, PcgBiomeLoadMode mode);

    PcgBiomeLoadingBounds regular_{};
    PcgBiomeLoadingBounds impostor_{};
    double x_ = 0.0;
    double y_ = 0.0;
    double z_ = 0.0;
    double range_ = 0.0;
    double impostorLoadingRange_ = 0.0;
    PcgBiomeLoadMode mode_ = PcgBiomeLoadMode::Disabled;
};

}  // namespace eve::procgen
