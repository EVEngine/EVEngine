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

bool Building::hasValidateRule(const std::string &name) {
    return PlacementSystem::hasValidateRule(name);
}

bool Building::hasSnapRule(const std::string &name) {
    return PlacementSystem::hasSnapRule(name);
}

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
    world.addFunc("setExtra", &PlacementWorld::setExtra);
    world.addFunc("getExtra", &PlacementWorld::getExtra);
    world.addFunc("worldToCellX", &PlacementWorld::worldToCellX);
    world.addFunc("worldToCellY", &PlacementWorld::worldToCellY);
    world.addFunc("cellToWorldX", &PlacementWorld::cellToWorldX);
    world.addFunc("cellToWorldY", &PlacementWorld::cellToWorldY);
    world.addFunc("fillTerrain", &PlacementWorld::fillTerrain);
    world.addFunc("setTerrain", &PlacementWorld::setTerrain);
    world.addFunc("getTerrain", &PlacementWorld::getTerrain);
    world.addFunc("inBounds", &PlacementWorld::inBounds);
    world.addFunc("getOccupant", &PlacementWorld::getOccupant);
    world.addFunc("isCellEmpty", &PlacementWorld::isCellEmpty);
    world.addFunc("getBuildingCount", &PlacementWorld::getBuildingCount);
    world.addFunc("hasBuilding", &PlacementWorld::hasBuilding);
    world.addFunc("getBuildingId", &PlacementWorld::getBuildingId);
    world.addFunc("getBuildingCellX", &PlacementWorld::getBuildingCellX);
    world.addFunc("getBuildingCellY", &PlacementWorld::getBuildingCellY);
    world.addFunc("getBuildingWorldX", &PlacementWorld::getBuildingWorldX);
    world.addFunc("getBuildingWorldY", &PlacementWorld::getBuildingWorldY);
    world.addFunc("getBuildingRotation", &PlacementWorld::getBuildingRotation);
    world.addFunc("getBuildingProp", &PlacementWorld::getBuildingProp);
    world.addFunc("setBuildingProp", &PlacementWorld::setBuildingProp);
    world.addFunc("buildingHasTag", &PlacementWorld::buildingHasTag);
    world.addFunc("getBuildingInstanceAt", &PlacementWorld::getBuildingInstanceAt);
    world.addFunc("canPlace", &PlacementWorld::canPlace);
    world.addFunc("canPlaceReason", &PlacementWorld::canPlaceReason);
    world.addFunc("placeAt", &PlacementWorld::placeAt);
    world.addFunc("placeAtWorld", &PlacementWorld::placeAtWorld);
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
    ghost.addFunc("getRotationDeg", &Ghost::getRotationDeg);
    ghost.addFunc("setRotationDeg", &Ghost::setRotationDeg);
    ghost.addFunc("rotateBy", &Ghost::rotateBy);
    ghost.addFunc("isValid", &Ghost::isValid);
    ghost.addFunc("getReason", &Ghost::getReason);
    ghost.addFunc("setFromWorld", &Ghost::setFromWorld);
    ghost.addFunc("validate", &Ghost::validate);
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
    cls.addFunc("buildingHasTag", &Building::buildingHasTag);
    cls.addFunc("getBuildingExtra", &Building::getBuildingExtra);
    cls.addFunc("getBuildingCost", &Building::getBuildingCost);
    cls.addFunc("newWorld", &Building::newWorld);
    cls.addFunc("newGhost", &Building::newGhost);
    cls.addFunc("hasValidateRule", &Building::hasValidateRule);
    cls.addFunc("hasSnapRule", &Building::hasSnapRule);
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
