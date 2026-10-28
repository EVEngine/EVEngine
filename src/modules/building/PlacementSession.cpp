#include "building/PlacementSession.h"
#include "building/BuildingDef.h"
#include "building/Ghost.h"
#include "building/PlacementWorld.h"
#include "building/PlacementSystem.h"
#include "grid/GridConfig.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace eve::building {

namespace {

PlacementSystem::SurfacePatch surfacePatchFromGhost(const PlacementWorld &world,
                                                     const Ghost &ghost) {
    PlacementSystem::SurfacePatch patch;
    if (world.getGrid().plane == grid::GridPlane::XZ) {
        patch.anchor.worldX = ghost.getWorldX();
        patch.anchor.worldY = ghost.getElevation();
        patch.anchor.worldZ = ghost.getWorldY();
    } else {
        patch.anchor.worldX = ghost.getWorldX();
        patch.anchor.worldY = ghost.getWorldY();
        patch.anchor.worldZ = ghost.getElevation();
    }
    patch.anchor.surfaceId = ghost.getSurfaceId();
    patch.anchor.surfaceRevision = static_cast<uint64_t>(ghost.getSurfaceRevision());
    patch.anchor.normalX = ghost.getSurfaceNormalX();
    patch.anchor.normalY = ghost.getSurfaceNormalY();
    patch.anchor.normalZ = ghost.getSurfaceNormalZ();
    patch.anchor.tangentX = ghost.getSurfaceTangentX();
    patch.anchor.tangentY = ghost.getSurfaceTangentY();
    patch.anchor.tangentZ = ghost.getSurfaceTangentZ();
    patch.samples.resize(static_cast<size_t>(std::max(ghost.getSurfaceSampleCount(), 0)));
    patch.maxSlopeDegrees = ghost.getSurfaceMaxSlopeDegrees();
    patch.heightDelta = ghost.getSurfaceHeightDelta();
    return patch;
}

std::optional<PlacementSystem::PatternKind> patternKindFromName(const std::string &name) {
    if (name == "edge_line") return PlacementSystem::PatternKind::EdgeLine;
    if (name == "edge_path") return PlacementSystem::PatternKind::EdgePath;
    if (name == "edge_cubic_bezier")
        return PlacementSystem::PatternKind::EdgeCubicBezier;
    if (name == "rectangle_fill") return PlacementSystem::PatternKind::RectangleFill;
    if (name == "rectangle_outline")
        return PlacementSystem::PatternKind::RectangleOutline;
    if (name == "circle_brush") return PlacementSystem::PatternKind::CircleBrush;
    return std::nullopt;
}

std::string patternKindName(PlacementSystem::PatternKind kind) {
    switch (kind) {
        case PlacementSystem::PatternKind::EdgeLine: return "edge_line";
        case PlacementSystem::PatternKind::EdgePath: return "edge_path";
        case PlacementSystem::PatternKind::EdgeCubicBezier: return "edge_cubic_bezier";
        case PlacementSystem::PatternKind::RectangleFill: return "rectangle_fill";
        case PlacementSystem::PatternKind::RectangleOutline: return "rectangle_outline";
        case PlacementSystem::PatternKind::CircleBrush: return "circle_brush";
    }
    return {};
}

bool edgePattern(PlacementSystem::PatternKind kind) {
    return kind == PlacementSystem::PatternKind::EdgeLine ||
           kind == PlacementSystem::PatternKind::EdgePath ||
           kind == PlacementSystem::PatternKind::EdgeCubicBezier;
}

}  // namespace

PlacementSession::PlacementSession() { ghost_ = new Ghost(); }

void PlacementSession::destroy() {
    if (ghost_) ghost_->destroy();
    ghost_ = nullptr;
    delete this;
}

bool PlacementSession::startPlacement(PlacementWorld *world, const std::string &buildingId) {
    if (!world || buildingId.empty()) return false;
    world_ = world;
    ghost_->setBuildingId(buildingId);
    active_ = true;
    mode_ = "place";
    lastPlacedIds_.clear();
    edgePathVertices_.clear();
    edgeCurveControlPoints_.clear();
    lastEditedId_ = 0;
    areaPreview_ = {};
    patternRequest_ = {};
    patternPreview_ = {};
    patternActive_ = false;
    return true;
}

