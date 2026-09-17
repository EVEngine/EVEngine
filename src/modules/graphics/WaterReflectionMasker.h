#pragma once

#include "common/Result.h"

#include <cstdint>
#include <vector>

namespace ssq { class Table; }
namespace eve::image { class ImageData; }

namespace eve::graphics {

struct WaterPlanarReflectionSettings;

/** @brief Pcg ReflectionMasker channel selection. */
enum class WaterReflectionMaskChannel { R = 0, G = 1, B = 2, A = 3, RGBA = 4 };
/** @brief Observable reflection-state transition after one player-position sample. */
enum class WaterReflectionMaskTransition { Unchanged = 0, Enabled = 1, Disabled = 2 };

/**
 * @brief Own a CPU snapshot of Pcg's planar-reflection mask and evaluate player movement against it.
 * @details setMask copies ImageData pixels. evaluate mutates only settings.enabled and the retained height-feature
 *          state when the sampled boolean changes; no image or terrain pointer is retained.
 * @thread Game thread only.
 */
class WaterReflectionMasker {
public:
    /** @brief Configure inclusive thresholds, channel selection and initial scene-profile state. */
    [[nodiscard]] Result<void> configure(WaterReflectionMaskChannel channel, float minimum, float maximum,
                                         bool reflectionsEnabled);
    /** @brief Replace the owned mask snapshot from any readable ImageData format. */
    [[nodiscard]] Result<void> setMask(const image::ImageData& mask);
    /** @brief Clear the mask; subsequent evaluations disable reflections when state changes. */
    void clearMask() noexcept;
    /** @brief Sample a player X/Z position in one terrain's origin and size and update planar reflection settings. */
    [[nodiscard]] Result<WaterReflectionMaskTransition> evaluate(float playerX, float playerZ,
        float terrainX, float terrainZ, float terrainWidth, float terrainDepth,
        WaterPlanarReflectionSettings& settings);
    /** @brief Return the last published mask state. */
    bool getEnabled() const noexcept { return enabled_; }
    /** @brief Return Pcg's coupled height-feature state. */
    bool getHeightFeaturesEnabled() const noexcept { return heightFeaturesEnabled_; }
    /** @brief Return the most recently sampled pixel X or -1 when no in-bounds pixel was sampled. */
    int getSampleX() const noexcept { return sampleX_; }
    /** @brief Return the most recently sampled pixel Y or -1 when no in-bounds pixel was sampled. */
    int getSampleY() const noexcept { return sampleY_; }

private:
    struct Pixel { float r=0.f,g=0.f,b=0.f,a=0.f; };
    WaterReflectionMaskChannel channel_ = WaterReflectionMaskChannel::R;
    float minimum_ = .35f;
    float maximum_ = 1.f;
    int width_ = 0;
    int height_ = 0;
    std::vector<Pixel> pixels_;
    bool enabled_ = false;
    bool heightFeaturesEnabled_ = false;
    int sampleX_ = -1;
    int sampleY_ = -1;
};

/** @brief Register Pcg reflection-mask bindings. */
void exposeWaterReflectionMaskerBindings(ssq::Table& table);

}  // namespace eve::graphics
