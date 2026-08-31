# Tilemap模块

**脚本入口：** `eve.Map()`

创建或载入 TileLayer，设置瓦片、投影、图层并提交渲染；可用 Pathfinder 做格子寻路与群体 Flow Field，用 Fov 做动态视野与探索记忆。

## 基本用法

```squirrel
local map = eve.Map();
local layer = map.newLayerFromFile("maps/level.json");
layer.setTile(2, 3, 7);
map.update(dt);
map.render(gfx);
```

## 对象关系与调用时机

`Map` 聚合 TileLayer 并统一更新/渲染；TileLayer 保存网格、投影、origin、层级和图集资源。JSON loader 把 Tiled 数据映射到这些对象。寻路在格子索引空间进行，与投影解耦；需要世界坐标时对路径点调用 `tileToWorld*`。

## 目标导向指南

### 载入 Tiled 地图

把 JSON 和图集放在游戏目录，用 `newLayerFromFile(path)` 创建层；初始化后设置 origin、layer 和 visible。每帧 `map.update(dt)`，渲染时 `map.render(gfx)`。

### 运行时修改瓦片

先用地图坐标换算接口把世界位置转成格子，再 `setTile(x, y, gid)`；批量生成地图时先 resize，再填充，避免重复重建图层。0 通常表示空瓦片。

`fillRect(x, y, width, height, gid)` 会裁剪到地图边界，并把整次区域修改发布为一个 revision。绑定到图层的 Pathfinder 与 Fov 会在下一次查询或 `compute()` 时根据 revision 自动同步；通常不再需要手动调用 `syncFromLayer()`。

```squirrel
layer.fillRect(8, 8, 16, 12, 4);
print(layer.getRevision() + " chunks=" + layer.getNonEmptyChunkCount() +
      "/" + layer.getChunkCount() + " size=" + layer.getChunkSize() + "\n");
```

地图内部以 32×32 chunk 建立空间索引。带 Camera2D 的正常 `map.render(gfx)` 会先按视口剔除 chunk；可通过 `map.getLastVisitedChunkCount()`、`map.getLastVisitedCellCount()` 和现有的可见瓦片统计检查本帧工作量。

### TileSet v2 数据、动画与 terrain

运行时可以逐帧建立动画，并为 GID 写入保留类型的自定义数据：

```squirrel
layer.clearTileAnimation(20);
layer.addTileAnimationFrame(20, 21, 100);
layer.addTileAnimationFrame(20, 22, 140);
print(layer.getTileAnimationFrameCount(20) + "\n");

layer.setTileDataString(20, "biome", "marsh");
layer.setTileDataNumber(20, "damage", 2.5);
layer.setTileDataBool(20, "wet", true);
print(layer.getTileDataType(20, "damage") + " " +
      layer.getTileDataString(20, "biome") + " " +
      layer.getTileDataNumber(20, "damage") + " " +
      layer.getTileDataBool(20, "wet") + "\n");
```

terrain rule 使用 8 位邻接掩码（从西北开始顺时针）。绘制一个 terrain 会重新解析目标及其一格邻域：

```squirrel
layer.clearTerrainRules();
layer.setTerrainRule(30, 1, 0);       // 孤立草地
layer.setTerrainRule(31, 1, 1 << 3);  // 东侧相连
layer.paintTerrain(4, 4, 1);
print(layer.getTerrain(4, 4) + "\n");
```

Tiled JSON 的 `animation`、typed `properties`、`wangsets` 与无限地图 `chunks` 可直接导入。地图可以引用多个外部 JSON `.tsj` 或 XML `.tsx` tileset；每个 atlas 按地图中的 `firstgid` 选择，图片路径相对 tileset 文件解析。外部 tileset 也会加入热重载依赖。无限地图当前会按已存在 chunk 的包围盒规范化为运行时图层，并把负 chunk 坐标折算进图层 origin。

Tile properties 中的 `walkable`、`cost`、`enterMask`、`exitMask` 会进入统一导航资料。方向位为 `N=1, E=2, S=4, W=8`；寻路同时检查源格的 `exitMask` 和目标格的反向 `enterMask`。没有声明时四向均允许。这样悬崖边、单向台阶和墙口不需要再维护一份独立的 Pathfinder 阻挡表。

### 组合项目自己的 2.5D 资产工作流

独立 PNG、特殊 pivot 或不规则 atlas 不要求采用引擎内置成品导入器。项目工作流只需生成 `eve.tileset/1` manifest，运行时载入：

```squirrel
local layer = map.newLayer(32, 32, 150, 75);
layer.applyConfig(@"{""orientation"":""isometric""}");
layer.loadTilesetManifest("assets/tiles.tileset.json");
```

