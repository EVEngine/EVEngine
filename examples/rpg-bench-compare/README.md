# Mini RPG Workshop (`rpg-bench-compare`)

Playable slice for the Godot vs EVEngine agent bench: character, combat,
inventory, dialogue, map edit, and scene prop edit in one project.

## Run

```sh
# From repo root (Linux debug binary already built on Cloud VM):
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
export ALSOFT_DRIVERS=null
export XDG_RUNTIME_DIR=/tmp/xdg-runtime
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"

cd examples/rpg-bench-compare
xvfb-run -a ../../build/linux-debug/src/engine/eve run
```

Or:

```sh
make run/linux-debug GAME=examples/rpg-bench-compare
```

Headless smoke:

```sh
MIN_RUN_SECONDS=2 bash scripts/smoke_examples.sh rpg-bench-compare
```

## Modes / key bindings

| Key | Action |
|---|---|
| **F1** | Explore |
| **F2** | Battle (vs goblin) |
| **F3** | Map edit |
| **F4** | Scene edit (2D prop + optional `scene_director`) |
| WASD / arrows | Move (explore) or move prop (scene) |
| **E** | Talk to keeper / goblin hint |
| **Q** | Drink Herb Potion |
| **C** | Print HP/MP/ATK/DEF |
| **R** | Reset adventure (explore) / reset scene (scene) |
| **1 / 2 / 3** | Strike / Arc Bolt / Mend (battle); dialogue choices; scene spawns |
| **Space** | Advance dialogue |
| **Esc** | Flee battle / cancel dialogue |
| LMB / RMB / MMB | Floor / wall / erase (map edit) |
| **Z / Y** | Undo / redo paint |

## Checklist coverage

1. **Character** — `eve.RPG()` actor with HP/MP/ATK/DEF
2. **Combat** — turn fight vs goblin; Strike + Arc Bolt + Mend
3. **Inventory** — potion + iron blade; **Q** consumes potion
4. **Dialogue** — keeper offer with accept/later choice (`workshop-dialogue.dnut`)
5. **Map edit** — paint floor/wall; `Pathfinder` blocks gid=2 in explore
6. **Scene edit** — move 2D crate marker (blocks walk in play); optional 3D props

## Data

- `data/skills.json`, `effects.json`, `items.json`
- `data/workshop-dialogue.dnut`
- `data/workshop-map.json` + `world-object-contract.json`
