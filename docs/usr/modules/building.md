# 建筑放置 — `building`

面向策略 / RTS / 经营模拟（ETS）的建筑放置框架：数据驱动定义、格子占用世界、鬼影预览、
可插拔校验与吸附规则。设计见 [建筑放置系统设计](../../dev/建筑放置系统设计.md)。

可运行示例：[`examples/building`](../../../examples/building/)（`make building`）。

## 离散楼层

`PlacementWorld` 把 `level` 作为 cell/edge 权威地址的一部分。使用
`setActiveLevel(level)` 选择当前编辑层，使用 `setFloorHeight(height)` 设置楼层间沿网格
平面法向的距离。原有放置与占用 API 操作活动层；`getOccupantAtLevel`、
`getAnyOccupantAtLevel`、`getEdgeOccupantAtLevel` 可在不切换活动层时显式查询。不同层可占用
同一格或同一边，同层仍会冲突，edge 拓扑也只在本层连接。BuildingFX 使用
`level * floorHeight + elevation` 投影最终高度。

BuildingFX 可用 `setLevelVisibilityMode(world, mode)` 控制楼层显示：`all` 显示全部，
`active` 仅显示活动层，`active_and_below` 显示活动层及以下。切换
`world.setActiveLevel(...)` 后调用 `sync(world)` 即会原地刷新现有视觉的可见性。

结构定义可设置 `structuralRole`（例如 `floor`、`wall`、`ceiling`、`roof`、`stair`）。
当 `supportMode` 为 `cell_below` 时，定义的每个占地格都必须在下一层找到支撑实例；可用
`supportTag` 进一步限定支撑类型。缺少支撑时预览和放置返回稳定原因 `support_missing`。
普通拆除或移动仍被上层实例依赖的支撑体会被拒绝，原因为 `support_in_use`；脚本可用
`getBuildingSupportCount/getBuildingSupportAt/getBuildingDependentCount` 查询关系，或调用
`removeBuildingCascade` 按依赖优先顺序拆除整个结构子树。C++ 应使用返回 owning receipt 的
`removeBuildingCascadeResult`。定义热重载或外部恢复后，应调用
`rebuildStructuralLinksResult` 原子验证并重建派生链接。

## 入口

```squirrel
building <- eve.Building();
building.registerBuildingsFromJson(jsonText);
local world = building.newWorld(64, 64, 32.0); // 宽、高、格子像素/世界单位
local ghost = building.newGhost();
```

## 最小示例

```squirrel
building <- eve.Building();
building.registerBuildingsFromJson(@"
[
  {\"id\":\"house\",\"displayName\":\"House\",\"footprintW\":2,\"footprintH\":2,
   \"tags\":[\"house\"],\"cost\":{\"wood\":20}},
  {\"id\":\"road\",\"footprintW\":1,\"footprintH\":1,\"tags\":[\"road\"]}
]
");

world <- building.newWorld(32, 32, 32.0);
world.setId("town");
world.fillTerrain(1);

ghost <- building.newGhost();
ghost.setBuildingId("house");
ghost.setFromWorld(world, mouseX, mouseY); // 按定义 snapMode 吸附
if (ghost.validate(world)) {
    local id = world.placeGhost(ghost);
    print("placed " + id + "\n");
} else {
    print("cannot place: " + ghost.getReason() + "\n");
}
```

## 任务导向示例

### 1. 沿道路摆摊（邻接标签）

定义里设 `"requireAdjacentTag":"road"`，先铺 `road`，再放摊位；失败原因为 `adjacency_tag`。

### 2. 码头只能建在水域

地形语义由 `world.setTerrain(x,y,semantic)` 写入；定义 `"requireTerrain":[2]`。
陆地格会得到 `terrain_mismatch`。

### 3. 自定义“金币不足”校验（C++）

```cpp
PlacementSystem::registerValidateRule("needGold",
    [](const PlacementWorld&, const PlacementQuery&, std::string* reason) {
        if (playerGold < 10) { if (reason) *reason = "not_enough_gold"; return false; }
        return true;
    });
```

建筑定义 `"validateRule":"needGold"`；脚本侧只需选用该规则名。

## API 快查

### `Building`（模块）