void PlacementSession::stopPlacement() {
    active_ = false;
    world_ = nullptr;
    edgePathVertices_.clear();
    edgeCurveControlPoints_.clear();
    patternRequest_ = {};
    patternPreview_ = {};
    patternActive_ = false;
}

std::string PlacementSession::getBuildingId() const {
    return ghost_ ? ghost_->getBuildingId() : std::string{};
}

void PlacementSession::setMode(const std::string &mode) {
    if (mode == "remove") {
        mode_ = "remove";
    } else {
        mode_ = "place";
    }
}

void PlacementSession::setRotationDeg(float deg) {
    if (ghost_) ghost_->setRotationDeg(deg);
}

void PlacementSession::rotateBy(float deltaDeg) {
    if (ghost_) ghost_->rotateBy(deltaDeg);
}

float PlacementSession::getRotationDeg() const {
    return ghost_ ? ghost_->getRotationDeg() : 0.f;
}

bool PlacementSession::updateFromWorld(PlacementWorld *world, float worldX, float worldY) {
    if (!active_ || !ghost_) return false;
    if (world) world_ = world;
    if (!world_) return false;
    ghost_->setFromWorld(world_, worldX, worldY);
    refreshValidate();
    return true;
}

bool PlacementSession::updateFromWorld3D(PlacementWorld *world, float worldX, float worldY,
                                         float worldZ) {
    if (!active_ || !ghost_) return false;
    if (world) world_ = world;
    if (!world_) return false;
    ghost_->setFromWorld3D(world_, worldX, worldY, worldZ);
    refreshValidate();
    return true;
}

bool PlacementSession::updateFromSurface(PlacementWorld *world, const std::string &surface,
                                         float x, float y) {
    if (!active_ || !ghost_) return false;
    if (world) world_ = world;
    if (!world_) return false;
    ghost_->setFromSurface(world_, surface, x, y);
    refreshValidate();
    return true;
}

EdgeUpdateStatus PlacementSession::updateEdge(PlacementWorld *world, int cellX, int cellY,
                                              const std::string &direction) {
    if (!active_ || !ghost_) return EdgeUpdateStatus::Rejected;
    if (world) world_ = world;
    if (!world_) return EdgeUpdateStatus::Rejected;
    ghost_->setEdge(world_, cellX, cellY, direction);
    refreshValidate();
    return ghost_->isValid() ? EdgeUpdateStatus::Updated : EdgeUpdateStatus::Rejected;
}

CornerUpdateStatus PlacementSession::updateCorner(PlacementWorld *world, int vertexX,
                                                   int vertexY) {
    if (!active_ || !ghost_) return CornerUpdateStatus::Rejected;
    if (world) world_ = world;
    if (!world_) return CornerUpdateStatus::Rejected;
    ghost_->setCorner(world_, vertexX, vertexY);
    refreshValidate();
    return ghost_->isValid() ? CornerUpdateStatus::Updated : CornerUpdateStatus::Rejected;
}

FreeUpdateStatus PlacementSession::updateFree(PlacementWorld *world, float worldX,
                                               float worldY, float elevation) {
    if (!active_ || !ghost_) return FreeUpdateStatus::Rejected;
    if (world) world_ = world;
    if (!world_) return FreeUpdateStatus::Rejected;
    ghost_->setFree(world_, worldX, worldY, elevation);
    refreshValidate();
    return ghost_->isValid() ? FreeUpdateStatus::Updated : FreeUpdateStatus::Rejected;
}

bool PlacementSession::isValid() const { return ghost_ ? ghost_->isValid() : false; }

std::string PlacementSession::getReason() const {
    return ghost_ ? ghost_->getReason() : std::string{};
}

void PlacementSession::refreshValidate() {
    if (world_ && ghost_) ghost_->validate(world_);
}

