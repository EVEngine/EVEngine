# Material Studio

A reusable, script-composed material editor with a native, schema-driven document.
The sample entry point is deliberately small: the engine embeds the studio helper,
so projects and packaged SDK games do not need to copy its implementation.

```squirrel
persist studio = null;
eve_init = function() {
    studio = createMaterialStudio({width=config.width, height=config.height});
};
eve_update = function(dt) { studio.update(dt); };
eve_render = function() { studio.render(); };
```

Run with `make run/linux-debug GAME=examples/material-editor` or
`make run/win32-debug GAME=examples/material-editor`. A headless Linux host needs
software Vulkan, null OpenAL and Xvfb as described in the repository AGENTS.md.

## Included tools

- A UE5-inspired Material Ball with two diagonal recessed rings, a beveled/inset
  top cap and a turned pedestal, plus sphere, cube, cylinder and torus models.
- Switchable Photo Studio, Daylight and Sunset skyboxes. Background geometry and
  prefiltered reflection lighting share the same six textures. The example opens
  in Daylight; the reusable factory defaults to Photo Studio.
- Project-relative GLB/glTF/OBJ/FBX static-model loading, with all uploaded mesh
  parts normalized together and assigned the edited material. Native importing
  requires `model3d` (enabled in normal 3D profiles); a host resolver can replace it.
- A separate Preview Settings panel with model/environment selection, IBL intensity,
  skybox/floor visibility and six-face custom skybox loading.
- Three studio lights, a shadow floor, orbit/zoom, a turntable and seven material presets.
- Shading, color, metallic, roughness, blend, transparency, parallax and shadow controls.
- A schema property picker covering every canonical material property, including
  textures, shader references and vegetation settings; typed numeric, enum,
  boolean, color, string and vector editors plus per-property reset.
- Candidate-first validation, atomic multi-property patches and undo/redo.
  Core inspector drags preview a temporary draft and commit once on mouse release.
  A concurrent script edit discards a stale gesture instead of overwriting newer state.
- Versioned JSON save/load. Imports migrate through the existing material v1–v9
  loader and are undoable. Invalid imports preserve the current document/history.

The built-in preview renders conventional tint/metallic/roughness, albedo, normal,
height/parallax, surface and lighting settings. Advanced ORM/emission/vegetation
properties are editable and preserved; their renderer is supplied with `applyPreview`.
This release does not add a visual shader graph compiler or a node canvas.

## Native document API

All mutating calls return checked Result tables. Read `ok` before `value`; failures
carry `status.summary` and structured diagnostics. The VM owns created editors.
Instances and their UI are used only on the owning thread.

```squirrel
local created = materialEditor.create("materials.gold");
if (!created.ok) throw created.status.summary;
local document = created.value;
local changed = document.patch({
    ["shading.tint"] = [1.0, 0.72, 0.22, 1.0],
    ["shading.metallic"] = 1.0,
    ["shading.roughness"] = 0.22,
});
if (!changed.ok) throw changed.status.summary;
```

| API | Behavior |
| --- | --- |
| `create(id)` | Create an owned material editor; empty ids are rejected |
| `configureWorkspace(workspace)` | Atomically install toolbar/preview/inspector panels |
| `schema()` | Detached property descriptors, defaults, types, enums and numeric bounds |
| `read(path)` | Checked owning property value; unknown paths fail |
| `set(path, value)` | Validate and commit one property |
| `patch(table)` | Validate the entire patch and commit one undo record |
| `snapshot()` / `restore(table)` | Export/import a detached canonical versioned snapshot |
| `toJson()` / `fromJson(text)` | Deterministic JSON and checked atomic import |
| `undo()` / `redo()` | Compensate/reapply one patch or import |
| `canUndo()` / `canRedo()` / `getRevision()` | History/revision queries |

No-op edits preserve the revision and redo branch. Cross-field dependencies can
be changed together in one patch, such as a custom shading model and its shader.
The document owns authoring state; it does not retain a renderer or runtime resource.
The studio observes document revisions, so external script edits refresh its widgets
and preview during `update`.

## Custom composition and rendering

Construct `MaterialStudio(options)` and customize before `mount()`, or use
`createMaterialStudio(options)` to construct and mount immediately. Only one mounted
studio should consume a UI context's global event queue. A studio cannot mount twice.

