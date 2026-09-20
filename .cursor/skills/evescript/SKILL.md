---
name: evescript
description: Write EveScript (.nut) for EVEngine. Use when editing game scripts, calling eve.* APIs, or driving the engine from an AI agent.
---

# EveScript / EVEngine for agents

Do not invent Unity C#, Godot GDScript, Lua, or JavaScript APIs. EVEngine games are `.nut` (EveScript = Squirrel + compile-time extensions). There is no second runtime language.

Lookup before writing (do not guess Unity/Godot names):

1. Live MCP: `eve_api_search` / `eve_api_get` on `bin/eve run --debug --mcp-port=7529 …`
2. **SDK zip** (no engine source): `share/eve/ai/eve-api.json` (same catalog as this `eve` binary)
3. Engine git tree only: `python3 scripts/generate_binding_contracts.py --json-output eve-api.json`

Cursor skill copy in an unzipped SDK: `.cursor/skills/evescript/SKILL.md`. Human tutorial: `docs/usr/EVESCRIPT.md` in the source tree / online docs. Longer engine-maintainer note: `docs/dev/AI知识补偿.md`.

## Language (differences from JS/C#)

- Lifecycle: `eve_init`, `eve_update(dt)`, `eve_render`, optional `eve_reload` / `eve_asset_reload`.
- Hot-reload-safe state: `persist name = init` (not C# fields, not `static`).
- Slot assign: `eve_update <- function(dt) { ... }` or `eve_update = function(dt) { ... }`.
- Modules: `config.nut` lists `modules` / `optionalModules` by **root slot** (`gfx`, not CMake `graphics`).
- Do not use `"Graphics" in eve` — `in` does not trigger lazy binding. Use `eve.moduleList` / `has_module("gfx")` / `ensure_module("audio")`.
- `switch` is a Squirrel keyword. UI factory: `ui["switch"]`.
- Import: `import { x } from "./scripts/foo.nut"` then `export function`.
- Types are gradual (`float`, `int`, `->`). They erase at runtime; they help the compiler/LSP.

Minimal game:

```squirrel
persist playerX: float = 100.0

eve_init <- function() {
    gfx.setBackgroundColor(0.08, 0.10, 0.16, 1.0)
}

eve_update <- function(dt: float) {
    playerX += 180.0 * dt
}

eve_render <- function() {
    gfx.clear()
    gfx.drawSolidRect(playerX, 220.0, 48.0, 48.0, 0.3, 0.75, 1.0, 1.0)
}
```

## Conventions

- Construct: `eve.Physics()`, `eve.Audio()`. `load.nut` already creates globals `gfx`, `window`, `keyboard`, `mouse`, `timer`, `physics`, …
- Truth for callable methods is Binding Contracts (`addFunc`), not every C++ public method.
- Programming errors throw; recoverable resource misses return `false` / Result. Check `result.ok` then use `result.value`, or `eve.result.ignore(result, "reason")`. Never discard a Result table.
- `update(dt)` is seconds. 2D physics is pixel space unless documented otherwise.
- Do not load disk resources inside `eve_render()`.

## Concept map (index only — APIs are not compatible)

| You know | In EVEngine |
| --- | --- |
| Unity GameObject | Entity + components (`extends eve.Component` / `eve.Entity`) |
| MonoBehaviour.Update | `eve_update(dt)` |
| Godot Node | Scene node (`eve.Scene` / MCP `eve_scene_*`) |
| Prefab / packed scene | Versioned definition / asset document, not a `.nut` class dump |
| Play Mode | MCP `eve_play` + `game.agent.json` |
| Inspector tweak | Editor commands / EditorHost JSON, not `eve_eval` |

## Gold examples

- SDK and tree: `share/eve/examples/basic/main.nut` or `examples/basic/main.nut` — persist, physics, particles, lifecycle
- Engine tree only: `examples/ecs/main.nut` (Component / Entity / System), `examples/primitive-drawing/main.nut` (Result tables)

## Runtime agents

- Observe/act through `eve_play` when `game.agent.json` exists. Do **not** teach `eve_eval` to change HP or skip puzzles.
- Screenshots: engine MCP (`eve_screenshot` / play capture), never the desktop/Xvfb framebuffer.
- Evidence: `eve_agent_session_*` — no verbal "done".
