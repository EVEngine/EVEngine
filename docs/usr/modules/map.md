# Tilemap模块

**脚本入口：** `eve.Map()`

创建或载入 TileLayer，设置瓦片、投影、图层并提交渲染。

## 基本用法

```squirrel
local map = eve.Map();
local layer = map.newLayerFromFile("maps/level.json");
layer.setTile(2, 3, 7);
map.update(dt);
map.render(gfx);
```

## 对象关系与调用时机

`Map` 聚合 TileLayer 并统一更新/渲染；TileLayer 保存网格、投影、origin、层级和图集资源。JSON loader 把 Tiled 数据映射到这些对象。

## 目标导向指南

### 载入 Tiled 地图

把 JSON 和图集放在游戏目录，用 `newLayerFromFile(path)` 创建层；初始化后设置 origin、layer 和 visible。每帧 `map.update(dt)`，渲染时 `map.render(gfx)`。

### 运行时修改瓦片

先用地图坐标换算接口把世界位置转成格子，再 `setTile(x, y, gid)`；批量生成地图时先 resize，再填充，避免重复重建图层。0 通常表示空瓦片。

## 常见问题

- GID 与图集编号混淆：0 为空，其余值遵循 tileset 映射。
- 世界坐标直接作为 tile 索引：先做投影换算。
- 修改 JSON 后未启用 watch/autoReload。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `applyConfig()`、`clear()`、`depthYAt()`、`fill()`、`getAutoReload()`、`getConfigPath()`、`getLayer()`、`getLayerCount()`
- `getMapHeight()`、`getMapWidth()`、`getName()`、`getObjectCount()`、`getObjectGid()`、`getObjectHeight()`、`getObjectName()`、`getObjectType()`
- `getObjectWidth()`、`getObjectX()`、`getObjectY()`、`getTile()`、`getTileHeight()`、`getTileWidth()`、`getTilesetColumns()`、`getTilesetFirstGid()`
- `getTilesetTexture()`、`getX()`、`getY()`、`isVisible()`、`loadConfig()`、`loadFromFile()`、`newLayer()`、`newLayerFromFile()`
- `pollConfigs()`、`reloadConfig()`、`render()`、`resize()`、`setAutoReload()`、`setCamera()`、`setCanvas()`、`setLayer()`
- `setOrigin()`、`setTile()`、`setTileSize()`、`setTileset()`、`setTilesetTileSize()`、`setTint()`、`setVisible()`、`tileToWorldX()`
- `tileToWorldY()`、`update()`、`worldToTileX()`、`worldToTileY()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/map/`](../../../src/modules/map/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `map`。
