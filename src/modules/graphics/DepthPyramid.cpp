#include "graphics/DepthPyramid.h"

#include <algorithm>
#include <string>
#include <vector>

#include "common/Exception.h"
#include "graphics/BlendMode.h"
#include "graphics/Canvas.h"
#include "graphics/Color.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/shaders/PostProcessWgsl.h"
#include "graphics/shaders/depth_pyramid_downsample_frag_spv.inc"

namespace eve::graphics {
namespace {

std::vector<uint32_t> copySpv(const uint32_t *data, size_t count) {
    return std::vector<uint32_t>(data, data + count);
}

}  // namespace

DepthPyramid::DepthPyramid(Graphics *gfx) : gfx_(gfx) {
    if (!gfx_) throw eve::Exception("DepthPyramid: null graphics");
    downsample_ = gfx_->getBackendName() == "webgpu"
                      ? gfx_->newShaderFromWgsl(
                            {}, std::string(shaders::kPostCommon) +
                                    shaders::kDepthPyramidDownsample)
                      : gfx_->newShaderFromSpv(
                            {}, copySpv(depth_pyramid_downsample_frag_spv,
                                       depth_pyramid_downsample_frag_spv_count));
    if (!downsample_ || !downsample_->gpuHandle)
        throw eve::Exception("DepthPyramid: failed to create shader");
    downsample_->declareFloat("texelW");
    downsample_->declareFloat("texelH");
    downsample_->declareFloat("firstLevel");
}

void DepthPyramid::ensureTargets(int width, int height, int maxLevels) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    maxLevels = std::clamp(maxLevels, 1, kMaxLevels);
    int desiredLevels = 1;
    int desiredWidth = width;
    int desiredHeight = height;
    while (desiredLevels < maxLevels && (desiredWidth > 1 || desiredHeight > 1)) {
        desiredWidth = std::max(desiredWidth / 2, 1);
        desiredHeight = std::max(desiredHeight / 2, 1);
        ++desiredLevels;
    }
    if (atlas_ && sourceWidth_ == width && sourceHeight_ == height &&
        levelCount_ == desiredLevels)
        return;
    sourceWidth_ = width;
    sourceHeight_ = height;
    levelCount_ = 0;
    int levelWidth = width;
    int levelHeight = height;
    while (levelCount_ < desiredLevels) {
        levels_[levelCount_] = gfx_->newHDRCanvas(levelWidth, levelHeight);
        if (!levels_[levelCount_])
            throw eve::Exception("DepthPyramid: failed to allocate level");
        ++levelCount_;
        if (levelWidth == 1 && levelHeight == 1) break;
        levelWidth = std::max(levelWidth / 2, 1);
        levelHeight = std::max(levelHeight / 2, 1);
    }
    atlas_ = gfx_->newHDRCanvas(width * 2, height);
    if (!atlas_) throw eve::Exception("DepthPyramid: failed to allocate atlas");
}

Texture *DepthPyramid::build(Texture *depth, int maxLevels) {
    if (!depth) throw eve::Exception("DepthPyramid.build: null depth");
    ensureTargets(depth->getWidth(), depth->getHeight(), maxLevels);
    Canvas *previousCanvas = gfx_->getCanvas();
    Texture *input = depth;
    for (int level = 0; level < levelCount_; ++level) {
        Canvas *target = levels_[level];
        downsample_->sendFloat("texelW", 1.f / float(std::max(input->getWidth(), 1)));
        downsample_->sendFloat("texelH", 1.f / float(std::max(input->getHeight(), 1)));
        downsample_->sendFloat("firstLevel", level == 0 ? 1.f : 0.f);
        target->clear(Color(1.f, 1.f, 0.f, 1.f), {}, {});
        gfx_->setCanvas(target);
        gfx_->drawTexturedRectShaderUV(input, downsample_, 0.f, 0.f, float(target->getWidth()),
                                       float(target->getHeight()), 0.f, 0.f, 1.f, 1.f,
                                       Color(1.f, 1.f, 1.f, 1.f), false, BlendMode::Opaque);
        input = target->getTexture();
    }

    atlas_->clear(Color(1.f, 1.f, 0.f, 1.f), {}, {});
    gfx_->setCanvas(atlas_);
    float offsetX = 0.f;
    for (int level = 0; level < levelCount_; ++level) {
        Canvas *source = levels_[level];
        gfx_->drawTexturedRectShaderUV(
            source->getTexture(), nullptr, offsetX, 0.f, float(source->getWidth()),
            float(source->getHeight()), 0.f, 0.f, 1.f, 1.f, Color(1.f, 1.f, 1.f, 1.f), false,
            BlendMode::Opaque);
        offsetX += float(source->getWidth());
    }
    gfx_->setCanvas(previousCanvas);
    Texture *result = atlas_->getTexture();
    if (!result) throw eve::Exception("DepthPyramid: atlas texture missing");
    return result;
}

}  // namespace eve::graphics
