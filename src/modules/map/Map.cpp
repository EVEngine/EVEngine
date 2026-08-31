#include "map/Map.h"

#include "common/Capability.h"
#include "common/Module.h"
#include "common/SquirrelBinding.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "map/ArtifactProvider.h"
#include "map/DualGrid.h"
#include "map/FlowField.h"
#include "map/Fov.h"
#include "map/MapObjectContract.h"
#include "map/Path.h"
#include "map/Pathfinder.h"
#include "map/TileConfig.h"
#include "map/TileSystem.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace eve::map {
namespace {

void retireReplacedLayers(const std::vector<TileLayer *> &previous,
                          const std::vector<TileLayer *> &replacement) {
    for (TileLayer *layer : previous) {
        if (!layer || std::find(replacement.begin(), replacement.end(), layer) != replacement.end())
            continue;
        layer->clear();
        layer->setVisible(false);
    }
}

void discardCandidateLayers(const std::vector<TileLayer *> &layers) {
    for (TileLayer *layer : layers) {
        if (!layer) continue;
        layer->clear();
        layer->setVisible(false);
    }
}

}  // namespace

Map::Map() { registerMapArtifactProvider(); }

Module_IMPL(Map, new Map());

TileLayer *Map::newLayer(int mapW, int mapH, float tileW, float tileH) {
    return TileLayer::createLayer(mapW, mapH, tileW, tileH);
}

Pathfinder *Map::newPathfinder(TileLayer *layer) {
    if (!layer) return nullptr;
    return new Pathfinder(layer);
}

Pathfinder *Map::newPathfinderSize(int mapW, int mapH) {
    if (mapW <= 0 || mapH <= 0) return nullptr;
    return new Pathfinder(mapW, mapH);
}

Fov *Map::newFov(TileLayer *layer) {
    if (!layer) return nullptr;
    return new Fov(layer);
}

Fov *Map::newFovSize(int mapW, int mapH) {
    if (mapW <= 0 || mapH <= 0) return nullptr;
    return new Fov(mapW, mapH);
}

Fov *Map::newFovVolume(int mapW, int mapH, int depth) {
    if (mapW <= 0 || mapH <= 0 || depth <= 0) return nullptr;
    return new Fov(mapW, mapH, depth);
}

TileLayer *Map::newLayerFromFile(const std::string &path) {
    std::vector<MapObject> objs;
    auto layers = loadMapFile(path, &objs, nullptr);
    if (layers.empty()) return nullptr;
    retireReplacedLayers(loadedLayers_, layers);
    setObjects(std::move(objs));
    loadedLayers_ = std::move(layers);
    return loadedLayers_.front();
}

int Map::loadFromFile(const std::string &path) {
    std::vector<MapObject> objs;
    auto layers = loadMapFile(path, &objs, nullptr);
    if (layers.empty()) return 0;
    retireReplacedLayers(loadedLayers_, layers);
    setObjects(std::move(objs));
    loadedLayers_ = std::move(layers);
    return int(loadedLayers_.size());
}

void Map::update(float dt) {
    TileRenderSystem::update(dt);
    TileConfigSystem::poll();
}

void Map::render(graphics::Graphics *gfx) {
    if (!gfx) return;
    std::vector<graphics::DrawItem2D> items;
    graphics::RenderSystem::collectSprites(items);
    TileRenderSystem::collect(items, gfx->getWidth(), gfx->getHeight());
    graphics::RenderSystem::drawItems(*gfx, items, false);
}

int Map::pollConfigs() { return TileConfigSystem::poll(); }

int Map::getLastVisibleTileCount() const { return TileRenderSystem::lastVisibleTileCount(); }
int Map::getLastCustomVisualCount() const { return TileRenderSystem::lastCustomVisualCount(); }
int Map::getLastAtlasCount() const { return TileRenderSystem::lastAtlasCount(); }
int Map::getLastVisitedChunkCount() const { return TileRenderSystem::lastVisitedChunkCount(); }
int Map::getLastVisitedCellCount() const { return TileRenderSystem::lastVisitedCellCount(); }

