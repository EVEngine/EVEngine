# 声明式场景树模块

## AI 原生作者文档

`eve.SceneEditorModule().createAuthoringSession(id)` 返回拥有 target 的标准 Result。
作者文档保存层级、资源配方、镜头、光照和关系；`session.execute/undo/redo/saveJson/restoreJson`
共用正式事务历史。原有 createSession/createLiveSession 保持其层级契约。
`inspectAuthoringSnapshot(snapshot,id,[minX,minY,minZ,maxX,maxY,maxZ])` 返回完整世界矩阵、
八角点、世界 AABB、尺寸和底部中心；不变更数据、不保留输入引用。
旋转单位为弧度，FOV 为度，bounds 为未变形本地空间。缺失或非法数据返回结构化错误。
ground/align/screen 的规划代价为每次编辑的祖先遍历和八角点变换；不在每帧重复查询。
文档及脚本示例见 [AI 场景搭建契约](../../dev/2026-10-11-ai-scene-authoring.md)。

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
scene.collectIdsWith(Bouncer)                     // 当前 host 上挂了该类的节点 id
scene.detachEntity("player", e)                   // 立刻卸载（触发 onDetach + destroy）
// update(dt) 内请用延迟队列，帧末统一卸载：
//   scene.scheduleDetachEntity("player", e)
//   scene.scheduleDetachEntityAt(hostName, "player", e)

e.disable()                                       // 跳过 update；触发 onDisable
e.enable()                                        // 恢复；触发 onEnable
e.isEnabled()                                     // true/false

local ref = scene.getNodeRef("player")            // 节点句柄
local p = ref.getPosition()                       // [x, y, z]
ref.getWorldPosition()                            // world 位置（需先 updateTransforms/update）
ref.setPosition(0.0, 0.0, 0.0)
ref.scheduleDetachEntity(e)                       // 等价于 scene.scheduleDetachEntityAt(...)
```

生命周期：reconcile / 全量 rebuild 按节点 id 保留绑定；节点被移除时自动调用 `onDetach()` 并销毁实例。同一节点重复 attach 同 class 会先卸载旧实例。`update(dt)` 中同步 `detachEntity` 不安全——请用 `scheduleDetachEntity` / `scheduleDetachEntityAt`，它们在当次 `scene.update` 的全部脚本 `update` 结束后再卸载。

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

#### 脚本 link kind：让自己的对象跟随节点

上面几类都由引擎实现；`linkScriptAt()` 让你把**自己的脚本对象**挂进同一条变换管线，不需要写 C++：

```squirrel
class Follower {
    function onNodeTransform(nodeId, x, y, z, yaw, pitch, roll, sx, sy, sz) {
        // syncMode "node"：节点每次脏了就被调用
    }
    function nodeTransform() {
        // syncMode "body"：返回 [x, y, z, yaw, pitch, roll]（local），节点被移动过去
        return [0.0, 1.5, 0.0, 0.0, 0.0, 0.0]
    }
}

