#pragma once

#include <array>

namespace eve::graphics {

class Canvas;
class Graphics;
class Shader;
class Texture;

/** @brief Cross-backend min/max hardware-depth pyramid packed into one texture atlas. */
class DepthPyramid {
public:
    /** @brief Create the depth reduction shader. */
    explicit DepthPyramid(Graphics *gfx);

    /**
     * @brief Build the min/max hierarchy from a hardware depth texture.
     * @param depth Hardware depth source, near=0 and far=1.
     * @param maxLevels Maximum hierarchy levels to build for the active quality budget.
     * @return RG min/max depth atlas; level zero starts at x=0 and each lower
     * level is packed directly to the right of its predecessor.
     */
    Texture *build(Texture *depth, int maxLevels = 8);

    /** @brief Number of valid levels in the last build. */
    int getLevelCount() const { return levelCount_; }
    /** @brief Source depth width represented by level zero. */
    int getSourceWidth() const { return sourceWidth_; }
    /** @brief Source depth height represented by level zero. */
    int getSourceHeight() const { return sourceHeight_; }

private:
    static constexpr int kMaxLevels = 8;
    void ensureTargets(int width, int height, int maxLevels);

    Graphics *gfx_ = nullptr;
    Shader *downsample_ = nullptr;
    std::array<Canvas *, kMaxLevels> levels_{};
    Canvas *atlas_ = nullptr;
    int sourceWidth_ = 0;
    int sourceHeight_ = 0;
    int levelCount_ = 0;
};

}  // namespace eve::graphics
