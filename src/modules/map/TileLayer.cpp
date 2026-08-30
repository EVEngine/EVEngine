#include "map/TileLayer.h"
#include "map/TileConfig.h"
#include "map/TileProjection.h"

#include "graphics/Canvas.h"
#include "graphics/Texture.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

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
    tiles()->terrainIds.assign(n, -1);
    rebuildSpatialIndex();
}

void TileLayer::setTile(int tx, int ty, int gid) {
    auto c = config();
    if (tx < 0 || ty < 0 || tx >= c->mapW || ty >= c->mapH) return;
    auto t = tiles();
    const size_t index = size_t(ty * c->mapW + tx);
    const uint32_t next = uint32_t(gid < 0 ? 0 : gid);
    const bool wasEmpty = tileGid(t->gids[index]) == 0;
    const bool isEmpty = tileGid(next) == 0;
    if (t->gids[index] == next) return;
    t->gids[index] = next;
    const int chunkIndex = (ty / Tiles::kChunkSize) * t->chunkColumns + tx / Tiles::kChunkSize;
    if (chunkIndex >= 0 && chunkIndex < int(t->chunks.size()) && wasEmpty != isEmpty)
        t->chunks[size_t(chunkIndex)].nonEmpty += isEmpty ? -1 : 1;
    ++t->revision;
}

int TileLayer::getTile(int tx, int ty) {
    auto c = config();
    if (tx < 0 || ty < 0 || tx >= c->mapW || ty >= c->mapH) return 0;
    return int(tiles()->gids[size_t(ty * c->mapW + tx)]);
}

void TileLayer::fillRect(int x, int y, int width, int height, int gid) {
    auto c = config();
    if (width <= 0 || height <= 0 || c->mapW <= 0 || c->mapH <= 0) return;
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(c->mapW, x + width);
    const int y1 = std::min(c->mapH, y + height);
    if (x0 >= x1 || y0 >= y1) return;
    const uint32_t next = uint32_t(gid < 0 ? 0 : gid);
    bool changed = false;
    auto t = tiles();
    for (int ty = y0; ty < y1; ++ty) {
        for (int tx = x0; tx < x1; ++tx) {
            const size_t index = size_t(ty * c->mapW + tx);
            if (t->gids[index] == next) continue;
            t->gids[index] = next;
            changed = true;
        }
    }
    if (changed) {
        rebuildSpatialIndex();
    }
}

void TileLayer::fill(int gid) {
    const uint32_t g = uint32_t(gid < 0 ? 0 : gid);
    auto t = tiles();
    bool changed = false;
    for (auto &v : t->gids) {
        if (v != g) changed = true;
        v = g;
    }
    if (changed) {
        rebuildSpatialIndex();
    }
}

void TileLayer::clear() { fill(0); }

int TileLayer::getRevision() { return int(tiles()->revision & 0x7fffffffu); }
int TileLayer::getChunkSize() { return Tiles::kChunkSize; }
int TileLayer::getChunkCount() { return int(tiles()->chunks.size()); }
int TileLayer::getNonEmptyChunkCount() {
    int count = 0;
    for (const auto &chunk : tiles()->chunks)
        if (chunk.nonEmpty > 0) ++count;
    return count;
}

void TileLayer::rebuildSpatialIndex() {
    auto c = config();
    auto t = tiles();
    t->chunkColumns = (c->mapW + Tiles::kChunkSize - 1) / Tiles::kChunkSize;
    t->chunkRows = (c->mapH + Tiles::kChunkSize - 1) / Tiles::kChunkSize;
    t->chunks.assign(size_t(t->chunkColumns * t->chunkRows), {});
    for (int y = 0; y < c->mapH; ++y) {
        for (int x = 0; x < c->mapW; ++x) {
            const size_t index = size_t(y * c->mapW + x);
            if (index >= t->gids.size() || tileGid(t->gids[index]) == 0) continue;
            const int chunkIndex = (y / Tiles::kChunkSize) * t->chunkColumns + x / Tiles::kChunkSize;
            ++t->chunks[size_t(chunkIndex)].nonEmpty;
        }
    }
    ++t->revision;
}

