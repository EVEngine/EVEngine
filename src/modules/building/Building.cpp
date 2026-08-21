#include "building/Building.h"
#include "building/BuildingDef.h"
#include "building/PlacementSystem.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::building {

Module_IMPL(Building, new Building());

int Building::registerBuildingsFromJson(const std::string &json) {
    PlacementSystem::ensureBuiltins();
    return BuildingRegistry::loadFromJson(json, nullptr);
}

void Building::clearBuildingDefinitions() { BuildingRegistry::clear(); }

int Building::getBuildingDefinitionCount() { return BuildingRegistry::count(); }

bool Building::hasBuildingDefinition(const std::string &buildingId) {
    return BuildingRegistry::find(buildingId) != nullptr;
}

std::string Building::getBuildingDisplayName(const std::string &buildingId) {
    const auto *def = BuildingRegistry::find(buildingId);
    return def ? def->displayName : std::string{};
}

std::string Building::getBuildingCategory(const std::string &buildingId) {
    const auto *def = BuildingRegistry::find(buildingId);
    return def ? def->category : std::string{};
}

int Building::getBuildingFootprintW(const std::string &buildingId) {
    const auto *def = BuildingRegistry::find(buildingId);
    return def ? def->footprintW : 0;
}

int Building::getBuildingFootprintH(const std::string &buildingId) {
    const auto *def = BuildingRegistry::find(buildingId);
    return def ? def->footprintH : 0;
}

std::string Building::getBuildingSnapMode(const std::string &buildingId) {
    const auto *def = BuildingRegistry::find(buildingId);
    return def ? def->snapMode : std::string{};
}

std::string Building::getBuildingRotationMode(const std::string &buildingId) {
    const auto *def = BuildingRegistry::find(buildingId);
    return def ? def->rotationMode : std::string{};
}

std::string Building::getBuildingValidateRule(const std::string &buildingId) {
    const auto *def = BuildingRegistry::find(buildingId);
    return def ? def->validateRule : std::string{};
}

std::string Building::getBuildingChannel(const std::string &buildingId) {
    const auto *def = BuildingRegistry::find(buildingId);
    return def ? def->channel : std::string{};
}

std::string Building::getBuildingRenderMode(const std::string &buildingId) {
    const auto *def = BuildingRegistry::find(buildingId);
    return def ? def->renderMode : std::string{};
}

std::string Building::getBuildingVisual2d(const std::string &buildingId, const std::string &key,
                                          const std::string &fallback) {
    const auto *def = BuildingRegistry::find(buildingId);
    return def ? def->getVisual2d(key, fallback) : fallback;
}

std::string Building::getBuildingVisual3d(const std::string &buildingId, const std::string &key,
                                          const std::string &fallback) {
    const auto *def = BuildingRegistry::find(buildingId);
    return def ? def->getVisual3d(key, fallback) : fallback;
}

bool Building::buildingHasTag(const std::string &buildingId, const std::string &tag) {
    const auto *def = BuildingRegistry::find(buildingId);
    return def ? def->hasTag(tag) : false;
}

std::string Building::getBuildingExtra(const std::string &buildingId, const std::string &key,
                                       const std::string &fallback) {
    const auto *def = BuildingRegistry::find(buildingId);
    return def ? def->getExtra(key, fallback) : fallback;
}

int Building::getBuildingCost(const std::string &buildingId, const std::string &resource) {
    const auto *def = BuildingRegistry::find(buildingId);
    return def ? def->getCost(resource, 0) : 0;
}

PlacementWorld *Building::newWorld(int width, int height, float cellSize) {
    PlacementSystem::ensureBuiltins();
    return new PlacementWorld(width, height, cellSize);
}

Ghost *Building::newGhost() {
    PlacementSystem::ensureBuiltins();
    return new Ghost();
}

PlacementSession *Building::newSession() {
    PlacementSystem::ensureBuiltins();
    return new PlacementSession();
}

