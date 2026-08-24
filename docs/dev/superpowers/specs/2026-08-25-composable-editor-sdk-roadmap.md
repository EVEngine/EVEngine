# Composable editor SDK roadmap

## Product boundary

EVEngine should provide editor building blocks, not one mandatory editor. A game,
developer editor, and in-game builder share the same ECS world, commands, documents,
property schemas, selection channels, and render systems. Each application composes
its own panels and presenters around those services.

`EditorWorkspace` is the composition root for that policy. It stores UI-neutral
panel descriptors, layout regions, mode, selection, and focus. Squirrel code maps
descriptors to UI builders, so a project can replace its entire editor presentation
without changing engine C++.

## Current reusable foundation

- `EditorWorkspace`: panel discovery, regions, layout, selection/focus, edit/play/simulate mode.
- `EditorSession` and command registry: the same semantic actions can be called by game UI,
  editor UI, automation, or an AI tool.
- property schemas and reflection: generated inspectors and custom MVVM presenters can bind
  to the same object model.
- document, asset database, graph, extension, selection, and focus services: domain editors
  can add capabilities without depending on a particular window toolkit.
- ECS and normal graphics/procgen/runtime modules: previews operate on real runtime data.

### Backend-neutral texture presentation

Declarative image, image-button, and viewport widgets now queue backend-neutral textured
rectangles when a renderer cannot honor per-command texture handles. The Vulkan backend
composites those rectangles in the active UI render pass using engine texture descriptors;
WebGPU retains its native ImGui texture path. A swapchain pixel regression proves a Canvas reaches
the final frame rather than sampling the font atlas. The example uses this bridge for a replaceable
embedded 3D runtime presenter.

## Domain API completeness audit

### Terrain and materials

Available: heightmaps, procedural generation, smooth mesh creation/update, raise/lower brush,
runtime materials, and a working terrain example. Missing editor-library pieces are flatten,
smooth, stamp and erosion tools; material-layer/splat assets; brush stroke transactions;
selection-aware property providers; and bake/publish documents. These should implement common
editable-target and command interfaces rather than terrain-specific panels.

### Avatar and animation

Available: avatar assembly and broad animation playback/blending/runtime bindings. Missing are
avatar slot catalogs, dependency and compatibility validation, skeleton/bone properties,
retarget preview, clip/timeline/curve documents, and animation-state graph presenters. Preview
hosts must consume normal avatar and animation components so the authored result is identical
to game playback.

### Procedural generation and voxel

Available: seeded height/texture generators, voxel worlds and meshing. Missing are schema-driven
generator parameters, revisioned previews, seed comparisons, bake/publish commands, chunk-aware
voxel edit targets, voxel palettes, and undoable brush transactions. Procgen output should be a
document revision that terrain, voxel, tilemap, or material tools can consume.

### Tilemap

Available: tile buffers/maps, brushes, projection helpers, and runtime examples. Missing are
layer documents, tile palette assets, multi-cell selection/stamps, collision/navigation overlays,
and property/command adapters. A tilemap tool should be another editable-target capability, not
a special editor executable.

### Dialogue

Available: dialogue compilation, localization, voice/toolchain support, and runtime playback.
Missing are a dialogue graph domain, node/edge property providers, localization grid, diagnostics
with quick fixes, preview session, and document persistence. The graph presenter should remain
replaceable by any UI implementation.

### Card and RTS modes

Available: card presentation/targeting and RTS-oriented runtime examples and navigation systems.
Card authoring still needs card/deck definition documents, rule validation, board-layout preview,
and probability/test tools. RTS authoring needs archetype palettes, placement commands,
navigation/flow-field overlays, faction/scenario documents, and simulation controls.

Both modes integrate through the same services: card placement, RTS unit placement, terrain
sculpting, and dialogue node creation are semantic commands; selected objects expose property
schemas; previews run normal ECS systems; and project extensions contribute panels/tools through
workspace descriptors.

## Delivery sequence

1. Stabilize the workspace composition API and example, including dynamic panels and shared
   selection/focus (this increment).
2. Define generic editable-target, tool, stroke transaction, preview-host, and property-provider
   interfaces on the completed texture/viewport bridge; migrate terrain first as the end-to-end
   proof.
3. Add material/procgen/voxel/tilemap adapters and document persistence.
4. Add graph/timeline foundations for dialogue and animation, then avatar validation/preview.
5. Add card and RTS project extensions that only compose the shared foundations.

Each increment must include binding documentation, focused tests, a runnable example path, and
visual verification. If a domain needs an application-specific assumption, keep it in the example
or extension rather than the engine core.