int Map::publishCollision(TileLayer *layer) {
    collisionRects_.clear();
    if (!layer) return 0;
    const auto config = layer->config();
    const auto tiles = layer->tiles();
    const auto tileset = layer->tileset();
    if (config->orientation != MapOrientation::Orthogonal) return 0;

    std::vector<uint8_t> solid(size_t(config->mapW * config->mapH), 0);
    for (int y = 0; y < config->mapH; ++y) {
        for (int x = 0; x < config->mapW; ++x) {
            const int gid = int(tileGid(tiles->gids[size_t(y * config->mapW + x)]));
            const auto it = std::find_if(tileset->visuals.begin(), tileset->visuals.end(),
                                         [gid](const auto &v) { return v.gid == gid; });
            if (gid != 0 && it != tileset->visuals.end() && !it->collisionShapes.empty()) {
                const float originX = config->originX + x * (config->tileW + config->cellGapX);
                const float originY = config->originY + y * (config->tileH + config->cellGapY);
                for (const auto &shape : it->collisionShapes)
                    collisionRects_.push_back({originX + shape.x, originY + shape.y, shape.width, shape.height});
            } else {
                solid[size_t(y * config->mapW + x)] = gid != 0 && it != tileset->visuals.end() && !it->walkable;
            }
        }
    }
    for (int y = 0; y < config->mapH; ++y) {
        for (int x = 0; x < config->mapW; ++x) {
            if (!solid[size_t(y * config->mapW + x)]) continue;
            int width = 1;
            while (x + width < config->mapW && solid[size_t(y * config->mapW + x + width)])
                ++width;
            int height = 1;
            bool canGrow = true;
            while (y + height < config->mapH && canGrow) {
                for (int dx = 0; dx < width; ++dx)
                    if (!solid[size_t((y + height) * config->mapW + x + dx)]) canGrow = false;
                if (canGrow) ++height;
            }
            for (int dy = 0; dy < height; ++dy)
                for (int dx = 0; dx < width; ++dx)
                    solid[size_t((y + dy) * config->mapW + x + dx)] = 0;
            collisionRects_.push_back({config->originX + x * (config->tileW + config->cellGapX),
                                       config->originY + y * (config->tileH + config->cellGapY),
                                       width * (config->tileW + config->cellGapX),
                                       height * (config->tileH + config->cellGapY)});
        }
    }
    cap::forEach<ITileCollisionSink>([&](ITileCollisionSink *sink) {
        sink->replaceTileCollision(layer, collisionRects_.data(), collisionRects_.size());
    });
    return int(collisionRects_.size());
}

int Map::getCollisionRectCount() const { return int(collisionRects_.size()); }
float Map::getCollisionRectX(int index) const {
    return index >= 0 && index < int(collisionRects_.size())
               ? collisionRects_[size_t(index)].x
               : 0.f;
}
float Map::getCollisionRectY(int index) const {
    return index >= 0 && index < int(collisionRects_.size())
               ? collisionRects_[size_t(index)].y
               : 0.f;
}
float Map::getCollisionRectWidth(int index) const {
    return index >= 0 && index < int(collisionRects_.size())
               ? collisionRects_[size_t(index)].width
               : 0.f;
}
float Map::getCollisionRectHeight(int index) const {
    return index >= 0 && index < int(collisionRects_.size())
               ? collisionRects_[size_t(index)].height
               : 0.f;
}

int Map::getLayerCount() const {
    if (ecs::current()->getManager<TileLayer>() == nullptr) return 0;
    int n = 0;
    auto view = ecs::View<TileLayer, TileLayer::Config>();
    for (auto it = view.begin(); it != view.end(); ++it) ++n;
    return n;
}

TileLayer *Map::getLayer(int index) const {
    if (index < 0 || index >= int(loadedLayers_.size())) return nullptr;
    return loadedLayers_[size_t(index)];
}

void Map::setObjects(std::vector<MapObject> objects) { objects_ = std::move(objects); }

int Map::getObjectCount() const { return int(objects_.size()); }

std::string Map::getObjectName(int i) const {
    if (i < 0 || i >= int(objects_.size())) return {};
    return objects_[size_t(i)].name;
}

std::string Map::getObjectType(int i) const {
    if (i < 0 || i >= int(objects_.size())) return {};
    return objects_[size_t(i)].type;
}

float Map::getObjectX(int i) const {
    if (i < 0 || i >= int(objects_.size())) return 0.f;
    return objects_[size_t(i)].x;
}

float Map::getObjectY(int i) const {
    if (i < 0 || i >= int(objects_.size())) return 0.f;
    return objects_[size_t(i)].y;
}

float Map::getObjectWidth(int i) const {
    if (i < 0 || i >= int(objects_.size())) return 0.f;
    return objects_[size_t(i)].width;
}

