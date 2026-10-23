# EVEngine Examples

- [galaxy](galaxy/README.md): procedural spiral galaxy with a 3D star cloud, four arms, warm core, dust lanes, and interactive viewing controls.

[中文 — complete example catalog](README.md) · [Render gallery](../docs/images/examples/README.en.md) · [User guide](../docs/en/usr/README.md)

This is a curated English starting point. Chinese documentation remains authoritative. Examples demonstrate the source revision they accompany; a screenshot from `dev` does not guarantee that an older released SDK includes that capability.

## Run your first example

The SDK ships **basic**. From the extracted desktop SDK directory:

```sh
# macOS / Linux
bin/eve run share/eve/examples/basic
# Windows
bin\eve.exe run share\eve\examples\basic
```

Other examples live in this repository. Obtain the source archive or checkout corresponding to your SDK's release tag, then run from that repository root using the SDK executable's absolute path:

```sh
/absolute/path/to/sdk/bin/eve run examples/rpg
```

On Windows, use the equivalent absolute `eve.exe` path. These script examples do not inherently require compiling the engine, but an example using newer bindings needs a compatible engine. If you are following `dev`, use its corresponding source build and [build guide](../docs/en/dev/BUILDING.md). `make run/<platform>-debug GAME=examples/rpg` is a source-build command, not an SDK prerequisite.

## Learn the basics

- [Basic](basic/README.en.md): six bundled lessons: rectangle, image, movement, physics, particles and fire; then try the UI variants.
- [DevLab](devlab/README.en.md): console, pause, stepping, snapshots and hot reload. Run with `--debug`.

## Choose a showcase

- [Terrain editor](terrain-editor/README.en.md): edit a 3D heightfield inside the game and save/reload a validated document.
- [RPG dungeon survival](rpg/README.en.md): play a small combat loop and change data-driven skills and effects.
- [Roguelike generator](roguelike-generator/README.md): tune deterministic rooms, corridors, decoration and props.
- [Weather](weather/README.md): explore rain, snow, fog and storm presets.
- [Hex planet](hex-planet/README.md): explore a procedural planet example and its gameplay controls.

The last three guides already use English. The [gallery](../docs/images/examples/README.en.md) includes fourteen existing engine-captured frames; interactive editor behavior is better explored by running the example.

## Try AI-assisted development

Start with [AI and MCP setup](../docs/en/usr/AI-MCP.md), then choose:

- [AI Game](ai-game/README.en.md): structured gameplay actions, observations, deterministic stepping and checkpoints.
- [AI Editor](ai-editor/README.en.md): create a live editor, change its model, observe events and verify rendering.

- [Modern balcony](modern-balcony/README.md): SCAD architecture, procedural garden, two skinned animated cats, atmospheric sky and KHR ray tracing / portable reflections.

Both include Python drivers that exercise the protocol without an LLM. Graphical verification still needs a working desktop graphics environment. For startup problems and version mismatches, see the [FAQ](../docs/en/usr/FAQ.md).

- [SCAD warm modern living room](scad-living-room/README.md): original procedural interior, warm lights, PBR, shadows, volumetrics, AO/SSGI/SSR/TAA and automatic hardware RT reflections.
