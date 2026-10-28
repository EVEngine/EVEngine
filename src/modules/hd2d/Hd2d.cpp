#include "hd2d/Hd2d.h"
#include <algorithm>
#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/Exception.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"

// Hd2d.cpp
namespace eve::hd2d {

Hd2D::Hd2D()  = default;
Hd2D::~Hd2D() = default;

Module_IMPL(Hd2D, new Hd2D());

TileMap3D *Hd2D::newTileMap3D() { return new TileMap3D(); }

Sprite3D *Hd2D::newSprite(graphics::Graphics *gfx) {
    if (!gfx) throw eve::Exception("Hd2D.newSprite: null graphics");
    auto *sprite = new Sprite3D();
    sprite->buildQuad(gfx);
    return sprite;
}

Hd2dLook *Hd2D::newLook() { return new Hd2dLook(); }

Hd2dLook *Hd2D::newMiniatureLook() { return new Hd2dLook(Hd2dLook::miniature()); }

void Hd2D::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Hd2D::create, false);
    expose(cls);

    auto tile = table.addClass<TileMap3D>("TileMap3D", std::function<TileMap3D *()>([]() { return nullptr; }), true);
    tile.addFunc("setSideDepth", &TileMap3D::setSideDepth);
    tile.addFunc("getSideDepth", &TileMap3D::getSideDepth);
    tile.addFunc("setHeightScale", &TileMap3D::setHeightScale);
    tile.addFunc("getHeightScale", &TileMap3D::getHeightScale);
    tile.addFunc("setWallUV", &TileMap3D::setWallUV);
    tile.addFunc("setTint", &TileMap3D::setTint);
    tile.addFunc("buildMesh", &TileMap3D::buildMesh);
    tile.addFunc("buildRenderable", &TileMap3D::buildRenderable);
    tile.addFunc("getTileCount", &TileMap3D::getTileCount);

    auto sprite = table.addClass<Sprite3D>("Sprite3D", std::function<Sprite3D *()>([]() { return nullptr; }), true);
    sprite.addFunc("setTexture", &Sprite3D::setTexture);
    sprite.addFunc("getTexture", &Sprite3D::getTexture);
    sprite.addFunc("setFrame", &Sprite3D::setFrame);
    sprite.addFunc("setFlipX", &Sprite3D::setFlipX);
    sprite.addFunc("setFlipY", &Sprite3D::setFlipY);
    sprite.addFunc("setFrameGrid", &Sprite3D::setFrameGrid);
    sprite.addFunc("getFrameGridColumns", &Sprite3D::getFrameGridColumns);
    sprite.addFunc("getFrameGridRows", &Sprite3D::getFrameGridRows);
    sprite.addFunc("setFrameIndex", &Sprite3D::setFrameIndex);
    sprite.addFunc("getFrameIndex", &Sprite3D::getFrameIndex);
    sprite.addFunc("getFrameCount", &Sprite3D::getFrameCount);
    sprite.addFunc("play", &Sprite3D::play);
    sprite.addFunc("stop", &Sprite3D::stop);
    sprite.addFunc("isPlaying", &Sprite3D::isPlaying);
    sprite.addFunc("update", &Sprite3D::update);
    sprite.addFunc("setCamera", &Sprite3D::setCamera);
    sprite.addFunc("setPivot", &Sprite3D::setPivot);
    sprite.addFunc("setPosition", &Sprite3D::setPosition);
    sprite.addFunc("getPositionX", &Sprite3D::getPositionX);
    sprite.addFunc("getPositionY", &Sprite3D::getPositionY);
    sprite.addFunc("getPositionZ", &Sprite3D::getPositionZ);
    sprite.addFunc("setSize", &Sprite3D::setSize);
    sprite.addFunc("getWidth", &Sprite3D::getWidth);
    sprite.addFunc("getHeight", &Sprite3D::getHeight);
    sprite.addFunc("setTint", &Sprite3D::setTint);
    sprite.addFunc("setVisible", &Sprite3D::setVisible);
    sprite.addFunc("getVisible", &Sprite3D::getVisible);
    sprite.addFunc("setAlphaCutoff", &Sprite3D::setAlphaCutoff);
    sprite.addFunc("getAlphaCutoff", &Sprite3D::getAlphaCutoff);
    sprite.addFunc("setDepthWrite", &Sprite3D::setDepthWrite);
    sprite.addFunc("getDepthWrite", &Sprite3D::getDepthWrite);
    sprite.addFunc("setDoubleSided", &Sprite3D::setDoubleSided);
    sprite.addFunc("getDoubleSided", &Sprite3D::getDoubleSided);
    sprite.addFunc("setBillboardMode", &Sprite3D::setBillboardMode);
    sprite.addFunc("getBillboardMode", &Sprite3D::getBillboardMode);

    auto look = table.addClass<Hd2dLook>("Hd2dLook", std::function<Hd2dLook *()>([]() { return nullptr; }), true);
    look.addFunc("setFocusDistance", &Hd2dLook::setFocusDistance);
    look.addFunc("getFocusDistance", &Hd2dLook::getFocusDistance);
    look.addFunc("setMaxBlur", &Hd2dLook::setMaxBlur);
    look.addFunc("getMaxBlur", &Hd2dLook::getMaxBlur);
    look.addFunc("setFocusRange", &Hd2dLook::setFocusRange);
    look.addFunc("getFocusRange", &Hd2dLook::getFocusRange);
    look.addFunc("setBloomIntensity", &Hd2dLook::setBloomIntensity);
    look.addFunc("getBloomIntensity", &Hd2dLook::getBloomIntensity);
    look.addFunc("setBloomThreshold", &Hd2dLook::setBloomThreshold);
    look.addFunc("getBloomThreshold", &Hd2dLook::getBloomThreshold);
    look.addFunc("apply", &Hd2dLook::apply);
    look.addFunc("applyPixelSampler", &Hd2dLook::applyPixelSampler);
}

void Hd2D::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Hd2D::getName);
    cls.addFunc("newTileMap3D", &Hd2D::newTileMap3D);
    cls.addFunc("newSprite", &Hd2D::newSprite);
    cls.addFunc("newLook", &Hd2D::newLook);
    cls.addFunc("newMiniatureLook", &Hd2D::newMiniatureLook);
}

}  // namespace eve::hd2d

// Hd2dLook.cpp
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
    look.focusDistance_  = 16.f;
    look.maxBlurPx_      = 7.f;
    look.focusRange_     = 12.f;
    look.bloomIntensity_ = 0.45f;
    look.bloomThreshold_ = 1.1f;
    return look;
}

Hd2dLook Hd2dLook::soft() {
    Hd2dLook look;
    look.focusDistance_  = 22.f;
    look.maxBlurPx_      = 3.f;
    look.focusRange_     = 18.f;
    look.bloomIntensity_ = 0.2f;
    look.bloomThreshold_ = 1.4f;
    return look;
}

}  // namespace eve::hd2d
