# 空间索引模块

**脚本入口：** `eve.Spatial()`

提供四叉树、八叉树、空间哈希与空间二分树（及其二维/三维版本），用于快速判断元素是否落在视口、拾取区或邻近范围内。

## 基本用法

```squirrel
local spatial = eve.Spatial();
local tree = spatial.newQuadTree(0, 0, 2048, 2048);

tree.insert(1, 100, 100, 132, 132);   // id + AABB
tree.insert(2, 400, 200, 432, 232);

local n = tree.queryRect(90, 90, 200, 200);
for (local i = 0; i < tree.getResultCount(); i++) {
    local id = tree.getResultId(i);
    // 用 id 查自己的实体 / 地图对象
}
```

## 对象关系与调用时机

`Spatial` 只做工厂；真正持有数据的是 `QuadTree` / `Octree` / `SpatialHash2D` / `SpatialHash3D` / `BSPTree2D` / `BSPTree3D`。  
索引里只存整数 `id` 与包围盒，不拥有实体。物体移动时调用 `update`；销毁时 `remove`。查询结果写在该索引对象内部，下一次 `query*` 会覆盖。

## 目标导向指南

### 地图视口裁剪

用地图世界范围创建 `QuadTree` 或 `SpatialHash2D`，把可交互对象 / 装饰物 id 与 AABB 插入；每帧用相机可见矩形 `queryRect`，只更新或绘制命中 id。

### 3D 场景邻近查询

用场景包围盒创建 `Octree` 或 `SpatialHash3D`；用 `querySphere` 找玩家附近可交互体，再做精确逻辑。

### 结构怎么选

- 物体大小接近、移动多：优先 `SpatialHash2D` / `SpatialHash3D`（调好 `cellSize`）
- 大地图静态或半静态：`QuadTree` / `Octree`
- 需要沿轴二分剪枝：`BSPTree2D` / `BSPTree3D`

## 常见问题

- 把 `procgen` 的 `dungeon.bsp` 当成空间查询树——那是地牢生成，不是本模块。
- 忘记在物体移动后 `update`，导致查询仍命中旧位置。
- `getResultId` 在新的 `query*` 之后仍使用旧循环变量——结果缓冲已被覆盖。
- 根包围盒过小：树/BSP 构造时给出的世界范围应覆盖所有可能插入的 AABB。

## API 快查

- 模块：`getName()`、`newQuadTree()`、`newOctree()`、`newSpatialHash2D()`、`newSpatialHash3D()`、`newBSPTree2D()`、`newBSPTree3D()`
- 共用：`clear()`、`insert()`、`remove()`、`update()`、`contains()`、`getCount()`、`getResultCount()`、`getResultId()`
- 2D 查询：`queryPoint()`、`queryRect()`、`queryCircle()`
- 3D 查询：`queryPoint()`、`queryAABB()`、`querySphere()`
- 树/BSP：`getMinX/Y[/Z]`、`getMaxX/Y[/Z]`、`getMaxDepth()`、`getMaxPerNode()`
- 哈希：`setCellSize()`、`getCellSize()`

## 使用要点

- 模块对象和索引对象应保存在全局或实体状态中，不要每帧重建整棵树（除非批量替换世界）。
- `id` 由脚本自定义，保持稳定即可；不要假设与 ECS 实体句柄自动同步。
- 参数约束与默认值以头文件及 `addFunc` 绑定为准。

**源码：** [`src/modules/spatial/`](../../../src/modules/spatial/)  
**设计文档：** [`docs/dev/空间索引模块设计.md`](../../dev/空间索引模块设计.md)  
**相关测试：** [`test/spatial.cpp`](../../../test/spatial.cpp)
