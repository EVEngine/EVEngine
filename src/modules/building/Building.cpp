#include "building/Building.h"
#include <algorithm>
#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>
#include "building/BuildingDef.h"
#include "building/PlacementSystem.h"
#include "common/Json.h"

// Building.cpp
namespace eve::building {
void exposeHouseBindings(ssq::Table &table);

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

std::string Building::getBuildingPlacementKind(const std::string &buildingId) {
    const auto *def = BuildingRegistry::find(buildingId);
    return def ? def->placementKind : std::string{};
}

std::string Building::getBuildingConnectionGroup(const std::string &buildingId) {
    const auto *def = BuildingRegistry::find(buildingId);
    return def ? def->connectionGroup : std::string{};
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

bool Building::hasValidateRule(const std::string &ruleName) { return PlacementSystem::hasValidateRule(ruleName); }

bool Building::hasSnapRule(const std::string &ruleName) { return PlacementSystem::hasSnapRule(ruleName); }

bool Building::hasSurface(const std::string &surfaceName) { return PlacementSystem::hasSurface(surfaceName); }

int Building::getSurfaceCount() { return int(PlacementSystem::surfaceNames().size()); }

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
    exposeHouseBindings(table);
    auto cls = table.addClass(name, Building::create, false);
    expose(cls);

    auto world = table.addClass<PlacementWorld>(
        "PlacementWorld",
        std::function<PlacementWorld *(int, int, float)>(
            [](int w, int h, float cs) -> PlacementWorld * { return new PlacementWorld(w, h, cs); }),
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
    world.addFunc("getFloorHeight", &PlacementWorld::getFloorHeight);
    world.addFunc("setFloorHeight", &PlacementWorld::setFloorHeight);
    world.addFunc("getActiveLevel", &PlacementWorld::getActiveLevel);
    world.addFunc("setActiveLevel", &PlacementWorld::setActiveLevel);
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
    world.addFunc("getOccupantAtLevel", &PlacementWorld::getOccupantAtLevel);
    world.addFunc("getAnyOccupant", &PlacementWorld::getAnyOccupant);
    world.addFunc("getAnyOccupantAtLevel", &PlacementWorld::getAnyOccupantAtLevel);
    world.addFunc("isCellEmpty", &PlacementWorld::isCellEmpty);
    world.addFunc("isCellEmptyInChannel", &PlacementWorld::isCellEmptyInChannel);
    world.addFunc("getEdgeOccupant", &PlacementWorld::getEdgeOccupant);
    world.addFunc("getEdgeOccupantAtLevel", &PlacementWorld::getEdgeOccupantAtLevel);
    world.addFunc("isEdgeEmpty", &PlacementWorld::isEdgeEmpty);
    world.addFunc("getEdgeConnectionMask", &PlacementWorld::getEdgeConnectionMask);
    world.addFunc("getEdgeVariant", &PlacementWorld::getEdgeVariant);
    world.addFunc("getBuildingCount", &PlacementWorld::getBuildingCount);
    world.addFunc("hasBuilding", &PlacementWorld::hasBuilding);
    world.addFunc("getBuildingId", &PlacementWorld::getBuildingId);
    world.addFunc("getBuildingCellX", &PlacementWorld::getBuildingCellX);
    world.addFunc("getBuildingCellY", &PlacementWorld::getBuildingCellY);
    world.addFunc("getBuildingWorldX", &PlacementWorld::getBuildingWorldX);
    world.addFunc("getBuildingWorldY", &PlacementWorld::getBuildingWorldY);
    world.addFunc("getBuildingWorldZ", &PlacementWorld::getBuildingWorldZ);
    world.addFunc("getBuildingElevation", &PlacementWorld::getBuildingElevation);
    world.addFunc("getBuildingLevel", &PlacementWorld::getBuildingLevel);
    world.addFunc("getBuildingSupportCount", &PlacementWorld::getBuildingSupportCount);
    world.addFunc("getBuildingSupportAt", &PlacementWorld::getBuildingSupportAt);
    world.addFunc("getBuildingDependentCount", &PlacementWorld::getBuildingDependentCount);
    world.addFunc("getBuildingSurfaceId", &PlacementWorld::getBuildingSurfaceId);
    world.addFunc("getBuildingSurfaceRevision", &PlacementWorld::getBuildingSurfaceRevision);
    world.addFunc("getBuildingSurfaceNormalX", &PlacementWorld::getBuildingSurfaceNormalX);
    world.addFunc("getBuildingSurfaceNormalY", &PlacementWorld::getBuildingSurfaceNormalY);
    world.addFunc("getBuildingSurfaceNormalZ", &PlacementWorld::getBuildingSurfaceNormalZ);
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
    world.addFunc("placeEdge", &PlacementWorld::placeEdge);
    world.addFunc("canPlaceEdge", &PlacementWorld::canPlaceEdge);
    world.addFunc("canPlaceEdgeReason", &PlacementWorld::canPlaceEdgeReason);
    world.addFunc("placeCorner", &PlacementWorld::placeCorner);
    world.addFunc("canPlaceCorner", &PlacementWorld::canPlaceCorner);
    world.addFunc("canPlaceCornerReason", &PlacementWorld::canPlaceCornerReason);
    world.addFunc("getCornerOccupant", &PlacementWorld::getCornerOccupant);
    world.addFunc("getCornerOccupantAtLevel", &PlacementWorld::getCornerOccupantAtLevel);
    world.addFunc("isCornerEmpty", &PlacementWorld::isCornerEmpty);
    world.addFunc("placeFree", &PlacementWorld::placeFree);
    world.addFunc("canPlaceFree", &PlacementWorld::canPlaceFree);
    world.addFunc("canPlaceFreeReason", &PlacementWorld::canPlaceFreeReason);
    world.addFunc("getFreeOccupant", &PlacementWorld::getFreeOccupant);
    world.addFunc("getFreeOccupantAtLevel", &PlacementWorld::getFreeOccupantAtLevel);
    world.addFunc("removeBuilding", &PlacementWorld::removeBuilding);
    world.addFunc("canRemoveBuildingReason", &PlacementWorld::canRemoveBuildingReason);
    world.addFunc("removeBuildingCascade", &PlacementWorld::removeBuildingCascade);
    world.addFunc("moveBuilding", &PlacementWorld::moveBuilding);
    world.addFunc("clearBuildings", &PlacementWorld::clearBuildings);

    auto ghost =
        table.addClass<Ghost>("Ghost", std::function<Ghost *()>([]() -> Ghost * { return new Ghost(); }), true);
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
    ghost.addFunc("getPlacementKind", &Ghost::getPlacementKind);
    ghost.addFunc("getEdgeAxis", &Ghost::getEdgeAxis);
    ghost.addFunc("setEdge", &Ghost::setEdge);
    ghost.addFunc("setCorner", &Ghost::setCorner);
    ghost.addFunc("setFree", &Ghost::setFree);
    ghost.addFunc("isValid", &Ghost::isValid);
    ghost.addFunc("getReason", &Ghost::getReason);
    ghost.addFunc("getSurfaceId", &Ghost::getSurfaceId);
    ghost.addFunc("getSurfaceRevision", &Ghost::getSurfaceRevision);
    ghost.addFunc("getSurfaceNormalX", &Ghost::getSurfaceNormalX);
    ghost.addFunc("getSurfaceNormalY", &Ghost::getSurfaceNormalY);
    ghost.addFunc("getSurfaceNormalZ", &Ghost::getSurfaceNormalZ);
    ghost.addFunc("getSurfaceTangentX", &Ghost::getSurfaceTangentX);
    ghost.addFunc("getSurfaceTangentY", &Ghost::getSurfaceTangentY);
    ghost.addFunc("getSurfaceTangentZ", &Ghost::getSurfaceTangentZ);
    ghost.addFunc("getSurfaceSampleCount", &Ghost::getSurfaceSampleCount);
    ghost.addFunc("getSurfaceMaxSlopeDegrees", &Ghost::getSurfaceMaxSlopeDegrees);
    ghost.addFunc("getSurfaceHeightDelta", &Ghost::getSurfaceHeightDelta);
    ghost.addFunc("setFromWorld", &Ghost::setFromWorld);
    ghost.addFunc("setFromWorld3D", &Ghost::setFromWorld3D);
    ghost.addFunc("setFromSurface", &Ghost::setFromSurface);
    ghost.addFunc("validate", &Ghost::validate);

    auto session = table.addClass<PlacementSession>(
        "PlacementSession",
        std::function<PlacementSession *()>([]() -> PlacementSession * { return new PlacementSession(); }), true);
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
    session.addFunc("updateEdge", &PlacementSession::updateEdge);
    session.addFunc("updateCorner", &PlacementSession::updateCorner);
    session.addFunc("updateFree", &PlacementSession::updateFree);
    session.addFunc("isValid", &PlacementSession::isValid);
    session.addFunc("getReason", &PlacementSession::getReason);
    session.addFunc("execute", &PlacementSession::execute);
    session.addFunc("executeEdgeLine", &PlacementSession::executeEdgeLine);
    session.addFunc("beginEdgePath", &PlacementSession::beginEdgePath);
    session.addFunc("appendEdgePathVertex", &PlacementSession::appendEdgePathVertex);
    session.addFunc("executeEdgePath", &PlacementSession::executeEdgePath);
    session.addFunc("getEdgePathVertexCount", &PlacementSession::getEdgePathVertexCount);
    session.addFunc("beginEdgeCurve", &PlacementSession::beginEdgeCurve);
    session.addFunc("appendEdgeCurveControlPoint", &PlacementSession::appendEdgeCurveControlPoint);
    session.addFunc("executeEdgeCurve", &PlacementSession::executeEdgeCurve);
    session.addFunc("executeEdgeCurveOnSurface", &PlacementSession::executeEdgeCurveOnSurface);
    session.addFunc("getEdgeCurveControlPointCount", &PlacementSession::getEdgeCurveControlPointCount);
    session.addFunc("getLastPlacedCount", &PlacementSession::getLastPlacedCount);
    session.addFunc("getLastPlacedId", &PlacementSession::getLastPlacedId);
    session.addFunc("executeMove", &PlacementSession::executeMove);
    session.addFunc("executeReplace", &PlacementSession::executeReplace);
    session.addFunc("getLastEditedId", &PlacementSession::getLastEditedId);
    session.addFunc("previewRectangle", &PlacementSession::previewRectangle);
    session.addFunc("previewBrush", &PlacementSession::previewBrush);
    session.addFunc("executeRectangle", &PlacementSession::executeRectangle);
    session.addFunc("executeBrush", &PlacementSession::executeBrush);
    session.addFunc("getAreaPreviewCount", &PlacementSession::getAreaPreviewCount);
    session.addFunc("getAreaPreviewCellX", &PlacementSession::getAreaPreviewCellX);
    session.addFunc("getAreaPreviewCellY", &PlacementSession::getAreaPreviewCellY);
    session.addFunc("getAreaPreviewAccepted", &PlacementSession::getAreaPreviewAccepted);
    session.addFunc("getAreaPreviewReason", &PlacementSession::getAreaPreviewReason);
    session.addFunc("beginPattern", &PlacementSession::beginPattern);
    session.addFunc("appendPatternPoint", &PlacementSession::appendPatternPoint);
    session.addFunc("setPatternRadius", &PlacementSession::setPatternRadius);
    session.addFunc("setPatternSubdivisions", &PlacementSession::setPatternSubdivisions);
    session.addFunc("setPatternSurface", &PlacementSession::setPatternSurface);
    session.addFunc("previewPattern", &PlacementSession::previewPattern);
    session.addFunc("executePattern", &PlacementSession::executePattern);
    session.addFunc("getPatternKind", &PlacementSession::getPatternKind);
    session.addFunc("getPatternPointCount", &PlacementSession::getPatternPointCount);
    session.addFunc("getPatternPreviewCount", &PlacementSession::getPatternPreviewCount);
    session.addFunc("getPatternPreviewX", &PlacementSession::getPatternPreviewX);
    session.addFunc("getPatternPreviewY", &PlacementSession::getPatternPreviewY);
    session.addFunc("getPatternPreviewAxis", &PlacementSession::getPatternPreviewAxis);
    session.addFunc("getPatternPreviewAccepted", &PlacementSession::getPatternPreviewAccepted);
    session.addFunc("getPatternPreviewReason", &PlacementSession::getPatternPreviewReason);
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
    cls.addFunc("getBuildingPlacementKind", &Building::getBuildingPlacementKind);
    cls.addFunc("getBuildingConnectionGroup", &Building::getBuildingConnectionGroup);
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

// BuildingDef.cpp
namespace eve::building {

using eve::json::Value;

namespace {
bool validConvexPolygon(const std::vector<float> &vertices);
}

bool BuildingDefinition::hasTag(const std::string &tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

std::string BuildingDefinition::getExtra(const std::string &key, const std::string &fallback) const {
    auto it = extra.find(key);
    return it == extra.end() ? fallback : it->second;
}

std::string BuildingDefinition::getVisual2d(const std::string &key, const std::string &fallback) const {
    auto it = visual2d.find(key);
    return it == visual2d.end() ? fallback : it->second;
}

std::string BuildingDefinition::getVisual3d(const std::string &key, const std::string &fallback) const {
    auto it = visual3d.find(key);
    return it == visual3d.end() ? fallback : it->second;
}

int BuildingDefinition::getCost(const std::string &resource, int fallback) const {
    auto it = cost.find(resource);
    return it == cost.end() ? fallback : it->second;
}

bool BuildingDefinition::maskAt(int localX, int localY) const {
    if (localX < 0 || localY < 0 || localX >= footprintW || localY >= footprintH) return false;
    if (footprintMask.empty()) return true;
    const size_t idx = size_t(localY) * size_t(footprintW) + size_t(localX);
    if (idx >= footprintMask.size()) return false;
    return footprintMask[idx] != 0;
}

bool PlacedBuilding::hasTag(const std::string &tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

std::string PlacedBuilding::getProp(const std::string &key, const std::string &fallback) const {
    auto it = props.find(key);
    return it == props.end() ? fallback : it->second;
}

void PlacedBuilding::setProp(const std::string &key, const std::string &value) { props[key] = value; }

std::unordered_map<std::string, BuildingDefinition> &BuildingRegistry::table() {
    static std::unordered_map<std::string, BuildingDefinition> t;
    return t;
}

void BuildingRegistry::registerBuilding(const BuildingDefinition &def) {
    if (def.id.empty()) return;
    BuildingDefinition copy = def;
    if (copy.footprintW <= 0) copy.footprintW = 1;
    if (copy.footprintH <= 0) copy.footprintH = 1;
    if (copy.displayName.empty()) copy.displayName = copy.id;
    if (copy.snapMode.empty()) copy.snapMode = "grid";
    if (copy.rotationMode.empty()) copy.rotationMode = "cardinal";
    if (copy.validateRule.empty()) copy.validateRule = "default";
    if (copy.placementKind != "edge" && copy.placementKind != "corner" && copy.placementKind != "free")
        copy.placementKind = "cell";
    if (!std::isfinite(copy.edgeThicknessCells) || copy.edgeThicknessCells < 0.f) copy.edgeThicknessCells = 0.f;
    if (!std::isfinite(copy.freeRadiusCells) || copy.freeRadiusCells <= 0.f) copy.freeRadiusCells = 0.25f;
    if (!std::isfinite(copy.freeFootprintWidthCells) || copy.freeFootprintWidthCells <= 0.f ||
        !std::isfinite(copy.freeFootprintHeightCells) || copy.freeFootprintHeightCells <= 0.f) {
        copy.freeFootprintWidthCells  = 0.f;
        copy.freeFootprintHeightCells = 0.f;
    }
    if (!copy.freeFootprintVertices.empty() && !validConvexPolygon(copy.freeFootprintVertices))
        copy.freeFootprintVertices.clear();
    if (copy.supportMode != "cell_below" && copy.supportMode != "corner_below") copy.supportMode = "none";
    copy.maxSurfaceSlopeDegrees = std::clamp(copy.maxSurfaceSlopeDegrees, 0.f, 180.f);
    const size_t expected       = size_t(copy.footprintW) * size_t(copy.footprintH);
    if (!copy.footprintMask.empty() && copy.footprintMask.size() != expected) {
        copy.footprintMask.clear();  // 长度不匹配时回退实心矩形
    }
    table()[copy.id] = std::move(copy);
}

const BuildingDefinition *BuildingRegistry::find(const std::string &id) {
    auto &t  = table();
    auto  it = t.find(id);
    return it == t.end() ? nullptr : &it->second;
}

bool BuildingRegistry::remove(const std::string &id) { return table().erase(id) > 0; }

void BuildingRegistry::clear() { table().clear(); }

int BuildingRegistry::count() { return int(table().size()); }

namespace {

bool validConvexPolygon(const std::vector<float> &vertices) {
    if (vertices.size() < 6 || vertices.size() % 2 != 0 || vertices.size() > 128) return false;
    float        winding = 0.f;
    const size_t count   = vertices.size() / 2;
    for (size_t i = 0; i < count; ++i) {
        const size_t next  = (i + 1) % count;
        const size_t after = (i + 2) % count;
        const float  ax    = vertices[next * 2] - vertices[i * 2];
        const float  ay    = vertices[next * 2 + 1] - vertices[i * 2 + 1];
        const float  bx    = vertices[after * 2] - vertices[next * 2];
        const float  by    = vertices[after * 2 + 1] - vertices[next * 2 + 1];
        if (!std::isfinite(vertices[i * 2]) || !std::isfinite(vertices[i * 2 + 1])) return false;
        const float cross = ax * by - ay * bx;
        if (std::fabs(cross) <= 1e-5f) continue;
        if (winding == 0.f)
            winding = cross;
        else if (cross * winding < 0.f)
            return false;
    }
    return winding != 0.f;
}

BuildingDefinition parseBuildingObject(Value o) {
    BuildingDefinition def;
    if (!o.isObject()) return def;
    def.id                       = o.getString("id");
    def.displayName              = o.getString("displayName", def.id);
    def.category                 = o.getString("category");
    def.channel                  = o.getString("channel");
    def.renderMode               = o.getString("renderMode");
    def.placementKind            = o.getString("placementKind", "cell");
    def.freeRadiusCells          = o.getFloat("freeRadiusCells", 0.25f);
    def.freeFootprintWidthCells  = o.getFloat("freeFootprintWidthCells", 0.f);
    def.freeFootprintHeightCells = o.getFloat("freeFootprintHeightCells", 0.f);
    def.freeFootprintVertices    = o.getFloatArray("freeFootprintVertices");
    def.connectionGroup          = o.getString("connectionGroup");
    def.edgeThicknessCells = o.getFloat("edgeThicknessCells", 0.f);
    def.blockedByEdgeChannels = o.getStringArray("blockedByEdgeChannels");
    def.structuralRole           = o.getString("structuralRole");
    def.supportMode              = o.getString("supportMode", "none");
    def.supportTag               = o.getString("supportTag");
    def.footprintW               = o.getInt("footprintW", 1);
    def.footprintH               = o.getInt("footprintH", 1);
    def.snapMode                 = o.getString("snapMode", "grid");
    def.rotationMode             = o.getString("rotationMode", "cardinal");
    def.validateRule             = o.getString("validateRule", "default");
    def.maxSurfaceSlopeDegrees   = o.getFloat("maxSurfaceSlopeDegrees", 180.f);
    def.maxSurfaceHeightDelta    = o.getFloat("maxSurfaceHeightDelta", -1.f);
    def.tags                     = o.getStringArray("tags");
    def.requireTerrain           = o.getIntArray("requireTerrain");
    def.forbidTerrain            = o.getIntArray("forbidTerrain");
    def.requireAdjacentTag       = o.getString("requireAdjacentTag");
    def.requireAdjacentTerrain   = o.getInt("requireAdjacentTerrain", -1);
    def.cost                     = o.getIntMap("cost");
    def.extra                    = o.getStringMap("extra");
    def.visual2d                 = o.getStringMap("visual2d");
    def.visual3d                 = o.getStringMap("visual3d");

    const Value mask = o.get("footprintMask");
    def.footprintMask.reserve(mask.size());
    for (size_t i = 0; i < mask.size(); ++i) def.footprintMask.push_back(uint8_t(mask.at(i).asInt(0) != 0 ? 1 : 0));
    return def;
}

}  // namespace

int BuildingRegistry::loadFromJson(const std::string &json, std::string *error) {
    std::string               err;
    const eve::json::Document doc = eve::json::Document::parse(json, &err);
    if (!doc.valid()) {
        if (error) *error = err.empty() ? "invalid json" : err;
        return 0;
    }

    const Value root = doc.root();
    int         n    = 0;
    if (root.isArray()) {
        for (size_t i = 0; i < root.size(); ++i) {
            BuildingDefinition def = parseBuildingObject(root.at(i));
            if (def.id.empty()) continue;
            registerBuilding(def);
            ++n;
        }
    } else if (root.isObject()) {
        BuildingDefinition def = parseBuildingObject(root);
        if (!def.id.empty()) {
            registerBuilding(def);
            ++n;
        }
    }
    return n;
}

}  // namespace eve::building
