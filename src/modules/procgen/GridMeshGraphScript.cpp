#include "procgen/GridMeshGraphScript.h"

#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "procgen/BuildLayerStack.h"
#include "procgen/GridGraph.h"
#include "procgen/IncrementalBuild.h"
#include "procgen/MeshGraph.h"
#include "procgen/ObjectBuildLayer.h"
#include "procgen/PointGraph.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>

namespace eve::procgen {

void exposeGridMeshGraphs(ssq::Table& table) {
    auto grid = table.addClass<GridGraph>("ProcgenGridGraph",
                                          std::function<GridGraph*()>([]() -> GridGraph* { return nullptr; }), true);
    const HSQUIRRELVM gridVm = grid.getHandle();
    grid.addFunc("addNode", [gridVm](GridGraph* self, const std::string& id, const std::string& operation) {
        return eve::script::projectResult(gridVm, self->addNode(id, operation));
    });
    grid.addFunc("connect", [gridVm](GridGraph* self, const std::string& from, const std::string& to, int slot) {
        return eve::script::projectResult(gridVm, self->connect(from, to, slot));
    });
    grid.addFunc("setNodeGrid", [gridVm](GridGraph* self, const std::string& id, Grid2D* value) {
        if (!value)
            return eve::script::projectResult(
                gridVm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "grid is null")));
        return eve::script::projectResult(gridVm, self->setNodeGrid(id, *value));
    });
    grid.addFunc("setNodeInt", [gridVm](GridGraph* self, const std::string& id, const std::string& key, int value) {
        return eve::script::projectResult(gridVm, self->setNodeInt(id, key, value));
    });
    grid.addFunc("setNodeFloat", [gridVm](GridGraph* self, const std::string& id, const std::string& key, float value) {
        return eve::script::projectResult(gridVm, self->setNodeFloat(id, key, value));
    });
    grid.addFunc("setNodeString",
                 [gridVm](GridGraph* self, const std::string& id, const std::string& key, const std::string& value) {
                     return eve::script::projectResult(gridVm, self->setNodeString(id, key, value));
                 });
    grid.addFunc("setNodePointSubgraph", [gridVm](GridGraph* self, const std::string& id, PointGraph* nested,
                                                  const std::string& input, const std::string& output) {
        if (!nested)
            return eve::script::projectResult(gridVm, Result<void>::failure(Diagnostic::error(
                                                          DiagnosticCode::InvalidArgument, "point graph is null")));
        return eve::script::projectResult(gridVm, self->setNodePointSubgraph(id, *nested, input, output));
    });
    grid.addFunc("execute", [gridVm](GridGraph* self, const std::string& output) -> ssq::Table {
        auto result = self->execute(output);
        if (!result.ok()) return eve::script::projectStatusResult(gridVm, result.status());
        auto        value = std::move(result).takeValue();
        ssq::Object object;
        if (auto* outputGrid = std::get_if<Grid2D>(&value)) {
            auto instance =
                eve::script::makeOwnedSquirrelInstance<Grid2D>(gridVm, std::make_unique<Grid2D>(*outputGrid));
            if (!instance.ok()) return eve::script::projectStatusResult(gridVm, instance.status());
            object = std::move(instance).takeValue();
        } else {
            auto instance = eve::script::makeOwnedSquirrelInstance<PointSet>(
                gridVm, std::make_unique<PointSet>(std::get<PointSet>(std::move(value))));
            if (!instance.ok()) return eve::script::projectStatusResult(gridVm, instance.status());
            object = std::move(instance).takeValue();
        }
        return eve::script::projectStatusResult(gridVm, Status::success(), std::move(object));
    });
    grid.addFunc("validate", [gridVm](GridGraph* self, const std::string& output) {
        return eve::script::projectResult(gridVm, self->validate(output));
    });
    grid.addFunc("clearCache", &GridGraph::clearCache);
    grid.addFunc("getRevision", [](GridGraph* self) { return self->revision(); });
    grid.addFunc("serializeDefinition", &GridGraph::serializeDefinition);
    grid.addFunc("deserializeDefinition", [gridVm](GridGraph* self, const std::string& definition) {
        return eve::script::projectResult(gridVm, self->deserializeDefinition(definition));
    });

    auto mesh = table.addClass<MeshGraph>("ProcgenMeshGraph",
                                          std::function<MeshGraph*()>([]() -> MeshGraph* { return nullptr; }), true);
    const HSQUIRRELVM meshVm = mesh.getHandle();
    mesh.addFunc("addNode", [meshVm](MeshGraph* self, const std::string& id, const std::string& operation) {
        return eve::script::projectResult(meshVm, self->addNode(id, operation));
    });
    mesh.addFunc("connect", [meshVm](MeshGraph* self, const std::string& from, const std::string& to, int slot) {
        return eve::script::projectResult(meshVm, self->connect(from, to, slot));
    });
    mesh.addFunc("setNodeGrid", [meshVm](MeshGraph* self, const std::string& id, Grid2D* value) {
        if (!value)
            return eve::script::projectResult(
                meshVm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "grid is null")));
        return eve::script::projectResult(meshVm, self->setNodeGrid(id, *value));
    });
    mesh.addFunc("setNodePoints", [meshVm](MeshGraph* self, const std::string& id, PointSet* value) {
        if (!value)
            return eve::script::projectResult(
                meshVm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "points are null")));
        return eve::script::projectResult(meshVm, self->setNodePoints(id, *value));
    });
    mesh.addFunc("setNodeMesh", [meshVm](MeshGraph* self, const std::string& id, MeshBuild* value) {
        if (!value)
            return eve::script::projectResult(
                meshVm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "mesh is null")));
        return eve::script::projectResult(meshVm, self->setNodeMesh(id, *value));
    });
    mesh.addFunc("setNodeFloat", [meshVm](MeshGraph* self, const std::string& id, const std::string& key, float value) {
        return eve::script::projectResult(meshVm, self->setNodeFloat(id, key, value));
    });
    mesh.addFunc("setNodeString",
                 [meshVm](MeshGraph* self, const std::string& id, const std::string& key, const std::string& value) {
                     return eve::script::projectResult(meshVm, self->setNodeString(id, key, value));
                 });
    mesh.addFunc("execute", [meshVm](MeshGraph* self, const std::string& output) -> ssq::Table {
        auto result = self->execute(output);
        if (!result.ok()) return eve::script::projectStatusResult(meshVm, result.status());
        auto instance = eve::script::makeOwnedSquirrelInstance<MeshBuild>(
            meshVm, std::make_unique<MeshBuild>(std::move(result).takeValue()));
        if (!instance.ok()) return eve::script::projectStatusResult(meshVm, instance.status());
        return eve::script::projectStatusResult(meshVm, Status::success(), std::move(instance).takeValue());
    });
    mesh.addFunc("validate", [meshVm](MeshGraph* self, const std::string& output) {
        return eve::script::projectResult(meshVm, self->validate(output));
    });
    mesh.addFunc("clearCache", &MeshGraph::clearCache);
    mesh.addFunc("getRevision", [](MeshGraph* self) { return self->revision(); });
    mesh.addFunc("serializeDefinition", &MeshGraph::serializeDefinition);
    mesh.addFunc("deserializeDefinition", [meshVm](MeshGraph* self, const std::string& definition) {
        return eve::script::projectResult(meshVm, self->deserializeDefinition(definition));
    });

    auto objects = table.addClass<ObjectBuildLayer>("ProcgenObjectBuildLayer", ssq::Class::Ctor<ObjectBuildLayer()>());
    const HSQUIRRELVM objectsVm = objects.getHandle();
    objects.addFunc("addAsset", [objectsVm](ObjectBuildLayer* self, const std::string& asset, float weight) {
        return eve::script::projectResult(objectsVm, self->addAsset(asset, weight));
    });
    objects.addFunc("clearAssets", &ObjectBuildLayer::clearAssets);
    objects.addFunc("setSeed", [](ObjectBuildLayer* self, std::uint32_t seed) { self->setSeed(seed); });
    objects.addFunc("setLayerOffset",
                    [](ObjectBuildLayer* self, float x, float y, float z) { self->setLayerOffset(x, y, z); });
    objects.addFunc("setLayerScale", [objectsVm](ObjectBuildLayer* self, float x, float y, float z) {
        return eve::script::projectResult(objectsVm, self->setLayerScale(x, y, z));
    });
    objects.addFunc("setPositionRadius", [objectsVm](ObjectBuildLayer* self, float radius) {
        return eve::script::projectResult(objectsVm, self->setPositionRadius(radius));
    });
    objects.addFunc("setRandomRotation", [objectsVm](ObjectBuildLayer* self, float minPitch, float maxPitch,
                                                     float minYaw, float maxYaw, float minRoll, float maxRoll) {
        return eve::script::projectResult(
            objectsVm, self->setRandomRotation(minPitch, maxPitch, minYaw, maxYaw, minRoll, maxRoll));
    });
    objects.addFunc("setRandomScale", [objectsVm](ObjectBuildLayer* self, float minX, float maxX, float minY,
                                                  float maxY, float minZ, float maxZ, bool uniform) {
        return eve::script::projectResult(objectsVm, self->setRandomScale(minX, maxX, minY, maxY, minZ, maxZ, uniform));
    });
    objects.addFunc("setOrientation",
                    [objectsVm](ObjectBuildLayer* self, float cellSize, float yawOffset, bool invert) {
                        return eve::script::projectResult(objectsVm, self->setOrientation(cellSize, yawOffset, invert));
                    });
    objects.addFunc("disableOrientation", [](ObjectBuildLayer* self) { self->disableOrientation(); });
    objects.addFunc("setPlaceOnTop", [](ObjectBuildLayer* self, bool enabled, bool useLowest, float topOffset) {
        self->setPlaceOnTop(enabled, useLowest, topOffset);
    });
    objects.addFunc("addChild", [objectsVm](ObjectBuildLayer* self, const std::string& asset, int count, float radius,
                                            float minScale, float maxScale, float minYaw, float maxYaw) {
        return eve::script::projectResult(objectsVm,
                                          self->addChild(asset, count, radius, minScale, maxScale, minYaw, maxYaw));
    });
    objects.addFunc("clearChildRules", &ObjectBuildLayer::clearChildRules);
    objects.addFunc("serializeDefinition", &ObjectBuildLayer::serializeDefinition);
    objects.addFunc("deserializeDefinition", [objectsVm](ObjectBuildLayer* self, const std::string& definition) {
        return eve::script::projectResult(objectsVm, self->deserializeDefinition(definition));
    });
    objects.addFunc("build", [objectsVm](ObjectBuildLayer* self, PointSet* source) -> ssq::Table {
        if (!source)
            return eve::script::projectStatusResult(
                objectsVm,
                Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "source points are null"))
                    .status());
        auto result = self->build(*source);
        if (!result.ok()) return eve::script::projectStatusResult(objectsVm, result.status());
        auto instance = eve::script::makeOwnedSquirrelInstance<PointSet>(
            objectsVm, std::make_unique<PointSet>(std::move(result).takeValue()));
        if (!instance.ok()) return eve::script::projectStatusResult(objectsVm, instance.status());
        return eve::script::projectStatusResult(objectsVm, Status::success(), std::move(instance).takeValue());
    });
    objects.addFunc("buildOriented",
                    [objectsVm](ObjectBuildLayer* self, PointSet* source, PointSet* orientation) -> ssq::Table {
                        if (!source || !orientation)
                            return eve::script::projectStatusResult(
                                objectsVm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                                   "source or orientation is null"))
                                               .status());
                        auto result = self->build(*source, orientation);
                        if (!result.ok()) return eve::script::projectStatusResult(objectsVm, result.status());
                        auto instance = eve::script::makeOwnedSquirrelInstance<PointSet>(
                            objectsVm, std::make_unique<PointSet>(std::move(result).takeValue()));
                        if (!instance.ok()) return eve::script::projectStatusResult(objectsVm, instance.status());
                        return eve::script::projectStatusResult(objectsVm, Status::success(),
                                                                std::move(instance).takeValue());
                    });

    auto execution = table.addClass<BuildLayerExecution>(
        "ProcgenBuildLayerExecution",
        std::function<BuildLayerExecution*()>([]() -> BuildLayerExecution* { return nullptr; }), true);
    const HSQUIRRELVM executionVm = execution.getHandle();
    execution.addFunc("getCount", [](BuildLayerExecution* self) { return self->getCount(); });
    execution.addFunc("getId", &BuildLayerExecution::getId);
    execution.addFunc("getType", &BuildLayerExecution::getType);
    execution.addFunc("getMesh", [executionVm](BuildLayerExecution* self, int index) -> ssq::Table {
        auto result = self->getMesh(index);
        if (!result.ok()) return eve::script::projectStatusResult(executionVm, result.status());
        auto instance = eve::script::makeOwnedSquirrelInstance<MeshBuild>(
            executionVm, std::make_unique<MeshBuild>(std::move(result).takeValue()));
        if (!instance.ok()) return eve::script::projectStatusResult(executionVm, instance.status());
        return eve::script::projectStatusResult(executionVm, Status::success(), std::move(instance).takeValue());
    });
    execution.addFunc("getPoints", [executionVm](BuildLayerExecution* self, int index) -> ssq::Table {
        auto result = self->getPoints(index);
        if (!result.ok()) return eve::script::projectStatusResult(executionVm, result.status());
        auto instance = eve::script::makeOwnedSquirrelInstance<PointSet>(
            executionVm, std::make_unique<PointSet>(std::move(result).takeValue()));
        if (!instance.ok()) return eve::script::projectStatusResult(executionVm, instance.status());
        return eve::script::projectStatusResult(executionVm, Status::success(), std::move(instance).takeValue());
    });

    auto stack = table.addClass<BuildLayerStack>("ProcgenBuildLayerStack", ssq::Class::Ctor<BuildLayerStack()>());
    const HSQUIRRELVM stackVm = stack.getHandle();
    stack.addFunc("addTileLayer", [stackVm](BuildLayerStack* self, const std::string& id, bool enabled, float cellSize,
                                            float height, const std::string& group) {
        return eve::script::projectResult(stackVm, self->addTileLayer(id, enabled, cellSize, height, group));
    });
    stack.addFunc("addObjectLayer", [stackVm](BuildLayerStack* self, const std::string& id, bool enabled,
                                              ObjectBuildLayer* layer) {
        if (!layer)
            return eve::script::projectResult(stackVm, Result<void>::failure(Diagnostic::error(
                                                           DiagnosticCode::InvalidArgument, "object layer is null")));
        return eve::script::projectResult(stackVm, self->addObjectLayer(id, enabled, *layer));
    });
    stack.addFunc("setEnabled", [stackVm](BuildLayerStack* self, const std::string& id, bool enabled) {
        return eve::script::projectResult(stackVm, self->setEnabled(id, enabled));
    });
    stack.addFunc("clear", &BuildLayerStack::clear);
    stack.addFunc("getLayerCount", [](BuildLayerStack* self) { return self->getLayerCount(); });
    stack.addFunc("getLayerId", &BuildLayerStack::getLayerId);
    stack.addFunc("getLayerType", &BuildLayerStack::getLayerType);
    stack.addFunc("isLayerEnabled", [](BuildLayerStack* self, int index) { return self->isLayerEnabled(index); });
    stack.addFunc("serializeDefinition", &BuildLayerStack::serializeDefinition);
    stack.addFunc("deserializeDefinition", [stackVm](BuildLayerStack* self, const std::string& definition) {
        return eve::script::projectResult(stackVm, self->deserializeDefinition(definition));
    });
    stack.addFunc("execute", [stackVm](BuildLayerStack* self, Grid2D* grid, PointSet* points) -> ssq::Table {
        if (!grid || !points)
            return eve::script::projectStatusResult(
                stackVm, Result<void>::failure(
                             Diagnostic::error(DiagnosticCode::InvalidArgument, "grid or points input is null"))
                             .status());
        auto result = self->execute(*grid, *points);
        if (!result.ok()) return eve::script::projectStatusResult(stackVm, result.status());
        auto instance = eve::script::makeOwnedSquirrelInstance<BuildLayerExecution>(
            stackVm, std::make_unique<BuildLayerExecution>(std::move(result).takeValue()));
        if (!instance.ok()) return eve::script::projectStatusResult(stackVm, instance.status());
        return eve::script::projectStatusResult(stackVm, Status::success(), std::move(instance).takeValue());
    });
    stack.addFunc(
        "executeOriented",
        [stackVm](BuildLayerStack* self, Grid2D* grid, PointSet* points, PointSet* orientation) -> ssq::Table {
            if (!grid || !points || !orientation)
                return eve::script::projectStatusResult(
                    stackVm, Result<void>::failure(
                                 Diagnostic::error(DiagnosticCode::InvalidArgument, "build-layer input is null"))
                                 .status());
            auto result = self->execute(*grid, *points, orientation);
            if (!result.ok()) return eve::script::projectStatusResult(stackVm, result.status());
            auto instance = eve::script::makeOwnedSquirrelInstance<BuildLayerExecution>(
                stackVm, std::make_unique<BuildLayerExecution>(std::move(result).takeValue()));
            if (!instance.ok()) return eve::script::projectStatusResult(stackVm, instance.status());
            return eve::script::projectStatusResult(stackVm, Status::success(), std::move(instance).takeValue());
        });

    auto delta = table.addClass<IncrementalBuildDelta>(
        "ProcgenIncrementalBuildDelta",
        std::function<IncrementalBuildDelta*()>([]() -> IncrementalBuildDelta* { return nullptr; }), true);
    const HSQUIRRELVM deltaVm = delta.getHandle();
    delta.addFunc("getCount", [](IncrementalBuildDelta* self) { return self->getCount(); });
    delta.addFunc("getClusterX", [](IncrementalBuildDelta* self, int index) { return self->getClusterX(index); });
    delta.addFunc("getClusterZ", [](IncrementalBuildDelta* self, int index) { return self->getClusterZ(index); });
    delta.addFunc("isRemoved", [](IncrementalBuildDelta* self, int index) { return self->isRemoved(index); });
    delta.addFunc("getArtifacts", [deltaVm](IncrementalBuildDelta* self, int index) -> ssq::Table {
        auto result = self->getArtifacts(index);
        if (!result.ok()) return eve::script::projectStatusResult(deltaVm, result.status());
        auto instance = eve::script::makeOwnedSquirrelInstance<BuildLayerExecution>(
            deltaVm, std::make_unique<BuildLayerExecution>(std::move(result).takeValue()));
        if (!instance.ok()) return eve::script::projectStatusResult(deltaVm, instance.status());
        return eve::script::projectStatusResult(deltaVm, Status::success(), std::move(instance).takeValue());
    });

    auto incremental = table.addClass<IncrementalBuildExecutor>("ProcgenIncrementalBuildExecutor",
                                                                 ssq::Class::Ctor<IncrementalBuildExecutor()>());
    const HSQUIRRELVM incrementalVm = incremental.getHandle();
    incremental.addFunc("clear", &IncrementalBuildExecutor::clear);
    incremental.addFunc("getCachedClusterCount",
                        [](IncrementalBuildExecutor* self) { return self->getCachedClusterCount(); });
    incremental.addFunc("getCachedArtifacts",
                        [incrementalVm](IncrementalBuildExecutor* self, int x, int z) -> ssq::Table {
                            auto result = self->getCachedArtifacts(x, z);
                            if (!result.ok()) return eve::script::projectStatusResult(incrementalVm, result.status());
                            auto instance = eve::script::makeOwnedSquirrelInstance<BuildLayerExecution>(
                                incrementalVm,
                                std::make_unique<BuildLayerExecution>(std::move(result).takeValue()));
                            if (!instance.ok())
                                return eve::script::projectStatusResult(incrementalVm, instance.status());
                            return eve::script::projectStatusResult(incrementalVm, Status::success(),
                                                                    std::move(instance).takeValue());
                        });
    incremental.addFunc(
        "update",
        [incrementalVm](IncrementalBuildExecutor* self, BuildLayerStack* buildStack, Grid2D* grid, PointSet* points,
                        int clusterSizeCells, float cellSizeWorld) -> ssq::Table {
            if (!buildStack || !grid || !points)
                return eve::script::projectStatusResult(
                    incrementalVm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                           "incremental build input is null"))
                                       .status());
            auto result = self->update(*buildStack, *grid, *points, clusterSizeCells, cellSizeWorld);
            if (!result.ok()) return eve::script::projectStatusResult(incrementalVm, result.status());
            auto instance = eve::script::makeOwnedSquirrelInstance<IncrementalBuildDelta>(
                incrementalVm, std::make_unique<IncrementalBuildDelta>(std::move(result).takeValue()));
            if (!instance.ok()) return eve::script::projectStatusResult(incrementalVm, instance.status());
            return eve::script::projectStatusResult(incrementalVm, Status::success(), std::move(instance).takeValue());
        });
    incremental.addFunc(
        "updateOriented",
        [incrementalVm](IncrementalBuildExecutor* self, BuildLayerStack* buildStack, Grid2D* grid, PointSet* points,
                        int clusterSizeCells, float cellSizeWorld, PointSet* orientation) -> ssq::Table {
            if (!buildStack || !grid || !points || !orientation)
                return eve::script::projectStatusResult(
                    incrementalVm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                           "incremental build input is null"))
                                       .status());
            auto result =
                self->update(*buildStack, *grid, *points, clusterSizeCells, cellSizeWorld, orientation);
            if (!result.ok()) return eve::script::projectStatusResult(incrementalVm, result.status());
            auto instance = eve::script::makeOwnedSquirrelInstance<IncrementalBuildDelta>(
                incrementalVm, std::make_unique<IncrementalBuildDelta>(std::move(result).takeValue()));
            if (!instance.ok()) return eve::script::projectStatusResult(incrementalVm, instance.status());
            return eve::script::projectStatusResult(incrementalVm, Status::success(), std::move(instance).takeValue());
        });
}

}  // namespace eve::procgen
