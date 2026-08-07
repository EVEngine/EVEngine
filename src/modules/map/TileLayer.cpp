#include "map/TileLayer.h"
#include "map/TileConfig.h"
#include "map/TileProjection.h"

#include <algorithm>

namespace eve::map {

TileLayer *TileLayer::createLayer(int mapW, int mapH, float tileW, float tileH) {
    TileLayer *e = TileLayer::create();
    e->config()->entity = e;
    e->resize(mapW, mapH);
    e->setTileSize(tileW, tileH);
    auto ts = e->tileset();
    ts->tileW = int(tileW > 0.f ? tileW : 32.f);
    ts->tileH = int(tileH > 0.f ? tileH : 32.f);
    (void)e->draw();
    (void)e->resource();
    return e;
}

void TileLayer::setOrigin(float x, float y) {
    config()->originX = x;
    config()->originY = y;
}

float TileLayer::getX() { return config()->originX; }
float TileLayer::getY() { return config()->originY; }

int TileLayer::getMapWidth() { return config()->mapW; }
int TileLayer::getMapHeight() { return config()->mapH; }
float TileLayer::getTileWidth() { return config()->tileW; }
float TileLayer::getTileHeight() { return config()->tileH; }

void TileLayer::setTileSize(float tileW, float tileH) {
    auto c = config();
    c->tileW = tileW > 0.f ? tileW : 1.f;
    c->tileH = tileH > 0.f ? tileH : 1.f;
}

void TileLayer::resize(int mapW, int mapH) {
    auto c = config();
    c->mapW = mapW > 0 ? mapW : 0;
    c->mapH = mapH > 0 ? mapH : 0;
    const size_t n = size_t(std::max(0, c->mapW) * std::max(0, c->mapH));
    tiles()->gids.assign(n, 0u);
}

void TileLayer::setTile(int tx, int ty, int gid) {
    auto c = config();
    if (tx < 0 || ty < 0 || tx >= c->mapW || ty >= c->mapH) return;
    tiles()->gids[size_t(ty * c->mapW + tx)] = uint32_t(gid < 0 ? 0 : gid);
}

int TileLayer::getTile(int tx, int ty) {
    auto c = config();
    if (tx < 0 || ty < 0 || tx >= c->mapW || ty >= c->mapH) return 0;
    return int(tiles()->gids[size_t(ty * c->mapW + tx)]);
}

void TileLayer::fill(int gid) {
    const uint32_t g = uint32_t(gid < 0 ? 0 : gid);
    for (auto &v : tiles()->gids) v = g;
}

void TileLayer::clear() { fill(0); }

void TileLayer::setTileset(graphics::Texture *texture, int firstGid, int columns, int margin,
                           int spacing) {
    auto ts = tileset();
    ts->texture = texture;
    ts->firstGid = firstGid > 0 ? firstGid : 1;
    ts->columns = columns > 0 ? columns : 1;
    ts->margin = margin > 0 ? margin : 0;
    ts->spacing = spacing > 0 ? spacing : 0;
    if (texture) {
        // Default atlas tile size from Config when not set yet.
        if (ts->tileW <= 0) ts->tileW = int(config()->tileW);
        if (ts->tileH <= 0) ts->tileH = int(config()->tileH);
    }
}

void TileLayer::setTilesetTileSize(int tileW, int tileH) {
    auto ts = tileset();
    ts->tileW = tileW > 0 ? tileW : 1;
    ts->tileH = tileH > 0 ? tileH : 1;
}

graphics::Texture *TileLayer::getTilesetTexture() { return tileset()->texture; }
int TileLayer::getTilesetFirstGid() { return tileset()->firstGid; }
int TileLayer::getTilesetColumns() { return tileset()->columns; }

void TileLayer::setCanvas(graphics::Canvas *canvas) { draw()->canvas = canvas; }
void TileLayer::setCamera(graphics::Camera2D *camera) { draw()->camera = camera; }

void TileLayer::setLayer(int layer) { draw()->layer = layer; }
int TileLayer::getLayer() { return draw()->layer; }

void TileLayer::setVisible(bool visible) { draw()->visible = visible; }
bool TileLayer::isVisible() { return draw()->visible; }

void TileLayer::setTint(float r, float g, float b, float a) {
    draw()->tint = Color{r, g, b, a};
}

bool TileLayer::applyConfig(const std::string &json) { return applyConfigText(this, json, nullptr); }

bool TileLayer::loadConfig(const std::string &path) { return loadConfigFile(this, path, nullptr); }

bool TileLayer::reloadConfig() { return reloadConfigFile(this, nullptr); }

void TileLayer::setAutoReload(bool enable) { resource()->autoReload = enable; }
bool TileLayer::getAutoReload() { return resource()->autoReload; }
std::string TileLayer::getConfigPath() { return resource()->path; }

void TileLayer::tileToWorld(int tx, int ty, float &wx, float &wy) {
    eve::map::tileToWorld(*config(), tx, ty, wx, wy);
}

float TileLayer::depthY(int tx, int ty) { return tileToDepthY(*config(), tx, ty); }

void TileLayer::worldToTile(float wx, float wy, int &tx, int &ty) {
    eve::map::worldToTile(*config(), wx, wy, tx, ty);
}

float TileLayer::tileToWorldX(int tx, int ty) {
    float wx = 0.f, wy = 0.f;
    tileToWorld(tx, ty, wx, wy);
    return wx;
}

float TileLayer::tileToWorldY(int tx, int ty) {
    float wx = 0.f, wy = 0.f;
    tileToWorld(tx, ty, wx, wy);
    return wy;
}

float TileLayer::depthYAt(int tx, int ty) { return depthY(tx, ty); }

int TileLayer::worldToTileX(float wx, float wy) {
    int tx = 0, ty = 0;
    worldToTile(wx, wy, tx, ty);
    return tx;
}

int TileLayer::worldToTileY(float wx, float wy) {
    int tx = 0, ty = 0;
    worldToTile(wx, wy, tx, ty);
    return ty;
}

}  // namespace eve::map