| 方法 | 说明 |
|------|------|
| `registerBuildingsFromJson(json)` | 批量注册定义，返回成功数 |
| `clearBuildingDefinitions()` | 清空定义表 |
| `getBuildingDefinitionCount()` / `hasBuildingDefinition(id)` | 查询 |
| `getBuildingDisplayName` / `Category` / `FootprintW` / `FootprintH` | 定义字段 |
| `getBuildingSnapMode` / `RotationMode` / `ValidateRule` | 策略名 |
| `buildingHasTag` / `getBuildingExtra` / `getBuildingCost` | 标签与扩展 |
| `newWorld(w,h,cellSize)` / `newGhost()` | 工厂 |
| `hasValidateRule` / `hasSnapRule` | 内置+已注册规则 |
| `clearChangeEvents` / `getChangeEvent*` | 变更事件 poll |

### `PlacementWorld`

| 方法 | 说明 |
|------|------|
| `setOrigin` / `setCellSize` / `worldToCell*` / `cellToWorld*` | 坐标 |
| `fillTerrain` / `setTerrain` / `getTerrain` | 地形语义 |
| `canPlace` / `canPlaceReason` / `placeAt` / `placeAtWorld` / `placeGhost` | 放置 |
| `removeBuilding` / `moveBuilding` / `clearBuildings` | 拆除与移动 |
| `getOccupant` / `isCellEmpty` / `getBuilding*` | 查询 |

### `Ghost`

| 方法 | 说明 |
|------|------|
| `setBuildingId` / `setCell` / `setFromWorld` / `setRotationDeg` / `rotateBy` | 姿态 |
| `validate(world)` / `isValid` / `getReason` | 校验结果 |

## 生命周期注意

- 将 `PlacementWorld` / `Ghost` 保存在全局或实体组件中；不要每帧 `newWorld`。
- 变更事件不会自动清空；批量操作后调用 `clearChangeEvents` 或按帧 poll。
- 自定义 validate/snap/hook 只能在 C++ 注册；脚本通过字符串策略名选用。
- 模块**不**扣资源、不画鬼影；`cost` 仅作数据提示，由游戏在 hook 或脚本中处理。

## 多样式网格 / 3D / Tilemap（2026-08-18 起）

坐标换算统一走 `grid` 模块（纯数学），`PlacementWorld` 缺省为正交 2D，旧脚本零改动。

### 网格布局

```squirrel
world.setGridLayout("isometric");          // rectangle | hexagon | isometric | staggered | isometric-z-as-y
world.setGridPlane("xz");                  // xy（默认）| xz（3D：第二轴 -> 世界 Z）
world.setCellGap(2.0, 2.0);
world.setStagger("y", "odd");
world.setHexSideLength(14.0);
```

### 绑定 Tilemap（复用投影 + GID 地形）

```squirrel
world.bindTileLayer(layer);                // 同时复用 layer 的 orientation/stagger/hex
world.setTerrainGid(2, 2);                 // GID 2 -> 地形语义 2（水域）
world.setTerrainGidMapJson("{\"1\":1,\"2\":2}");  // 或整表
```

绑定后 `getTerrain` 从 GID 懒解析；`setTerrain` 手动覆盖优先。

### 3D 放置（XZ 平面 + 表面接口）

```squirrel
world.setGridPlane("xz");
ghost.setFromSurface(world, "plane", hitX, hitZ);   // 内置平面表面；高度 setPlaneSurfaceHeight
ghost.setFromWorld3D(world, worldX, worldY, worldZ); // 直接喂真实世界坐标
world.placeAtWorld3D("house", x, y, z, rot);
```

物理射线由游戏在 C++ 侧实现 Provider；引擎内置 `plane`、不可变规则高度场
`HeightfieldSurface`，以及不可变静态三角网格 `StaticMeshSurface`。

新实现应优先使用 `PlacementSystem::registerSurfaceProvider`。Provider 返回受检查的
`Result<PlacementHit>`；命中除世界坐标外还包含归一化法线、正交切线框架、稳定
`surfaceId`、`surfaceRevision`、primitive id 与表面标签。`registerSurface` 保留为
兼容适配层，只能用 `bool` 表达“命中/未命中”。非法坐标或零法线会被结构化拒绝。

C++ 高度场使用行主序样本，支持独立原点、XY 间距、高度缩放/偏移及稳定版本：