local follower = Follower()
eve.provider.publish("my.follower", follower)          // 先发布，再 link
scene.linkScriptAt("host", "soldier", "my.follower", "node")   // 或 "body"
```

- **`mode`**：`"node"` = 节点驱动脚本（对每个脏节点调用 `onNodeTransform`）；`"body"` = 脚本驱动节点（在 world 传播前调用 `nodeTransform()`，节点被移到它返回的 local 变换）。只有对应模式的方法会被调用，两个都写也可以。
- **provider 必须先发布**：`linkScriptAt` 解析的是 `eve.provider` 里的**具名提供方**，并记录它的 generation，所以退役后旧链接会被报成"死链"，而不会悄悄接到同名的新对象上。
- **跨 host 用 `linkScriptAt(hostName, …)`**：它本身就带 host 参数（selected host 也走这个签名）。
- **失败语义**：host 不存在、provider 未发布、节点不存在都返回 `Result` 表（`ok=false` + `code`），不抛异常。

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

> **陷阱**：在 `eve.SceneComponent` 的子类方法里，请写 `this.scene()`（等同 `scene()`），
> **不要**裸写 `scene.beginBuild()`。裸名 `scene` 在子类里解析到的是 `scene()` 这个
> **访问器方法本身**，而不是它的返回值，于是报 `the index 'beginBuild' does not exist`，
> 错误信息指不到真正原因。访问器本身已经优先返回你设的全局槽位（`::scene`），所以
> `this.scene()` 与你自己的全局 `scene` 语义一致——只有"不带调用"的裸名会踩到。

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

## 绑定与行为说明

- `linkRenderable2D()`、`linkRenderable3D()`、`linkPhysics2D()`、`linkPhysics3D()`、`linkCamera3D()`、`linkAudio3D()`、`unlinkNodeKind()`、`linkCount()`（及 host-scoped `*At` 变体）
- `getNodePosition()`、`getNodeRotation()`、`getNodeScale()`、`getNodeVisible()`、`getNodeWorldPosition()`、`getNodeWorldRotation()`、`getNodeWorldScale()`、`localToWorld()`、`worldToLocal()`、`setNodeParent()`、`removeNode()`、`addNodeChild()`、`removeNodeChild()`、`setNodeQuaternion()`、`getNodeQuaternion()`、`setNodeLookAt()`、`addNodeTag()`、`removeNodeTag()`、`hasNodeTag()`、`getNodeTags()`、`collectIdsByTag()`、`setNodeLayer()`、`getNodeLayer()`（及 host-scoped `*At` 变体）
- `setNodeBounds()`、`hasNodeBounds()`、`getNodeBounds()`、`serializeHost()`、`deserializeHost()`、`pickRay()`、`pickScreen()`、`collectFrustumIds()`、`syncSpatialIndex()`、`nodeIdFromSpatialId()`（及 host-scoped `*At` 变体）

Pcg `Growth` 由 `eve.PcgGrowth()` 承接。`configure(startScale,endScale,variance,growthTime)` 配置范围，`start(realtime,seed)` 选择确定性的实际终点；`advance(realtime)` 返回统一缩放，`applyPcgGrowth(growth,node,realtime)` 将其应用到真实 `SceneNodeRef`，并在 `die(realtime)` 后五秒删除节点。状态可用 `getActualEndScale()`、`getScale()`、`getFinished()`、`shouldDestroy(realtime)` 查询。

辅助对象：

- `eve.SceneEntity`：`scene()` / `node()` / `onAttach()` / `onDetach()` / `onEnable()` / `onDisable()` / `enable()` / `disable()` / `setEnabled()` / `isEnabled()` / `update(dt)`，组件槽字段与 `eve.Entity` 一致。
- `eve.SceneNodeRef`（节点句柄）：`getNodeId()`、`getPersistentId()`、`getHostName()`、`isValid()`、`getScene()`、`setPosition()` / `getPosition()`、`setRotation()` / `getRotation()`、`setScale()` / `getScale()`、`setVisible()` / `isVisible()`、`getWorldPositionX()`、`getWorldPositionY()`、`getWorldPositionZ()`、`getWorldMatrix()`、`getForward()/getRight()/getUp()`、`getParentId()`、`getChildCount()`、`getChildIdAt()`、`getPath()`、`attachEntity()`、`detachEntity()`、`scheduleDetachEntity()`、`getEntity()`、`hasEntity()`、`entitiesOf()`、`linkRenderable2D/3D()`、`linkPhysics2D/3D()`、`linkCamera3D()`、`linkAudio3D()`、`unlinkNode()`、`unlinkNodeKind()`、`linkCount()`、`localToWorld()`、`worldToLocal()`、`setParent()`、`removeNode()`、`setQuaternion()`、`getQuaternion()`、`lookAt()`、`addTag()`、`removeTag()`、`hasTag()`、`getTags()`、`setLayer()`、`getLayer()`、`setBounds()`、`hasBounds()`、`getBounds()`。

## 跟随玩家的环境对象

`FollowPlayerSettings`、`FollowPlayerInput`、`FollowPlayerOutput` 与
`evaluateFollowPlayer(output,settings,input)` 对应 Pcg `FollowPlayerSystem`。在 LateUpdate 阶段填入玩家世界
坐标，将 `applyPosition=true` 的输出位置应用到每个调用者拥有的粒子、水体或场景节点。关闭 offset 时完全
复制玩家坐标；普通 offset 使用 `(playerX+offsetX, playerY-offsetY, playerZ-offsetZ)`；water 模式在玩家
Y<1 时额外抬高 70，否则抬高 10，再减 offsetY。`useScale` 独立发布一次性 scale 写入语义。

计算层不查找全局 Camera、不持有对象列表，也不直接依赖 graphics、water 或 particles provider；调用者负责
稳定对象身份和实际变换写入。缺少玩家或关闭 follow 时不发布位置，缩放仍可发布。所有输入先验证有限性，
失败不会部分覆盖 output。

脚本字段：settings 提供 `followPlayer`、`waterObject`、`useOffset`、`offsetX/Y/Z`、`useScale`、
`scaleX/scaleY/scaleZ`；input 提供 `hasPlayer`、`playerX/Y/Z`；output 提供 `applyPosition`、`x/y/z`、
`applyScale` 与 `scaleX/scaleY/scaleZ`。

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。

**源码：** [`src/modules/scene/`](../../../src/modules/scene/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `scene`。
### Pcg 位置与书签

`LocationProfile` 保存一次性的启动位置，并管理 Pcg `LocationSystem` 风格的命名书签。
`LocationPose` 以位置和四元数保存相机/玩家姿态；`LocationBookmark` 还保存控制器与场景名。
书签暴露 `name`、`controller`、`scene` 字段，以及 `setCamera`、`setPlayer`、`clearPlayer`。
脚本可调用 `saveLocation`、`saveLocationWithPlayer`、`loadLocation`、`addBookmark`、
`overrideBookmark`、`removeBookmark`、`loadBookmark`、`previousBookmark`、`nextBookmark`、
`clearBookmarks`、`getBookmarkCount`、`getBookmarkName`、`getBookmarkController`、
`getBookmarkScene` 和 `hasSavedLocation`。修改与读取操作返回标准 `Result`；加载启动位置只成功一次。
`serializeJson` 与 `restoreJson` 使用 `eve.scene.location-profile` schema version 1；恢复会拒绝未知字段，
并在任何解析或验证失败时保留原状态。
### PcgScenePlayer 地形视锥裁剪

`applyPcgTerrainCullingAt(host,camera,viewWidth,viewHeight,tag)` 检查指定 host 内带 tag 且具有 bounds 的
节点，并把视锥相交结果原子应用到节点 visibility；返回实际变化数量。当前 host 的简写为
`applyPcgTerrainCulling(camera,viewWidth,viewHeight,tag)`。未标记节点不受影响，场景加载器或 streaming
层可在组成变化后重新调用，替代 Pcg 在 sceneLoaded/sceneUnloaded 后 0.5 秒重新扫描 Unity Terrain 的路径。

### PcgBuildConfig

`eve.PcgBuildConfig()` 对应 Pcg ResourcesSystem 的 `BuildConfig`。发布类型为 Addressables=0、RegularBuild=1；`addHistory(category,sceneName,timestamp)` 按调用顺序记录七类构建事件，category 为 0..6。使用 `getHistoryCount/Category/SceneName/Timestamp` 查询，越界或非法枚举返回结构化错误。

脚本方法：`setPublicationType()`、`getPublicationType()`、`addHistory()`、`clearHistory()`、`getHistoryCount()`、`getHistoryCategory()`、`getHistorySceneName()`、`getHistoryTimestamp()`。

### 组件修改自动合并

C++ `SceneComponent::markDirty()` / `setState()` 自动排队，在 `TransformSystem::updateAll` 开始 ECS View 前刷新；脚本组件在引擎每帧的绘制准备阶段自动刷新，不依赖游戏保留默认 eve_update。无需每帧主动 `rebuild()`。构建期间的新修改留到下一批，失败保留脏状态与未处理队列。

组件仅保存 host 的 ECS generation handle；host 先销毁时解析失败、排队更新跳过，组件先销毁时弱注册失效。重新 attach 后重新标脏。Getter 不构建树；兼容 `rebuild(forceFull)` 保留显式同步刷新语义。组件不可复制或移动，不允许在自己的 build 回调中销毁自己。

## 补充绑定参考

参数与接收者见 [源码生成参考](scene/api-reference.md)，使用前先确认本章的前置条件与生命周期。

## 集成任务：把场景树与玩法状态组合

先 build/mount SceneHost，再挂 SceneEntity/Renderable link；update 前准备输入，系统修改 local TRS，按 scene.update/updateTransforms 的约定完成 world 变换同步。不要把节点路径当持久实体 ID，也不要让 ECS 和物理世界各自写一套位置。物理拥有运动时由物理结果更新节点；纯场景拥有运动时只设节点变换。销毁时解除挂载实体和 link，再清 host；恢复后重新建立派生 link，不能复用旧指针。

完整项目：[examples/scene-ecs](../../../examples/scene-ecs/README.md)，
其 [main.nut](../../../examples/scene-ecs/main.nut) 展示实际生命周期。
先验证现有资产可加载，再换成自己的资源。测试失败、取消、资源缺失和旧 revision，
不能只以脚本没有语法错误作为渲染或玩法正确的证据。
### 关系约束摆放

作者文档 v2 支持 `supported_on`（指定对象接触支撑）、`nonpenetrating`（对象对防穿插）、
`visible_fraction`（subject 可见比例）和 `occludes`（subject 实际挡住 reference 的比例）。
视觉关系用 `minimum` / `maximum` 定义比例区间，minimum 必填，maximum 默认 1。
`scene.authoring.relations.set.v1` 设置关系数组；删除叶节点与相关关系一次撤销恢复。
v1 文档显式迁移为空关系。关系不改变 transform parent，不每帧自动修复。

`SceneEditorSession.beginRelationPlacement` 复用已有私有物理预览，一个对象可调整，锁定
环境保留为障碍。`checkRelationPlacement` 返回逐关系接触/穿透/法线证据和候选世界变换。
硬约束不满足或不可测量时，现有 `commitPhysicsPlacement` 拒绝提交；取消不改变场景。
物理证据为碰撞代理精度；视觉证据为实际不透明三角形像素/深度测量。全部视觉约束都会检查，
提交重新测量并验证文档/投影/网格代次；一个可调整叶节点，尚无区域 mask 或多对象联合搜索。
原生视觉准备接口是 `prepareRelationVisibility(host, request)`；Director 的 relation_begin 自动调用。
提供者缺失、深度歧义及不可测量的硬关系拒绝提交。详见
[视觉关系实现与验证](../../dev/2026-10-11-scene-visual-relations-validation.md)。

MCP `eve_scene_modify` 的 `relations`、`relation_begin/update/check/commit/discard` 动作及
示例见 [关系摆放实现与验证](../../dev/2026-10-11-scene-relations-validation.md)。

### 对象区域遮挡

作者文档 v3 支持视觉关系中的对象局部 local_box 区域；v1/v2 自动迁移。visible_fraction
测 subject.region，occludes 测 reference.region。MCP relations 的 region 省略 geometryRevision
时会读取原生版本绑定，已有旧版本必须显式重新绑定。geometry_revision 动作查询版本。
区域随对象变换，真实三角形像素/深度决定分母；整对象结果不能替代区域结果。
参见 [区域接口与验证](../../dev/2026-10-11-scene-regions-validation.md)。

## 投影轮廓重叠

作者文档 v4 支持 projected_overlap(subject, reference, minimum, maximum)，可附 reference 的 local_box region。测量两个对象独立真实投影 mask 的交集/reference 像素数；前后顺序及第三者遮挡不改变该构图指标。visibility-evidence v3 的 projectedOverlaps 返回交集像素、双方像素数及比例；任一 mask 少于 4 像素不可测。关系通过原有 relations/relation_begin/relation_check/relation_commit 正式路径使用，提交重新测量并一次撤销。详见 [契约与验证](../../dev/2026-10-12-scene-projected-overlap-validation.md)。