int PlacementSession::execute() {
    if (!active_ || !world_ || !ghost_) return 0;
    if (mode_ == "remove") {
        int occ = 0;
        if (ghost_->getPlacementKind() == "edge") {
            const std::string direction =
                ghost_->getEdgeAxis() == "horizontal" ? "north" : "west";
            const BuildingDefinition *definition = BuildingRegistry::find(ghost_->getBuildingId());
            const std::string channel = definition ? definition->channel : std::string{};
            occ = world_->getEdgeOccupant(channel, ghost_->getCellX(), ghost_->getCellY(), direction);
        } else if (ghost_->getPlacementKind() == "corner") {
            const BuildingDefinition *definition = BuildingRegistry::find(ghost_->getBuildingId());
            const std::string channel = definition ? definition->channel : std::string{};
            occ = world_->getCornerOccupant(channel, ghost_->getCellX(), ghost_->getCellY());
        } else if (ghost_->getPlacementKind() == "free") {
            const BuildingDefinition *definition = BuildingRegistry::find(ghost_->getBuildingId());
            const std::string channel = definition ? definition->channel : std::string{};
            occ = world_->getFreeOccupant(channel, ghost_->getWorldX(), ghost_->getWorldY());
        } else {
            occ = world_->getOccupant(ghost_->getCellX(), ghost_->getCellY());
        }
        if (occ <= 0) return 0;
        return world_->removeBuilding(occ) ? occ : 0;
    }
    return world_->placeGhost(ghost_);
}

EdgeLineExecuteStatus PlacementSession::executeEdgeLine(int startVertexX, int startVertexY,
                                                        int endVertexX, int endVertexY) {
    lastPlacedIds_.clear();
    if (!active_ || !world_ || !ghost_ || mode_ != "place")
        return EdgeLineExecuteStatus::Rejected;
    PlacementSystem::PatternRequest request;
    request.kind = PlacementSystem::PatternKind::EdgeLine;
    request.points = {{static_cast<float>(startVertexX), static_cast<float>(startVertexY)},
                      {static_cast<float>(endVertexX), static_cast<float>(endVertexY)}};
    auto result = PlacementSystem::placePattern(world_, ghost_->getBuildingId(), request);
    if (!result.ok()) return EdgeLineExecuteStatus::Rejected;
    lastPlacedIds_ = std::move(result).takeValue().instanceIds;
    return EdgeLineExecuteStatus::Placed;
}

EdgePathUpdateStatus PlacementSession::beginEdgePath(int vertexX, int vertexY) {
    edgePathVertices_.clear();
    if (!active_ || !world_ || !ghost_ || mode_ != "place")
        return EdgePathUpdateStatus::Rejected;
    const BuildingDefinition *definition = BuildingRegistry::find(ghost_->getBuildingId());
    if (!definition || definition->placementKind != "edge") return EdgePathUpdateStatus::Rejected;
    edgePathVertices_.push_back({vertexX, vertexY});
    return EdgePathUpdateStatus::Updated;
}

EdgePathUpdateStatus PlacementSession::appendEdgePathVertex(int vertexX, int vertexY) {
    if (edgePathVertices_.empty()) return EdgePathUpdateStatus::Rejected;
    const CornerAddress previous = edgePathVertices_.back();
    if ((previous.x == vertexX) == (previous.y == vertexY))
        return EdgePathUpdateStatus::Rejected;
    edgePathVertices_.push_back({vertexX, vertexY});
    return EdgePathUpdateStatus::Updated;
}

EdgeLineExecuteStatus PlacementSession::executeEdgePath() {
    lastPlacedIds_.clear();
    if (!active_ || !world_ || !ghost_ || mode_ != "place" || edgePathVertices_.size() < 2)
        return EdgeLineExecuteStatus::Rejected;
    PlacementSystem::PatternRequest request;
    request.kind = PlacementSystem::PatternKind::EdgePath;
    for (const CornerAddress &vertex : edgePathVertices_)
        request.points.push_back(
            {static_cast<float>(vertex.x), static_cast<float>(vertex.y)});
    auto result = PlacementSystem::placePattern(world_, ghost_->getBuildingId(), request);
    if (!result.ok()) return EdgeLineExecuteStatus::Rejected;
    lastPlacedIds_ = std::move(result).takeValue().instanceIds;
    edgePathVertices_.clear();
    return EdgeLineExecuteStatus::Placed;
}

