# Tilemap模块

**脚本入口：** `eve.Map()`

创建或载入 TileLayer，设置瓦片、投影、图层并提交渲染；可用 Pathfinder 做格子寻路与群体 Flow Field。

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

先用地图坐标换算接口把世界位置转成格子，再 `setTile(x, y, gid)`；批量生成地图时先 resize，再填充，避免重复重建图层。0 通常表示空瓦片。改瓦片后若已创建 Pathfinder，调用 `syncFromLayer()`。

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
- `getObjectWidth()`、`getObjectX()`、`getObjectY()`、`getTile()`、`getTileHeight()`、`getTileWidth()`、`getTilesetColumns()`、`getTilesetFirstGid()`
- `getTilesetTexture()`、`getX()`、`getY()`、`isVisible()`、`loadConfig()`、`loadFromFile()`、`newLayer()`、`newLayerFromFile()`
- `newPathfinder()`、`newPathfinderSize()`
- `pollConfigs()`、`reloadConfig()`、`render()`、`resize()`、`resolveDualGrid()`、`resolveDualGridFilled()`、`setAutoReload()`、`setCamera()`、`setCanvas()`、`setLayer()`
- `setOrigin()`、`setTile()`、`setTileSize()`、`setTileset()`、`setTilesetTileSize()`、`setTint()`、`setVisible()`、`tileToWorldX()`
- `tileToWorldY()`、`update()`、`worldToTileX()`、`worldToTileY()`、`dualGridFrame()`、`dualGridMaskAt()`、`dualGridOffsetX()`、`dualGridOffsetY()`、`lastDualGridError()`

Pathfinder：`setTopology`、`getTopology`、`setDiagonal`、`blockGid`、`unblockGid`、`clearBlockedGids`、`setBlockEmpty`、`setBlocked`、`isWalkable`、`setCellCost`、`getCellCost`、`syncFromLayer`、`findPath`、`buildFlowField`、`followFlow`、`findGroupPath`、`invalidateCache`

Path：`getLength`、`getX`、`getY`、`getTotalCost`

FlowField：`getWidth`、`getHeight`、`getGoalX`、`getGoalY`、`costAt`、`nextX`、`nextY`、`isReachable`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/map/`](../../../src/modules/map/)
**设计：** [`docs/dev/寻路系统设计.md`](../../dev/寻路系统设计.md)
**相关测试：** [`test/map.cpp`](../../../test/map.cpp)、[`test/map_path.cpp`](../../../test/map_path.cpp)
