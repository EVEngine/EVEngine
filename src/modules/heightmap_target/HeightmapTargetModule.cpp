#include "heightmap_target/HeightmapTargetModule.h"

#include "editor/EditorSession.h"
#include "procgen_editing/HeightmapBrush.h"
#include "procgen_editing/HeightmapTarget.h"
#include "procgen_graphics_editing/HeightmapMesh.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <functional>
#include <utility>

namespace eve::heightmap_target {

Module_IMPL(HeightmapTargetModule, new HeightmapTargetModule());

std::unique_ptr<procgen_editing::HeightmapTarget> HeightmapTargetModule::newTarget(
    std::string id, procgen::Heightmap* heightmap) const {
    return procgen_editing::createHeightmapTarget(std::move(id), heightmap);
}

void HeightmapTargetModule::bind(editor::EditorSession* session,
                                 procgen_editing::HeightmapTarget* target) const {
    if (session) session->bindTarget(target);
}

int HeightmapTargetModule::applyBrush(procgen::Heightmap* heightmap, float centerX,
                                      float centerY, float radius, float strength) const {
    auto result = procgen_editing::applyHeightmapBrush(heightmap, centerX, centerY, radius, strength);
    return result.ok() ? result.value() : 0;
}

graphics::Mesh* HeightmapTargetModule::newMesh(procgen::Heightmap* heightmap, float cellSize,
                                               float heightScale) const {
    auto result = procgen_graphics_editing::createHeightmapMesh(heightmap, cellSize, heightScale, false);
    return result.ok() ? result.value() : nullptr;
}

bool HeightmapTargetModule::updateMesh(graphics::Mesh* mesh, graphics::Graphics* graphics,
                                       procgen::Heightmap* heightmap, float cellSize,
                                       float heightScale) const {
    return procgen_graphics_editing::updateHeightmapMesh(
               mesh, graphics, heightmap, cellSize, heightScale, false)
        .ok();
}

graphics::Mesh* HeightmapTargetModule::newSmoothMesh(procgen::Heightmap* heightmap, float cellSize,
                                                     float heightScale) const {
    auto result = procgen_graphics_editing::createHeightmapMesh(heightmap, cellSize, heightScale, true);
    return result.ok() ? result.value() : nullptr;
}

bool HeightmapTargetModule::updateSmoothMesh(graphics::Mesh* mesh, graphics::Graphics* graphics,
                                             procgen::Heightmap* heightmap, float cellSize,
                                             float heightScale) const {
    return procgen_graphics_editing::updateHeightmapMesh(
               mesh, graphics, heightmap, cellSize, heightScale, true)
        .ok();
}

void HeightmapTargetModule::expose(ssq::Table& table) {
    auto target = table.addClass<procgen_editing::HeightmapTarget>(
        "HeightmapTarget",
        std::function<procgen_editing::HeightmapTarget*()>([]() -> procgen_editing::HeightmapTarget* {
            return nullptr;
        }),
        true);
    target.addFunc("getTargetId", [](procgen_editing::HeightmapTarget* self) {
        return self ? self->targetId().value() : std::string{};
    });
    target.addFunc("getRevision", [](procgen_editing::HeightmapTarget* self) {
        return self ? static_cast<int64_t>(self->revision()) : int64_t{0};
    });
    target.addFunc("getWidth", &procgen_editing::HeightmapTarget::width);
    target.addFunc("getHeight", &procgen_editing::HeightmapTarget::height);
    target.addFunc("readScalar", &procgen_editing::HeightmapTarget::readScalar);
    target.addFunc("writeScalar", &procgen_editing::HeightmapTarget::writeScalar);
    target.addFunc("sampleScalar", &procgen_editing::HeightmapTarget::sampleScalar);
    target.addFunc("clearDirtyRegion", &procgen_editing::HeightmapTarget::clearDirtyRegion);

    auto module = table.addClass(name, HeightmapTargetModule::create, false);
    expose(module);
}

void HeightmapTargetModule::expose(ssq::Class& cls) {
    cls.addFunc("create", [](HeightmapTargetModule* self, const std::string& id,
                             procgen::Heightmap* heightmap) {
        return self->newTarget(id, heightmap).release();
    });
    cls.addFunc("bind", &HeightmapTargetModule::bind);
    cls.addFunc("applyBrush", &HeightmapTargetModule::applyBrush);
    cls.addFunc("newMesh", &HeightmapTargetModule::newMesh);
    cls.addFunc("updateMesh", &HeightmapTargetModule::updateMesh);
    cls.addFunc("newSmoothMesh", &HeightmapTargetModule::newSmoothMesh);
    cls.addFunc("updateSmoothMesh", &HeightmapTargetModule::updateSmoothMesh);
}

}  // namespace eve::heightmap_target