bool Building::hasValidateRule(const std::string &ruleName) {
    return PlacementSystem::hasValidateRule(ruleName);
}

bool Building::hasSnapRule(const std::string &ruleName) {
    return PlacementSystem::hasSnapRule(ruleName);
}

bool Building::hasSurface(const std::string &surfaceName) {
    return PlacementSystem::hasSurface(surfaceName);
}

int Building::getSurfaceCount() {
    return int(PlacementSystem::surfaceNames().size());
}

std::string Building::getSurfaceName(int index) {
    const auto names = PlacementSystem::surfaceNames();
    if (index < 0 || index >= int(names.size())) return {};
    return names[size_t(index)];
}

void Building::setPlaneSurfaceHeight(float h) { PlacementSystem::setPlaneSurfaceHeight(h); }

float Building::getPlaneSurfaceHeight() { return PlacementSystem::getPlaneSurfaceHeight(); }

void Building::clearChangeEvents() { PlacementSystem::clearEvents(); }

int Building::getChangeEventCount() const { return int(PlacementSystem::events().size()); }

std::string Building::getChangeEventAction(int index) const {
    const auto &evs = PlacementSystem::events();
    if (index < 0 || size_t(index) >= evs.size()) return {};
    return evs[size_t(index)].action;
}

std::string Building::getChangeEventWorldId(int index) const {
    const auto &evs = PlacementSystem::events();
    if (index < 0 || size_t(index) >= evs.size()) return {};
    return evs[size_t(index)].worldId;
}

std::string Building::getChangeEventBuildingId(int index) const {
    const auto &evs = PlacementSystem::events();
    if (index < 0 || size_t(index) >= evs.size()) return {};
    return evs[size_t(index)].buildingId;
}

int Building::getChangeEventInstanceId(int index) const {
    const auto &evs = PlacementSystem::events();
    if (index < 0 || size_t(index) >= evs.size()) return 0;
    return evs[size_t(index)].instanceId;
}

int Building::getChangeEventCellX(int index) const {
    const auto &evs = PlacementSystem::events();
    if (index < 0 || size_t(index) >= evs.size()) return -1;
    return evs[size_t(index)].cellX;
}

int Building::getChangeEventCellY(int index) const {
    const auto &evs = PlacementSystem::events();
    if (index < 0 || size_t(index) >= evs.size()) return -1;
    return evs[size_t(index)].cellY;
}

int Building::getChangeEventOtherCellX(int index) const {
    const auto &evs = PlacementSystem::events();
    if (index < 0 || size_t(index) >= evs.size()) return -1;
    return evs[size_t(index)].otherCellX;
}

int Building::getChangeEventOtherCellY(int index) const {
    const auto &evs = PlacementSystem::events();
    if (index < 0 || size_t(index) >= evs.size()) return -1;
    return evs[size_t(index)].otherCellY;
}

float Building::getChangeEventRotation(int index) const {
    const auto &evs = PlacementSystem::events();
    if (index < 0 || size_t(index) >= evs.size()) return 0.f;
    return evs[size_t(index)].rotationDeg;
}

