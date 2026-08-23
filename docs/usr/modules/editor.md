# 编辑器构件模块

**脚本入口：** `eve.Editor()`

引擎**不附带**完整 3D 场景编辑器或 2D 地图编辑器，而是提供组装自定义编辑器所需的构件。新的工具协议不在核心中枚举“瓦片笔刷、地形隆起、摆放单位”等工具类型；任何实现 `IEditorTool` 的代码都能进入同一个会话。

推荐把编辑器拆成五类可替换组件：

| 协议 | 职责 |
|------|------|
| `IEditorTool` | 生命周期、输入与手势；原生或 Squirrel 工具一视同仁 |
| `IEditableTarget` + capability | 被编辑对象；工具只查询自己需要的能力 |
| `IBrushKernel` / `IFieldBrushOperation` | 笔刷形状、衰减和“画什么”分别组合 |
| `IEditCommand` / `IEditConstraint` | 通用撤销事务与项目美术/玩法限制 |
| `IEditorOverlay` / `IEditorInspector` | 与渲染器和 UI 框架无关的预览、属性呈现 |

因此红警 2 / 帝国时代 2 风格的格子地图、魔兽 3 风格的 3D 地图和连续高度场不需要三套会话。它们分别提供目标 capability、工具和视口坐标转换即可。

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

```squirrel
// 高度图地形网格（配合 procgen.generateHeightmap / procgen.newHeightmap）。
// Smooth 变体用高度场梯度生成平滑顶点法线，坑/坡连续着色（无平直三角片）。
local mesh = editor.newHeightmapMeshSmooth(hm, 0.5, 3.2);
terrainEnt.setMesh(mesh);
// 高度图编辑后原地更新（复用 GPU 缓冲，指针不变）
editor.updateHeightmapMeshSmooth(mesh, gfx, hm, 0.5, 3.2);
```

## 接口式工具会话（C++）

```cpp
EditorSession session;
TileBufferTarget target("ground", &tiles);
ConstantBrushFalloff hardEdge;
CircleBrushKernel circle(&hardEdge);
PaintIntFieldOperation paintGrass(17);
FieldBrushTool paint("paint-grass", "Paint Grass", &circle, &paintGrass);

session.bindTarget(&target);
session.addTool(&paint);              // 接受任意 IEditorTool
session.activateTool("paint-grass");

EditorPointerEvent down;
down.phase = EditorPointerEvent::Phase::Down;
down.x = tileX;
down.y = tileY;
session.dispatchPointer(down);        // 自动开启一次可撤销 stroke
```

将 `TileBufferTarget` 换成 `HeightmapTarget`、把操作换成 `AddScalarFieldOperation`，同一个 `FieldBrushTool` 就成为带衰减的地形升降笔刷。项目也可以实现新的 `IEditableTarget` capability（对象放置、道路、区域、体素等）以及对应操作；无需修改 `EditorSession`。

### 项目限制

实现 `IEditConstraint::evaluate()` 并注册到 `session.constraints()`。约束可以允许、给出警告或拒绝任意 `IEditCommand`，例如锁定水岸坡度、限定可用 tile、吸附建筑朝向、阻止穿过地图边界。所有命令都经 `EditorContext::execute()` 进入约束和事务，拒绝的命令不会污染撤销栈。

### 自定义呈现

视口实现 `IEditorOverlay`，Inspector 实现 `IEditorInspector`。工具只输出圆、线、矩形、文本和属性意图，所以可同时接入 2.5D 正交视口、3D 透视视口、ImGui 或游戏自己的 UI。

## Squirrel 自定义工具

脚本工具也实现相同的 `IEditorTool` 协议。回调返回位标志：`1` 表示已处理，`2` 表示捕获指针，`4` 表示释放指针。

```squirrel
local editor = eve.Editor();
local session = editor.newSession();
local road = editor.newScriptTool("road", "Road");

road.setActivateCallback(function() { previewRoad(); });
road.setPointerCallback(function(phase, pointerId, button, x, y, dx, dy,
                                 pressure, shift, control, alt) {
    if (phase == 0) { beginRoad(x, y); return 1 | 2; } // Down
    if (phase == 1) { updateRoad(x, y); return 1; }    // Move
    if (phase == 2) { finishRoad(); return 1 | 4; }    // Up
    cancelRoad(); return 1 | 4;
});

session.addTool(road);
session.activateTool("road");
// 视口负责把屏幕/射线坐标变换到工具坐标后转发。
session.dispatchPointer(0, 0, 0, mapX, mapY, 0.0, 0.0, 1.0);
```

`EditorSession` 和工具都是非拥有关系；脚本必须像上例一样持有 `road`，直到从会话移除。

## 地图笔刷

旧的 `Brush` / `EditorHistory` API 为兼容已有脚本保留。新编辑器优先使用上面的协议式会话；旧 API 适合很小的纯 tile 工具。

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

- 模块：`newSession` / `newScriptTool` / `newGizmo` / `newGizmoManager` / `newTileBuffer` / `newBrush` / `newToolbar` / `newInspector` / `newDock` / `newHistory` / `newHeightmapMesh` / `updateHeightmapMesh` / `newHeightmapMeshSmooth` / `updateHeightmapMeshSmooth`
- 会话：`addTool` / `removeTool` / `clearTools` / `activateTool` / `getActiveToolId` / `dispatchPointer` / `hasPointerCapture` / `update` / `cancelActiveTool` / `undo` / `redo`
- 脚本工具：`setShortcut` / `setActivateCallback` / `setDeactivateCallback` / `setPointerCallback` / `setKeyCallback` / `setUpdateCallback` / `setCancelCallback`
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
