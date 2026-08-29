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

### 给节点挂脚本行为（场景节点 ↔ 脚本 ECS）

每个节点都可以挂一个 `eve.SceneEntity` 子类实例（`extends eve.Entity`，数据组件槽沿用脚本 ECS 声明方式）。挂载后实例自动进入 `eve.view(cls)`，可被 `eve.System` 批量驱动；`scene.update(dt)` 先同步 transforms，再对全部已挂实例调用 `update(dt)`。

```squirrel
class MoveComp extends eve.Component { speed = 60.0 }

class Bouncer extends eve.SceneEntity {
    move = MoveComp
    function onAttach() { node().setPosition(120.0, 60.0, 0.0) }
    function update(dt) {
        local p = node().getPosition()
        node().setPosition(p[0], p[1] + move.speed * dt, p[2])
    }
    function onDetach() {}
}

local e = scene.attachEntity("player", Bouncer)   // 返回实例
scene.hasEntity("player", Bouncer)                // true
scene.getEntity("player", Bouncer)                // 按类查找
scene.entitiesOf("player")                        // 该节点全部实例
scene.detachEntity("player", e)                   // 卸载（触发 onDetach + destroy）

local ref = scene.getNodeRef("player")            // 节点句柄
local p = ref.getPosition()                       // [x, y, z]
ref.getWorldPosition()                            // world 位置（需先 updateTransforms/update）
ref.setPosition(0.0, 0.0, 0.0)
```

生命周期：reconcile / 全量 rebuild 按节点 id 保留绑定；节点被移除时自动调用 `onDetach()` 并销毁实例。同一节点重复 attach 同 class 会先卸载旧实例。

### 通用 link 系统（节点 ↔ 渲染/物理/相机/音频）

一个节点可以同时挂多种外部对象的 link（每类一个），`TransformSystem` 按类型同步：

```squirrel
scene.linkRenderable3D("soldier", mesh)              // 渲染体：world TRS + visible
scene.linkPhysics3D("soldier", body)                 // 物理体：默认 node→body（kinematic）
scene.linkPhysics3D("soldier", body, "body")         // body→node（dynamic，body 权威）
scene.linkPhysics2D("soldier", body2d, "node")
scene.linkCamera3D("soldier", cam)                   // 相机：eye 跟随节点 world 位置
scene.linkAudio3D("soldier", source)                 // 3D 音源：position 跟随节点

scene.linkCount("soldier")                           // 4
scene.unlinkNodeKind("soldier", "physics3d")         // 只拆一类
scene.unlinkNode("soldier")                          // 拆全部
```

物理 2D 同步的是 local x/y/roll（像素 + 弧度），物理 3D 同步 local x/y/z + 欧拉角（四元数内部转换）；body→node 模式适合根级节点（local≈world）。link 在 reconcile / 全量 rebuild 后按 id 保留；`SceneLoader` 导入的模型节点同样可挂 link，热重载后保留。

### 变换读写 / 空间换算 / 层级操作

与 `setNode*` 配套的 getter 与换算已补齐（selected host；跨 host 用 `*At` 变体）：

```squirrel
scene.getNodePosition("ball")           // [x, y, z]
scene.getNodeRotation("ball")           // [yaw, pitch, roll]
scene.getNodeScale("ball")
scene.getNodeVisible("ball")
scene.getNodeWorldPosition("ball")      // 需先 updateTransforms/update
scene.getNodeWorldRotation("ball")
scene.getNodeWorldScale("ball")
scene.localToWorld("ball", 0, 0, 0)     // 节点 local → world
scene.worldToLocal("ball", x, y, z)

scene.setNodeParent("ball", "pivot")    // 重挂父节点（环检测）；"" 表示摘除
scene.removeNode("ball")                // 从父节点摘除（arena 节点仍在，rebuild 才删除）
scene.addNodeChild("root", "ball")
scene.removeNodeChild("root", "ball")

scene.setNodeQuaternion("ball", qx, qy, qz, qw)
scene.getNodeQuaternion("ball")         // [qx, qy, qz, qw]
scene.setNodeLookAt("ball", tx, ty, tz) // local +Z 指向目标
```

