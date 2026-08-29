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
// 项目组合一个可撤销的高度场工具；C++ 不认识“地形编辑器”窗口。
local target = editor.newHeightmapTarget("terrain", hm);
local falloff = editor.newSmoothBrushFalloff();
local kernel = editor.newCircleBrushKernel();
kernel.setSmoothFalloff(falloff);

local operation = editor.newAddScalarFieldOperation();
local sculpt = editor.newFieldBrushTool("terrain-sculpt", "Sculpt");
sculpt.setCircleKernel(kernel);
sculpt.setAddScalarOperation(operation);
sculpt.setRadius(3.0);
sculpt.setStrength(0.15); // 负值降低地形

local session = editor.newSession();
session.addFieldTool(sculpt);
session.bindHeightmapTarget(target);
session.activateTool("terrain-sculpt");

// 视口把鼠标射线换算为高度图坐标；一次 Down..Up 自动合并成一条事务。
session.dispatchPointer(0, 0, 0, cellX, cellY, 0.0, 0.0, 1.0);
session.dispatchPointer(2, 0, 0, cellX, cellY, 0.0, 0.0, 1.0);
session.undo();
session.redo();

// HeightmapTarget 直接包装同一个 hm；revision 变化时原地刷新 GPU 网格。
local mesh = editor.newHeightmapMeshSmooth(hm, 0.5, 3.2);
if (target.getRevision() != lastRevision)
    editor.updateHeightmapMeshSmooth(mesh, gfx, hm, 0.5, 3.2);
```

Target、kernel、falloff、operation 与 tool 之间是非拥有关系，项目需把它们保存在持久状态中。
`applyHeightmapBrush` 仍作为简单脚本的兼容入口，但定制编辑器应使用上面的组件和统一事务栈。

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

脚本可把同一个整数字段工具直接绑定到运行中的 `map.TileLayer`，修改会立即进入正常地图渲染、寻路与 FOV revision 流程：

```squirrel
local layer = eve.Map().newLayer(64, 64, 32, 32);
local target = editor.newTileLayerTarget("ground", layer);
local paint = editor.newPaintIntFieldOperation(17);
local hard = editor.newConstantBrushFalloff();
local circle = editor.newCircleBrushKernel();
circle.setConstantFalloff(hard);
local tool = editor.newFieldBrushTool("paint-grass", "Paint Grass");
tool.setCircleKernel(circle);
tool.setPaintIntOperation(paint);
session.addFieldTool(tool);
session.bindTileLayerTarget(target);
```

### 三维稀疏体积

Voxel 不会被伪装成二维 tile。`IIntVolumeTarget`、球/盒 Kernel、体积操作和
`dispatchPointer3D` 构成独立的三维 capability，但仍复用 `IEditorTool` 生命周期、
constraint 与 stroke transaction：

```squirrel
local world = eve.Voxel().newWorld();
local target = editor.newVoxelWorldTarget("terrain.voxels", world);
local hard = editor.newConstantBrushFalloff();
local sphere = editor.newSphereVolumeBrushKernel();
sphere.setConstantFalloff(hard);
local paint = editor.newPaintIntVolumeOperation(7); // 0 为擦除
local tool = editor.newVolumeBrushTool("voxel.paint", "Paint Voxels");
tool.setSphereKernel(sphere);
tool.setPaintIntOperation(paint);
tool.setRadius(2.5);
session.addVolumeTool(tool);
session.bindVoxelWorldTarget(target);
session.activateTool("voxel.paint");

