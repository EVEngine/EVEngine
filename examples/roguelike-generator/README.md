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

Objects (`getObjectType`) place the player **spawn**, **stairs**, and themed props.
Each prop exposes its semantic role, configurable asset id (`getObjectAsset`),
rotation, footprint and placement flags. `getMeta` records `seed`, `rooms`, `floorPattern`,
`decorTiles`, `corridorStyle` so a level can be reproduced or saved.

The 8-bit wall mask is exactly the "tile direction" detail that powers
direction-aware autotiled walls; you can also run it on **any** existing grid
via `procgen.autotileGrid(grid)`.

In the default 2D view, each walkable cell computes a four-direction terrain
mask (`E=1, S=2, W=4, N=8`). The mask selects one of all 16 combinations in
`textures/dungeon_tiles_ground.png`; edges, corners, corridors, T-junctions,
and fully connected floors therefore join correctly. A second `map.TileLayer`
draws props and markers from the original `dungeon_tiles.png` over the composed
ground. The default generation parameters produce a compact, centered dungeon
with smaller rooms and one-tile corridors, closer to the source artwork's
showcase. The 2.5D view keeps the procedural solid-color wall extrusion so its
height remains adjustable.

## Generation rules (Params)

- `seed` — deterministic replay.
- `roomCount`, `roomMin`, `roomMax` — room placement budget / sizes.
- `corridorStyle` = `"l" | "straight" | "diagonal"`.
- `floorPattern` = `"brick" | "checker" | "plank" | "cobble" | "plain"`.
- `floorVariants` — number of floor variants (detail 1..N).
- `decorDensity` (0..1) — how many floor cells get scattered decor tiles.
- `decorSet` = `"none" | "pillars" | "treasure" | "nature" | "mixed"`.
- `propDensity` (0..1) — sparse room-edge clutter budget.
- `assetPack` — informational pack id; the algorithm never branches on it.
- `assets.<role>` — comma-separated model/prefab ids for architecture and prop
  roles. See `assetpacks/kaykit_dungeon.nut` for the optional KayKit adapter.
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

The generator is asset-pack agnostic. `assetpacks/kaykit_dungeon.nut` maps the
complete Dungeon Pack families (modular walls, corners/junctions, doors/windows,
floors/foundations/grates, ceilings, stairs/rails, barriers, columns, furniture,
storage, treasure, lighting, banners, traps, food, tavern pieces and clutter) to
semantic pools. Copy that small file to adapt another pack or change any pool at
runtime; no engine rebuild is needed. The actual KayKit models are intentionally
not vendored by this example.

`textures/dungeon_tiles.png` is a **CC0** "Dungeon tileset" by Buch
(OpenGameArt) — see [`textures/README.md`](textures/README.md).