float Map::getObjectHeight(int i) const {
    if (i < 0 || i >= int(objects_.size())) return 0.f;
    return objects_[size_t(i)].height;
}

int Map::getObjectGid(int i) const {
    if (i < 0 || i >= int(objects_.size())) return 0;
    return int(objects_[size_t(i)].gid);
}

eve::Result<int> Map::loadFromFileWithObjectContract(const std::string &path,
                                                     std::string_view contractJson) {
    std::vector<MapObject> objects;
    std::string error;
    auto layers = loadMapFile(path, &objects, &error);
    if (layers.empty()) {
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, error.empty() ? "map could not be loaded" : error, path, {},
            "map.load"));
    }
    auto admitted = validateMapObjects(objects, contractJson);
    if (!admitted.ok()) {
        discardCandidateLayers(layers);
        return eve::Result<int>::failure(admitted.status());
    }
    retireReplacedLayers(loadedLayers_, layers);
    setObjects(std::move(objects));
    loadedLayers_ = std::move(layers);
    return eve::Result<int>::success(static_cast<int>(loadedLayers_.size()),
                                     eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<int> Map::loadFromTextWithObjectContract(std::string_view mapJson,
                                                     std::string_view contractJson) {
    std::vector<MapObject> objects;
    std::string error;
    auto layers = loadMapText(std::string(mapJson), &objects, &error);
    if (layers.empty()) {
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, error.empty() ? "map text could not be loaded" : error,
            "$map", {}, "map.load"));
    }
    auto admitted = validateMapObjects(objects, contractJson);
    if (!admitted.ok()) {
        discardCandidateLayers(layers);
        return eve::Result<int>::failure(admitted.status());
    }
    retireReplacedLayers(loadedLayers_, layers);
    setObjects(std::move(objects));
    loadedLayers_ = std::move(layers);
    return eve::Result<int>::success(static_cast<int>(loadedLayers_.size()),
                                     eve::Status::success(eve::StatusCode::Applied));
}

int Map::getObjectPropertyCount(int objectIndex) const {
    if (objectIndex < 0 || objectIndex >= int(objects_.size())) return 0;
    return int(objects_[size_t(objectIndex)].properties.size());
}

std::string Map::getObjectPropertyName(int objectIndex, int propertyIndex) const {
    if (objectIndex < 0 || objectIndex >= int(objects_.size()) || propertyIndex < 0) return {};
    const auto &properties = objects_[size_t(objectIndex)].properties;
    if (propertyIndex >= int(properties.size())) return {};
    auto it = properties.begin();
    std::advance(it, propertyIndex);
    return it->first;
}

bool Map::hasObjectProperty(int objectIndex, std::string_view name) const {
    if (objectIndex < 0 || objectIndex >= int(objects_.size())) return false;
    return objects_[size_t(objectIndex)].properties.contains(std::string(name));
}

std::string Map::getObjectProperty(int objectIndex, std::string_view name, std::string defaultValue) const {
    if (objectIndex < 0 || objectIndex >= int(objects_.size())) return defaultValue;
    const auto &properties = objects_[size_t(objectIndex)].properties;
    auto it = properties.find(std::string(name));
    return it == properties.end() ? defaultValue : it->second;
}

std::optional<std::size_t> Map::findObjectByName(std::string_view name) const {
    for (std::size_t index = 0; index < objects_.size(); ++index)
        if (objects_[index].name == name) return index;
    return std::nullopt;
}

