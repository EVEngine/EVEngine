# Top-Down Procedural Map

俯视角程序化地图效果展示：噪声大陆 + 生物群系 splat + 河湖水面 + 树木/灌木/岩石点缀，
用略带倾斜的俯视相机自由平移浏览。对应常见的 Unity「俯视角程序化无限地图」演示风格
（本示例先生成一整块确定性大陆；流式九宫格可在此基础上接 `PcgTerrainStreaming`）。

## Run

```sh
make run/<platform>-debug GAME=examples/topdown-procmap
```

Linux headless smoke:

```sh
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
export ALSOFT_DRIVERS=null
export XDG_RUNTIME_DIR=/tmp/xdg-runtime
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"
MIN_RUN_SECONDS=2 RUN_SECONDS=8 bash scripts/smoke_examples.sh topdown-procmap
```

## Controls

| Input | Action |
|---|---|
| `W` `A` `S` `D` / arrows | Pan focus |
| `Q` `E` | Orbit yaw |
| Mouse wheel | Zoom (camera height) |
| `R` | New deterministic seed |
| `Space` | Toggle gentle auto-pan |

## Pipeline

1. `procgen.generateHeightmap` — continental fBm with ridge/warp
2. Thermal + hydraulic + scaled fluvial erosion
3. `analyzeTerrainScaled` — climate biomes + hydrology
4. Chunked `buildTerrainChunk` / splat material shader + river/lake meshes
5. Deterministic decoration scatter from `getBiomeName` (shared `mesh.tree` /
   `mesh.bush` / `mesh.rock` prototypes)

After ~2 s the example writes `topdown-procmap.png` next to `main.nut` via
`gfx.saveFramePng` (engine-owned readback, not the Xvfb framebuffer).
