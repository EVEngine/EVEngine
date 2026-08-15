# Roguelike Generator example

Interactive showcase of the **`level.roguelike`** procgen algorithm — a
seed-driven, configurable room-and-corridor dungeon generator in the spirit of
RoguelikeGenerator Pro. It demonstrates procedural creation of 2D / 2.5D game
worlds: every run is reproducible from a seed, and generation rules
(room count, corridor style, floor pattern, decor) are tweakable at runtime.

Run:

```sh
make run/win32-debug GAME=examples/roguelike-generator
# or
build/win32-debug/src/engine/eve.exe run   # from inside this directory
```

## What it generates

On top of the plain wall/floor grid, `level.roguelike` writes a per-cell
**detail** layer and emits **objects**, all readable from the returned `Grid2D`:

| cell | `getCell` | `getDetail` (variant / direction) |
|------|-----------|-----------------------------------|
| wall | `1`       | 8-bit autotile mask — which adjacent cells are open floor |
| floor | `2`      | floor-pattern variant `1..N` |
| corridor | `3`   | floor-pattern variant `1..N` |
| decor tile | `2`  | `>= 100` → scattered decor tile (rubble / grass / pebble) |

Objects (`getObjectType`) place the player **spawn**, **stairs**, and props
(**pillar** / **chest**). `getMeta` records `seed`, `rooms`, `floorPattern`,
`decorTiles`, `corridorStyle` so a level can be reproduced or saved.

The 8-bit wall mask is exactly the "tile direction" detail that powers
direction-aware autotiled walls; you can also run it on **any** existing grid
via `procgen.autotileGrid(grid)`.

## Generation rules (Params)

- `seed` — deterministic replay.
- `roomCount`, `roomMin`, `roomMax` — room placement budget / sizes.
- `corridorStyle` = `"l" | "straight" | "diagonal"`.
- `floorPattern` = `"brick" | "checker" | "plank" | "cobble" | "plain"`.
- `floorVariants` — number of floor variants (detail 1..N).
- `decorDensity` (0..1) — how many floor cells get scattered decor tiles.
- `decorSet` = `"none" | "pillars" | "treasure" | "nature" | "mixed"`.
- `autotile` (0/1) — compute wall direction masks into `detail`.
- `padding` / `spacing` / `corridorWidth` — wall border, room gaps, corridor width.

## Controls

| key | action |
|-----|--------|
| `R` / `Space` | regenerate with a fresh random seed (`procgen.randomSeed()`) |
| `S` | step to the next deterministic seed |
| `1`–`4` | room count presets (6 / 12 / 18 / 26) |
| `5`–`7` | corridor style: l / straight / diagonal |
| `P` | cycle floor pattern |
| `D` | cycle decor set |
| `V` | toggle 2D ↔ 2.5D (sprite-stacked wall extrusion) |
| `[` / `]` | wall extrusion height (2.5D) |
| `T` | toggle the CC0 tileset preview panel |

## Assets

`textures/dungeon_tiles.png` is a **CC0** "Dungeon tileset" by Buch
(OpenGameArt) — see [`textures/README.md`](textures/README.md).