EdgePathUpdateStatus PlacementSession::beginEdgeCurve(float x, float y) {
    edgeCurveControlPoints_.clear();
    if (!active_ || !world_ || !ghost_ || mode_ != "place" || !std::isfinite(x) ||
        !std::isfinite(y))
        return EdgePathUpdateStatus::Rejected;
    const BuildingDefinition *definition = BuildingRegistry::find(ghost_->getBuildingId());
    if (!definition || definition->placementKind != "edge") return EdgePathUpdateStatus::Rejected;
    edgeCurveControlPoints_.push_back({x, y});
    return EdgePathUpdateStatus::Updated;
}

EdgePathUpdateStatus PlacementSession::appendEdgeCurveControlPoint(float x, float y) {
    if (edgeCurveControlPoints_.empty() || edgeCurveControlPoints_.size() >= 4 ||
        !std::isfinite(x) || !std::isfinite(y))
        return EdgePathUpdateStatus::Rejected;
    edgeCurveControlPoints_.push_back({x, y});
    return EdgePathUpdateStatus::Updated;
}

EdgeLineExecuteStatus PlacementSession::executeEdgeCurve(int subdivisions) {
    lastPlacedIds_.clear();
    if (!active_ || !world_ || !ghost_ || mode_ != "place" ||
        edgeCurveControlPoints_.size() != 4)
        return EdgeLineExecuteStatus::Rejected;
    PlacementSystem::PatternRequest request;
    request.kind = PlacementSystem::PatternKind::EdgeCubicBezier;
    request.points = edgeCurveControlPoints_;
    request.subdivisions = subdivisions;
    auto result = PlacementSystem::placePattern(world_, ghost_->getBuildingId(), request);
    if (!result.ok()) return EdgeLineExecuteStatus::Rejected;
    lastPlacedIds_ = std::move(result).takeValue().instanceIds;
    edgeCurveControlPoints_.clear();
    return EdgeLineExecuteStatus::Placed;
}

EdgeLineExecuteStatus PlacementSession::executeEdgeCurveOnSurface(
    int subdivisions, const std::string &surfaceName) {
    lastPlacedIds_.clear();
    if (!active_ || !world_ || !ghost_ || mode_ != "place" ||
        edgeCurveControlPoints_.size() != 4 || surfaceName.empty())
        return EdgeLineExecuteStatus::Rejected;
    PlacementSystem::PatternRequest request;
    request.kind = PlacementSystem::PatternKind::EdgeCubicBezier;
    request.points = edgeCurveControlPoints_;
    request.subdivisions = subdivisions;
    request.surfaceName = surfaceName;
    auto result = PlacementSystem::placePattern(world_, ghost_->getBuildingId(), request);
    if (!result.ok()) return EdgeLineExecuteStatus::Rejected;
    lastPlacedIds_ = std::move(result).takeValue().instanceIds;
    edgeCurveControlPoints_.clear();
    return EdgeLineExecuteStatus::Placed;
}

int PlacementSession::getLastPlacedId(int index) const {
    if (index < 0 || index >= static_cast<int>(lastPlacedIds_.size())) return 0;
    return lastPlacedIds_[static_cast<size_t>(index)];
}

