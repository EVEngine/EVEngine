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
- `updateAreaPreview(world, session)`：复制会话当前区域草稿的格坐标与接受状态，供
  2D/3D heatmap 呈现；`clearAreaPreview(world)` 清除草稿，
  `getAreaPreviewCount/getAreaPreviewAccepted` 用于查询当前投影。
- corner 视觉锚定网格顶点，并使用 `width/depth/height`（3D）或 `width/height`（2D）；
  `size` 是宽深缺省值，最终回退为较短格边的 20%。
- `hideGhost(world)`：隐藏鬼影。
- `setGridVisible(world, visible)` / `getGridVisible(world)`。
- `drawGrid2D(world, gfx)` / `drawGrid3D(world, gfx)`。
- 多层显示：`setLevelVisibilityMode(world, mode, level)` / `getLevelVisibilityMode(world)`；
  `isVisualVisible(world, instanceId)` 查询过滤后的可见性。
- 视觉诊断：`getVisualResource`、`getVisualVariant`、`getVisualFallbackReason`，以及组级
  `getCurveGroupCount` / `getContinuousCurveVisualCount`。

### 连续曲线墙体网格（C++）

`BuildingFx::buildEdgeCurveMesh` 接收四个逻辑网格控制点、2–4096 个解析细分、墙厚、墙高和
基础高度，返回完全拥有的 `CurveMeshData`：位置、分面法线、弧长归一化 UV、索引、样本数和
曲线长度。每个采样环为左/右/顶/底四个独立面保留顶点，并生成首尾端盖，因此可直接交给
`Graphics::newMeshFromArrays` 上传。XY 和 XZ 平面均受支持；rectangle 和 affine isometric
布局使用网格基向量投影。hex/staggered 的投影不是全局仿射，当前会返回明确的 Unsupported，
不会生成形状错误的网格。

`buildEdgeCurveGroupMeshForInstance` 可从任一成员 id 查询 `PlacementWorld` 的权威曲线组，
并用其控制点和细分数生成相同的拥有型 CPU 网格；成员不属于有效组时返回结构化诊断。已提交
`sync()` 会为每个有效 3D 曲线组生成一个连续 Renderable，并按组缓存 CPU 数据和 Graphics
拥有的 Mesh。控制点、细分、尺寸或楼层高度变化时优先调用 `updateMeshVertices` 原位更新；
后端不能更新时才创建替代 Mesh。连续视觉可用时离散成员会隐藏，组解散时连续 Renderable
立即销毁而离散边重新显示；同一组 ID 经撤销/重做恢复时可复用缓存 Mesh。无 Graphics、2D
定义、生成失败或不支持的布局不会吞掉墙体，而是保留离散视觉，并通过
`getCurveVisualFallbackReason` 暴露稳定回退原因。组级编辑持久化由 `building_editing`
schema v8 提供。Graphics 仍是上传后 Mesh 的唯一所有者，BuildingFX 不直接 delete 它。

交互式拖动期间可调用 `updateEdgeCurvePreview`：它不会修改 `PlacementWorld`，只创建或原位
更新一份半透明、无阴影的连续曲线预览。`clearEdgeCurvePreview` 在手势取消或提交后清除预览；
`hasEdgeCurvePreview` 与 `getEdgeCurvePreviewFallbackReason` 让 UI 区分 CPU 草稿有效、GPU 已呈现
和 `graphics_unavailable` 等显式回退。预览 Mesh 同样由 Graphics 拥有并跨连续拖动复用。

命名表面的运行时预览使用
`updateEdgeCurveSurfacePreview(world, session, subdivisions, surfaceName)`。它直接读取会话拥有的
四个控制点，调用与最终 `executeEdgeCurveOnSurface` 相同的表面采样契约，并返回明确的
`CurvePreviewUpdateStatus`。成功后可通过 `getEdgeCurvePreviewSurfaceId` 查询当前草稿对应的
表面身份；C++ 还可查询 64 位 revision。provider 缺失、跨 surface identity/revision 或网格
生成失败时不会覆盖上一份有效草稿，因此鼠标拖动期间不会因一次瞬时无效命中而闪回平面。

表面曲线组存在提交后的世界空间样本帧时，BuildingFX 使用 `buildSurfaceCurveMesh`：中心线
沿每个表面命中点移动，墙高沿局部法线挤出，墙宽使用“法线 × 曲线切线”的局部横向轴，
弧长 UV 采用真实三维中心线距离。无表面样本的旧曲线继续走平面投影路径。表面帧属于曲线组
快照，provider 后续卸载不会让已提交网格失去确定性。

## 生命周期

- `world` 参数是 `building` 模块创建的 `PlacementWorld`，本模块不持有其所有权；
  `world.destroy()` 前先 `detach`。
- `attach` 按 `world.getId()` 记录，多个世界可同时挂接。
- `CurveMeshData` 由调用方持有；生成期间只同步借用 `world`，不会保留引用，也不会修改图形状态。

