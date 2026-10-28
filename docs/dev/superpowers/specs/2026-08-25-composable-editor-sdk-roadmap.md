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
- `IEditableTarget`, field capabilities, replaceable brush kernels/falloffs/operations, and
  `FieldBrushTool`: projects assemble tile or scalar-field tools without adding a domain editor
  to C++. Script factories expose the same composition path and `EditorSession` transaction stack.
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

The audit uses six editor-facing contracts rather than counting UI panels:

1. stable identity and deterministic enumeration;
2. UI-neutral schema/metadata for project-built presenters;
3. mutable runtime data shared by game and editor;
4. a target/document adapter with validation and revisions;
5. command/transaction compatibility where interactive edits require undo;
6. a real runtime preview path.

The composable-editor example is evidence for these contracts, not an official editor
product. Domain-specific layout, filtering, workflow and game rules remain project code.

### Terrain and materials

Complete for the composable SDK baseline: heightmaps, live smooth mesh updates, runtime PBR
materials, `HeightmapTarget`, replaceable kernels/falloffs/operations, and undoable field-tool
transactions. Procgen recipe descriptors drive the project's material controls and the result is
uploaded into an ordinary runtime `Material` shown in the embedded viewport.

Future product extensions: flatten/smooth/stamp/erosion operations, material-layer/splat assets,
selection-aware property providers, and bake/publish documents. They must extend common targets
and commands rather than add a terrain-specific panel.

### Avatar and animation

Complete for the composable SDK baseline: deterministic avatar slot/option enumeration,
compatibility metadata and diagnostics, reflected avatar parameter metadata, mutable animation
clip metadata, stable clip enumeration, and project-side preview actions. These APIs expose
ordinary avatar and animation runtime objects, so a custom host previews the same components used
by game playback.

Future product extensions: skeleton/bone property providers, retarget preview, curve/keyframe
documents and animation-state graph compilation. Timeline and graph presenters remain replaceable.

### Procedural generation and voxel

Complete for the composable SDK baseline: seeded generators; schema-driven grid, texture, PBR and
mesh recipes; shared defaults; height/texture/material/mesh output; voxel worlds and meshing;
`VoxelWorldTarget`; composable sphere/box kernels; and undoable volume brush transactions. One
project-owned field builder renders every recipe kind, proving schemas are presentation-neutral.

Future product extensions: revisioned comparison views, bake/publish commands, voxel palette
assets and selection-aware property providers. Procgen artifacts should eventually become saved
document revisions without changing the recipe schema contract.

### Tilemap

Complete for the composable SDK baseline: tile buffers/maps, projection helpers and a live
`TileLayerTarget` adapter using the same field tool and undo transactions as terrain. The adapter
edits real runtime layer storage rather than copying it into an editor-only model.

Future product extensions: saved layer documents, tile palette assets, multi-cell stamps,
collision/navigation overlays and selection-aware property providers. A tilemap tool remains
another target capability, not a special executable.

### Dialogue

Complete for the composable SDK baseline: `ConversationDocument` provides stable node identity,
reflected node-field metadata, node/route CRUD, structured diagnostics and transactional
`DialogueFlow.applyDocument`. The example generates ordinary project UI from the field schema and
applies the document to the same registry used by runtime playback.

Future product extensions: disk persistence/migration, localization grids, quick-fix commands and
a cancellable preview session. Node-canvas and form presenters remain project choices.

### Card and RTS modes

Complete for the composable SDK baseline: Card definitions have mutable registration,
deterministically sorted IDs and normal runtime instances/decks; Crowd agents have stable logical
IDs over compact swap-pop storage. A project adapter validates Card/RTS definitions through the
generic Schema/Definitions libraries, derives flow-field costs from the live terrain heightmap,
creates ECS objects through the shared command service, and synchronizes Crowd state back to ECS.

Future product extensions: saved card/deck/faction/scenario documents, rule and probability tools,
board-layout preview, archetype palettes, placement constraints and navigation overlays. Those
policies belong to reusable domain services or project extensions, never a mandatory shell.

Both modes integrate through the same services: card placement, RTS unit placement, terrain
sculpting, and dialogue node creation are semantic commands; selected objects expose property
schemas; previews run normal ECS systems; and project extensions contribute panels/tools through
workspace descriptors.

## Delivery sequence

1. Stabilize the workspace composition API and example, including dynamic panels and shared
   selection/focus (complete).
2. Expose generic editable-target, tool, and stroke transaction components to projects; use the
   completed texture/viewport bridge as a replaceable preview host and migrate terrain as the
   end-to-end proof (complete except selection-aware property providers).
3. Add material/procgen/voxel/tilemap adapters and schema-driven recipes (complete for the live
   runtime path; saved artifact documents remain future work).
4. Add dialogue document authoring, animation clip metadata and avatar validation/preview
   contracts (complete for the composable SDK baseline; generic timeline/curve tooling remains
   future work).
5. Add Card and RTS project extensions that only compose the shared foundations (complete).

The next layer is product hardening, not a fixed editor: saved domain documents, generic
selection-aware property adapters, bake/publish tasks, richer overlays and graph/timeline
presenters. Each can be adopted independently by developer or runtime hosts.

Each increment must include binding documentation, focused tests, a runnable example path, and
visual verification. If a domain needs an application-specific assumption, keep it in the example
or extension rather than the engine core.