// 视口 raycast 决定三维目标坐标，再转发 Down / Move / Up。
session.dispatchPointer3D(0, 0, 0, vx, vy, vz, 0, 0, 0, 1.0);
session.dispatchPointer3D(2, 0, 0, vx, vy, vz, 0, 0, 0, 1.0);
```

`VoxelWorldTarget.getRevision()` 读取 live world 的单调 revision，因此游戏逻辑、流式加载或
其他脚本产生的变化也能使预览与保存票据失效。`getDirtyMinX/Y/Z`、`getDirtyMaxX/Y/Z`
提供局部 remesh/overlay 范围；`clearDirtyVolume` 在消费者完成刷新后清除它。

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

### 注册并执行项目命令

项目可用 `registerScriptCommand(id, name, category, callback)` 把游戏专用操作注入统一命令
服务，退出插件或编辑器时用 `unregisterScriptCommand(id)` 清理。会话通过
`getCommandCount()` 与 `getCommandId/Name/Category(index)` 枚举命令；
`planCommand(id, payload)` 只生成并保留计划，随后用 `executePlan(planId, context)` 执行；
不需要预览时可直接 `executeCommand(id, payload)`。这些入口和 C++ 命令服务共享约束、
事务及 HostProfile 策略，因此游戏内建造玩法和开发编辑器可以复用同一条命令路径。

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

### 可组合 Workspace（推荐）

`EditorWorkspace` 是 UI 无关的组合模型，不提供固定编辑器窗口。项目注册任意面板描述，
再由 `ui`、游戏 HUD、MCP host 或其他 presenter 动态生成界面。Workspace 同时提供带通道的
Selection/Focus，因此开发编辑器和游戏内建造模式可以共享语义状态而使用不同布局。

```squirrel
local ws = editor.newWorkspace("level", "My Level Tools");
ws.setRegionSize("left", 240);
ws.registerPanel("outliner", "Outliner", "left", 10);
ws.registerPanel("viewport", "Scene", "center", 20);
ws.setPanelCapability("viewport", "scene.viewport.3d");
ws.layout(config.width, config.height);

// 枚举 descriptor 动态生成项目自己的 UI，而不是依赖固定 shell。
for (local i = 0; i < ws.getPanelCount(); ++i) {
    print(ws.getPanelId(i) + " -> " + ws.getPanelRegion(i) + "\n");
}

ws.select("world", "scene", "level-1", "tree-42", "vegetation.tree", false);
```

完整组合示例见 [`examples/composable-editor`](../../../examples/composable-editor)：项目脚本用
五个面板 builder 组合地形、材质、反射 MVVM、ECS 与游戏命令；C++ 不认识这些具体面板。

### 动作时间轴编辑器

`newActionTimelineEditor(targetId, timelineTable)` 把版本化的 `eve.action.timeline`
资产交给原生动作编辑器。原生对象是时间轴、命中测试、事务、撤销/重做和确定性预览游标的
唯一事实源；脚本只负责用项目自己的 UI 绘制 `getItem*` 布局并转发指针输入。

```squirrel
local created = editor.newActionTimelineEditor("ability.light-attack", timelineAsset);
if (!created.ok) throw created.status.summary;
local actionEditor = created.value; // ownership == "owned"

local ws = editor.newWorkspace("combat", "Combat Action Editor");
local composed = actionEditor.configureWorkspace(ws); // Assets/Preview/Inspector/Timeline
actionEditor.setViewport(900.0, 36.0, 145.0);

// 视口输入；一次 Down..Up 只生成一个撤销事务。
actionEditor.pointerDown(mouseX, mouseY, false);
actionEditor.pointerMove(mouseX);
actionEditor.pointerUp(mouseX);

