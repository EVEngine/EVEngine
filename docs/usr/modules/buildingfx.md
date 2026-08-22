# 建筑放置可视化（BuildingFx）

**脚本入口：** `eve.BuildingFx()`

`building` 模块的可选渲染桥：把 `PlacementWorld` 里的已放置建筑和鬼影
（`Ghost`）同步为可见的 2D/3D 视觉，并绘制放置网格。纯逻辑的 `building`
模块不依赖渲染，需要画面时再接本模块。

## 基本用法

```squirrel
fx <- eve.BuildingFx();
fx.attach(world);                 // world = eve.Building().newWorld(...)
fx.setGridVisible(world, true);

function eve_update(dt) {
    fx.sync(world);                       // 已放置建筑 → 视觉
    fx.updateGhost(world, session.getGhost());  // 鬼影跟随鼠标/吸附
}

function eve_render() {
    gfx.clear();
    // 2D 网格：fx.drawGrid2D(world, gfx);  3D 网格：fx.drawGrid3D(world, gfx);
    gfx.render3D();
}
```

## 目标导向指南

### 3D 城镇沙盒（放置 + 拆除 + 鬼影预览）

完整示例见 [`examples/building-3d`](../../../examples/building-3d/main.nut)：
`attach(world)` → 每帧 `sync(world)` + `updateGhost(world, ghost)`，选择建筑时
`setGridVisible` 显示网格，放置/拆除后视觉自动跟随 `PlacementWorld` 的占用图。

### 2D 策略游戏网格预览

`drawGrid2D(world, gfx)` 在 2D 相机下绘制放置网格；建筑视觉用 `getVisualCount`
轮询当前已同步实例数，便于调试。

## API 快查

### `BuildingFx`（模块）

- `attach(world)` / `detach(world)` / `isAttached(world)` / `getAttachedCount()`。
- `sync(world)`：把占用图中的建筑同步为视觉（每帧调用）。
- `getVisualCount(world)`：当前视觉实例数。
- `updateGhost(world, ghost)`：同步鬼影位置/朝向/合法性（失败会半透明/变红）。
- `hideGhost(world)`：隐藏鬼影。
- `setGridVisible(world, visible)` / `getGridVisible(world)`。
- `drawGrid2D(world, gfx)` / `drawGrid3D(world, gfx)`。

## 生命周期

- `world` 参数是 `building` 模块创建的 `PlacementWorld`，本模块不持有其所有权；
  `world.destroy()` 前先 `detach`。
- `attach` 按 `world.getId()` 记录，多个世界可同时挂接。

