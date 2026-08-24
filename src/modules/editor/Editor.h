#pragma once

#include "common/Module.h"
#include "editor/EditorCommandService.h"

#include <memory>
#include <string>

namespace eve {
namespace graphics {
class Graphics;
class Mesh;
}  // namespace graphics
#ifdef EVENGINE_HAS_PROCGEN
namespace procgen {
class Heightmap;
}
#endif
#ifdef EVENGINE_HAS_MAP
namespace map {
class TileLayer;
}
#endif
}  // namespace eve

namespace eve::editor {

class TransformGizmo;
class GizmoManager;
class TileBuffer;
class Brush;
class EditorToolbar;
class EditorInspector;
class EditorDock;
class EditorHistory;
class EditorSession;
class TileBufferTarget;
#ifdef EVENGINE_HAS_MAP
class TileLayerTarget;
#endif
class HeightmapTarget;
class ScriptEditorTool;
class EditorAutomationProvider;

/**
 * @brief Editor building blocks — not a shipped 3D/map editor app.
 * Script: `editor <- eve.Editor();`
 *
 * Provides transform gizmos, map brushes, and UI shell helpers so games/tools
 * can assemble their own editors (Love2D-style runtime tooling).
 */
class Editor : public Module {
public:
    Module_REG(Editor);
    Editor();
    ~Editor() override;

    TransformGizmo* newGizmo();
    GizmoManager*   newGizmoManager();

    TileBuffer* newTileBuffer(int width, int height);
    Brush*      newBrush();

    EditorToolbar*   newToolbar();
    EditorInspector* newInspector();
    EditorDock*      newDock();
    EditorHistory*   newHistory();
    /** @brief Create a host for interchangeable IEditorTool implementations. */
    EditorSession* newSession();
    /** @brief Return the UI- and script-neutral command registry owned by this editor module. */
    EditorCommandService& commandService() { return commandService_; }
    /** @brief Return the immutable command registry owned by this editor module. */
    const EditorCommandService& commandService() const { return commandService_; }
    /** @brief Adapt existing fields to capability-based editor targets. */
    TileBufferTarget* newTileBufferTarget(const std::string& id, TileBuffer* buffer);
#ifdef EVENGINE_HAS_MAP
    /** @brief Adapt a live map layer to the editor command/undo/brush protocol. */
    TileLayerTarget* newTileLayerTarget(const std::string& id, map::TileLayer* layer);
#endif
    /** @brief Create a script-backed implementation of the IEditorTool protocol. */
    ScriptEditorTool* newScriptTool(const std::string& id, const std::string& label);
#ifdef EVENGINE_HAS_PROCGEN
    HeightmapTarget* newHeightmapTarget(const std::string& id, procgen::Heightmap* heightmap);

    /**
     * @brief Apply a circular linear-falloff brush to a heightmap in one native call.
     * @param hm Heightmap to edit.
     * @param centerX Brush center in heightmap-cell coordinates.
     * @param centerY Brush center in heightmap-cell coordinates.
     * @param radius Radius in cells; values below zero are rejected.
     * @param strength Signed center-height delta (positive raises, negative lowers).
     * @return Number of heightmap samples changed.
     */
    int applyHeightmapBrush(procgen::Heightmap* hm, float centerX, float centerY, float radius, float strength);

    /**
     * Build a flat-shaded terrain mesh from a heightmap (grid of cells,
     * X/Z in world units = index * cellSize, Y = height * heightScale).
     * Caller attaches it to a Renderable3D via setMesh(). Returns nullptr on
     * failure (no Graphics module / null heightmap).
     */
    graphics::Mesh* newHeightmapMesh(procgen::Heightmap* hm, float cellSize, float heightScale);
    /** In-place vertex update after heightmap edits (reuses GPU buffers). */
    bool updateHeightmapMesh(graphics::Mesh* mesh, graphics::Graphics* gfx, procgen::Heightmap* hm, float cellSize,
                             float heightScale);

    /**
     * @brief Heightmap mesh with smooth per-vertex normals (gradient of the
     * height field), so bowls/craters shade continuously instead of showing
     * flat-shaded triangle facets. Uses one shared vertex per heightmap sample
     * and a static indexed topology, reducing update bandwidth substantially.
     */
    graphics::Mesh* newHeightmapMeshSmooth(procgen::Heightmap* hm, float cellSize, float heightScale);
    /** @brief In-place update of a smooth-normal heightmap mesh. */
    bool updateHeightmapMeshSmooth(graphics::Mesh* mesh, graphics::Graphics* gfx, procgen::Heightmap* hm,
                                   float cellSize, float heightScale);
#endif

private:
    EditorCommandService                      commandService_;
    std::unique_ptr<EditorAutomationProvider> automation_;
};

}  // namespace eve::editor
