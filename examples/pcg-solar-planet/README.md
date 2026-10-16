# PCG Solar Planet Lab

SebLague [Solar-System Episode 02](https://github.com/SebLague/Solar-System/tree/Episode_02)
ideas, rebuilt on EVEngine's PCG / math / graphics stack: compare sphere topologies,
displace them with a CPU height field (continents, ridged mountains, domain warp,
crater SDFs), then shade with height/slope colours and procedural triplanar detail.
An optional ocean shell adds the specular water cue from the same episode.

```sh
make run/linux-debug  GAME=examples/pcg-solar-planet
make run/win32-debug  GAME=examples/pcg-solar-planet
```

Headless smoke (CI contract):

```sh
MIN_RUN_SECONDS=2 bash scripts/smoke_examples.sh pcg-solar-planet
```

## What it demonstrates

| Episode 02 topic | This example |
|---|---|
| UV / Fibonacci / Cube / Icosphere meshes | Script topologies in `planet_gen.nut` → `gfx.newMeshFromArrays` |
| Compute-shader vertex heights | CPU analogue: `math.fbm3` / `ridged3` / `noise3` warp per vertex |
| Crater SDF + SmoothMin/Max | Tutorial-style `smoothMin` / `smoothMaxK` + `math.smoothMax` for continents |
| Fractal / warped / ridged noise | Presets: Earth-like, Moon, Hyperion |
| Triplanar mapping | Fragment-stage procedural triplanar fBm (no UVs required) |
| Ocean + height/slope colouring | Ocean shell mesh + `solar_planet_*.frag` SPIR-V |

Height is authored on the CPU (same contract as `mesh.rock` / hex-sphere generators).
There is no UE-style GPU height compute graph on this path; the lab keeps the tutorial
math portable and deterministic under `seed`.

## Controls

| Input | Action |
|---|---|
| LMB drag | orbit |
| wheel | zoom |
| `Space` | toggle auto-spin |
| `R` / **New seed** | new deterministic seed |
| `1`–`4` | UV / Fibonacci / Cube / Icosphere |
| `Q` / `E` | previous / next height preset |
| `[` / `]` | resolution down / up |
| UI **Topology** / **Preset** / **Rebuild** | same actions |

## Presets

- **Earth-like** — continent fBm, ocean floor via `smoothMax`, ridged mountains under a mask, light domain warp, ocean shell.
- **Moon craters** — low-frequency shape noise + crater bowl/rim SDFs (no ocean).
- **Hyperion** — strong warp + ridged noise + sparse deep craters for an irregular body.
- **Bare mesh** — unit sphere only, for comparing topology distributions.

## Files

| File | Role |
|---|---|
| `planet_gen.nut` | topologies, height field, crater SDF |
| `main.nut` | orbit camera, UI, mesh upload, renderables |
| `shaders/solar_planet_terrain.frag(.spv)` | height/slope/triplanar colouring |
| `shaders/solar_planet_ocean.frag(.spv)` | specular ocean shell |

Recompile shaders after editing GLSL:

```sh
glslc -fshader-stage=frag -o shaders/solar_planet_terrain.frag.spv shaders/solar_planet_terrain.frag
glslc -fshader-stage=frag -o shaders/solar_planet_ocean.frag.spv   shaders/solar_planet_ocean.frag
```

## Related examples

- [`rock-generator`](../rock-generator) — `mesh.rock` icosphere deformation + stone textures
- [`hex-planet`](../hex-planet) — Goldberg hex sphere + playable civilisation
- [`terrain-gallery`](../terrain-gallery) — planar heightmap ridge/warp + hydrology
- [`terrain-smooth-mountain`](../terrain-smooth-mountain) — `smoothMax` / SmoothRaise blending
