# 编辑器构件模块

**脚本入口：** `eve.Editor()`

引擎**不附带**完整 3D 场景编辑器或 2D 地图编辑器，而是提供组装自定义编辑器所需的构件：3D 变换操作框、地图笔刷、以及 Toolbar / Inspector / Dock / History 等 UI 状态辅助。

设计参考了 Three.js `TransformControls`、Babylon.js `GizmoManager`、ImGuizmo、Unity `GridBrushBase` 与 Godot 编辑器插件的常见 API 形态；详见[编辑器模块设计](../../dev/编辑器模块设计.md)。

## 基本用法

```squirrel
local editor = eve.Editor();

// 3D 操作框
local gizmo = editor.newGizmo();
gizmo.setMode("translate");   // translate | rotate | scale | bound
gizmo.setSpace("world");      // local | world
gizmo.setPosition(0, 1, 0);
gizmo.setSize(1.0);

// 每帧：用相机射线拾取并拖拽
local axis = gizmo.pick(rayOx, rayOy, rayOz, rayDx, rayDy, rayDz);
if (axis != "" && mouseDown) {
    gizmo.beginDrag(axis, rayOx, rayOy, rayOz, rayDx, rayDy, rayDz);
}
if (gizmo.isDragging()) {
    gizmo.updateDrag(rayOx, rayOy, rayOz, rayDx, rayDy, rayDz);
    // 把 gizmo.getPosition* / getRotation* / getScale* 写回场景节点
}
if (mouseUp) gizmo.endDrag();

// 绘制：遍历 getPart* 用调试线 / 网格画出轴、环、平面
for (local i = 0; i < gizmo.getPartCount(); i++) {
    local kind = gizmo.getPartKind(i); // axis | plane | ring | box | center | handle
    // ...
}
```

## 地图笔刷

```squirrel
local buf = editor.newTileBuffer(64, 64);
local brush = editor.newBrush();
brush.setTool("paint");  // paint | erase | fill | line | rect | stamp
brush.setTile(3);
brush.setSize(3);
brush.setShape("circle");
brush.paintAt(buf, tx, ty);

// 同步到 map.TileLayer（示例）
for (local y = 0; y < buf.getHeight(); y++)
    for (local x = 0; x < buf.getWidth(); x++)
        layer.setTile(x, y, buf.getGid(x, y));
```

## 编辑器壳

```squirrel
local dock = editor.newDock();
dock.setRegionSize("left", 200);
dock.layout(config.width, config.height);
// 用 dock.getRegionX/Y/W/H("center"|"left"|...) 放置 ui 窗口

local toolbar = editor.newToolbar();
toolbar.addTool("move", "Move");
toolbar.addTool("paint", "Paint");
toolbar.setShortcut("move", "W");

local insp = editor.newInspector();
insp.addChoice("mode", "Mode", "translate,rotate,scale", "translate");
insp.addFloat3("pos", "Position", 0, 0, 0);
```

## 对象关系

| 类型 | 职责 |
|------|------|
| `TransformGizmo` | 单对象 TRS/包围盒手柄、射线交互、绘制部件 |
| `GizmoManager` | 多 mode 开关 + attach，转发 pick/drag（Babylon 风格） |
| `TileBuffer` | 与 `map` 解耦的 GID 网格 |
| `Brush` | 笔刷工具；产出 preview / change 列表 |
| `EditorHistory` | 撤销栈；瓦片分组可 `applyLastToBuffer` |
| Toolbar / Inspector / Dock | 仅状态与矩形，由 `ui` 渲染 |

## 目标导向指南

### 给场景节点加平移旋转缩放

创建 `GizmoManager`，`attach()` 后按需 `setPositionEnabled` / `setRotationEnabled` / `setScaleEnabled`；把相机 `screenToRay` 交给 `pick` / `updateDrag`，再写回 `scene` 节点 local TRS。

### 做简易 tile 地图编辑器

`TileBuffer` + `Brush` + `EditorHistory`：每次 `paintAt` 后把 `getChange*` 记入 `beginGroup`/`recordTile`/`endGroup`；Undo 时 `applyLastToBuffer`。用 `EditorDock` 划分调色板与视口，用 `EditorToolbar` 切换 paint/erase/fill。

## 常见问题

- 期望引擎弹出完整编辑器窗口——本模块只提供构件，需自行用 `ui` / `graphics` 组装。
- 笔刷直接改 `TileLayer`——请先画在 `TileBuffer`，再同步，避免模块硬耦合。
- `getPart*` 在 mode/TRS 变化后过期——调用 `rebuildParts()` 或任意 set*（多数 set 已自动重建）。

## API 快查

- 模块：`newGizmo` / `newGizmoManager` / `newTileBuffer` / `newBrush` / `newToolbar` / `newInspector` / `newDock` / `newHistory`
- Gizmo：`setMode` / `setSpace` / `setPosition` / `setRotationEuler` / `setScale` / `setBounds` / `setSnap*` / `pick` / `beginDrag` / `updateDrag` / `endDrag` / `getPart*`
- Manager：`set*Enabled` / `attach` / `detach` / `getGizmo` / `pick` / `beginDrag` / `updateDrag`
- Brush：`setTool` / `setSize` / `setShape` / `setTile` / `paintAt` / `eraseAt` / `floodFill` / `paintLine` / `paintRect` / `preview*` / `getChange*`
- History：`push` / `beginGroup` / `recordTile` / `endGroup` / `undo` / `redo` / `applyLastToBuffer`

## 使用要点

- 模块与辅助对象应长期持有；不要每帧 `newGizmo()`。
- 枚举一律 string，非法值抛异常。
- 绘制与输入由宿主完成；本模块不做 GPU 提交。

**源码：** [`src/modules/editor/`](../../../src/modules/editor/)  
**设计文档：** [`docs/dev/编辑器模块设计.md`](../../dev/编辑器模块设计.md)  
**相关测试：** [`test/editor.cpp`](../../../test/editor.cpp)
