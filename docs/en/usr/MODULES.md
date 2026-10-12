# Module Handbook

[中文](../../usr/MODULES.md) · [User guide](README.md) · [API conventions](API-CONVENTIONS.md)

Download an SDK from [Releases](https://github.com/EVEngine/EVEngine/releases), unzip it and run `eve run <game-directory>`. No engine compilation is needed.

Guides explain tasks, working examples, ownership and failure handling. Exact C++ signatures belong in [Doxygen](https://evengine.github.io/EVEngine/); callable EveScript signatures belong in the SDK's `share/eve/ai/eve-api.json` or editor/MCP API lookup. A C++ public method is not necessarily script-callable.

English guides are maintained by hand with Chinese as the authoritative source. This first batch covers onboarding, language, saving and common game-development workflows. Links marked **Chinese** lead to guides not yet translated; they remain useful and are not represented as English documentation.

Use `has_module("slot")` for optional features. Project module selection and source-build trimming are different; see [EveScript](EVESCRIPT.md) and [module trimming](TRIMMING.md).

[SCAD and LDraw model import](modules/asset-structured-models.md) converts procedural solids and part assemblies into canonical native static prefabs.

## Runtime and input

- [Window](modules/window.md): window dimensions, resizing and DPI.
- [Keyboard](modules/keyboard.md): held keys, edge detection and text input.
- [Mouse](modules/mouse.md): pointer coordinates, relative motion and wheel snapshots.
- [Agent — Chinese](../../usr/modules/agent.md): policy learning, decisions, tensor inference and trajectory replay.
- [Events — Chinese](../../usr/modules/event.md): platform event pumping and string-message queues.
- [Reactive streams — Chinese](../../usr/modules/rx.md): Subjects, operators, reactive properties and event bridging.
- [Timer — Chinese](../../usr/modules/timer.md): elapsed time and frame intervals.
- [OS — Chinese](../../usr/modules/os.md): operating system, CPU, memory, battery, clipboard and GPU information.
- [Touch — Chinese](../../usr/modules/touch.md): touch counts and normalized/screen coordinates.
- [Joystick — Chinese](../../usr/modules/joystick.md): axes, buttons, hats, controller mappings and vibration.

## Assets and I/O

- [Filesystem and hot reload](modules/filesystem.md): virtual paths, save files and local/remote reload.
- [Data — Chinese](../../usr/modules/data.md): byte data/views, compression, hashes and JSON/XML.
- [Image — Chinese](../../usr/modules/image.md): script-binding limits and image data from font glyphs.
- [Font — Chinese](../../usr/modules/font.md): font data, glyph metrics and bitmaps.
- [Localization — Chinese](../../usr/modules/i18n.md): JSON dictionaries, placeholders, plurals and language fallback.
- [Sound data — Chinese](../../usr/modules/sound.md): decoding and samples for audio sources.
- [Audio — Chinese](../../usr/modules/audio.md): playback, volume, pitch, looping and 3D source positions.
- [3D models — Chinese](../../usr/modules/model3d.md): model loading for rendering.
- [Network — Chinese](../../usr/modules/network.md): HTTP, TCP and network state.

## Gameplay

- [Script ECS](modules/entity.md): components, entity composition, systems and GPU integration.
- [Physics — Chinese](../../usr/modules/physics.md): 2D and 3D worlds, contacts and spatial queries.
- [Cloth — Chinese](../../usr/modules/physics_cloth.md): cloth simulation.
- [Rope — Chinese](../../usr/modules/physics_rope.md): rope simulation and attachments.
- [Destruction — Chinese](../../usr/modules/physics_destruction.md): geometry collections and runtime destruction.
- [Destruction graphics — Chinese](../../usr/modules/physics_destruction_graphics.md): destruction presentation.
- [Tilemap — Chinese](../../usr/modules/map.md): layers, tiles, projections and rendering.
- [Particles — Chinese](../../usr/modules/particles.md): emitters, movement, color, lifetime and rendering.
- [PixelWorld — Chinese](../../usr/modules/pixelworld.md): material-cell simulation.
- [PixelWorld replay — Chinese](../../usr/modules/pixelworld_replay.md): deterministic replay.
- [PixelWorld streaming — Chinese](../../usr/modules/pixelworld_streaming.md): interest areas and authoritative corrections.
- [PixelWorld editor — Chinese](../../usr/modules/pixelworld_editor.md): material catalog editing.
- [Animation — Chinese](../../usr/modules/animation.md): tweening, skeletal playback, motion matching, procedural poses and trails.
- [Climbing — Chinese](../../usr/modules/climbing.md): obstacle detection and physics-constrained vault/mantle actions.
- [Action and abilities — Chinese](../../usr/modules/action.md): grants, cooldowns, activation and action submission.
- [Combat — Chinese](../../usr/modules/combat.md): combat runtime and simulation.
- [Settlement — Chinese](../../usr/modules/settlement.md): deterministic settlement rules and ledgers.
- [NPC AI — Chinese](../../usr/modules/npc_ai.md): state trees, blackboards, sensing memory and scheduling.
- [RPG — Chinese](../../usr/modules/rpg.md): attributes, statuses, skills, casting and damage.
- [Inventory — Chinese](../../usr/modules/inventory.md): bags, transfer, equipment and capacity/stacking rules.
- [Cards — Chinese](../../usr/modules/cardgame.md): hands, drawing, shuffling, dragging and costs.
- [Building — Chinese](../../usr/modules/building.md): definitions, occupied cells, previews, validation and snapping.
- [Building visuals — Chinese](../../usr/modules/buildingfx.md): 2D/3D placement presentation.
- [House generation — Chinese](../../usr/modules/housegen.md): component-based generation into layout JSON.
- [ArchSpace — Chinese](../../usr/modules/archspace.md): floor, wall, room and opening documents.
- [Procedural generation — Chinese](../../usr/modules/procgen.md): meshes, maps, images and GPU textures.
- [Hex maps — Chinese](../../usr/modules/hexmap.md): planar/spherical terrain, pathfinding, units and saves.
- [Weapons — Chinese](../../usr/modules/weapon.md): ammunition, mounts, aiming, firing and reloading.
- [Vehicles — Chinese](../../usr/modules/vehicle.md): movement, seats, turrets, damage and physics integration.
- [Effects — Chinese](../../usr/modules/effects.md): stackable and expiring effects.
- [State patches — Chinese](../../usr/modules/statepatch.md): atomic JSON updates with conflict checks.
- [Orders — Chinese](../../usr/modules/orders.md): priority queues, preemption, timeouts and snapshots.
- [Social graph — Chinese](../../usr/modules/social.md): ownership, control, roles and directed relations.
- [Production — Chinese](../../usr/modules/production.md): parallel slots and resumable production tasks.
- [Authority — Chinese](../../usr/modules/authority.md): prioritized allow/deny rules.
- [Sensing — Chinese](../../usr/modules/sensing.md): candidate queries and selection.
- [Emergence — Chinese](../../usr/modules/emergence.md): rule-based triggers.

## Rendering and scenes

- [Graphics: getting started](modules/graphics.md): 2D frames, sprites, 3D resources and rendering boundaries.
- [UI: getting started](modules/ui.md): retained trees, stable IDs, events and reusable components.
- [Fog — Chinese](../../usr/modules/graphics_fog.md): fluid density, ray marching and froxel integration.
- [Voxel — Chinese](../../usr/modules/voxel.md): chunk worlds, meshes, picking, streaming and saves.
- [Sprite stacking — Chinese](../../usr/modules/spritestack.md): layered images for pseudo-3D rendering.
- [Day/night — Chinese](../../usr/modules/daynight.md): sun orbit, sky and night lighting.
- [Weather — Chinese](../../usr/modules/weather.md): rain, snow, lightning and wind.
- [Snow — Chinese](../../usr/modules/snow.md): interactive accumulation and displacement.
- [Avatar — Chinese](../../usr/modules/avatar.md): layered characters, expressions, lip sync and motion.
- [Dialogue — Chinese](../../usr/modules/dialogue.md): staging, typing, choices and `.dnut` scripts.
- [Camera controllers — Chinese](../../usr/modules/camera.md): follow, orbit, top-down, first-person and sequences.
- [Scene loading — Chinese](../../usr/modules/sceneloader.md): glTF/OBJ/FBX, reload and async loading.
- [Scene graph — Chinese](../../usr/modules/scene.md): hierarchy, local/world transforms and render entities.
- [Decals — Chinese](../../usr/modules/decal.md): projected and procedural decals.

## Advanced capabilities

- [Math — Chinese](../../usr/modules/math.md): vectors, matrices, geometry, noise, random numbers and easing.
- [Spatial indexing — Chinese](../../usr/modules/spatial.md): culling and neighborhood queries.
- [Editor components — Chinese](../../usr/modules/editor.md): gizmos, brushes, inspectors, docks and history.
- [IK — Chinese](../../usr/modules/ik.md): skeletons, targets and FABRIK solving.
- [GPU compute — Chinese](../../usr/modules/gpgpu.md): buffers, compute shaders and dispatch.
- [Tensor — Chinese](../../usr/modules/tensor.md): eager operations and compiled graphs.
- [Threads — Chinese](../../usr/modules/thread.md): safe worker tasks and main-thread delivery.
- [Stylized rendering — Chinese](../../usr/modules/stylize.md): cartoon, watercolor, ink and pixel effects.
- [Virtual geometry — Chinese](../../usr/modules/virtualgeometry.md): GPU cluster culling and LOD.
- [Grid — Chinese](../../usr/modules/grid.md): native coordinate/topology services used by map and building.
- [Native plugins — Chinese](../../usr/modules/plugins.md): SDK-built dynamic modules.
- [Built-in demo — Chinese](../../usr/modules/demo.md): runtime installation checks.

For player persistence, start with [Saving games across modules](SAVE.md). Additional guides remain indexed by the [Chinese handbook](../../usr/MODULES.md).

## Extended gameplay and advanced guides (Chinese)

The [expanded Chinese navigation](../../usr/MODULES.md) includes Economy, Attributes, Decision,
Definitions, Tags, GameEvent, PolicyRegistry, Transaction and RTS; it also covers provider/editor
satellites, editable targets, RayTracing, StylizeAction, Fluids and native asset workflows.
Start with the [complete game tutorial](../../usr/GAME_DEVELOPMENT.md), then follow those
module chapters for tasks, initialization, lifetime and failure handling. Supplementary signatures
are generated from source; they are not a platform execution report.
