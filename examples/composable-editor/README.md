# Composable editor SDK example

This is intentionally an example, not a fixed EVEngine editor product. The C++
`EditorWorkspace` stores UI-neutral panel, dock, selection, focus, and mode state.
The project registers five arbitrary descriptors and maps them to Squirrel view
builders. Replacing `panelBuilders` creates a different editor without touching C++.

The example demonstrates:

- workspace metadata dynamically generating a project-specific UI;
- an MVVM-style reflected `WorldEditorVM` with two-way controls;
- schema-driven procgen controls assembled by the project from reflected parameter
  types, bounds, defaults, advanced flags, and choices (no built-in procgen panel);
- a live embedded 3D runtime viewport using `procgen`, materials, and a field tool assembled
  from reusable target, kernel, falloff, operation, and transaction components;
- channelled semantic selection shared by hierarchy, viewport, and inspector;
- ECS-backed runtime objects created through the same command service used by a
  developer editor or an in-game builder;
- extension-shaped actions for RTS, card, voxel, dialogue, and avatar workflows.

Run with:

```powershell
make run/win32-debug GAME=examples/composable-editor
```

The terrain tool is also ordinary project composition: it binds a `HeightmapTarget`
to a generic `FieldBrushTool` and sends world-space pointer events through the shared
`EditorSession`; undo and redo therefore use the same transaction stack as other
editable domains. The domain buttons are deliberately thin. Subsequent editor-module increments
register real domain property providers, tools, previews, and documents while this
example remains a small composition root.

The scene presenter is ordinary project code: `panelScene` declares a composable
`UI::Viewport`, while `eve_render` supplies its Canvas from the normal 3D runtime.
Projects can replace either function without changing the C++ editor library.
The bottom panel deliberately chooses `cave.cellular` in project code, enumerates its
registered schema, and maps each semantic field kind to ordinary UI widgets. Changing
the algorithm or the widget mapping produces a different tool without changing the engine.