```cpp
HeightfieldSurface::Config config;
config.width = width;
config.height = height;
config.spacingX = cellWidth;
config.spacingY = cellDepth;
config.surfaceId = "terrain.chunk.0";
config.surfaceRevision = terrainRevision;
auto surface = HeightfieldSurface::create(config, std::move(rowMajorHeights));
if (surface.ok()) {
    auto registered = PlacementSystem::registerHeightfieldSurface(
        "terrain", std::move(surface).takeValue());
    // registered 必须检查；unregisterSurface("terrain") 释放 registry 所持引用。
}
```

采样范围包含最外圈顶点，范围外返回 `NotFound`。内部使用双线性高度；先以中心差分
（边界为单边差分）得到顶点梯度，再双线性插值梯度，因此共享片边界上的法线和切线
连续。XZ 网格把高度映射到 world Y，XY 网格映射到 world Z。对象构造完成后不可变，可
并发采样；注册、替换和注销仍限定调用线程且不得与
采样并发。修改地形时应构造新对象、递增 `surfaceRevision` 后原子替换注册项。

静态网格使用拥有所有权的世界空间 XYZ 顶点、三角形索引和可选逐顶点法线，在创建时
构建确定性的 median-split BVH：

```cpp
StaticMeshSurface::Config config;
config.surfaceId = "terrain.mesh.0";
config.surfaceRevision = meshRevision;
config.hitSelection = StaticMeshSurface::HitSelection::Highest;
auto surface = StaticMeshSurface::create(
    config, std::move(packedWorldPositions), std::move(triangleIndices),
    std::move(optionalPackedWorldNormals));
if (surface.ok()) {
    auto registered = PlacementSystem::registerStaticMeshSurface(
        "terrain-mesh", std::move(surface).takeValue());
    // registered 必须检查。
}
```

查询仍采用网格平面坐标而非相机射线：XZ 网格沿世界 Y 投影，XY 网格沿世界 Z 投影；
与投影方向平行、在平面上退化的三角面不会命中。重叠层可确定性选择 `Highest`、
`Lowest` 或离 `referenceHeight` 最近的面，相同高度/距离以源三角形序号决胜；该序号写入
`primitiveId`。法线优先重心插值逐顶点法线，否则使用几何法线，并可统一朝向网格 up。
对象构造后不可变且可并发采样，注册表持有快照至替换或注销。动态物理体、相机射线和
任意方向墙面拾取仍应使用自定义 `registerSurfaceProvider`。

通过表面放置后，Ghost 与 `PlacedBuilding` 会保留表面身份、revision 和局部法线。
脚本可用 `ghost.getSurfaceId/getSurfaceRevision/getSurfaceNormal*`，以及
`world.getBuildingSurfaceId/getBuildingSurfaceRevision/getBuildingSurfaceNormal*` 查询。
这些值是放置时快照；provider revision 变化后的已放置实例自动重投影尚未提供，当前
不会静默移动已经放置的建筑。

多格建筑通过 `sampleSurfacePatch` 覆盖旋转后的实际占地掩码。定义可配置：

```json
{
  "id": "hill-house",
  "footprintW": 2,
  "footprintH": 2,
  "maxSurfaceSlopeDegrees": 25.0,
  "maxSurfaceHeightDelta": 0.4
}
```

默认规则会以 `surface_slope` 或 `surface_height_delta` 拒绝超过限制的 Ghost；跨
`surfaceId` 或 revision 的 footprint 会在采样阶段返回 Conflict。旋转带表面附着的
Ghost 后，下一次 `validate` 会自动按新 footprint 重采样。3D `buildingfx` 使用表面
tangent/normal/bitangent 对齐建筑、Ghost 与占地光标，并保留 `free` snap 的精确锚点。

### 通道（同格叠放）

定义 `"channel":"floor"` / `"channel":"furniture"`，同一格允许跨通道建筑并存；
`getOccupantInChannel(channel, x, y)` / `isCellEmptyInChannel` 按通道查询。

### 边对象（墙、围栏、门）

边对象使用独立于格子 footprint 的占用域，因此墙不会错误占用相邻房间的地板格：

```json
{
  "id": "stone-wall",
  "placementKind": "edge",
  "connectionGroup": "stone-wall-network",
  "channel": "walls",
  "renderMode": "3d",
  "visual3d": {"height":"3.0", "thickness":"0.25"}
}
```

```squirrel
session.startPlacement(world, "stone-wall");
session.updateEdge(world, cellX, cellY, "north"); // north | east | south | west
if (session.isValid()) session.execute();
```