- `getDocument()`, `getWorkspace()` and `getMaterial()` expose the native document,
  composable workspace and current graphics material. Publication creates a complete
  replacement material before attaching it; previously returned materials are snapshots
  of earlier previews. Preview material edits are temporary; persist changes through the document.
- `panelBuilders` maps panel ids to `function(studio)` builders. The defaults are
  `toolbar`, `preview`, `inspector`, and `properties`. Supply `options.panels` to
  override them, and register extra panels on the workspace before rebuilding.
- `resize(width, height)` relays out and rebuilds panels. Hosts call it on resize.
- `presets` and `applyPreset(name)` expose the preset palette.
- `selectPreviewModel(id)` chooses `shaderball`, `sphere`, `cube`, `cylinder` or
  `torus`; `loadPreviewModel(path)` loads a project-relative model and adds it
  to the picker. Bounds include all uploaded submeshes; the preview is centered
  and scaled to the same 2.6-unit extent, including during turntable rotation.
- `registerPreviewModel(id, label, provider)` adds a custom mesh provider returning
  an array of meshes. `options.resolveModel(path)` can provide that same array for
  file loading in a host with a separate asset pipeline. Meshes share model space.
- `getPreviewRenderables()` returns a detached array of the current mesh-part
  renderables. A custom `applyPreview` callback should publish to every part.
  These entities are borrowed and invalidated by model switches; reacquire the
  array for each publication instead of retaining old renderables.
- `selectEnvironment(id)` chooses `studio`, `daylight` or `sunset`.
  `loadSkybox(folder)` loads `px.png`, `nx.png`, `py.png`, `ny.png`, `pz.png`,
  `nz.png` in cubemap order +X/-X/+Y/-Y/+Z/-Z.
  `registerEnvironment(id, label, faces)` accepts six explicit texture paths.
- `previewModel` and `environment` options select initial presets;
  `previewModels` accepts `{id,label,provider}` entries and `environments` accepts
  `{id,label,faces}` entries for initial custom palettes.
- Preview models and skies are cached per studio. Switching them preserves the
  material document, its revision and its undo history. Failed decoding, invalid
  bounds or missing skybox faces preserve the previous active preview.
- `options.resolveTexture(asset)` and `options.resolveShader(asset)` translate
  asset references into live graphics resources. The default texture resolver reads
  filenames. Shader resolution requires the host callback. Resources must outlive draws.
- `options.applyPreview(studio, snapshot)` replaces the complete built-in material
  publisher, enabling specialized PBR, vegetation or custom-shader workflows.
- `options.save(json)` / `options.load()` override storage. Without them, Save/Load
  use `fs.writeTextAtomic`/`fs.readText` at `options.path` (default
  `material-studio-v9.json`); the host must set up its filesystem write directory.
- `id`, `title`, `width`, `height` and optional `screenshot` configure the host.
  The sample opts into an engine-owned screenshot; the reusable helper defaults it off.

C++ hosts can use `material_editor::MaterialEditor` directly. Existing
`MaterialDocumentTarget`, `MaterialPublishingTarget`, `IMaterialRuntimeSink`,
`MaterialStudioController`, and `MaterialPreviewService` remain the lower-level
extension boundaries for atomic runtime publication, coalesced gestures and
revision-safe isolated previews. No new persistent format or parallel material
schema is introduced.

Callbacks run synchronously on the owning thread. Do not reenter mount/update/render
or mutate the document from resource-resolution/publication callbacks. Custom
publishers must stage their resources before changing live renderer state.

## Architecture and validation

The existing material document remains the single authoring owner. Public native
operations use checked Results, detached snapshots and owner-thread contracts;
self-referencing authority/history make the facade immovable. Persistence keeps
the canonical material schema and migrations. No ECS model or new file format is
introduced, and no architecture exception is required.

The material editor explicitly depends on its graphics publication adapter. The
minimal profile with `material_editor` enabled has been configured, compiled,
linked and run as a composition check. The helper also loads and constructs
independent instances with editor modules absent, and rejects mounting with a
clear missing-module message.