PlacementEditExecuteStatus PlacementSession::executeMove(int instanceId) {
    lastEditedId_ = 0;
    if (!active_ || !world_ || !ghost_ || mode_ != "place")
        return PlacementEditExecuteStatus::Rejected;
    eve::Result<PlacementEditReceipt> result =
        ghost_->getPlacementKind() == "edge"
            ? PlacementSystem::moveEdgeResult(
                  world_, instanceId, ghost_->getCellX(), ghost_->getCellY(),
                  ghost_->getEdgeAxis() == "horizontal" ? "north" : "west")
        : ghost_->getPlacementKind() == "corner"
            ? PlacementSystem::moveCornerResult(world_, instanceId, ghost_->getCellX(),
                                                ghost_->getCellY())
        : ghost_->getPlacementKind() == "free"
            ? (!ghost_->getSurfaceId().empty()
                   ? PlacementSystem::moveFreeSurfaceResult(
                         world_, instanceId, surfacePatchFromGhost(*world_, *ghost_),
                         ghost_->getRotationDeg())
                   : PlacementSystem::moveFreeResult(
                         world_, instanceId, ghost_->getWorldX(), ghost_->getWorldY(),
                         ghost_->getElevation(), ghost_->getRotationDeg()))
            : PlacementSystem::moveBuildingResult(world_, instanceId, ghost_->getCellX(),
                                                  ghost_->getCellY(), ghost_->getRotationDeg());
    if (!result.ok()) return PlacementEditExecuteStatus::Rejected;
    lastEditedId_ = std::move(result).takeValue().after.instanceId;
    return PlacementEditExecuteStatus::Committed;
}

PlacementEditExecuteStatus PlacementSession::executeReplace(int instanceId) {
    lastEditedId_ = 0;
    if (!active_ || !world_ || !ghost_ || mode_ != "place")
        return PlacementEditExecuteStatus::Rejected;
    auto result =
        PlacementSystem::replaceBuildingResult(world_, instanceId, ghost_->getBuildingId());
    if (!result.ok()) return PlacementEditExecuteStatus::Rejected;
    lastEditedId_ = std::move(result).takeValue().after.instanceId;
    return PlacementEditExecuteStatus::Committed;
}

AreaExecuteStatus PlacementSession::previewRectangle(int minCellX, int minCellY, int maxCellX,
                                                      int maxCellY) {
    areaPreview_ = {};
    if (!active_ || !world_ || !ghost_ || mode_ != "place") return AreaExecuteStatus::Rejected;
    PlacementSystem::PatternRequest request;
    request.kind = PlacementSystem::PatternKind::RectangleFill;
    request.points = {{static_cast<float>(minCellX), static_cast<float>(minCellY)},
                      {static_cast<float>(maxCellX), static_cast<float>(maxCellY)}};
    request.rotationDeg = ghost_->getRotationDeg();
    auto result = PlacementSystem::previewPattern(world_, ghost_->getBuildingId(), request);
    if (!result.ok()) return AreaExecuteStatus::Rejected;
    areaPreview_ = std::move(result).takeValue().area;
    return areaPreview_.rejectedCount == 0 ? AreaExecuteStatus::Accepted
                                           : AreaExecuteStatus::Rejected;
}

AreaExecuteStatus PlacementSession::previewBrush(int centerCellX, int centerCellY, int radius) {
    areaPreview_ = {};
    if (!active_ || !world_ || !ghost_ || mode_ != "place") return AreaExecuteStatus::Rejected;
    PlacementSystem::PatternRequest request;
    request.kind = PlacementSystem::PatternKind::CircleBrush;
    request.points = {{static_cast<float>(centerCellX), static_cast<float>(centerCellY)}};
    request.radius = radius;
    request.rotationDeg = ghost_->getRotationDeg();
    auto result = PlacementSystem::previewPattern(world_, ghost_->getBuildingId(), request);
    if (!result.ok()) return AreaExecuteStatus::Rejected;
    areaPreview_ = std::move(result).takeValue().area;
    return areaPreview_.rejectedCount == 0 ? AreaExecuteStatus::Accepted
                                           : AreaExecuteStatus::Rejected;
}

AreaExecuteStatus PlacementSession::executeRectangle(int minCellX, int minCellY, int maxCellX,
                                                      int maxCellY) {
    lastPlacedIds_.clear();
    if (!active_ || !world_ || !ghost_ || mode_ != "place") return AreaExecuteStatus::Rejected;
    PlacementSystem::PatternRequest request;
    request.kind = PlacementSystem::PatternKind::RectangleFill;
    request.points = {{static_cast<float>(minCellX), static_cast<float>(minCellY)},
                      {static_cast<float>(maxCellX), static_cast<float>(maxCellY)}};
    request.rotationDeg = ghost_->getRotationDeg();
    auto result = PlacementSystem::placePattern(world_, ghost_->getBuildingId(), request);
    if (!result.ok()) return AreaExecuteStatus::Rejected;
    auto placement = std::move(result).takeValue();
    areaPreview_ = std::move(placement.preview.area);
    lastPlacedIds_ = std::move(placement.instanceIds);
    return AreaExecuteStatus::Accepted;
}