`north/south` 和 `east/west` 会规范化为唯一物理边，例如 `(x,y,north)` 与
`(x,y-1,south)` 指向同一占用。`world.getEdgeConnectionMask(instanceId)` 返回同一
`connectionGroup` 的六位邻接信息：沿边起点/终点延伸，以及两个端点各自的两条垂直
分支。BuildingFX 使用边中点、长度和 `thickness` 生成视觉。

连续墙线通过会话原子提交：

```squirrel
local status = session.executeEdgeLine(startVertexX, startVertexY, endVertexX, endVertexY);
for (local i = 0; i < session.getLastPlacedCount(); ++i)
    print(session.getLastPlacedId(i) + "\n");
```

端点必须形成非空水平线或垂直线。系统会先验证全部单位边；任意一段越界或被占用时，
不会创建实例、写占用或发送部分事件。成功后每段仍拥有独立 instance id。

带转角的正交路径也可以作为一次手势提交：

```squirrel
session.beginEdgePath(startVertexX, startVertexY);
session.appendEdgePathVertex(turnVertexX, turnVertexY);
session.appendEdgePathVertex(endVertexX, endVertexY);
local status = session.executeEdgePath();
```

每个相邻顶点必须形成非空水平或垂直段，同一路径不能重复经过同一物理边。系统在写入前
预检整条路径，失败时不会留下半条墙；成功 id 按路径方向排序，BuildingFX 会根据现有边
拓扑自动选择转角视觉。`building_editing::BuildingTarget::makeEdgePath` 将同一路径编码为一个
可撤销/重做的原子批次。

三次 Bezier 曲线使用四个逻辑网格控制点和固定细分数：

```squirrel
session.beginEdgeCurve(1.0, 1.0);
session.appendEdgeCurveControlPoint(1.0, 6.0);
session.appendEdgeCurveControlPoint(7.0, 6.0);
session.appendEdgeCurveControlPoint(7.0, 1.0);
local status = session.executeEdgeCurve(24);
```

细分数范围为 2–4096。采样点以固定公式舍入到逻辑顶点，再以确定性 Manhattan 步进连接，
同样走整条路径的重复边、边界和占用预检。相同控制点与细分数会得到相同单位边序列，适合
重放、撤销和联网命令。曲线若自交并重复经过同一物理边会整体拒绝。当前 BuildingFX 将结果
显示为拓扑正确的单位边组合。成功提交还会在 `PlacementWorld` 创建一个权威曲线组，保存
强类型组 id、四个控制点、细分数和有序成员 id；clone/swap 会完整保留该状态。移动、替换或
删除任一成员会原子解散整个组并清除其余成员链接，防止旧控制点继续驱动视觉；普通单位边
仍然保留。单实例 restore 不会凭不完整快照重建曲线组；`building_editing` schema v8 使用
组级 codec 同时保存组身份、控制点、细分数和有序实例快照，撤销删除整组，重做在独立候选
世界中按原 ID 全量预检后原子恢复。任一成员冲突都会拒绝整个恢复且不改变目标世界。

编辑器可在每次控制点拖动时调用 `previewEdgeCubicBezier` 获得带 world revision 的不可变
草稿，内容包括四个控制点、确定性采样顶点、规范边序列和结构化诊断。`edgeCubicBezierGizmo`
把相同草稿投影为 UI 中立的四个球形手柄、虚线控制多边形和红/绿采样路径；视口只负责绘制
与拖拽，不拥有曲线事实。编辑已有曲线时传入任一成员 id，预览会在候选世界中临时排除原组。
拖拽结束后使用 `makeUpdateEdgeCubicBezier` 生成一个 `building.edge-curve.replace.v1` 原子事务：
组 id 保持稳定，新旧边集合整体替换，撤销恢复完整旧控制点和成员集合。

首次创建贴地曲线使用 `makeEdgeCubicBezierOnSurface`；它先生成同一离散边候选，再完整采样
命名 surface provider，最后把边实例和表面帧编码进同一个 `building.edge-curve.set.v1` 操作。
`previewEdgeCubicBezier` 与 `edgeCubicBezierGizmo` 的可选 `surfaceName` 参数沿用相同采样规则。
成功 draft 携带 provider、surface identity/revision 和拥有型世界坐标/法线样本；gizmo 的控制柄、
控制多边形和解析曲线路径会投射到表面。采样失败时 draft 返回 Rejected 和结构化诊断，路径使用
红色回退投影，且不会产生可提交操作。

