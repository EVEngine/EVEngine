# Castle Forge — Procedural Castle Generator

Interactive showcase for `procgen`'s `mesh.castle` recipe. It generates concentric
crenellated wall tiers, corner and interval towers, a gatehouse with a real opening,
wall walks, a multi-storey keep, courtyard buildings, and physical stair flights.

## Run

```sh
make run/macosx-debug GAME=examples/castle-generator
make run/win32-debug GAME=examples/castle-generator
make run/linux-debug GAME=examples/castle-generator
```

## Controls

| Key | Action |
|-----|--------|
| `R` | Advance the deterministic seed |
| `1` / `2` | Remove / add a concentric wall ring |
| `3` / `4` | Remove / add a keep floor |
| `D` | Cycle structural, crenellated, and full detail |

The recipe exposes dimensions, ring inset and height progression, wall and tower
proportions, tower tessellation and spacing, gate width, keep footprint and floors,
floor/stair dimensions, courtyard density, merlon size, detail level, UV repeat and
world scale. `buildMesh()` returns a Result projection; after checking `ok`, its
`value` metadata reports the generated structural counts
for graph tooling and budget checks.

The example deliberately uses the graph-style path rather than the one-call shortcut:
`buildMesh()` → `copyGroup()` → `uploadMesh()`; check the Result returned by the
generation/upload operations before reading `value`. `copyGroup()` itself returns
an owning mesh object. Named wall, tower, stair, keep and
gatehouse groups receive separate material tints and can independently receive
collision or LOD policies in a game.

## 2026-11-12 画面改善

补石基、连续水道、入口桥面、石材砌块/箭窗、屋顶和旗帜；区分墙体、塔楼、内堡与石板屋面色盘，改善取景及参数反馈。保留种子、圈数、楼层、细节调整。

[当前实帧](../../docs/images/examples/review-2026-11-12/nine-improved/castle-generator.png) · [前后与状态对照](../../docs/images/examples/review-2026-11-12/nine-improved/README.md)
