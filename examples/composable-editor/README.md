# Composable editor SDK example

This is intentionally an example, not a fixed EVEngine editor product. The C++
`EditorWorkspace` stores UI-neutral panel, dock, selection, focus, and mode state.
The project registers five arbitrary descriptors and maps them to Squirrel view
builders. Replacing `panelBuilders` creates a different editor without touching C++.

The example demonstrates:

- workspace metadata dynamically generating a project-specific UI;
- an MVVM-style reflected `WorldEditorVM` with two-way controls;
- a live 3D game scene using `procgen`, terrain mesh editing, and materials;
- channelled semantic selection shared by hierarchy, viewport, and inspector;
- ECS-backed runtime objects created through the same command service used by a
  developer editor or an in-game builder;
- extension-shaped actions for RTS, card, voxel, dialogue, and avatar workflows.

Run with:

```powershell
make run/win32-debug GAME=examples/composable-editor
```

The domain buttons are deliberately thin. Subsequent editor-module increments
register real domain property providers, tools, previews, and documents while this
example remains a small composition root.