`BuildingEdgeCurveDragSession` 把视口射线适配为上述 draft/事务协议：按最近命中的球形控制柄
开始拖动，在相机朝向的拖动平面上投影后转换回连续逻辑网格坐标，每次移动都重新生成
surface-aware preview 与 gizmo。鼠标释放时仅为最后一个有效 draft 生成
`building.edge-curve.replace.v1`；视口仍通过编辑 authority 预检和提交该操作。会话记录开始时的
target revision，拖动期间若世界被其他编辑修改，则返回 Conflict、取消手势且不覆盖并发修改。
输入层不被保留，target 由调用方拥有并必须活得比拖拽会话更久。

`building_editor::BuildingEdgeCurveTool` 已把该协议接入通用 `EditorSession`。宿主实现
`IBuildingViewportAdapter`，同步提供屏幕指针到世界射线以及世界点到 overlay 坐标的投影；工具
在 Pointer Down 命中控制柄后请求捕获，Move 只更新拥有型 preview，Pointer Up 才通过借用的
`IEditAuthority` 预检并提交一个操作。Cancel、未命中、投影失败和 revision conflict 均不改变
世界。工具把四个控制柄与控制线/曲线路径输出到 `IEditorOverlay`，并保存最后一次结构化诊断和
`TransactionReceipt`。提交后会采用 receipt 中的新曲线成员 id 和最新控制点，因此同一选择可
继续拖动；更换 viewport 或 authority 会先取消未提交手势，避免跨宿主提交。

自定义表面曲线使用 `placeEdgeCubicBezierOnSurface`。系统先完成整条离散边预检，再沿解析
Bezier 等距参数采样 `subdivisions+1` 个表面帧；所有命中必须具有相同 `surfaceId` 和
`surfaceRevision`，否则不写入任何边。成功后曲线组拥有 provider 名、表面身份/修订以及每个
样本的世界坐标和法线，因此渲染、快照、撤销和重放不依赖提供者继续存在。编辑控制点时会
重新调用原 provider，只有新样本全部连续有效时才替换旧组。

运行时脚本可在同一个 `PlacementSession` 中完成对应提交，无需绕过会话状态：

```squirrel
session.beginEdgeCurve(1.0, 1.0);
session.appendEdgeCurveControlPoint(1.0, 6.0);
session.appendEdgeCurveControlPoint(7.0, 6.0);
session.appendEdgeCurveControlPoint(7.0, 1.0);
local status = session.executeEdgeCurveOnSurface(24, "terrain.main");
```

该入口与平面曲线共享控制点、原子占用预检和 `lastPlacedIds`；仅在完整表面采样与边提交均
成功后清空控制点。失败会保留控制点，调用方可修改 provider、细分数或控制点后再次预览。

BuildingFX 会把连接掩码归类为 `isolated`、`end`、`straight`、`corner`、`tee`、
`cross`。视觉表可用 `variant.<类型>.<字段>` 覆盖基础值，例如：

```json
"visual3d": {
  "mesh":"models/wall.glb#main",
  "height":"3", "thickness":"0.25", "colorR":"0.6",
  "variant.end.mesh":"models/wall-end.glb#main",
  "variant.corner.mesh":"models/wall-corner.glb#main",
  "variant.corner.mask.34.mesh":"models/wall-corner-34.glb#main",
  "variant.end.rotationDeg":"15",
  "variant.corner.mirrorZ":"true",
  "variant.corner.thickness":"0.4",
  "variant.tee.colorR":"0.9"
}
```

字段解析顺序是精确 mask、拓扑类型、基础字段；因此特殊连接形态可以使用独立资源，未提供
精确资源时仍能回退到类型或基础资源。宿主 C++ 通过 `setMeshResolver` 把稳定资源 id 映射到
provider-owned `graphics::Mesh*`。邻接墙增删后下一次 `BuildingFx.sync` 会重新分类并刷新资源、
旋转、镜像、尺寸和颜色。`getVisualVariant/Resource/FallbackReason` 可用于调试；fallback 原因
为 `primitive_default`、`resolver_unavailable` 或 `resource_unresolved`。

### 角点对象（柱、连接节点）

`placementKind:"corner"` 使用网格顶点而不是格子或边。顶点坐标范围包含最外侧边界，
例如 `8×8` 世界的合法范围为 `(0..8, 0..8)`：