void TileLayer::clearTileAnimation(int gid) {
    auto &animations = tileset()->animations;
    animations.erase(std::remove_if(animations.begin(), animations.end(),
                                    [gid](const Tileset::Animation &a) { return a.gid == gid; }),
                     animations.end());
}

void TileLayer::addTileAnimationFrame(int gid, int frameGid, int durationMs) {
    if (gid <= 0 || frameGid <= 0) return;
    auto &animations = tileset()->animations;
    auto it = std::find_if(animations.begin(), animations.end(),
                           [gid](const Tileset::Animation &a) { return a.gid == gid; });
    if (it == animations.end()) {
        animations.push_back({});
        it = animations.end() - 1;
        it->gid = gid;
    }
    it->frames.push_back({frameGid, std::max(1, durationMs)});
}

int TileLayer::getTileAnimationFrameCount(int gid) {
    for (const auto &animation : tileset()->animations)
        if (animation.gid == gid) return int(animation.frames.size());
    return 0;
}

namespace {
TileLayer::Tileset::CustomData *findCustomData(TileLayer::Tileset &tileset, int gid,
                                               const std::string &name) {
    for (auto &entry : tileset.customData)
        if (entry.gid == gid && entry.name == name) return &entry;
    tileset.customData.push_back({gid, name, {}, {}});
    return &tileset.customData.back();
}

const TileLayer::Tileset::CustomData *findCustomData(const TileLayer::Tileset &tileset, int gid,
                                                     const std::string &name) {
    for (const auto &entry : tileset.customData)
        if (entry.gid == gid && entry.name == name) return &entry;
    return nullptr;
}
}  // namespace

void TileLayer::setTileDataString(int gid, const std::string &name, const std::string &value) {
    if (gid <= 0 || name.empty()) return;
    auto *entry = findCustomData(*tileset(), gid, name);
    entry->type = "string";
    entry->value = value;
}

void TileLayer::setTileDataNumber(int gid, const std::string &name, float value) {
    if (gid <= 0 || name.empty()) return;
    auto *entry = findCustomData(*tileset(), gid, name);
    entry->type = "number";
    std::ostringstream out;
    out << value;
    entry->value = out.str();
}

void TileLayer::setTileDataBool(int gid, const std::string &name, bool value) {
    if (gid <= 0 || name.empty()) return;
    auto *entry = findCustomData(*tileset(), gid, name);
    entry->type = "bool";
    entry->value = value ? "true" : "false";
}

std::string TileLayer::getTileDataType(int gid, const std::string &name) {
    const auto *entry = findCustomData(*tileset(), gid, name);
    return entry ? entry->type : std::string();
}

std::string TileLayer::getTileDataString(int gid, const std::string &name) {
    const auto *entry = findCustomData(*tileset(), gid, name);
    return entry ? entry->value : std::string();
}

float TileLayer::getTileDataNumber(int gid, const std::string &name) {
    const auto *entry = findCustomData(*tileset(), gid, name);
    return entry && (entry->type == "number" || entry->type == "float" || entry->type == "int")
               ? std::strtof(entry->value.c_str(), nullptr)
               : 0.f;
}

bool TileLayer::getTileDataBool(int gid, const std::string &name) {
    const auto *entry = findCustomData(*tileset(), gid, name);
    return entry && entry->type == "bool" && entry->value == "true";
}

void TileLayer::setTerrainRule(int gid, int terrain, int neighborMask) {
    setAutotileRule(gid, terrain, neighborMask, 1);
}

void TileLayer::defineAutotileFamily(int terrain, const std::string &kind, int seed) {
    if (terrain < 0) return;
    const std::string normalized = (kind == "shore" || kind == "wall" || kind == "waterfall") ? kind : "terrain";
    auto              ts         = tileset();
    auto              it         = std::find_if(ts->terrainFamilies.begin(), ts->terrainFamilies.end(),
                                                [terrain](const Tileset::TerrainFamily &family) { return family.terrain == terrain; });
    if (it == ts->terrainFamilies.end())
        ts->terrainFamilies.push_back({terrain, normalized, uint32_t(seed)});
    else {
        it->kind = normalized;
        it->seed = uint32_t(seed);
    }
    ++tiles()->revision;
}