Validation covers ten native/script contract cases in
`test/unit/material_editor/material_editor_tests.cpp` and
`test/integration/material-studio/material_studio_scripts.cpp`, the repository `make check` gate, and the
headless `material-editor` example smoke. A functional runtime scenario also
exercises custom panels, presets/history, external document edits, JSON disk
round-trips, resizing, preview overrides and resource-failure recovery.

The preview geometry is generated locally at runtime; it does not copy an Unreal
asset or need an external model download. Static node transforms are baked by the
existing importer before computing shared bounds. Selecting a model tests its
geometry with the current material; source materials are replaced deliberately.
Preview settings are session presentation state and are not added to material JSON.

```squirrel
studio.selectPreviewModel("shaderball");
studio.selectEnvironment("sunset");
studio.loadPreviewModel("assets/teapot.glb");
studio.loadSkybox("assets/skies/courtyard");
```

Rendered examples: [daylight / gold](material-editor-gold.png) and
[sunset / gold](material-editor-sunset.png). Both backgrounds and the reflection
cubemaps are rendered by the engine from their selected environment textures.

Preview antialiasing defaults to `antialiasing="high"`: SSAA renders at twice the
viewport width and height, then resolves with the engine's tent filter. `"ultra"`
uses four times each dimension with a Gaussian resolve (16× source pixels).
The Antialiasing selector switches quality at runtime; `"off"` renders at native
resolution. UI text is rendered afterwards at its own resolution. Higher quality
costs additional GPU time and render-target memory. Source canvases are reused
until the actual viewport pixel dimensions or quality change, including DPI and
layout changes; the UI retains ownership of its destination canvas.

```squirrel
local studio = createMaterialStudio({ antialiasing="ultra" });
studio.setAntialiasing("high"); // off | high | ultra; presentation only, no undo entry
```

The low-level `gfx.newAntiAliasing()` resolver remains available for custom
preview pipelines. This spatial path has no temporal history to invalidate when
switching models, materials, environments or moving the camera.

[Ultra SSAA screenshot](material-editor-aa-ultra.png) shows the fixed gold shader
ball in daylight. Runtime verification covers all three quality levels, source
pixel dimensions, resizing, canvas reuse and unchanged material revision/history.

The example now bundles two photographic cubemaps: **Outdoor Sky**
(default) and **Photo Studio**, selectable alongside the procedural
presets in Skybox. The six PNGs are CC0 Poly Haven derivatives; attribution is in
`assets/environments/ATTRIBUTION.md`. These are 128×128 LDR faces, not full-range
HDR lighting assets. Custom six-face skyboxes can use higher-resolution textures.
The background and environment reflections use the same selected faces. The
smaller round ground exposes the surrounding environment while receiving shadows.

PBR direct lighting, ambient fill and environment reflections are enabled by
default. The main directional light casts CSM shadows; the fill/rim lights add
illumination without extra shadows. Material Cast shadow / Receive shadow still
control the model; the ground receives shadows. Unlit materials intentionally
bypass lighting. The preview uses `gfx.renderScene3DWithShadowsToCanvas(canvas,
camera)` before SSAA resolution. This explicit path adds three directional
shadow-map passes per frame; `gfx.renderScene3DToCanvas(canvas,camera)` remains the
unshadowed forward path used for inexpensive captures. No SSAO/GI pass is implied.


Current rendered captures: [photographic sky + CSM](material-editor-skybox.png)
and [photographic studio + CSM](material-editor-photo-studio.png). The native
pixel test in `test/integration/material-studio/material_preview_shadows.cpp` verifies that receivers
actually darken, each shadow flag works, and unshadowed captures stay independent.
The Vulkan replay shares the normal shadow draw encoder, runs before offscreen
color on the same command buffer, and also works without a swapchain. WebGPU uses
its normal shadow encoder before offscreen color as well; that backend change
has not been exercised on this Linux Vulkan host.

The compact toolbar uses seven tinted material swatches and semantic icons for
undo, redo, save, load and turntable play/pause. Import/reset actions are icon
buttons; model/environment headings and visibility controls also use icons.
Format instructions and navigation help appear on hover instead of occupying
permanent paragraphs. Every action icon has a tooltip and an accessible name;
parameter labels remain visible. Existing action ids and material history are
preserved. The smaller toolbar/property regions leave more room for the preview.

[Current icon-based editor screenshot](material-editor-icons.png).