void Building::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Building::create, false);
    expose(cls);

    auto world = table.addClass<PlacementWorld>(
        "PlacementWorld",
        std::function<PlacementWorld *(int, int, float)>(
            [](int w, int h, float cs) -> PlacementWorld * {
                return new PlacementWorld(w, h, cs);
            }),
        true);
    world.addFunc("destroy", &PlacementWorld::destroy);
    world.addFunc("getId", &PlacementWorld::getId);
    world.addFunc("setId", &PlacementWorld::setId);
    world.addFunc("getWidth", &PlacementWorld::getWidth);
    world.addFunc("getHeight", &PlacementWorld::getHeight);
    world.addFunc("getCellSize", &PlacementWorld::getCellSize);
    world.addFunc("setCellSize", &PlacementWorld::setCellSize);
    world.addFunc("getOriginX", &PlacementWorld::getOriginX);
    world.addFunc("getOriginY", &PlacementWorld::getOriginY);
    world.addFunc("setOrigin", &PlacementWorld::setOrigin);
    world.addFunc("getSnapMode", &PlacementWorld::getSnapMode);
    world.addFunc("setSnapMode", &PlacementWorld::setSnapMode);
    world.addFunc("getValidateRule", &PlacementWorld::getValidateRule);
    world.addFunc("setValidateRule", &PlacementWorld::setValidateRule);
    world.addFunc("setGridLayout", &PlacementWorld::setGridLayout);
    world.addFunc("getGridLayoutName", &PlacementWorld::getGridLayoutName);
    world.addFunc("setGridPlane", &PlacementWorld::setGridPlane);
    world.addFunc("getGridPlaneName", &PlacementWorld::getGridPlaneName);
    world.addFunc("setCellGap", &PlacementWorld::setCellGap);
    world.addFunc("setHexSideLength", &PlacementWorld::setHexSideLength);
    world.addFunc("setStagger", &PlacementWorld::setStagger);
    world.addFunc("setGridFromLayer", &PlacementWorld::setGridFromLayer);
    world.addFunc("hasGridFromLayer", &PlacementWorld::hasGridFromLayer);
    world.addFunc("setExtra", &PlacementWorld::setExtra);
    world.addFunc("getExtra", &PlacementWorld::getExtra);
    world.addFunc("worldToCellX", &PlacementWorld::worldToCellX);
    world.addFunc("worldToCellY", &PlacementWorld::worldToCellY);
    world.addFunc("cellToWorldX", &PlacementWorld::cellToWorldX);
    world.addFunc("cellToWorldY", &PlacementWorld::cellToWorldY);
    world.addFunc("cellToWorld3DX", &PlacementWorld::cellToWorld3DX);
    world.addFunc("cellToWorld3DY", &PlacementWorld::cellToWorld3DY);
    world.addFunc("cellToWorld3DZ", &PlacementWorld::cellToWorld3DZ);
    world.addFunc("worldToCell3DX", &PlacementWorld::worldToCell3DX);
    world.addFunc("worldToCell3DY", &PlacementWorld::worldToCell3DY);
    world.addFunc("fillTerrain", &PlacementWorld::fillTerrain);
    world.addFunc("setTerrain", &PlacementWorld::setTerrain);
    world.addFunc("getTerrain", &PlacementWorld::getTerrain);
    world.addFunc("inBounds", &PlacementWorld::inBounds);
    world.addFunc("bindTileLayer", &PlacementWorld::bindTileLayer);
    world.addFunc("getTileLayer", &PlacementWorld::getTileLayer);
    world.addFunc("clearTileLayer", &PlacementWorld::clearTileLayer);
    world.addFunc("setTerrainGidMapJson", &PlacementWorld::setTerrainGidMapJson);
    world.addFunc("setTerrainGid", &PlacementWorld::setTerrainGid);
    world.addFunc("clearTerrainGidMap", &PlacementWorld::clearTerrainGidMap);
    world.addFunc("getOccupant", &PlacementWorld::getOccupant);
    world.addFunc("getOccupantInChannel", &PlacementWorld::getOccupantInChannel);
    world.addFunc("getAnyOccupant", &PlacementWorld::getAnyOccupant);
    world.addFunc("isCellEmpty", &PlacementWorld::isCellEmpty);
    world.addFunc("isCellEmptyInChannel", &PlacementWorld::isCellEmptyInChannel);
    world.addFunc("getBuildingCount", &PlacementWorld::getBuildingCount);
    world.addFunc("hasBuilding", &PlacementWorld::hasBuilding);
    world.addFunc("getBuildingId", &PlacementWorld::getBuildingId);
    world.addFunc("getBuildingCellX", &PlacementWorld::getBuildingCellX);
    world.addFunc("getBuildingCellY", &PlacementWorld::getBuildingCellY);
    world.addFunc("getBuildingWorldX", &PlacementWorld::getBuildingWorldX);
    world.addFunc("getBuildingWorldY", &PlacementWorld::getBuildingWorldY);
    world.addFunc("getBuildingWorldZ", &PlacementWorld::getBuildingWorldZ);
    world.addFunc("getBuildingElevation", &PlacementWorld::getBuildingElevation);
    world.addFunc("getBuildingChannel", &PlacementWorld::getBuildingChannel);
    world.addFunc("getBuildingRotation", &PlacementWorld::getBuildingRotation);
    world.addFunc("getBuildingProp", &PlacementWorld::getBuildingProp);
    world.addFunc("setBuildingProp", &PlacementWorld::setBuildingProp);
    world.addFunc("buildingHasTag", &PlacementWorld::buildingHasTag);
    world.addFunc("getBuildingInstanceAt", &PlacementWorld::getBuildingInstanceAt);
    world.addFunc("canPlace", &PlacementWorld::canPlace);
    world.addFunc("canPlaceReason", &PlacementWorld::canPlaceReason);
    world.addFunc("placeAt", &PlacementWorld::placeAt);
    world.addFunc("placeAtWorld", &PlacementWorld::placeAtWorld);
    world.addFunc("placeAtWorld3D", &PlacementWorld::placeAtWorld3D);
    world.addFunc("placeGhost", &PlacementWorld::placeGhost);
    world.addFunc("removeBuilding", &PlacementWorld::removeBuilding);
    world.addFunc("moveBuilding", &PlacementWorld::moveBuilding);
    world.addFunc("clearBuildings", &PlacementWorld::clearBuildings);

    auto ghost = table.addClass<Ghost>(
        "Ghost", std::function<Ghost *()>([]() -> Ghost * { return new Ghost(); }), true);
    ghost.addFunc("destroy", &Ghost::destroy);
    ghost.addFunc("getBuildingId", &Ghost::getBuildingId);
    ghost.addFunc("setBuildingId", &Ghost::setBuildingId);
    ghost.addFunc("getCellX", &Ghost::getCellX);
    ghost.addFunc("getCellY", &Ghost::getCellY);
    ghost.addFunc("setCell", &Ghost::setCell);
    ghost.addFunc("getWorldX", &Ghost::getWorldX);
    ghost.addFunc("getWorldY", &Ghost::getWorldY);
    ghost.addFunc("setWorld", &Ghost::setWorld);
    ghost.addFunc("getElevation", &Ghost::getElevation);
    ghost.addFunc("setElevation", &Ghost::setElevation);
    ghost.addFunc("getRotationDeg", &Ghost::getRotationDeg);
    ghost.addFunc("setRotationDeg", &Ghost::setRotationDeg);
    ghost.addFunc("rotateBy", &Ghost::rotateBy);
    ghost.addFunc("isValid", &Ghost::isValid);
    ghost.addFunc("getReason", &Ghost::getReason);
    ghost.addFunc("setFromWorld", &Ghost::setFromWorld);
    ghost.addFunc("setFromWorld3D", &Ghost::setFromWorld3D);
    ghost.addFunc("setFromSurface", &Ghost::setFromSurface);
    ghost.addFunc("validate", &Ghost::validate);

    auto session = table.addClass<PlacementSession>(
        "PlacementSession", std::function<PlacementSession *()>([]() -> PlacementSession * {
            return new PlacementSession();
        }),
        true);
    session.addFunc("destroy", &PlacementSession::destroy);
    session.addFunc("startPlacement", &PlacementSession::startPlacement);
    session.addFunc("stopPlacement", &PlacementSession::stopPlacement);
    session.addFunc("isActive", &PlacementSession::isActive);
    session.addFunc("getBuildingId", &PlacementSession::getBuildingId);
    session.addFunc("getGhost", &PlacementSession::getGhost);
    session.addFunc("setMode", &PlacementSession::setMode);
    session.addFunc("getMode", &PlacementSession::getMode);
    session.addFunc("setRotationDeg", &PlacementSession::setRotationDeg);
    session.addFunc("rotateBy", &PlacementSession::rotateBy);
    session.addFunc("getRotationDeg", &PlacementSession::getRotationDeg);
    session.addFunc("updateFromWorld", &PlacementSession::updateFromWorld);
    session.addFunc("updateFromWorld3D", &PlacementSession::updateFromWorld3D);
    session.addFunc("updateFromSurface", &PlacementSession::updateFromSurface);
    session.addFunc("isValid", &PlacementSession::isValid);
    session.addFunc("getReason", &PlacementSession::getReason);
    session.addFunc("execute", &PlacementSession::execute);
}