### 节点 tag / layer（分组查询）

```squirrel
scene.addNodeTag("ball", "unit")
scene.hasNodeTag("ball", "unit")
scene.removeNodeTag("ball", "unit")
scene.getNodeTags("ball")               // 数组
scene.collectIdsByTag("unit")           // 全树按 tag 收集 id
scene.setNodeLayer("ball", 3)
scene.getNodeLayer("ball")
```

声明式构建同样支持：`node("x").withTag("enemy").withLayer(2)`，reconcile patch 会同步 tag/layer。

### 节点生命周期事件

注册一个脚本回调，接收节点增删/移动/变更事件（`node_added` / `node_removed` / `node_moved` / `node_changed`）：

```squirrel
scene.setNodeEventHandler(function(action, nodeId, parentId) {
    // action: "node_added" | "node_removed" | "node_moved" | "node_changed"
})
```

`SceneComponent` 提供 `onMount()`（首次挂载调用一次）。默认 `eve_update` 已调用 `scene.updateTransformsAll()`；API 修改节点后只需增量同步（脏子树裁剪），全净树零开销。

### 错误语义

新增的 getter（`getNodePosition` 等）在节点不存在时抛 Squirrel 异常；可先用 `scene.hasNode(nodeId)` 守卫。setter 保持返回 bool。

### 节点 AABB、拾取与视锥裁剪

节点可设置 local 空间 AABB（拾取/裁剪/spatial 索引的基础；`Mesh` 不保留 CPU 顶点，故不做自动计算）：

```squirrel
scene.setNodeBounds("soldier", -1, -1, -1, 1, 1, 1)
scene.hasNodeBounds("soldier")
scene.getNodeBounds("soldier")             // [minX,minY,minZ,maxX,maxY,maxZ]

scene.pickRay(ox, oy, oz, dx, dy, dz)      // 最近命中节点 id（"" 未命中）
scene.pickScreen(cam, screenX, screenY, viewW, viewH)
scene.collectFrustumIds(cam, viewW, viewH) // 与相机视锥相交的节点 id 列表
```

### 场景序列化与空间索引

```squirrel
local json = scene.serializeHost()          // 当前 host → JSON 字符串
scene.deserializeHostAt("level2", json)     // JSON → 新 host（含 TRS/tags/layer/bounds）

local octree = spatial.newOctree(-500, -500, -500, 500, 500, 500)
scene.syncSpatialIndex(octree)              // 有 bounds 的节点写入八叉树（id = arena 索引）
octree.querySphere(cx, cy, cz, r)
for (local i = 0; i < octree.getResultCount(); ++i) {
    local nodeId = scene.nodeIdFromSpatialId(octree.getResultId(i))
}
```

声明式构建同样支持 bounds：`node("x").withBounds(-1,-1,-1,1,1,1)`，reconcile 与序列化均保留。

`SceneLoader` 导入的模型会自动填充 AABB：网格子节点取其 Assimp 网格的精确 local AABB，祖先节点取子节点并集（含变换），热重载后保留。导入模型无需手动 `setNodeBounds` 即可参与拾取、视锥裁剪与 spatial 索引。

## 常见问题

