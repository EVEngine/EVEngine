# 统一网格（Grid）

**脚本入口：** 无（纯 C++ 库，被 `map` / `building` 等模块消费）

`grid` 是引擎共用的网格**拓扑与投影**层：定义格子尺寸/间距/原点/拓扑
（orthogonal / isometric / staggered / hexagonal）以及"格子 ↔ 世界坐标"的
换算，供 tilemap、建筑放置、寻路等模块复用，避免各模块各写一套坐标换算。

## C++ API 快查

### `GridConfig`（[GridConfig.h](../../../src/modules/grid/GridConfig.h)）

- `cellW / cellH`：格子宽高；`cellGapX / cellGapY`：格子间距。
- `originX / originY`：原点偏移。
- `topology`：`"orthogonal"` / `"isometric"` / `"staggered"` / `"hexagonal"`。
- `axes`：2D 平面（`"xy"` / `"xz"`）——3D 建筑放置用 `xz`，Y 为高度。

### `GridProjection`（[GridProjection.h](../../../src/modules/grid/GridProjection.h)）

- `cellToWorld(cfg, cx, cy, px, py)`：格子 → 世界坐标。
- `cellToDepthY(cfg, cx, cy)`：等距/交错地图的绘制深度（Y 排序）。
- `worldToCell(cfg, px, py, cx, cy, ...)`：世界坐标 → 格子（含容差）。

## 生命周期

- `GridConfig` 是值类型，随 `map.TileLayer` / `building.PlacementWorld` 持有；
  不建模块实例、不注册到 `eve` 表。
- 脚本侧通过 `map`（`TileLayer`）和 `building`（`PlacementWorld.setGridPlane`）
  间接使用本层；直接做网格运算的 C++ 模块按需 `#include "grid/GridProjection.h"`。

