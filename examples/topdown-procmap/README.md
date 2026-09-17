# Top-Down Procedural Map

对照 B 站《俯视角程序化地图效果展示》截图做的 EVEngine 复刻演示。

视频里 Unity Hierarchy 的结构大致是：

| 视频 | 本示例 |
|---|---|
| `TerrainRoot` / `TerrainChunk` | `buildTerrainChunk` 网格分块 |
| `DetailsRoot` / `InstRoot`（Grass / Flower / Cliff / Stone / Tree） | 程序化 `mesh.tree` / `mesh.rock` / `mesh.bush` + `GrassField` 草地实例 |
| `Ocean`（青绿水深 + 浅滩） | 地形水体 shader，青绿 tint + 较高 `seaLevel` |
| 斜俯视 ARPG 镜头 | 约 60° 俯视 + WASD 平移 |

「好看」很大一部分来自手工预制件贴图与后期；这里用引擎已有的噪声地形、侵蚀、生物群系 splat、程序化树石和草地场，先把**结构与氛围**对齐。

## Run

```sh
make run/<platform>-debug GAME=examples/topdown-procmap
```

## Controls

| Input | Action |
|---|---|
| `W` `A` `S` `D` / arrows | Pan |
| `Q` `E` | Yaw |
| Mouse wheel | Zoom |
| `R` | New seed |
| `Space` | Toggle auto-pan |

## Pipeline

1. Continental heightmap with island/coast bias  
2. Thermal + hydraulic + fluvial erosion  
3. High sea-level climate analysis → beach / cove biomes  
4. Chunked splat terrain + turquoise river/lake meshes  
5. Slope-driven cliff rocks + biome tree/bush scatter  
6. `GrassField` ground cover with occasional flower tints  

After ~2 s writes `topdown-procmap.png` via `gfx.saveFramePng`.
