#include "map/TileLayer.h"
#include "map/TileConfig.h"
#include "map/TileProjection.h"

#include "graphics/Canvas.h"
#include "graphics/Texture.h"

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
    ts->visuals.clear();
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

void TileLayer::setCellGap(float gapX, float gapY) {
    config()->cellGapX = std::max(gapX, -config()->tileW + 0.001f);
    config()->cellGapY = std::max(gapY, -config()->tileH + 0.001f);
}

void TileLayer::setRenderSpacing(float spacingX, float spacingY) {
    config()->cellGapX = config()->tileW * (std::max(spacingX, 0.001f) - 1.f);
    config()->cellGapY = config()->tileH * (std::max(spacingY, 0.001f) - 1.f);
}

float TileLayer::getCellGapX() { return config()->cellGapX; }
float TileLayer::getCellGapY() { return config()->cellGapY; }
float TileLayer::getRenderSpacingX() {
    return config()->tileW != 0.f ? 1.f + config()->cellGapX / config()->tileW : 1.f;
}
float TileLayer::getRenderSpacingY() {
    return config()->tileH != 0.f ? 1.f + config()->cellGapY / config()->tileH : 1.f;
}

void TileLayer::setTileVisual(int gid, int x, int y, int width, int height, float pivotX,
                              float pivotY, float sortBias) {
    if (gid <= 0 || width <= 0 || height <= 0) return;
    auto &visuals = tileset()->visuals;
    auto it = std::find_if(visuals.begin(), visuals.end(),
                           [gid](const Tileset::Visual &v) { return v.gid == gid; });
    if (it == visuals.end()) {
        visuals.push_back({});
        it = visuals.end() - 1;
        it->gid = gid;
    }
    it->x = x;
    it->y = y;
    it->width = width;
    it->height = height;
    it->pivotX = pivotX;
    it->pivotY = pivotY;
    it->sortBias = sortBias;
}

void TileLayer::clearTileVisuals() { tileset()->visuals.clear(); }

int TileLayer::getTileVisualCount() { return int(tileset()->visuals.size()); }

void TileLayer::setTileMetadata(int gid, int footprintW, int footprintH, bool walkable,
                                float cost) {
    if (gid <= 0) return;
    auto &visuals = tileset()->visuals;
    auto it = std::find_if(visuals.begin(), visuals.end(),
                           [gid](const Tileset::Visual &v) { return v.gid == gid; });
    if (it == visuals.end()) {
        visuals.push_back({});
        it = visuals.end() - 1;
        it->gid = gid;
    }
    it->footprintW = std::max(1, footprintW);
    it->footprintH = std::max(1, footprintH);
    it->walkable = walkable;
    it->cost = cost > 0.f ? cost : 1.f;
}

bool TileLayer::loadTilesetManifest(const std::string &path) {
    return loadTilesetManifestFile(this, path, nullptr);
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
