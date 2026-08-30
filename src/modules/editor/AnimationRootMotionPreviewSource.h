#pragma once

/** @file AnimationRootMotionPreviewSource.h @brief Optional AnimClip-backed root-motion preview adapter. */

#include "action/ActionPreview.h"

#include <cstdint>

namespace eve::editor {

/**
 * @brief Samples root motion from the animation module's registered clip for an action URI.
 *
 * The source stores only a root-bone index. Each call resolves the clip from
 * AnimClipRegistry and retains no pointer after returning, so clip destruction
 * and hot reload remain observable through the registry. The class is usable
 * in trimmed builds; without animation it returns Unsupported.
 */
class AnimationRootMotionPreviewSource final : public action::IActionRootMotionSource {
public:
    /** @brief Construct a sampler for one zero-based root-bone track. */
    explicit AnimationRootMotionPreviewSource(std::uint32_t rootBone = 0) noexcept : rootBone_(rootBone) {}

    /** @brief Resolve one registered clip and sample its root track across the requested action duration. */
    [[nodiscard]] Result<std::vector<action::ActionPreviewPoint3>> sampleRootMotion(
        std::string_view animationUri, Duration duration, std::uint32_t sampleCount) const override;

private:
    std::uint32_t rootBone_ = 0;
};

}  // namespace eve::editor
