#pragma once

#include "common/Module.h"

#include <memory>
#include <string>

namespace eve::editor {
class EditorSession;
}
namespace eve::graphics {
class Graphics;
class Mesh;
}  // namespace eve::graphics
namespace eve::procgen {
class Heightmap;
}
namespace eve::procgen_editing {
class HeightmapTarget;
}

namespace eve::heightmap_target {

/** @brief Script-facing composition module for live heightmap editing targets. */
class HeightmapTargetModule final : public Module {
public:
    Module_REG(HeightmapTargetModule);

    /** @brief Create an independently owned adapter over a borrowed heightmap. */
    [[nodiscard]] std::unique_ptr<procgen_editing::HeightmapTarget> newTarget(std::string         id,
                                                                              procgen::Heightmap* heightmap) const;
    /** @brief Bind borrowed target and session objects without taking ownership. @thread Owner-thread only. */
    void bind(editor::EditorSession* session, procgen_editing::HeightmapTarget* target) const;
    /** @brief Compatibility projection of the canonical structured brush result for scripts. */
    int applyBrush(procgen::Heightmap* heightmap, float centerX, float centerY, float radius, float strength) const;
    /** @brief Compatibility projection that creates a flat-shaded preview mesh. @thread Render-thread only. */
    graphics::Mesh* newMesh(procgen::Heightmap* heightmap, float cellSize, float heightScale) const;
    /** @brief Compatibility projection that updates a flat-shaded preview mesh. @thread Render-thread only. */
    bool updateMesh(graphics::Mesh* mesh, graphics::Graphics* graphics, procgen::Heightmap* heightmap, float cellSize,
                    float heightScale) const;
    /** @brief Compatibility projection that creates a smooth-normal preview mesh. @thread Render-thread only. */
    graphics::Mesh* newSmoothMesh(procgen::Heightmap* heightmap, float cellSize, float heightScale) const;
    /** @brief Compatibility projection that updates a smooth-normal preview mesh. @thread Render-thread only. */
    bool updateSmoothMesh(graphics::Mesh* mesh, graphics::Graphics* graphics, procgen::Heightmap* heightmap,
                          float cellSize, float heightScale) const;
};

}  // namespace eve::heightmap_target