void TileLayer::setAutotileRule(int gid, int terrain, int neighborMask, int weight) {
    if (gid <= 0 || terrain < 0) return;
    auto &rules = tileset()->terrainRules;
    auto  it    = std::find_if(rules.begin(), rules.end(), [=](const Tileset::TerrainRule &rule) {
        return rule.gid == gid && rule.terrain == terrain && rule.neighborMask == (neighborMask & 0xff);
    });
    if (it == rules.end())
        rules.push_back({gid, terrain, neighborMask & 0xff, std::max(1, weight)});
    else
        it->weight = std::max(1, weight);
    ++tiles()->revision;
}

void TileLayer::clearTerrainRules() {
    tileset()->terrainRules.clear();
    tileset()->terrainFamilies.clear();
    ++tiles()->revision;
}

int TileLayer::getTerrain(int x, int y) {
    auto c = config();
    auto t = tiles();
    if (x < 0 || y < 0 || x >= c->mapW || y >= c->mapH) return -1;
    const size_t index = size_t(y * c->mapW + x);
    return index < t->terrainIds.size() ? t->terrainIds[index] : -1;
}

namespace {
uint32_t autotileHash(uint32_t seed, int terrain, int x, int y) {
    uint32_t value = seed ^ uint32_t(terrain) * 0x9e3779b9u;
    value ^= uint32_t(x) * 0x85ebca6bu;
    value ^= uint32_t(y) * 0xc2b2ae35u;
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    return value;
}

void resolveTerrainRegion(TileLayer *layer, int x0, int y0, int x1, int y1) {
    auto          c     = layer->config();
    auto          t     = layer->tiles();
    auto          ts    = layer->tileset();
    constexpr int dx[8] = {-1, 0, 1, 1, 1, 0, -1, -1};
    constexpr int dy[8] = {-1, -1, -1, 0, 1, 1, 1, 0};
    for (int cy = std::max(0, y0); cy <= std::min(c->mapH - 1, y1); ++cy) {
        for (int cx = std::max(0, x0); cx <= std::min(c->mapW - 1, x1); ++cx) {
            const size_t cell    = size_t(cy * c->mapW + cx);
            const int    terrain = cell < t->terrainIds.size() ? t->terrainIds[cell] : -1;
            if (terrain < 0) {
                t->gids[cell] = 0;
                continue;
            }
            int mask = 0;
            for (int i = 0; i < 8; ++i) {
                const int nx = cx + dx[i], ny = cy + dy[i];
                if (nx < 0 || ny < 0 || nx >= c->mapW || ny >= c->mapH) continue;
                if (t->terrainIds[size_t(ny * c->mapW + nx)] == terrain) mask |= 1 << i;
            }
            uint32_t   seed         = 0;
            int        relevantMask = 0xff;
            const auto family       = std::find_if(
                ts->terrainFamilies.begin(), ts->terrainFamilies.end(),
                [terrain](const TileLayer::Tileset::TerrainFamily &candidate) { return candidate.terrain == terrain; });
            if (family != ts->terrainFamilies.end()) {
                seed = family->seed;
                if (family->kind == "wall")
                    relevantMask = 0xaa;
                else if (family->kind == "waterfall")
                    relevantMask = 0x22;
            }
            const int exactMask   = mask & relevantMask;
            int       totalWeight = 0;
            for (const auto &rule : ts->terrainRules)
                if (rule.terrain == terrain && (rule.neighborMask & relevantMask) == exactMask)
                    totalWeight += std::max(1, rule.weight);
            if (totalWeight <= 0) continue;
            int pick = int(autotileHash(seed, terrain, cx, cy) % uint32_t(totalWeight));
            for (const auto &rule : ts->terrainRules) {
                if (rule.terrain != terrain || (rule.neighborMask & relevantMask) != exactMask) continue;
                pick -= std::max(1, rule.weight);
                if (pick < 0) {
                    t->gids[cell] = uint32_t(rule.gid);
                    break;
                }
            }
        }
    }
    layer->rebuildSpatialIndex();
}
}  // namespace