std::optional<std::size_t> Map::findObjectAt(float x, float y, std::string_view type) const {
    if (!std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
    for (std::size_t index = 0; index < objects_.size(); ++index) {
        const MapObject &object = objects_[index];
        if (!type.empty() && object.type != type) continue;
        const bool point = object.width <= 0.f && object.height <= 0.f;
        if (point ? (object.x == x && object.y == y)
                  : (x >= object.x && y >= object.y && x <= object.x + std::max(0.f, object.width) &&
                     y <= object.y + std::max(0.f, object.height)))
            return index;
    }
    return std::nullopt;
}

bool Map::resolveDualGrid(TileLayer *logic, TileLayer *display) {
    DualGridOptions opts;
    const bool ok = eve::map::resolveDualGrid(logic, display, opts, &dualGridError_);
    return ok;
}

bool Map::resolveDualGridFilled(TileLayer *logic, TileLayer *display, int filledGid) {
    DualGridOptions opts;
    opts.filledGid = filledGid;
    const bool ok = eve::map::resolveDualGrid(logic, display, opts, &dualGridError_);
    return ok;
}

int Map::dualGridMaskAt(TileLayer *logic, int dx, int dy, int filledGid) {
    if (!logic) return 0;
    return eve::map::dualGridMaskAt(*logic, dx, dy, filledGid);
}

int Map::dualGridFrame(int mask) { return dualGridDefaultFrame(mask); }

float Map::dualGridOffsetX(TileLayer *logic) {
    if (!logic) return 0.f;
    float ox = 0.f, oy = 0.f;
    dualGridHalfOffset(*logic->config(), ox, oy);
    return ox;
}

float Map::dualGridOffsetY(TileLayer *logic) {
    if (!logic) return 0.f;
    float ox = 0.f, oy = 0.f;
    dualGridHalfOffset(*logic->config(), ox, oy);
    return oy;
}

std::string Map::lastDualGridError() const { return dualGridError_; }

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
    layer.addFunc("setCellGap", &TileLayer::setCellGap);
    layer.addFunc("setRenderSpacing", &TileLayer::setRenderSpacing);
    layer.addFunc("getCellGapX", &TileLayer::getCellGapX);
    layer.addFunc("getCellGapY", &TileLayer::getCellGapY);
    layer.addFunc("getRenderSpacingX", &TileLayer::getRenderSpacingX);
    layer.addFunc("getRenderSpacingY", &TileLayer::getRenderSpacingY);
    layer.addFunc("resize", &TileLayer::resize);
    layer.addFunc("setTile", &TileLayer::setTile);
    layer.addFunc("getTile", &TileLayer::getTile);
    layer.addFunc("fillRect", &TileLayer::fillRect);
    layer.addFunc("fill", &TileLayer::fill);
    layer.addFunc("clear", &TileLayer::clear);
    layer.addFunc("getRevision", &TileLayer::getRevision);
    layer.addFunc("getChunkSize", &TileLayer::getChunkSize);
    layer.addFunc("getChunkCount", &TileLayer::getChunkCount);
    layer.addFunc("getNonEmptyChunkCount", &TileLayer::getNonEmptyChunkCount);
    layer.addFunc("clearTileAnimation", &TileLayer::clearTileAnimation);
    layer.addFunc("addTileAnimationFrame", &TileLayer::addTileAnimationFrame);
    layer.addFunc("getTileAnimationFrameCount", &TileLayer::getTileAnimationFrameCount);
    layer.addFunc("setTileDataString", &TileLayer::setTileDataString);
    layer.addFunc("setTileDataNumber", &TileLayer::setTileDataNumber);
    layer.addFunc("setTileDataBool", &TileLayer::setTileDataBool);
    layer.addFunc("getTileDataType", &TileLayer::getTileDataType);
    layer.addFunc("getTileDataString", &TileLayer::getTileDataString);
    layer.addFunc("getTileDataNumber", &TileLayer::getTileDataNumber);
    layer.addFunc("getTileDataBool", &TileLayer::getTileDataBool);
    layer.addFunc("setTerrainRule", &TileLayer::setTerrainRule);
    layer.addFunc("defineAutotileFamily", &TileLayer::defineAutotileFamily);
    layer.addFunc("setAutotileRule", &TileLayer::setAutotileRule);
    layer.addFunc("clearTerrainRules", &TileLayer::clearTerrainRules);
    layer.addFunc("paintTerrain", &TileLayer::paintTerrain);
    layer.addFunc("paintTerrainRect", &TileLayer::paintTerrainRect);
    layer.addFunc("fillTerrain", &TileLayer::fillTerrain);
    layer.addFunc("eraseTerrainRect", &TileLayer::eraseTerrainRect);
    layer.addFunc("getTerrain", &TileLayer::getTerrain);
    layer.addFunc("setTileset", &TileLayer::setTileset);
    layer.addFunc("setTilesetTileSize", &TileLayer::setTilesetTileSize);
    layer.addFunc("setTileVisual", &TileLayer::setTileVisual);
    layer.addFunc("clearTileVisuals", &TileLayer::clearTileVisuals);
    layer.addFunc("getTileVisualCount", &TileLayer::getTileVisualCount);
    layer.addFunc("setTileMetadata", &TileLayer::setTileMetadata);
    layer.addFunc("setTileNavigationProfile", &TileLayer::setTileNavigationProfile);
    layer.addFunc("loadTilesetManifest", &TileLayer::loadTilesetManifest);
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
    layer.addFunc("tileToWorldX", &TileLayer::tileToWorldX);
    layer.addFunc("tileToWorldY", &TileLayer::tileToWorldY);
    layer.addFunc("depthYAt", &TileLayer::depthYAt);
    layer.addFunc("worldToTileX", &TileLayer::worldToTileX);
    layer.addFunc("worldToTileY", &TileLayer::worldToTileY);

    auto path = table.addClass<Path>(
        "Path", std::function<Path *()>([]() -> Path * { return nullptr; }), true);
    path.addFunc("getLength", &Path::getLength);
    path.addFunc("getX", &Path::getX);
    path.addFunc("getY", &Path::getY);
    path.addFunc("getTotalCost", &Path::getTotalCost);

    auto field = table.addClass<FlowField>(
        "FlowField", std::function<FlowField *()>([]() -> FlowField * { return nullptr; }), true);
    field.addFunc("getWidth", &FlowField::getWidth);
    field.addFunc("getHeight", &FlowField::getHeight);
    field.addFunc("getGoalX", &FlowField::getGoalX);
    field.addFunc("getGoalY", &FlowField::getGoalY);
    field.addFunc("costAt", &FlowField::costAt);
    field.addFunc("nextX", &FlowField::nextX);
    field.addFunc("nextY", &FlowField::nextY);
    field.addFunc("isReachable", &FlowField::isReachable);

    auto pf = table.addClass<Pathfinder>(
        "Pathfinder", std::function<Pathfinder *()>([]() -> Pathfinder * { return nullptr; }), true);
    pf.addFunc("setTopology", &Pathfinder::setTopology);
    pf.addFunc("getTopology", &Pathfinder::getTopology);
    pf.addFunc("setDiagonal", &Pathfinder::setDiagonal);
    pf.addFunc("getDiagonal", &Pathfinder::getDiagonal);
    pf.addFunc("blockGid", &Pathfinder::blockGid);
    pf.addFunc("unblockGid", &Pathfinder::unblockGid);
    pf.addFunc("clearBlockedGids", &Pathfinder::clearBlockedGids);
    pf.addFunc("setBlockEmpty", &Pathfinder::setBlockEmpty);
    pf.addFunc("getBlockEmpty", &Pathfinder::getBlockEmpty);
    pf.addFunc("setBlocked", &Pathfinder::setBlocked);
    pf.addFunc("isWalkable", &Pathfinder::isWalkable);
    pf.addFunc("setCellCost", &Pathfinder::setCellCost);
    pf.addFunc("getCellCost", &Pathfinder::getCellCost);
    pf.addFunc("syncFromLayer", &Pathfinder::syncFromLayer);
    pf.addFunc("findPath", &Pathfinder::findPath);
    pf.addFunc("buildFlowField", &Pathfinder::buildFlowField);
    pf.addFunc("followFlow", &Pathfinder::followFlow);
    pf.addFunc("findGroupPath", &Pathfinder::findGroupPath);
    pf.addFunc("invalidateCache", &Pathfinder::invalidateCache);

    auto fov = table.addClass<Fov>("Fov", std::function<Fov *()>([]() -> Fov * { return nullptr; }),
                                   true);
    fov.addFunc("getWidth", &Fov::getWidth);
    fov.addFunc("getHeight", &Fov::getHeight);
    fov.addFunc("getDepth", &Fov::getDepth);
    fov.addFunc("setMode", &Fov::setMode);
    fov.addFunc("getMode", &Fov::getMode);
    fov.addFunc("setAlgorithm", &Fov::setAlgorithm);
    fov.addFunc("getAlgorithm", &Fov::getAlgorithm);
    fov.addFunc("setRadiusMetric", &Fov::setRadiusMetric);
    fov.addFunc("getRadiusMetric", &Fov::getRadiusMetric);
    fov.addFunc("setTopology", &Fov::setTopology);
    fov.addFunc("getTopology", &Fov::getTopology);
    fov.addFunc("setCornerPeek", &Fov::setCornerPeek);
    fov.addFunc("getCornerPeek", &Fov::getCornerPeek);
    fov.addFunc("blockOpaqueGid", &Fov::blockOpaqueGid);
    fov.addFunc("unblockOpaqueGid", &Fov::unblockOpaqueGid);
    fov.addFunc("clearOpaqueGids", &Fov::clearOpaqueGids);
    fov.addFunc("setBlockEmpty", &Fov::setBlockEmpty);
    fov.addFunc("getBlockEmpty", &Fov::getBlockEmpty);
    fov.addFunc("setOpaque", &Fov::setOpaque);
    fov.addFunc("isOpaque", &Fov::isOpaque);
    fov.addFunc("setOpaque3", &Fov::setOpaque3);
    fov.addFunc("isOpaque3", &Fov::isOpaque3);
    fov.addFunc("syncFromLayer", &Fov::syncFromLayer);
    fov.addFunc("setElevation", &Fov::setElevation);
    fov.addFunc("getElevation", &Fov::getElevation);
    fov.addFunc("setCliffBlock", &Fov::setCliffBlock);
    fov.addFunc("getCliffBlock", &Fov::getCliffBlock);
    fov.addFunc("setEyeOffset", &Fov::setEyeOffset);
    fov.addFunc("getEyeOffset", &Fov::getEyeOffset);
    fov.addFunc("setVerticalRange", &Fov::setVerticalRange);
    fov.addFunc("getVerticalRange", &Fov::getVerticalRange);
    fov.addFunc("addRevealer", &Fov::addRevealer);
    fov.addFunc("addRevealer3", &Fov::addRevealer3);
    fov.addFunc("removeRevealer", &Fov::removeRevealer);
    fov.addFunc("clearRevealers", &Fov::clearRevealers);
    fov.addFunc("setRevealerPosition", &Fov::setRevealerPosition);
    fov.addFunc("setRevealerPosition3", &Fov::setRevealerPosition3);
    fov.addFunc("setRevealerRadius", &Fov::setRevealerRadius);
    fov.addFunc("setRevealerFacing", &Fov::setRevealerFacing);
    fov.addFunc("clearRevealerFacing", &Fov::clearRevealerFacing);
    fov.addFunc("setRevealerEnabled", &Fov::setRevealerEnabled);
    fov.addFunc("getRevealerCount", &Fov::getRevealerCount);
    fov.addFunc("setRevealerPerception", &Fov::setRevealerPerception);
    fov.addFunc("getRevealerPerception", &Fov::getRevealerPerception);
    fov.addFunc("setPerceptionRadiusScale", &Fov::setPerceptionRadiusScale);
    fov.addFunc("getPerceptionRadiusScale", &Fov::getPerceptionRadiusScale);
    fov.addFunc("setDetectionMargin", &Fov::setDetectionMargin);
    fov.addFunc("getDetectionMargin", &Fov::getDetectionMargin);
    fov.addFunc("getEffectiveRadius", &Fov::getEffectiveRadius);
    fov.addFunc("canDetect", &Fov::canDetect);
    fov.addFunc("canDetect3", &Fov::canDetect3);
    fov.addFunc("markDirty", &Fov::markDirty);
    fov.addFunc("isDirty", &Fov::isDirty);
    fov.addFunc("compute", &Fov::compute);
    fov.addFunc("isVisible", &Fov::isVisible);
    fov.addFunc("isExplored", &Fov::isExplored);
    fov.addFunc("isVisible3", &Fov::isVisible3);
    fov.addFunc("isExplored3", &Fov::isExplored3);
    fov.addFunc("getState", &Fov::getState);
    fov.addFunc("getState3", &Fov::getState3);
    fov.addFunc("clearMemory", &Fov::clearMemory);
    fov.addFunc("resetVisibleOnly", &Fov::resetVisibleOnly);
    fov.addFunc("getMaskValue", &Fov::getMaskValue);
    fov.addFunc("getMaskByte", &Fov::getMaskByte);
    fov.addFunc("getMaskValue3", &Fov::getMaskValue3);
    fov.addFunc("getMaskByte3", &Fov::getMaskByte3);
    fov.addFunc("buildMaskTexture", &Fov::buildMaskTexture);
    fov.addFunc("buildMaskTextureSlice", &Fov::buildMaskTextureSlice);
}