AreaExecuteStatus PlacementSession::executeBrush(int centerCellX, int centerCellY, int radius) {
    lastPlacedIds_.clear();
    if (!active_ || !world_ || !ghost_ || mode_ != "place") return AreaExecuteStatus::Rejected;
    PlacementSystem::PatternRequest request;
    request.kind = PlacementSystem::PatternKind::CircleBrush;
    request.points = {{static_cast<float>(centerCellX), static_cast<float>(centerCellY)}};
    request.radius = radius;
    request.rotationDeg = ghost_->getRotationDeg();
    auto result = PlacementSystem::placePattern(world_, ghost_->getBuildingId(), request);
    if (!result.ok()) return AreaExecuteStatus::Rejected;
    auto placement = std::move(result).takeValue();
    areaPreview_ = std::move(placement.preview.area);
    lastPlacedIds_ = std::move(placement.instanceIds);
    return AreaExecuteStatus::Accepted;
}

int PlacementSession::getAreaPreviewCellX(int index) const {
    return index >= 0 && index < getAreaPreviewCount() ? areaPreview_.cells[size_t(index)].cellX : 0;
}
int PlacementSession::getAreaPreviewCellY(int index) const {
    return index >= 0 && index < getAreaPreviewCount() ? areaPreview_.cells[size_t(index)].cellY : 0;
}
bool PlacementSession::getAreaPreviewAccepted(int index) const {
    return index >= 0 && index < getAreaPreviewCount() && areaPreview_.cells[size_t(index)].accepted;
}
std::string PlacementSession::getAreaPreviewReason(int index) const {
    return index >= 0 && index < getAreaPreviewCount() ? areaPreview_.cells[size_t(index)].reason
                                                       : std::string{};
}

PatternUpdateStatus PlacementSession::beginPattern(const std::string &kind) {
    const auto parsed = patternKindFromName(kind);
    if (!active_ || !world_ || !ghost_ || mode_ != "place" || !parsed)
        return PatternUpdateStatus::Rejected;
    const BuildingDefinition *definition = BuildingRegistry::find(ghost_->getBuildingId());
    if (!definition || (edgePattern(*parsed) != (definition->placementKind == "edge")))
        return PatternUpdateStatus::Rejected;
    patternRequest_ = {};
    patternRequest_.kind = *parsed;
    patternRequest_.rotationDeg = ghost_->getRotationDeg();
    patternPreview_ = {};
    patternPreview_.request = patternRequest_;
    areaPreview_ = {};
    patternActive_ = true;
    return PatternUpdateStatus::Updated;
}

PatternUpdateStatus PlacementSession::appendPatternPoint(float x, float y) {
    if (!patternActive_ || !std::isfinite(x) || !std::isfinite(y))
        return PatternUpdateStatus::Rejected;
    size_t maximum = std::numeric_limits<size_t>::max();
    if (patternRequest_.kind == PlacementSystem::PatternKind::EdgeLine ||
        patternRequest_.kind == PlacementSystem::PatternKind::RectangleFill ||
        patternRequest_.kind == PlacementSystem::PatternKind::RectangleOutline)
        maximum = 2;
    else if (patternRequest_.kind == PlacementSystem::PatternKind::EdgeCubicBezier)
        maximum = 4;
    else if (patternRequest_.kind == PlacementSystem::PatternKind::CircleBrush)
        maximum = 1;
    if (patternRequest_.points.size() >= maximum) return PatternUpdateStatus::Rejected;
    patternRequest_.points.push_back({x, y});
    patternPreview_ = {};
    patternPreview_.request = patternRequest_;
    return PatternUpdateStatus::Updated;
}

PatternUpdateStatus PlacementSession::setPatternRadius(int radius) {
    if (!patternActive_ || radius < 0) return PatternUpdateStatus::Rejected;
    patternRequest_.radius = radius;
    return PatternUpdateStatus::Updated;
}