```squirrel
local id = world.placeCorner("wood-post", 4, 3);
local occupant = world.getCornerOccupant("posts", 4, 3);
```

corner 与 cell/edge 独立占用，同一坐标可同时存在地板、墙和柱。跨层柱可设置
`supportMode:"corner_below"`，并通过 `supportTag` 限定下层柱类型。C++ 应优先调用返回
结构化结果和 owning snapshot 的 `placeCornerResult`。Ghost 使用 `setCorner(world,x,y)`，
PlacementSession 使用 `updateCorner(world,x,y)`；放置、查询、移动、拆除、替换和结构级联均
保持 corner 域。`building_editing` v3 快照保存独立 `cornerX/cornerY`，撤销与重做不会把柱子
错误恢复成 cell 对象；旧 v1/v2 操作仍可读取。

BuildingFX 在顶点锚点生成 corner 视觉。3D 可用 `visual3d.width/depth/height`，2D 可用
`visual2d.width/height`；未配置宽深时使用 `size`，再回退到较短格边的 20%。Ghost 和占地
光标使用同一尺寸规则。

### 自由对象

`placementKind:"free"` 保存未吸附的世界平面坐标，适用于家具、植被与装饰物。定义通过
`freeRadiusCells` 声明圆形碰撞 footprint（单位为较短格边，默认 `0.25`）。同时提供正数
`freeFootprintWidthCells` 与 `freeFootprintHeightCells` 时改用随自由旋转姿态变化的 OBB；
宽度沿网格局部 X、长度沿网格局部 Y。`freeFootprintVertices` 可进一步提供以锚点为原点的
凸多边形局部 x/y 坐标对（单位仍为格）；它的优先级高于 OBB。数组必须包含 3–64 个顶点、
保持一致绕序且不能为凹多边形，否则注册时清除该多边形并回退 OBB/圆形。仅同楼层、同
channel 的 free 对象彼此阻挡，cell/edge/corner 占用保持独立：

```json
[
  {"id":"tree","placementKind":"free","snapMode":"free","rotationMode":"free",
   "channel":"decor","freeRadiusCells":0.3,"renderMode":"3d"},
  {"id":"bench","placementKind":"free","snapMode":"free","rotationMode":"free",
   "channel":"decor","freeFootprintWidthCells":2.0,"freeFootprintHeightCells":0.5},
  {"id":"diamond-table","placementKind":"free","snapMode":"free","rotationMode":"free",
   "channel":"decor","freeFootprintVertices":[-1,0, 0,-0.4, 1,0, 0,0.4]}
]
```

```squirrel
session.startPlacement(world, "tree");
session.updateFree(world, 12.25, 17.75, 0.5);
local id = session.execute();
```

`placeFree/canPlaceFree/getFreeOccupant` 提供直接脚本访问；碰撞返回稳定原因
`free_overlap`。圆形、OBB、凸多边形之间的任意组合均使用实际旋转姿态检测；点选也使用
同一权威 footprint。移动保持精确小数坐标、elevation 与自由旋转。BuildingFX 的宽深未
显式配置时回退为圆形直径或多边形局部包围尺寸。`building_editing` v6 快照保存
`freeRadius`、`freeHalfWidth`、`freeHalfHeight` 与 `freeFootprintVertices`，并通过
`makeMoveFree` 构造不经过格子吸附的可逆移动。`setFromSurface` 对 free 定义保留精确命中、
surface identity/revision、法线、切线和采样统计，并在提交前应用坡度与高度差限制；
BuildingFX 使用同一表面框架对齐视觉。free 对象也可通过表面 Ghost 在不同自定义表面间
原子移动；成功时位置与完整表面附件进入同一收据/事件，限制校验失败则不改变实例。
OBB/凸多边形的表面 Patch 会采样中心和全部旋转顶点，跨 surface identity/revision 会拒绝
提交。provider revision 主动变化后的已放置实例重采样，以及凹多边形/多轮廓 footprint
留待后续增强。

移动与替换也通过同一个会话原子提交。先把鬼影更新到目标姿态，再执行移动；替换使用
会话当前选择的建筑定义，并保持原 instance id、属性、驻军和表面附件：

```squirrel
session.startPlacement(world, "wood-chair");
session.updateFromWorld(world, targetX, targetY);
if (session.executeMove(existingId) == 0)
    print("move committed\n");

session.startPlacement(world, "stone-chair");
if (session.executeReplace(existingId) == 0)
    print("replace rejected\n");
```

