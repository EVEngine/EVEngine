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
world scale. `buildMesh()` metadata reports the generated structural counts
for graph tooling and budget checks.

The example deliberately uses the graph-style path rather than the one-call shortcut:
`buildMesh()` → `copyGroup()` → `uploadMesh()`. Named wall, tower, stair, keep and
gatehouse groups receive separate material tints and can independently receive
collision or LOD policies in a game.
