#pragma once

#include "common/Module.h"

namespace eve {
namespace graphics {
class Graphics;
class Mesh;
}
#ifdef EVENGINE_HAS_PROCGEN
namespace procgen {
class Heightmap;
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
    Editor() = default;
    ~Editor() override = default;

    TransformGizmo *newGizmo();
    GizmoManager   *newGizmoManager();

    TileBuffer *newTileBuffer(int width, int height);
    Brush      *newBrush();

    EditorToolbar   *newToolbar();
    EditorInspector *newInspector();
    EditorDock      *newDock();
    EditorHistory   *newHistory();

#ifdef EVENGINE_HAS_PROCGEN
    /**
     * Build a flat-shaded terrain mesh from a heightmap (grid of cells,
     * X/Z in world units = index * cellSize, Y = height * heightScale).
     * Caller attaches it to a Renderable3D via setMesh(). Returns nullptr on
     * failure (no Graphics module / null heightmap).
     */
    graphics::Mesh *newHeightmapMesh(procgen::Heightmap *hm, float cellSize, float heightScale);
    /** In-place vertex update after heightmap edits (reuses GPU buffers). */
    bool updateHeightmapMesh(graphics::Mesh *mesh, graphics::Graphics *gfx,
                             procgen::Heightmap *hm, float cellSize, float heightScale);
#endif
};

}  // namespace eve::editor
