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
- one reusable recipe-field builder compatible with grid, texture, PBR, and mesh
  recipe schemas; the project chooses its fields and layout, then uploads generated
  albedo/normal/height maps into a normal engine `Material`;
- a live embedded 3D runtime viewport using `procgen`, materials, and a field tool assembled
  from reusable target, kernel, falloff, operation, and transaction components;
- channelled semantic selection shared by hierarchy, viewport, and inspector;
- ECS-backed runtime objects created through the same command service used by a
  developer editor or an in-game builder;
- extension-shaped actions for RTS, card, voxel, dialogue, and avatar workflows.
- a project-owned `DialogueEditorComponent` that reflects `ConversationDocument`
  fields into ordinary UI controls, validates the graph, and applies it to the same
  runtime registry used by the game; the engine contains no fixed dialogue panel.
- project-owned Card/RTS adapters that validate definitions with generic `Schema`,
  store versioned data in `Definitions`, create real Card/ECS/Crowd runtime objects,
  and derive Crowd movement costs from the same editable terrain heightmap.

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
The inspector uses that exact field builder with `pbr.rock`, filters the project-relevant
knobs, and composes `ProcgenRecipeSchema`, `Params`, `ImageData`, GPU textures and
`Material`; there is no C++ material-editor window.

`dialogue_component.nut` is deliberately separate from the composition root. It
selects a compact subset of reflected line fields for this project's bottom bar;
another project can use the same document API for a node canvas, localization QA,
cutscene preview, or an in-game quest authoring screen without modifying C++.

`gameplay_components.nut` demonstrates the shared game/editor architecture. Card
definitions have deterministic IDs and mutable registration APIs for project tools;
RTS agents use stable logical IDs even though Crowd keeps swap-pop compact storage.
The component maps those agents back to ECS objects every frame and rebuilds movement
costs from terrain data. A different card game or RTS can replace the schemas,
definitions and presenters while retaining the same libraries.

Mesh generators use the same contract: call `getMeshRecipeSchema` and
`applyMeshRecipeDefaults`, pass the returned schema to `renderRecipeFields`, and feed
the edited `Params` to `buildMesh` or `generateMesh`. Built-in schemas cover marching
cubes, rocks, planets, trees, bushes, buildings, linear structures, and urban blocks;
projects decide whether those become asset inspectors, world brushes, RTS construction
tools, or runtime customization screens.
