# Water Splash VFX — procedural stylized splash (`mesh.water_splash`)

Native recipe `mesh.water_splash` builds a deterministic stylized water impact:
a tall central jet plus nested outward-flaring crown shells with jagged torn
lips and white foam vertex colors. This example is a live parameter lab with
an auto-playing staggered burst animation.

Run from the repository root:

```sh
make run/win32-debug GAME=examples/water-splash-vfx   # Windows Debug
make run GAME=examples/water-splash-vfx               # current host debug
```

Headless smoke:

```sh
MIN_RUN_SECONDS=2 bash scripts/smoke_examples.sh water-splash-vfx
```

## How it works

`procgen.generateMesh("mesh.water_splash", p, gfx)` (or `buildMesh` +
`uploadMesh`) returns nested open shells whose silhouette matches a common
toon-splash language: thin vertical spire, then shorter wider crowns that
flare at the rim. Jaggedness carves the top lip per radial column; foam amount
pushes vertex colors toward white near those peaks.

For staggered VFX playback the recipe accepts `onlyLayer` (`-1` = all, or
`0..layers-1`) so each crown can be its own `Renderable3D` with an independent
rise / scale / fade curve.

## Parameters

| Key | Default | Meaning |
|-----|---------|---------|
| `seed` | (from `newParams`) | deterministic jagged pattern |
| `height` | `2.4` | overall splash height |
| `width` | `3.2` | outer crown diameter |
| `layers` | `4` | number of nested shells (2–4) |
| `jaggedness` | `0.78` | torn-lip strength |
| `foam` | `0.85` | white foam tip amount |
| `flare` | `1.0` | outward crown flare scale |
| `onlyLayer` | `-1` | emit one layer for staggered VFX |
| `radialSegments` | `28` | radial resolution |
| `heightSegments` | `10` | vertical resolution |
| `bodyR/G/B` | cyan | shell body vertex color |
| `foamR/G/B` | white | foam tip vertex color |

## Controls

| Input | Action |
|-------|--------|
| Sliders | Rebuild splash geometry immediately |
| **Burst** / Space | Replay the staggered impact animation |
| **New seed** | Reroll jagged crown variation |
| **Reset** | Restore authored showcase values |

## Squirrel sketch

```squirrel
local paramsResult = procgen.newParams();
if (!paramsResult.ok) throw paramsResult.status.summary;
local p = paramsResult.value;
p.setSeed(20261103);
p.setFloat("height", 2.4);
p.setFloat("width", 3.2);
p.setFloat("jaggedness", 0.78);
local meshResult = procgen.generateMesh("mesh.water_splash", p, gfx);
if (!meshResult.ok) throw meshResult.status.summary;
local mesh = meshResult.value;
```
