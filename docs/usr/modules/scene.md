# 声明式场景树模块

**脚本入口：** `eve.Scene()`

用节点描述构建 2D/3D 层级，维护 local/world 变换并连接渲染实体。

## 基本用法

```squirrel
local scene = eve.Scene();
scene.beginBuild();
scene.beginNode("root", "Root");
scene.setBuildSpace("3d");
scene.addNode("player", "Player");
scene.end();
scene.mountBuildAs("level");
```

## 对象关系与调用时机

`Scene` 管理命名 SceneHost；Host 保存 SceneNode 树；节点 local TRS 经 TransformSystem 计算 world TRS；renderable link 把结果同步给渲染组件。

## 目标导向指南

### 构建层级场景

用 `beginBuild()`、`beginNode()` / `beginGroup()`、`end()` 建树，在打开的节点上设置 position/rotation/scale/space，最后 `mountBuildAs(name)`。父节点变换会传播给子节点。

### 移动节点并同步渲染实体

运行时用 `setNodePosition()` 等按 ID 修改 local transform，然后 `updateTransforms()`；通过 `linkRenderable2D/3D()` 连接的渲染实体会接收 world TRS。查询路径和祖先关系可用于编辑器与挂点逻辑。

## 常见问题

- `beginNode()` 与 `end()` 不配对导致 build 不完整。
- 修改 local transform 后不更新 transforms。
- 混淆节点 name、id 和 path：运行时修改优先用稳定 id。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `addNode()`、`beginBuild()`、`beginGroup()`、`beginNode()`、`bindOwner()`、`collectChildIds()`、`collectIds()`、`collectIdsByName()`
- `collectIdsFrom()`、`collectIdsVisible()`、`end()`、`findIdByName()`、`findIdByPath()`、`getChildCount()`、`getChildIdAt()`、`getName()`
- `getNodeCount()`、`getNodePath()`、`getParentId()`、`getRootId()`、`hasNode()`、`isAncestor()`、`isDescendant()`、`linkRenderable2D()`
- `linkRenderable3D()`、`mountBuild()`、`mountBuildAs()`、`remountBuildAs()`、`select()`、`setBuildPosition()`、`setBuildRotation()`、`setBuildScale()`
- `setBuildSpace()`、`setBuildVisible()`、`setHostLayer()`、`setHostVisible()`、`setNodePosition()`、`setNodeRotation()`、`setNodeScale()`、`setNodeVisible()`
- `unlinkNode()`、`updateTransforms()`、`updateTransformsAll()`、`walkBreadthFirstIds()`、`walkDepthFirstIds()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/scene/`](../../../src/modules/scene/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `scene`。
