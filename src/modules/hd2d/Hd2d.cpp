#include "hd2d/Hd2d.h"

#include "common/Exception.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem3D.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::hd2d {

Hd2D::Hd2D() = default;
Hd2D::~Hd2D() = default;

Module_IMPL(Hd2D, new Hd2D());

TileMap3D *Hd2D::newTileMap3D() { return new TileMap3D(); }

Sprite3D *Hd2D::newSprite(graphics::Graphics *gfx) {
    if (!gfx) throw eve::Exception("Hd2D.newSprite: null graphics");
    auto *sprite = new Sprite3D();
    sprite->buildQuad(gfx);
    return sprite;
}

void Hd2D::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Hd2D::create, false);
    expose(cls);

    auto tile = table.addClass<TileMap3D>(
        "TileMap3D", std::function<TileMap3D *()>([]() { return nullptr; }), true);
    tile.addFunc("setSideDepth", &TileMap3D::setSideDepth);
    tile.addFunc("getSideDepth", &TileMap3D::getSideDepth);
    tile.addFunc("setHeightScale", &TileMap3D::setHeightScale);
    tile.addFunc("getHeightScale", &TileMap3D::getHeightScale);
    tile.addFunc("setWallUV", &TileMap3D::setWallUV);
    tile.addFunc("setTint", &TileMap3D::setTint);
    tile.addFunc("buildMesh", &TileMap3D::buildMesh);
    tile.addFunc("buildRenderable", &TileMap3D::buildRenderable);
    tile.addFunc("getTileCount", &TileMap3D::getTileCount);

    auto sprite = table.addClass<Sprite3D>(
        "Sprite3D", std::function<Sprite3D *()>([]() { return nullptr; }), true);
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
}

void Hd2D::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Hd2D::getName);
    cls.addFunc("newTileMap3D", &Hd2D::newTileMap3D);
    cls.addFunc("newSprite", &Hd2D::newSprite);
}

}  // namespace eve::hd2d