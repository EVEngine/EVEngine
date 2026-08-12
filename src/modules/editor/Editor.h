#pragma once

#include "common/Module.h"

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
 * Editor building blocks — not a shipped 3D/map editor app.
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
};

}  // namespace eve::editor
