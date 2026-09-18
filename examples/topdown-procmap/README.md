# Top-Down Procedural Map

对照 B 站《俯视角程序化地图效果展示》截图做的 EVEngine 复刻演示。

视频里 Unity Hierarchy 的结构大致是：

| 视频 | 本示例 |
|---|---|
| `TerrainRoot` / `TerrainChunk` | `buildTerrainChunk` 网格分块 |
| `DetailsRoot` / `InstRoot`（Grass / Flower / Cliff / Stone / Tree） | `mesh.tree` clusters + `mesh.bush` cards（`tex.tree_atlas` / `tex.foliage` 蓝噪声叶面板）+ 花朵点缀；不用 GrassField 广告牌 |
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
5. Slope-driven cliff rocks + dense biome tree/bush scatter  
   (trees: cluster leaf cards on `tex.tree_atlas`; bushes: `tex.foliage` cards)  
6. Flower accents on grassland / forest edges (no GrassField billboards)  

After ~2 s writes `topdown-procmap.png` via `gfx.saveFramePng`.
