#include "hd2d/Hd2d.h"

#include "common/Exception.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"

#include <algorithm>
#include <cmath>

namespace eve::hd2d {

void Hd2dLook::setFocusDistance(float distance) {
    if (!std::isfinite(distance) || distance < 0.f)
        throw eve::Exception("Hd2dLook.setFocusDistance: distance must be finite and >= 0");
    focusDistance_ = distance;
}

void Hd2dLook::setMaxBlur(float blurPx) {
    if (!std::isfinite(blurPx) || blurPx < 0.f)
        throw eve::Exception("Hd2dLook.setMaxBlur: blur must be finite and >= 0");
    maxBlurPx_ = std::min(blurPx, 32.f);
}

void Hd2dLook::setFocusRange(float range) {
    if (!std::isfinite(range) || range <= 0.f)
        throw eve::Exception("Hd2dLook.setFocusRange: range must be finite and > 0");
    focusRange_ = range;
}

void Hd2dLook::setBloomIntensity(float intensity) {
    if (!std::isfinite(intensity) || intensity < 0.f)
        throw eve::Exception("Hd2dLook.setBloomIntensity: intensity must be finite and >= 0");
    bloomIntensity_ = std::min(intensity, 8.f);
}

void Hd2dLook::setBloomThreshold(float threshold) {
    if (!std::isfinite(threshold) || threshold < 0.f)
        throw eve::Exception("Hd2dLook.setBloomThreshold: threshold must be finite and >= 0");
    bloomThreshold_ = std::min(threshold, 16.f);
}

void Hd2dLook::apply(graphics::Camera3D *camera) const {
    if (!camera) throw eve::Exception("Hd2dLook.apply: null camera");
    camera->setDepthOfField(focusDistance_, maxBlurPx_, focusRange_);
    camera->setBloom(bloomIntensity_, bloomThreshold_);
}

void Hd2dLook::applyPixelSampler(graphics::Graphics *gfx, graphics::Texture *texture) const {
    if (!gfx) throw eve::Exception("Hd2dLook.applyPixelSampler: null graphics");
    if (!texture) throw eve::Exception("Hd2dLook.applyPixelSampler: null texture");
    gfx->setTextureSampler(texture, "nearest", "none", 1.f, 0.f);
}

Hd2dLook Hd2dLook::miniature() {
    Hd2dLook look;
    look.focusDistance_ = 16.f;
    look.maxBlurPx_ = 7.f;
    look.focusRange_ = 12.f;
    look.bloomIntensity_ = 0.45f;
    look.bloomThreshold_ = 1.1f;
    return look;
}

Hd2dLook Hd2dLook::soft() {
    Hd2dLook look;
    look.focusDistance_ = 22.f;
    look.maxBlurPx_ = 3.f;
    look.focusRange_ = 18.f;
    look.bloomIntensity_ = 0.2f;
    look.bloomThreshold_ = 1.4f;
    return look;
}

}  // namespace eve::hd2d