PatternUpdateStatus PlacementSession::setPatternSubdivisions(int subdivisions) {
    if (!patternActive_ || subdivisions <= 0) return PatternUpdateStatus::Rejected;
    patternRequest_.subdivisions = subdivisions;
    return PatternUpdateStatus::Updated;
}

PatternUpdateStatus PlacementSession::setPatternSurface(const std::string &surfaceName) {
    if (!patternActive_ ||
        patternRequest_.kind != PlacementSystem::PatternKind::EdgeCubicBezier)
        return PatternUpdateStatus::Rejected;
    patternRequest_.surfaceName = surfaceName;
    return PatternUpdateStatus::Updated;
}

PatternUpdateStatus PlacementSession::previewPattern() {
    patternPreview_ = {};
    areaPreview_ = {};
    if (!patternActive_ || !world_ || !ghost_ || mode_ != "place")
        return PatternUpdateStatus::Rejected;
    patternRequest_.rotationDeg = ghost_->getRotationDeg();
    auto preview = PlacementSystem::previewPattern(world_, ghost_->getBuildingId(),
                                                   patternRequest_);
    if (!preview.ok()) return PatternUpdateStatus::Rejected;
    patternPreview_ = std::move(preview).takeValue();
    areaPreview_ = patternPreview_.area;
    if (!edgePattern(patternRequest_.kind) && patternPreview_.area.rejectedCount != 0)
        return PatternUpdateStatus::Rejected;
    return PatternUpdateStatus::Updated;
}

PatternExecuteStatus PlacementSession::executePattern() {
    lastPlacedIds_.clear();
    if (!patternActive_ || !world_ || !ghost_ || mode_ != "place")
        return PatternExecuteStatus::Rejected;
    patternRequest_.rotationDeg = ghost_->getRotationDeg();
    auto placed = PlacementSystem::placePattern(world_, ghost_->getBuildingId(),
                                                patternRequest_);
    if (!placed.ok()) return PatternExecuteStatus::Rejected;
    auto receipt = std::move(placed).takeValue();
    patternPreview_ = std::move(receipt.preview);
    areaPreview_ = patternPreview_.area;
    lastPlacedIds_ = std::move(receipt.instanceIds);
    return PatternExecuteStatus::Placed;
}

std::string PlacementSession::getPatternKind() const {
    return patternActive_ ? patternKindName(patternRequest_.kind) : std::string{};
}

int PlacementSession::getPatternPreviewX(int index) const {
    if (index < 0 || index >= getPatternPreviewCount()) return 0;
    return edgePattern(patternPreview_.request.kind)
               ? patternPreview_.edge.edges[static_cast<size_t>(index)].x
               : patternPreview_.area.cells[static_cast<size_t>(index)].cellX;
}

int PlacementSession::getPatternPreviewY(int index) const {
    if (index < 0 || index >= getPatternPreviewCount()) return 0;
    return edgePattern(patternPreview_.request.kind)
               ? patternPreview_.edge.edges[static_cast<size_t>(index)].y
               : patternPreview_.area.cells[static_cast<size_t>(index)].cellY;
}

std::string PlacementSession::getPatternPreviewAxis(int index) const {
    if (index < 0 || index >= getPatternPreviewCount() ||
        !edgePattern(patternPreview_.request.kind))
        return {};
    return patternPreview_.edge.edges[static_cast<size_t>(index)].axis ==
                   EdgeAxis::Horizontal
               ? "horizontal"
               : "vertical";
}

bool PlacementSession::getPatternPreviewAccepted(int index) const {
    if (index < 0 || index >= getPatternPreviewCount()) return false;
    return edgePattern(patternPreview_.request.kind) ||
           patternPreview_.area.cells[static_cast<size_t>(index)].accepted;
}

std::string PlacementSession::getPatternPreviewReason(int index) const {
    if (index < 0 || index >= getPatternPreviewCount() ||
        edgePattern(patternPreview_.request.kind))
        return {};
    return patternPreview_.area.cells[static_cast<size_t>(index)].reason;
}

}  // namespace eve::building