void Building::expose(ssq::Class &cls) {
    cls.addFunc("registerBuildingsFromJson", &Building::registerBuildingsFromJson);
    cls.addFunc("clearBuildingDefinitions", &Building::clearBuildingDefinitions);
    cls.addFunc("getBuildingDefinitionCount", &Building::getBuildingDefinitionCount);
    cls.addFunc("hasBuildingDefinition", &Building::hasBuildingDefinition);
    cls.addFunc("getBuildingDisplayName", &Building::getBuildingDisplayName);
    cls.addFunc("getBuildingCategory", &Building::getBuildingCategory);
    cls.addFunc("getBuildingFootprintW", &Building::getBuildingFootprintW);
    cls.addFunc("getBuildingFootprintH", &Building::getBuildingFootprintH);
    cls.addFunc("getBuildingSnapMode", &Building::getBuildingSnapMode);
    cls.addFunc("getBuildingRotationMode", &Building::getBuildingRotationMode);
    cls.addFunc("getBuildingValidateRule", &Building::getBuildingValidateRule);
    cls.addFunc("getBuildingChannel", &Building::getBuildingChannel);
    cls.addFunc("getBuildingRenderMode", &Building::getBuildingRenderMode);
    cls.addFunc("getBuildingVisual2d", &Building::getBuildingVisual2d);
    cls.addFunc("getBuildingVisual3d", &Building::getBuildingVisual3d);
    cls.addFunc("buildingHasTag", &Building::buildingHasTag);
    cls.addFunc("getBuildingExtra", &Building::getBuildingExtra);
    cls.addFunc("getBuildingCost", &Building::getBuildingCost);
    cls.addFunc("newWorld", &Building::newWorld);
    cls.addFunc("newGhost", &Building::newGhost);
    cls.addFunc("newSession", &Building::newSession);
    cls.addFunc("hasValidateRule", &Building::hasValidateRule);
    cls.addFunc("hasSnapRule", &Building::hasSnapRule);
    cls.addFunc("hasSurface", &Building::hasSurface);
    cls.addFunc("getSurfaceCount", &Building::getSurfaceCount);
    cls.addFunc("getSurfaceName", &Building::getSurfaceName);
    cls.addFunc("setPlaneSurfaceHeight", &Building::setPlaneSurfaceHeight);
    cls.addFunc("getPlaneSurfaceHeight", &Building::getPlaneSurfaceHeight);
    cls.addFunc("clearChangeEvents", &Building::clearChangeEvents);
    cls.addFunc("getChangeEventCount", &Building::getChangeEventCount);
    cls.addFunc("getChangeEventAction", &Building::getChangeEventAction);
    cls.addFunc("getChangeEventWorldId", &Building::getChangeEventWorldId);
    cls.addFunc("getChangeEventBuildingId", &Building::getChangeEventBuildingId);
    cls.addFunc("getChangeEventInstanceId", &Building::getChangeEventInstanceId);
    cls.addFunc("getChangeEventCellX", &Building::getChangeEventCellX);
    cls.addFunc("getChangeEventCellY", &Building::getChangeEventCellY);
    cls.addFunc("getChangeEventOtherCellX", &Building::getChangeEventOtherCellX);
    cls.addFunc("getChangeEventOtherCellY", &Building::getChangeEventOtherCellY);
    cls.addFunc("getChangeEventRotation", &Building::getChangeEventRotation);
}

}  // namespace eve::building