每个 GID 可声明 `region`、`pivot`、`sortBias`、`footprint`、`walkable`、`cost` 与项目自定义字段。规则 atlas GID 仍走原路径。参考阶段管线位于 `tools/tile-pipeline/`，可用项目 Python 插件替换任一步；详见 `docs/dev/可组合2.5D-TileSet资产管线.md`。

逻辑格尺寸和渲染间距相互独立：

```squirrel
layer.setRenderSpacing(1.12, 1.12); // 相对逻辑 tile 尺寸
layer.setCellGap(8.0, 4.0);         // 或直接指定世界像素间隔
local gapX = layer.getCellGapX();
local gapY = layer.getCellGapY();
local spacingX = layer.getRenderSpacingX();
local spacingY = layer.getRenderSpacingY();
```

间距同时用于正反坐标换算、拾取和深度排序，但不改变 GID、寻路邻接关系或移动成本。JSON 配置可使用 `"renderSpacing":[1.12,1.12]` 或 `"cellGap":[8,4]`。

需要互相产生 2.5D 遮挡的地面、角色和建筑应使用相同的 `layer`，使统一队列按脚点深度排序。`layer` 是 HUD、前景遮罩等用途的硬排序屏障；同格内的细微顺序使用 TileSet `sortBias`。

### 单体寻路（A*）

```squirrel
local pf = map.newPathfinder(layer);
pf.setTopology("auto");   // ortho4 / ortho8 / hex；auto 按 orientation
pf.blockGid(1);           // 墙 GID
local path = pf.findPath(sx, sy, gx, gy);
if (path.getLength() > 0) {
    local x = path.getX(0);
    local y = path.getY(0);
}
```

正交/等距默认四向；`setDiagonal(true)` 后 `auto` 升为八向。交错/六角地图 `auto` 为 `hex`。

### 群体寻路（同目标 Flow Field）

多单位前往同一格时，建一次势场再各自跟随，避免每人跑一遍 A*：

```squirrel
local field = pf.buildFlowField(gx, gy);
local p1 = pf.followFlow(field, ax, ay);
local p2 = pf.findGroupPath(bx, by, gx, gy); // 内部复用缓存场
local nx = field.nextX(ax, ay);
local ny = field.nextY(ax, ay);
```

自定义网格（不绑 TileLayer）：`map.newPathfinderSize(w, h)`，再用 `setBlocked` / `setCellCost`。

### 动态视野（FOV / 战争迷雾）

格子可见性与探索记忆；遮挡（opaque）与寻路可行走（walkable）分开配置。默认算法为 recursive shadowcasting；可选 `raycast` / `permissive` / `rectangle`。模式：`grid2d`（默认）、`heightmap`、`volume`。拓扑：`ortho` / `hex`。

```squirrel
local fov = map.newFov(layer);
fov.setBlockEmpty(false);
fov.blockOpaqueGid(1);
fov.setRadiusMetric("chebyshev");
fov.setTopology("auto"); // 或 "hex"
local id = fov.addRevealer(sx, sy, 8);
// 软 RPG 挂钩：游戏自行读取 actor 属性后写入
fov.setRevealerPerception(id, actor.getFinalAttribute("perception"));
fov.setPerceptionRadiusScale(0.25);
fov.setDetectionMargin(0.0);
fov.compute();
if (fov.canDetect(id, tx, ty, targetStealth)) { /* 可见且感知足够 */ }
local tex = fov.buildMaskTexture(gfx); // RGBA8 FoW 遮罩，调用方拥有
```

自定义网格：`map.newFovSize(w, h)` + `setOpaque`。移动观察者后再次 `compute()`；无变更时 `isDirty()` 为 false，`compute` 会跳过。离开视野的格子保留 `explored`，可用 `clearMemory()` 清空。

高度场：

```squirrel
fov.setMode("heightmap");
fov.setElevation(4, 1, 2.0);
fov.setCliffBlock(1.0);
```

体素 3D：

```squirrel
local fov3 = map.newFovVolume(32, 32, 8);
fov3.setOpaque3(4, 4, 2, true);
fov3.addRevealer3(4, 4, 1, 10);
fov3.setVerticalRange(4);
fov3.compute();
fov3.isVisible3(8, 4, 1);
```

### Dual Grid（双网格）

Tiled **没有**原生 dual-grid；在编辑器里画逻辑填充层，运行时解算显示层。支持正交 / 等距 / 交错 / 六角：掩码按格子索引采样，显示层继承投影参数并施加对应半步 origin 偏移。