`executeMove` 和 `executeReplace` 返回命名状态（`Committed` 为 0，`Rejected` 为 1），
成功后 `getLastEditedId()` 返回受影响实例。移动支持 cell、edge 与 corner 鬼影；替换要求新旧定义
属于同一 placement domain。冲突、越界、未知定义或跨 domain 替换均在写入前拒绝，不发送
部分事件。C++ 应优先使用 `moveBuildingResult`、`moveEdgeResult` 和
`replaceBuildingResult` 获取结构化诊断及 before/after receipt。

矩形与圆形笔刷先预览、再原子提交：

```squirrel
session.startPlacement(world, "floor");
session.previewRectangle(2, 3, 6, 7); // 或 previewBrush(cx, cy, radius)
fx.updateAreaPreview(world, session);
fx.setGridVisible(world, true);        // drawGrid2D/drawGrid3D 显示绿/红热图

if (session.executeRectangle(2, 3, 6, 7) == 0) {
    for (local i = 0; i < session.getLastPlacedCount(); ++i)
        print(session.getLastPlacedId(i) + "\n");
}
```

预览按行主序返回每个锚点，`getAreaPreviewAccepted/Reason` 可显示具体冲突。圆形笔刷使用
包含边界的欧氏格距。区域内任一锚点非法或候选 footprint 互相覆盖时，执行会整体拒绝；
不会产生部分建筑。BuildingFX 的 2D/3D 热图直接复制会话预检结果，绿色表示可提交，红色
表示占用、越界、地形或区域内部冲突。

C++ 编辑工具应通过 `building_editing::BuildingPlacementTarget::makeRectangle` 或 `makeBrush`
生成一个 `DomainOperation`，再交给 `LocalWorldAuthority`。整个矩形或笔刷只有一个 revision、
一个 transaction receipt 和一个撤销记录；撤销会删除该手势创建的全部精确 instance id。
如果提交前出现新的占用冲突，staging candidate 会整体丢弃，权威世界与 Building 事件队列不变。

需要由同一个工具栏切换多种绘制方式时，使用统一 Placement Pattern 协议。当前内置
`edge_line`、`edge_path`、`edge_cubic_bezier`、`rectangle_fill`、`rectangle_outline` 和
`circle_brush` 六种模式；它们共享 owning request、非变异 preview、结构化失败和一次性原子
placement receipt。旧的 `executeEdgeLine`、`executeEdgePath`、`executeEdgeCurve`、矩形和
刷子入口仍保留，但现在都投影到相同权威执行路径。

```squirrel
session.startPlacement(world, "floor");
session.beginPattern("rectangle_outline");
session.appendPatternPoint(2, 2);
session.appendPatternPoint(8, 6);
if (session.previewPattern() == 0) {
    fx.updateAreaPreview(world, session);
    session.executePattern();
}
```

区域 preview 可继续通过 BuildingFX 热图显示；通用 UI 可读取
`getPatternPreviewCount/X/Y/Axis/Accepted/Reason`，无需理解每种模式的内部结果类型。矩形边框
按行主序生成唯一周长锚点，单行或单列不会重复。离散边和区域坐标必须是有限整数；Bezier
控制点保持连续逻辑坐标。只有 Bezier 当前接受 `setPatternSurface`，其他模式传入 surface 会以
结构化 InvalidArgument 拒绝。

### 视觉双形态与渲染桥（`eve.BuildingFx`）

```squirrel
fx <- eve.BuildingFx();
fx.attach(world);
fx.sync(world);                        // 按定义 renderMode/visual2d/visual3d 生成/同步/销毁视觉
fx.updateGhost(world, ghost);          // 每帧：鬼影 + 占地光标（绿/红）
fx.setGridVisible(world, true);
fx.drawGrid2D(world, gfx);             // 2D 网格叠加（渲染循环中调用）
fx.drawGrid3D(world, gfx, 0.01);       // 3D 平面线框
```

### 放置会话（`eve.Building.newSession`）

```squirrel
session <- building.newSession();
session.startPlacement(world, "house");
session.updateFromSurface(world, "plane", hitX, hitZ);  // 或 updateFromWorld / updateFromWorld3D
session.setMode("remove");
session.execute();                     // place 放置 / remove 拆除
```