actionEditor.play();
actionEditor.update(dt);
animationPlayer.setTime(actionEditor.getPreviewTime());
```

工厂与所有可能失败的编辑操作返回通用 Result 表：`ok`、`value`、`status.code`、
`status.summary` 和 `status.diagnostics`。返回的动作编辑器由 Squirrel VM release hook 拥有，
仅可在创建它的线程使用；`configureWorkspace` 不保留传入的 Workspace 指针，`snapshot`
返回与编辑器生命周期解耦的规范化资产值。完整可运行示例见
[`examples/combat-action-editor`](../../../examples/combat-action-editor)。

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
| `ActionTimelineEditor` | 版本化动作时间轴、事务、撤销/重做和确定性预览 |

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

- 模块：`newWorkspace` / `newActionTimelineEditor` / `newSession` / `newScriptTool` / `newFieldBrushTool` / `newVolumeBrushTool` / `newConstantBrushFalloff` / `newLinearBrushFalloff` / `newSmoothBrushFalloff` / `newCircleBrushKernel` / `newBoxBrushKernel` / `newSphereVolumeBrushKernel` / `newBoxVolumeBrushKernel` / `newPaintIntFieldOperation` / `newAddScalarFieldOperation` / `newPaintIntVolumeOperation` / `newTileBufferTarget` / `newTileLayerTarget` / `newHeightmapTarget` / `newVoxelWorldTarget` / `registerScriptCommand` / `unregisterScriptCommand` / `newGizmo` / `newGizmoManager` / `newTileBuffer` / `newBrush` / `newToolbar` / `newInspector` / `newDock` / `newHistory` / `applyHeightmapBrush` / `newHeightmapMesh` / `updateHeightmapMesh` / `newHeightmapMeshSmooth` / `updateHeightmapMeshSmooth`
- Workspace：`getId` / `getTitle` / `setTitle` / `registerPanel` / `removePanel` / `clearPanels` / `movePanel` / `setPanelCapability` / `setPanelContext` / `setPanelVisible` / `setPanelSingleton` / `activatePanel` / `getActivePanel` / `getPanelCount` / `getPanelId` / `getPanelTitle` / `getPanelRegion` / `getPanelCapability` / `getPanelContext` / `getPanelOrder` / `getPanelVisible` / `getPanelSingleton` / `setRegionSize` / `layout` / `getRegionX` / `getRegionY` / `getRegionW` / `getRegionH` / `setMode` / `getMode` / `select` / `clearSelection` / `getSelectionCount` / `getSelectionItem` / `getSelectionType` / `getPrimarySelection` / `getSelectionSequence` / `focus` / `getFocusedSurface` / `getRevision`
- 动作时间轴：`configureWorkspace` / `setViewport` / `pointerDown` / `pointerMove` / `pointerUp` / `seekX` / `seekSeconds` / `resizeState` / `undo` / `redo` / `update` / `snapshot` / `play` / `pause` / `isPlaying` / `canUndo` / `canRedo` / `isDragging` / `getDuration` / `getPreviewTime` / `getRevision` / `getAnimationUri` / `getTrackCount` / `getTrackId` / `getTrackLabel` / `getTrackKind` / `getTrackMuted` / `getLayoutWidth` / `getLayoutHeight` / `getPlayheadX` / `getItemCount` / `getItemId` / `getItemType` / `getItemState` / `getItemSelected` / `getItemMinX` / `getItemMaxX` / `getItemMinY` / `getItemMaxY` / `getStateStart` / `getStateEnd` / `getEventCount` / `getEventItemId` / `getEventType` / `getEventTime` / `getEventKind`
- 会话：`addTool` / `addFieldTool` / `addVolumeTool` / `removeTool` / `clearTools` / `bindTileBufferTarget` / `bindTileLayerTarget` / `bindHeightmapTarget` / `bindVoxelWorldTarget` / `clearTarget` / `activateTool` / `getActiveToolId` / `dispatchPointer` / `dispatchPointer3D` / `hasPointerCapture` / `update` / `cancelActiveTool` / `undo` / `redo` / `canUndo` / `canRedo` / `clearHistory` / `getCommandCount` / `getCommandId` / `getCommandName` / `getCommandCategory` / `planCommand` / `executePlan` / `executeCommand`
- 脚本工具：`setShortcut` / `setActivateCallback` / `setDeactivateCallback` / `setPointerCallback` / `setKeyCallback` / `setUpdateCallback` / `setCancelCallback`
- 字段工具：`setRadius` / `setStrength` / `getRadius` / `getStrength` / `setCircleKernel` / `setBoxKernel` / `setPaintIntOperation` / `setAddScalarOperation`
- Kernel：`setConstantFalloff` / `setLinearFalloff` / `setSmoothFalloff`
- 整数字段操作：`setValue` / `getValue`
- 字段 Target：`getTargetId` / `getRevision` / `getWidth` / `getHeight` / `readInt` / `writeInt` / `readScalar` / `writeScalar` / `sampleScalar` / `clearDirtyRegion`
- 体积工具/Target：`setSphereKernel` / `setBoxKernel` / `setPaintIntOperation` / `readInt3` / `writeInt3` / `getDirtyMinX` / `getDirtyMinY` / `getDirtyMinZ` / `getDirtyMaxX` / `getDirtyMaxY` / `getDirtyMaxZ` / `clearDirtyVolume`
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