```squirrel
local map = eve.Map();
local logic = map.newLayer(16, 12, 32, 32);
local display = map.newLayer(1, 1, 32, 32);
// display.setTileset(tex, 1, 4);  // 4×4 的 15-tile 图集
logic.setTile(3, 4, 1);
map.resolveDualGrid(logic, display);  // 半步偏移 + 15 片选瓦
// map.dualGridOffsetX(logic) / dualGridOffsetY(logic) 可查偏移量
```

`resolveDualGridFilled(logic, display, filledGid)` 只把指定 GID 当作填充。逻辑层可继续用于碰撞 / 寻路；默认会 `setVisible(false)`。

### 碰撞几何

`publishCollision(layer)` 会把 `setTileMetadata(..., walkable=false)` 标记的正交瓦片贪心合并成世界坐标矩形，减少静态碰撞体数量；已注册的物理/项目适配器会通过 `ITileCollisionSink` 一次性收到替换后的几何。没有适配器时仍可读取矩形，自行创建物理夹具：

`setTileNavigationProfile(gid, walkable, cost, enterMask, exitMask, opaque, semanticFlags)`
是同行性、碰撞与视野遮挡的统一画像。`Pathfinder`/Flow Field 使用 walkable、cost 与四向
enter/exit mask，`publishCollision` 使用不可通行状态或 Tiled tile object collision，`Fov`
使用 opaque；三者随同一 tile revision 自动失效，避免分别维护“可走”和“墙体”两套事实。

## 生产级自动贴图

逻辑 terrain grid 是权威数据，GID 只是派生显示结果。先定义 family 与精确 8 邻域规则，再由
point/rectangle/fill/erase 操作只重算 dirty region 外扩一格：

```nut
layer.defineAutotileFamily(1, "shore", 1337) // terrain | shore | wall | waterfall
layer.setAutotileRule(101, 1, neighborMask, 1) // gid, terrain, exact mask, weight
layer.paintTerrain(4, 3, 1)
layer.paintTerrainRect(2, 2, 8, 5, 1)
layer.fillTerrain(1)
layer.eraseTerrainRect(5, 2, 2, 1)
```

同一 mask 可配置多个带权变体；选择由 family seed、terrain 与坐标确定，不依赖时间或容器遍历
顺序。`wall` 解析四个正交邻居，`waterfall` 解析上下连续性，因此可以稳定表达墙顶/墙身/墙脚
以及瀑布口/循环水体/水花脚；动画仍使用 `addTileAnimationFrame`。完整的无素材示例位于
`examples/autotile-production`。

## Tiled 与 RPG Maker MV/MZ

Tiled JSON/TMJ 支持 embedded tileset、外部 TSJ/TSX、多 tileset、无限 chunk、嵌套 group、
typed properties、animation、Wang connectivity、水平/垂直/对角 transform flags，以及 tile
objectgroup 的矩形/多边形包围盒碰撞。外部引用相对 map/tileset 文件解析并参与 hot reload。
导入失败会恢复 Config、Tiles、Tileset、Draw 与 Resource 的旧快照，不留下半更新状态。

C++ 可直接导入 RPG Maker 工程，无需启动 RPG Maker：

```cpp
auto result = eve::map::importRpgMakerMap("data/Map001.json", "data/Tilesets.json", "RPG Maker MZ");
if (!result) {
    // inspect result.error() / diagnostic domain_code
} else {
    auto receipt = std::move(result).value();
    auto *navigation = receipt.navigationLayer;
}
```

适配器读取 A1-A5/B-E sheet、四个 tile plane 与 shadow plane，按 MV/MZ 官方 quarter-tile
table 解码 A1 水面/瀑布动画、A2 地面、A3 屋顶/墙、A4 墙顶/墙身。`navigationLayer` 把
passage、四方向 passage、star overlay、ladder、bush、counter、damage floor 与 terrain tag
合成为单一隐藏画像；源文件不会被修改，`Resource.sourceEngine/sourceVersion` 会记录来源。

```squirrel
local count = map.publishCollision(layer);
for (local i = 0; i < count; ++i) {
    local x = map.getCollisionRectX(i);
    local y = map.getCollisionRectY(i);
    local w = map.getCollisionRectWidth(i);
    local h = map.getCollisionRectHeight(i);
}
```

当前自动合并只支持正交层；等距、交错和六角层返回 0，项目应使用多边形适配器，避免把菱形误近似成轴对齐矩形。

## 常见问题

