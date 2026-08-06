#include "map/Map.h"
#include "map/TileSystem.h"
#include "map/TileConfig.h"
#include "graphics/Graphics.h"
#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::map {

Module_IMPL(Map, new Map());

TileLayer *Map::newLayer(int mapW, int mapH, float tileW, float tileH) {
    return TileLayer::createLayer(mapW, mapH, tileW, tileH);
}

TileLayer *Map::newLayerFromFile(const std::string &path) {
    auto layers = loadMapFile(path, nullptr);
    return layers.empty() ? nullptr : layers.front();
}

int Map::loadFromFile(const std::string &path) {
    auto layers = loadMapFile(path, nullptr);
    return int(layers.size());
}

void Map::update(float dt) {
    (void)dt;
    TileConfigSystem::poll();
}

void Map::render(graphics::Graphics *gfx) { TileRenderSystem::render(gfx); }

int Map::pollConfigs() { return TileConfigSystem::poll(); }

int Map::getLayerCount() const {
    if (ecs::ComponentManager<TileLayer>::inst().registy == nullptr) return 0;
    int n = 0;
    auto view = ecs::View<TileLayer, TileLayer::Config>();
    for (auto it = view.begin(); it != view.end(); ++it) ++n;
    return n;
}

void Map::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Map::create, false);
    expose(cls);

    auto layer = table.addClass<TileLayer>(
        "TileLayer", std::function<TileLayer *()>([]() -> TileLayer * { return nullptr; }), true);
    layer.addFunc("setOrigin", &TileLayer::setOrigin);
    layer.addFunc("getX", &TileLayer::getX);
    layer.addFunc("getY", &TileLayer::getY);
    layer.addFunc("getMapWidth", &TileLayer::getMapWidth);
    layer.addFunc("getMapHeight", &TileLayer::getMapHeight);
    layer.addFunc("getTileWidth", &TileLayer::getTileWidth);
    layer.addFunc("getTileHeight", &TileLayer::getTileHeight);
    layer.addFunc("setTileSize", &TileLayer::setTileSize);
    layer.addFunc("resize", &TileLayer::resize);
    layer.addFunc("setTile", &TileLayer::setTile);
    layer.addFunc("getTile", &TileLayer::getTile);
    layer.addFunc("fill", &TileLayer::fill);
    layer.addFunc("clear", &TileLayer::clear);
    layer.addFunc("setTileset", &TileLayer::setTileset);
    layer.addFunc("setTilesetTileSize", &TileLayer::setTilesetTileSize);
    layer.addFunc("getTilesetTexture", &TileLayer::getTilesetTexture);
    layer.addFunc("getTilesetFirstGid", &TileLayer::getTilesetFirstGid);
    layer.addFunc("getTilesetColumns", &TileLayer::getTilesetColumns);
    layer.addFunc("setCanvas", &TileLayer::setCanvas);
    layer.addFunc("setCamera", &TileLayer::setCamera);
    layer.addFunc("setLayer", &TileLayer::setLayer);
    layer.addFunc("getLayer", &TileLayer::getLayer);
    layer.addFunc("setVisible", &TileLayer::setVisible);
    layer.addFunc("isVisible", &TileLayer::isVisible);
    layer.addFunc("setTint", &TileLayer::setTint);
    layer.addFunc("applyConfig", &TileLayer::applyConfig);
    layer.addFunc("loadConfig", &TileLayer::loadConfig);
    layer.addFunc("reloadConfig", &TileLayer::reloadConfig);
    layer.addFunc("setAutoReload", &TileLayer::setAutoReload);
    layer.addFunc("getAutoReload", &TileLayer::getAutoReload);
    layer.addFunc("getConfigPath", &TileLayer::getConfigPath);
}

void Map::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Map::getName);
    cls.addFunc("newLayer", &Map::newLayer);
    cls.addFunc("newLayerFromFile", &Map::newLayerFromFile);
    cls.addFunc("loadFromFile", &Map::loadFromFile);
    cls.addFunc("update", &Map::update);
    cls.addFunc("render", &Map::render);
    cls.addFunc("pollConfigs", &Map::pollConfigs);
    cls.addFunc("getLayerCount", &Map::getLayerCount);
}

}  // namespace eve::map