void TileLayer::paintTerrain(int x, int y, int terrain) {
    auto c = config();
    auto t = tiles();
    if (x < 0 || y < 0 || x >= c->mapW || y >= c->mapH || terrain < 0) return;
    if (t->terrainIds.size() != t->gids.size()) t->terrainIds.assign(t->gids.size(), -1);
    t->terrainIds[size_t(y * c->mapW + x)] = terrain;
    resolveTerrainRegion(this, x - 1, y - 1, x + 1, y + 1);
}

void TileLayer::paintTerrainRect(int x, int y, int width, int height, int terrain) {
    auto c = config();
    auto t = tiles();
    if (width <= 0 || height <= 0 || terrain < 0) return;
    if (t->terrainIds.size() != t->gids.size()) t->terrainIds.assign(t->gids.size(), -1);
    const int x0 = std::max(0, x), y0 = std::max(0, y);
    const int x1 = std::min(c->mapW, x + width), y1 = std::min(c->mapH, y + height);
    if (x0 >= x1 || y0 >= y1) return;
    for (int cy = y0; cy < y1; ++cy)
        for (int cx = x0; cx < x1; ++cx) t->terrainIds[size_t(cy * c->mapW + cx)] = terrain;
    resolveTerrainRegion(this, x0 - 1, y0 - 1, x1, y1);
}

void TileLayer::fillTerrain(int terrain) {
    if (terrain < 0) return;
    auto t = tiles();
    t->terrainIds.assign(t->gids.size(), terrain);
    resolveTerrainRegion(this, 0, 0, config()->mapW - 1, config()->mapH - 1);
}

void TileLayer::eraseTerrainRect(int x, int y, int width, int height) {
    auto c = config();
    auto t = tiles();
    if (width <= 0 || height <= 0) return;
    if (t->terrainIds.size() != t->gids.size()) t->terrainIds.assign(t->gids.size(), -1);
    const int x0 = std::max(0, x), y0 = std::max(0, y);
    const int x1 = std::min(c->mapW, x + width), y1 = std::min(c->mapH, y + height);
    if (x0 >= x1 || y0 >= y1) return;
    for (int cy = y0; cy < y1; ++cy)
        for (int cx = x0; cx < x1; ++cx) t->terrainIds[size_t(cy * c->mapW + cx)] = -1;
    resolveTerrainRegion(this, x0 - 1, y0 - 1, x1, y1);
}

void TileLayer::setTileset(graphics::Texture *texture, int firstGid, int columns, int margin,
                           int spacing) {
    auto ts = tileset();
    ts->visuals.clear();
    ts->animations.clear();
    ts->terrainRules.clear();
    ts->terrainFamilies.clear();
    ts->customData.clear();
    ts->atlases.clear();
    ts->texture = texture;
    ts->firstGid = firstGid > 0 ? firstGid : 1;
    ts->columns = columns > 0 ? columns : 1;
    ts->margin = margin > 0 ? margin : 0;
    ts->spacing = spacing > 0 ? spacing : 0;
    ts->atlases.push_back(
        {texture, ts->firstGid, ts->columns, ts->tileW, ts->tileH, ts->margin, ts->spacing, resource()->texturePath});
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
    if (!ts->atlases.empty()) {
        ts->atlases.front().tileW = ts->tileW;
        ts->atlases.front().tileH = ts->tileH;
    }
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

void TileLayer::clearTileVisuals() {
    tileset()->visuals.clear();
    ++tiles()->revision;
}

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
    ++tiles()->revision;
}

void TileLayer::setTileNavigationProfile(int gid, bool walkable, float cost, int enterMask, int exitMask, bool opaque,
                                         int semanticFlags) {
    if (gid <= 0) return;
    auto ts = tileset();
    auto it = std::find_if(ts->visuals.begin(), ts->visuals.end(),
                           [gid](const Tileset::Visual &visual) { return visual.gid == gid; });
    if (it == ts->visuals.end()) {
        ts->visuals.push_back({});
        it      = ts->visuals.end() - 1;
        it->gid = gid;
    }
    it->walkable      = walkable;
    it->cost          = std::max(0.001f, cost);
    it->enterMask     = uint8_t(enterMask & 0xff);
    it->exitMask      = uint8_t(exitMask & 0xff);
    it->opaque        = opaque;
    it->semanticFlags = uint32_t(semanticFlags);
    ++tiles()->revision;
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