- GID 与图集编号混淆：0 为空，其余值遵循 tileset 映射；默认空瓦片不可走（`setBlockEmpty`）。
- 世界坐标直接作为 tile 索引：先做投影换算。
- 修改 JSON 后未启用 watch/autoReload。
- 期望 Tiled 直接导出 dual-grid 显示层：不支持；用逻辑层 + `resolveDualGrid`。
- 寻路失败：`getLength()==0`（越界、不可走或不可达），不是抛异常。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `applyConfig()`、`clear()`、`depthYAt()`、`fill()`、`getAutoReload()`、`getConfigPath()`、`getLayer()`、`getLayerCount()`
- `getMapHeight()`、`getMapWidth()`、`getName()`、`getObjectCount()`、`getObjectGid()`、`getObjectHeight()`、`getObjectName()`、`getObjectType()`
- `getLastVisibleTileCount()`、`getLastCustomVisualCount()`、`getLastAtlasCount()`
- `publishCollision()`、`getCollisionRectCount()`、`getCollisionRectX()`、`getCollisionRectY()`、`getCollisionRectWidth()`、`getCollisionRectHeight()`
- `getObjectWidth()`、`getObjectX()`、`getObjectY()`、`getTile()`、`getTileHeight()`、`getTileWidth()`、`getTilesetColumns()`、`getTilesetFirstGid()`
- `getTilesetTexture()`、`getX()`、`getY()`、`isVisible()`、`loadConfig()`、`loadFromFile()`、`newLayer()`、`newLayerFromFile()`
- `newPathfinder()`、`newPathfinderSize()`、`newFov()`、`newFovSize()`、`newFovVolume()`
- `pollConfigs()`、`reloadConfig()`、`render()`、`resize()`、`resolveDualGrid()`、`resolveDualGridFilled()`、`setAutoReload()`、`setCamera()`、`setCanvas()`、`setLayer()`
- `setOrigin()`、`setTile()`、`setTileSize()`、`setTileset()`、`setTilesetTileSize()`、`setTileVisual()`、`setTileMetadata()`、`clearTileVisuals()`、`getTileVisualCount()`、`loadTilesetManifest()`、`setTint()`、`setVisible()`、`tileToWorldX()`
- `tileToWorldY()`、`update()`、`worldToTileX()`、`worldToTileY()`、`dualGridFrame()`、`dualGridMaskAt()`、`dualGridOffsetX()`、`dualGridOffsetY()`、`lastDualGridError()`

Pathfinder：`setTopology`、`getTopology`、`setDiagonal`、`blockGid`、`unblockGid`、`clearBlockedGids`、`setBlockEmpty`、`setBlocked`、`isWalkable`、`setCellCost`、`getCellCost`、`syncFromLayer`、`findPath`、`buildFlowField`、`followFlow`、`findGroupPath`、`invalidateCache`

Path：`getLength`、`getX`、`getY`、`getTotalCost`

FlowField：`getWidth`、`getHeight`、`getGoalX`、`getGoalY`、`costAt`、`nextX`、`nextY`、`isReachable`

Fov：`getWidth`、`getHeight`、`getDepth`、`setMode`、`getMode`、`setAlgorithm`、`getAlgorithm`、`setRadiusMetric`、`getRadiusMetric`、`setTopology`、`getTopology`、`setCornerPeek`、`blockOpaqueGid`、`unblockOpaqueGid`、`clearOpaqueGids`、`setBlockEmpty`、`setOpaque`、`isOpaque`、`setOpaque3`、`isOpaque3`、`syncFromLayer`、`setElevation`、`getElevation`、`setCliffBlock`、`setEyeOffset`、`setVerticalRange`、`addRevealer`、`addRevealer3`、`removeRevealer`、`clearRevealers`、`setRevealerPosition`、`setRevealerPosition3`、`setRevealerRadius`、`setRevealerFacing`、`clearRevealerFacing`、`setRevealerEnabled`、`getRevealerCount`、`setRevealerPerception`、`getRevealerPerception`、`setPerceptionRadiusScale`、`setDetectionMargin`、`getEffectiveRadius`、`canDetect`、`canDetect3`、`markDirty`、`isDirty`、`compute`、`isVisible`、`isExplored`、`isVisible3`、`isExplored3`、`getState`、`getState3`、`clearMemory`、`resetVisibleOnly`、`getMaskValue`、`getMaskByte`、`getMaskValue3`、`getMaskByte3`、`buildMaskTexture`、`buildMaskTextureSlice`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/map/`](../../../src/modules/map/)
**设计：** [`docs/dev/寻路系统设计.md`](../../dev/寻路系统设计.md)、[`docs/dev/动态视野系统设计.md`](../../dev/动态视野系统设计.md)
**相关测试：** [`test/map.cpp`](../../../test/map.cpp)、[`test/map_path.cpp`](../../../test/map_path.cpp)、[`test/map_fov.cpp`](../../../test/map_fov.cpp)、[`test/hex_level_simulation.cpp`](../../../test/hex_level_simulation.cpp)、[`test/hex_level_data.cpp`](../../../test/hex_level_data.cpp)
**示例：** [`examples/hex-levels/`](../../../examples/hex-levels/)
