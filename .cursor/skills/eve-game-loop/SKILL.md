---
name: eve-game-loop
description: EVEngine playable game structure. Use when scaffolding a new .nut game, separating authority from presentation, wiring persist/save/HUD, or following the game-tutorial pattern.
---

# Playable game loop (EVEngine)

Human tutorial: `docs/usr/GAME_DEVELOPMENT.md`. Gold project:
`examples/game-tutorial/` (config, main, `scripts/rules.nut`, `game.agent.json`).

## Entry rules

1. Cold-start into a frame loop before adding content. Missing required services
   must fail loudly; optional audio may degrade silently.
2. One authoritative play state. Presentation (textures, HUD text, derived target
   positions) projects from that state — no second mutable score/HP/position.
3. Look up callable APIs with `eve_api_search` / SDK `share/eve/ai/eve-api.json`.
   Do not invent Unity/Godot APIs.
4. Verify with real frames + `game.agent.json` / `eve_play`, not compile-only.

## Structure

```text
game/
  config.nut          # window, modules by root slot (gfx, keyboard, ui, …)
  main.nut            # orchestration: init / update / render / reload
  scripts/rules.nut   # pure rules + save candidate checks (export functions)
  game.agent.json     # Play observe/act contract
  README.md
```

`config.nut` module strings are **script slots**, not CMake names. Import cannot
add missing native modules to the current SDK.

## Authority vs presentation

```squirrel
import { new_state, update_state } from "./scripts/rules.nut"
persist tutorial_state = new_state()
```

- `persist` survives hot-reload in-process; process restart needs explicit save/load.
- Rules take `dt` and bounds; do not read wall clock or anonymous globals for sim.
- Edge keys (`key_just_pressed`) queried once per frame by one input layer.
- Physics-owned bodies: do not also write renderable positions every frame.

## Lifecycle

| Slot | Owns |
| --- | --- |
| `eve_init` | resources, HUD tree, first state |
| `eve_update(dt)` | input → rules → systems (seconds) |
| `eve_render` | clear + draw + `ui.beginFrameAndRender()` only |
| `eve_reload` / `eve_asset_reload` | rebuild projections; keep persist |

Never load disk textures/shaders inside `eve_render()`.

## Save

- Schema id + version; reject unknown versions / extra fields.
- Atomic write where the platform supports it; on failure show status, do not
  pretend success.
- Checkpoints are not mid-fight replay unless the game defines that contract.

## Do not

- Hide init failures in empty `catch` and keep a half world.
- Rebuild the whole HUD every frame when `setText` / host updates suffice.
- Use `"Graphics" in eve` for module presence — use `has_module` / `eve.moduleList`.

## Gold

- `examples/game-tutorial/` + `docs/usr/GAME_DEVELOPMENT.md`
- Language skill: `.cursor/skills/evescript/SKILL.md`
