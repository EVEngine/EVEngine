#pragma once

#include "building/PlacementSystem.h"
#include "common/Result.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace eve::building {

/** @brief Immutable regularly sampled heightfield usable as an XY or XZ placement surface. */
class HeightfieldSurface final {
public:
    /** @brief Owning construction data. Samples are row-major plane-axis heights. */
    struct Config {
        int width = 0;
        int height = 0;
        float originX = 0.f;
        float originY = 0.f;
        float spacingX = 1.f;
        float spacingY = 1.f;
        float heightScale = 1.f;
        float heightOffset = 0.f;
        std::string surfaceId;
        uint64_t surfaceRevision = 0;
        std::vector<std::string> tags;
    };

    /**
     * @brief Validate and take ownership of an immutable heightfield.
     * @param config Dimensions, plane-space transform and stable surface identity.
     * @param samples Row-major samples; exactly width * height finite values are required.
     * @return A shareable immutable surface or a structured validation failure.
     */
    [[nodiscard]] static eve::Result<std::shared_ptr<const HeightfieldSurface>>
    create(Config config, std::vector<float> samples);

    /**
     * @brief Bilinearly sample height and its continuous local differential frame.
     * @param world Supplies whether plane coordinates map to world XY or XZ.
     * @param planeX First grid-plane coordinate.
     * @param planeY Second grid-plane coordinate.
     * @return An owning hit, or NotFound when the coordinate is outside the heightfield.
     * @thread Safe to call concurrently after construction. No callbacks are invoked.
     */
    [[nodiscard]] eve::Result<PlacementSystem::PlacementHit>
    sample(const PlacementWorld &world, float planeX, float planeY) const;

    /** @brief Return the immutable construction configuration. */
    const Config &config() const { return config_; }

private:
    HeightfieldSurface(Config config, std::vector<float> samples);

    Config config_;
    std::vector<float> samples_;
};

}  // namespace eve::building