void Map::expose(ssq::Class &cls) {
    const HSQUIRRELVM vm = cls.getHandle();
    cls.addFunc("getName", &Map::getName);
    cls.addFunc("newLayer", &Map::newLayer);
    cls.addFunc("newLayerFromFile", &Map::newLayerFromFile);
    cls.addFunc("loadFromFile", &Map::loadFromFile);
    cls.addFunc("loadFromFileWithObjectContract",
                [vm](Map *value, const std::string &path, const std::string &contractJson) {
                    if (!value)
                        return eve::script::projectResult(
                            vm, eve::Result<int>::failure(eve::Diagnostic::error(
                                    eve::DiagnosticCode::InvalidArgument, "Map receiver must not be null", "map",
                                    {}, "map.squirrel")),
                            [](int count) { return eve::Value(count); });
                    return eve::script::projectResult(vm,
                                                      value->loadFromFileWithObjectContract(path, contractJson),
                                                      [](int count) { return eve::Value(count); });
                });
    cls.addFunc("newPathfinder", &Map::newPathfinder);
    cls.addFunc("newPathfinderSize", &Map::newPathfinderSize);
    cls.addFunc("newFov", &Map::newFov);
    cls.addFunc("newFovSize", &Map::newFovSize);
    cls.addFunc("newFovVolume", &Map::newFovVolume);
    cls.addFunc("update", &Map::update);
    cls.addFunc("render", &Map::render);
    cls.addFunc("pollConfigs", &Map::pollConfigs);
    cls.addFunc("getLastVisibleTileCount", &Map::getLastVisibleTileCount);
    cls.addFunc("getLastCustomVisualCount", &Map::getLastCustomVisualCount);
    cls.addFunc("getLastAtlasCount", &Map::getLastAtlasCount);
    cls.addFunc("getLastVisitedChunkCount", &Map::getLastVisitedChunkCount);
    cls.addFunc("getLastVisitedCellCount", &Map::getLastVisitedCellCount);
    cls.addFunc("publishCollision", &Map::publishCollision);
    cls.addFunc("getCollisionRectCount", &Map::getCollisionRectCount);
    cls.addFunc("getCollisionRectX", &Map::getCollisionRectX);
    cls.addFunc("getCollisionRectY", &Map::getCollisionRectY);
    cls.addFunc("getCollisionRectWidth", &Map::getCollisionRectWidth);
    cls.addFunc("getCollisionRectHeight", &Map::getCollisionRectHeight);
    cls.addFunc("getLayerCount", &Map::getLayerCount);
    cls.addFunc("getLayer", &Map::getLayer);
    cls.addFunc("getObjectCount", &Map::getObjectCount);
    cls.addFunc("getObjectName", &Map::getObjectName);
    cls.addFunc("getObjectType", &Map::getObjectType);
    cls.addFunc("getObjectX", &Map::getObjectX);
    cls.addFunc("getObjectY", &Map::getObjectY);
    cls.addFunc("getObjectWidth", &Map::getObjectWidth);
    cls.addFunc("getObjectHeight", &Map::getObjectHeight);
    cls.addFunc("getObjectGid", &Map::getObjectGid);
    cls.addFunc("getObjectPropertyCount", &Map::getObjectPropertyCount);
    cls.addFunc("getObjectPropertyName", &Map::getObjectPropertyName);
    cls.addFunc("hasObjectProperty", [](Map *self, int objectIndex, const std::string &name) {
        return self && self->hasObjectProperty(objectIndex, name);
    });
    cls.addFunc("getObjectProperty",
                [](Map *self, int objectIndex, const std::string &name, const std::string &defaultValue) {
                    return self ? self->getObjectProperty(objectIndex, name, defaultValue) : defaultValue;
                });
    cls.addFunc("findObjectByName", [](Map *value, const std::string &name) -> int {
        if (!value) return -1;
        const auto found = value->findObjectByName(name);
        return found ? static_cast<int>(*found) : -1;
    });
    cls.addFunc("findObjectAt", [](Map *value, float x, float y, const std::string &type) -> int {
        if (!value) return -1;
        const auto found = value->findObjectAt(x, y, type);
        return found ? static_cast<int>(*found) : -1;
    });
    cls.addFunc("resolveDualGrid", &Map::resolveDualGrid);
    cls.addFunc("resolveDualGridFilled", &Map::resolveDualGridFilled);
    cls.addFunc("dualGridMaskAt", &Map::dualGridMaskAt);
    cls.addFunc("dualGridFrame", &Map::dualGridFrame);
    cls.addFunc("dualGridOffsetX", &Map::dualGridOffsetX);
    cls.addFunc("dualGridOffsetY", &Map::dualGridOffsetY);
    cls.addFunc("lastDualGridError", &Map::lastDualGridError);
}

}  // namespace eve::map