- `beginNode()` 与 `end()` 不配对导致 build 不完整。
- 修改 local transform 后不更新 transforms。
- 混淆节点 name、id 和 path：运行时修改优先用稳定 id。
- `getNodeRef()` 基于当前 selected host；跨 host 请用 `getNodeRefAt(hostName, nodeId)`。
- 脚本 ECS 的 `eve.view()` 返回缓存数组：只读遍历，不要在遍历中 push/pop。
- `unlinkNode(nodeId)` 现在移除该节点全部 link；只拆一类请用 `unlinkNodeKind(nodeId, kind)`。
- mount / rebuild 会校验节点 id 唯一性，重复 id 直接报错（含脚本 builder）。
- 被销毁的渲染/物理目标会在下一次变换更新时自动从节点上摘除 link（防悬垂指针）。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `addNode()`、`beginBuild()`、`beginGroup()`、`beginNode()`、`bindOwner()`、`collectChildIds()`、`collectIds()`、`collectIdsByName()`
- `collectIdsFrom()`、`collectIdsVisible()`、`end()`、`findIdByName()`、`findIdByPath()`、`getChildCount()`、`getChildIdAt()`、`getName()`
- `attachEntity()`、`attachEntityAt()`、`detachEntity()`、`detachEntityAt()`、`getEntity()`、`getEntityAt()`、`hasEntity()`、`hasEntityAt()`、`entitiesOf()`、`entitiesOfAt()`、`update()`、`currentHostName()`、`getNodeRef()`、`getNodeRefAt()`、`getNodeRefByPath()`
- `linkRenderable2D()`、`linkRenderable3D()`、`linkPhysics2D()`、`linkPhysics3D()`、`linkCamera3D()`、`linkAudio3D()`、`unlinkNodeKind()`、`linkCount()`（及 host-scoped `*At` 变体）
- `getNodePosition()`、`getNodeRotation()`、`getNodeScale()`、`getNodeVisible()`、`getNodeWorldPosition()`、`getNodeWorldRotation()`、`getNodeWorldScale()`、`localToWorld()`、`worldToLocal()`、`setNodeParent()`、`removeNode()`、`addNodeChild()`、`removeNodeChild()`、`setNodeQuaternion()`、`getNodeQuaternion()`、`setNodeLookAt()`、`addNodeTag()`、`removeNodeTag()`、`hasNodeTag()`、`getNodeTags()`、`collectIdsByTag()`、`setNodeLayer()`、`getNodeLayer()`（及 host-scoped `*At` 变体）
- `setNodeBounds()`、`hasNodeBounds()`、`getNodeBounds()`、`serializeHost()`、`deserializeHost()`、`pickRay()`、`pickScreen()`、`collectFrustumIds()`、`syncSpatialIndex()`、`nodeIdFromSpatialId()`（及 host-scoped `*At` 变体）
- `getNodeCount()`、`getNodePath()`、`getParentId()`、`getRootId()`、`hasNode()`、`isAncestor()`、`isDescendant()`、`linkRenderable2D()`
- `linkRenderable3D()`、`mountBuild()`、`mountBuildAs()`、`remountBuildAs()`、`select()`、`setBuildPosition()`、`setBuildRotation()`、`setBuildScale()`
- `setBuildSpace()`、`setBuildVisible()`、`setHostLayer()`、`setHostVisible()`、`setNodePosition()`、`setNodeRotation()`、`setNodeScale()`、`setNodeVisible()`
- `unlinkNode()`、`updateTransforms()`、`updateTransformsAll()`、`walkBreadthFirstIds()`、`walkDepthFirstIds()`

辅助对象：

- `eve.SceneEntity`：`scene()` / `node()` / `onAttach()` / `onDetach()` / `update(dt)`，组件槽字段与 `eve.Entity` 一致。
- `eve.SceneNodeRef`（节点句柄）：`getNodeId()`、`getPersistentId()`、`getHostName()`、`isValid()`、`getScene()`、`setPosition()` / `getPosition()`、`setRotation()` / `getRotation()`、`setScale()` / `getScale()`、`setVisible()` / `isVisible()`、`getWorldPosition*()`、`getWorldMatrix()`、`getForward()/getRight()/getUp()`、`getParentId()`、`getChildCount()`、`getChildIdAt()`、`getPath()`、`attachEntity()`、`detachEntity()`、`getEntity()`、`hasEntity()`、`entitiesOf()`、`linkRenderable2D/3D()`、`linkPhysics2D/3D()`、`linkCamera3D()`、`linkAudio3D()`、`unlinkNode()`、`unlinkNodeKind()`、`linkCount()`、`localToWorld()`、`worldToLocal()`、`setParent()`、`removeNode()`、`setQuaternion()`、`getQuaternion()`、`lookAt()`、`addTag()`、`removeTag()`、`hasTag()`、`getTags()`、`setLayer()`、`getLayer()`、`setBounds()`、`hasBounds()`、`getBounds()`。

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/scene/`](../../../src/modules/scene/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `scene`。
