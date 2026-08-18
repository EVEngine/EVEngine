# 场景节点 ↔ 脚本 ECS 打通设计 — `scene`

日期：2026-08-17  
状态：设计定稿，P0 已实现并通过构建+测试验证（2026-08-17，win32 Debug / MSVC 19.51）  
存放：`docs/dev/`

## 背景与结论

EVEngine 存在两套平行对象模型：

| 对象模型 | 载体 | 现状 |
|----------|------|------|
| 场景树 | `SceneHost`（已是 `ecs::Entity`）+ `SceneNode`（纯数据 arena） | TransformSystem / reconcile / renderable link 全部围绕纯数据节点 |
| 脚本 ECS | `eve.Component` / `eve.Entity` / `eve.System` / `eve.view`（Squirrel 注册表 `_ecsTypes`） | 游戏逻辑实体与场景树完全解耦 |

`bindOwner` 目前只把 **host** 绑定到一个 ownerId（UI 同款），无法表达"节点挂了哪些脚本组件"。

**结论**：引入懒创建的 C++ ECS 实体 **`SceneObject`** 作为节点的 ECS 身份，脚本侧提供 **`eve.SceneEntity`**（`extends eve.Entity`，复用现有脚本 ECS 全部机制），由 `Scene` 模块管理 attach / detach / update 生命周期，并新增 **`eve.SceneNodeRef`** 节点句柄供行为脚本读写变换。

该设计一次把"节点 ECS 身份"建好，后续物理/动画/相机/标签等系统联动全部变成"往 `SceneObject` 上加 C++ 组件 + 对应 link API"，不再动 `SceneNode` 布局，避免反复返工。

## 目标

1. 每个"挂载了脚本"的 `SceneNode` 拥有一个 C++ ECS 实体身份（`SceneObject`），为未来 C++ 组件（Tags / PhysicsLink / AnimLink / LightLink…）提供挂载点。
2. 脚本可为节点 attach 一个 `eve.SceneEntity` 子类实例，声明式组件槽、`eve.view(cls)` 查询、`eve.System` 批量驱动全部沿用现有脚本 ECS，零重复实现。
3. 完整生命周期：`attachEntity` / `detachEntity`、`onAttach()` / `onDetach()` / `update(dt)`；reconcile 按 id 保留绑定，节点移除自动销毁。
4. `eve.SceneNodeRef` 提供节点句柄：local/world 变换读写、父子/路径查询、实体组件查询，行为脚本不再依赖"selected host"全局状态。
5. 为后续改进（节点事件、物理/动画 link、序列化、轻量实例化）预留明确扩展点。

## 非目标（一期）

- 不把 `SceneNode` 本身改造成 `ecs::Entity`：保留 arena 布局与现有 tree/reconcile 语义（全量改造风险高、收益低）。
- 不做"C++ 实体即脚本实体"的桥接继承：脚本实体保持纯 Squirrel（`extends eve.Entity`）。若让脚本类 extends C++ 注册类，`eve.view(子类)` 会退化为按 C++ 类型包装、丢失子类字段与方法，view 语义会坏。
- 不做 `enable/disable`（`onEnable/onDisable`）、`onTransformChanged` 事件、update 中延迟销毁队列（列入 P1）。
- 不做 Prefab / 实例化（`NodeDesc` 可复用，但实例化语义另立设计）。

## 核心抽象

```text
SceneNode（arena，保持纯数据）
  ├─ 现有：id / key / name / space / TRS / visible / linkKind+linkTarget / children
  └─ 新增：uint32_t objectId = 0     // 懒创建 SceneObject 的 ECS id；0 = 未创建

SceneObject : public ecs::Entity      // 新文件 SceneObject.h/.cpp —— 节点 ECS 身份
  ├─ Meta          { entity*, hostName, nodeId }
  ├─ ScriptBindings{ std::vector<HSQOBJECT> instances }   // 根引用的脚本实体
  └─ 未来组件扩展点：Tags / PhysicsLink / AnimLink / LightLink / …

eve.SceneEntity <- class extends eve.Entity    // 脚本基类（post-ECS 注入）
  ├─ 新增字段：_scene（eve.Scene 实例）、hostName、nodeId
  ├─ 方法：scene() -> eve.Scene、node() -> eve.SceneNodeRef
  └─ 生命周期钩子：onAttach() / onDetach() / update(dt)，默认空实现
     （组件槽字段沿用 eve.Entity.create() 的 _ecsCollectSlots，天然进 _ecsTypes）

SceneNodeRef（C++ 绑定类，脚本名 eve.SceneNodeRef）
  ├─ 持有 hostName + nodeId 字符串（不持有节点指针，跨 rebuild 安全）
  └─ local/world 变换、父子/路径、实体 attach/detach 转发
```

### 为什么是"懒创建 SceneObject"

- 纯 C++ 场景（如 `SceneLoader` 导入的模型树）零额外开销：只有脚本 attach 时才创建实体。
- `SceneNode` 不依赖 ECS 句柄生命周期：arena 重建/移动不影响 objectId（存 ECS id）。
- 与 `SceneHost` 已是 ECS 实体的既有模式一致。

## 脚本 API 设计

### `Scene` 模块新增方法

| 方法 | 签名 | 说明 |
|------|------|------|
| `attachEntity` | `(nodeId, cls) -> instance` | 在节点上挂一个 `cls`（extends `eve.SceneEntity`）实例；同节点同 class 已存在则先自动 detach 旧的（Unity 语义）。返回实例 |
| `detachEntity` | `(nodeId, instance) -> bool` | 按实例卸载；触发 `onDetach()`、脚本侧 `destroy()`、释放 C++ 根引用 |
| `getEntity` | `(nodeId, cls) -> instance|null` | 按类查找（`eve.isSubclass` 匹配，含子类） |
| `hasEntity` | `(nodeId, cls) -> bool` | 是否挂了该类 |
| `entitiesOf` | `(nodeId) -> array` | 节点全部脚本实体 |
| `update` | `(dt)` | 先同步 transforms（P0 全量，P1 增量），再对全部已挂脚本实体调用 `update(dt)` |
| `getNodeRef` | `(nodeId) -> SceneNodeRef` | 当前 selected host 下的节点句柄（hostName 为空则每次解析到 selected host） |
| `getNodeRef` | `(hostName, nodeId) -> SceneNodeRef` | 指定 host 的节点句柄（`SceneEntity.node()` 走这条，跨 host 安全） |
| `getNodeRefByPath` | `(path) -> SceneNodeRef` | 路径版 |
| `collectIdsWith` | `(cls) -> array` | P1：遍历树返回挂了 `cls` 的节点 id（P0 用 `eve.view(cls)` 读 `.nodeId` 即可，无需 C++ 支持） |

### `eve.SceneNodeRef` 方法

| 分组 | 方法 |
|------|------|
| local 变换 | `getPositionX/Y/Z`、`getPosition() -> {x,y,z}`、`setPosition(x,y,z)`、`getRotationYaw/Pitch/Roll`、`getRotation()`、`setRotation(yaw,pitch,roll)`、`getScaleX/Y/Z`、`setScale(sx,sy,sz)` |
| world 变换 | `getWorldPositionX/Y/Z`、`getWorldPosition()`、`getWorldMatrix()`（16 个 float 的数组）、`getForward/Right/Up`（返回 `{x,y,z}`） |
| 可见性 | `isVisible()` / `setVisible(bool)` |
| 结构 | `getParentId()`、`getChildCount()`、`getChildIdAt(i)`、`getPath()`、`getNodeId()`、`getHostName()` |
| 实体 | `getEntity(cls)`、`hasEntity(cls)`、`entitiesOf()`、`attachEntity(cls)`、`detachEntity(instance)` |

变换 getter 采用引擎既有惯例（Camera2D 的 `getX/getY`、Light3D 的 `getX/getY/getZ` 都是标量 getter），`getPosition()` 表返回为 gameplay 便利，不引入跨模块 `Vec3` 链接依赖。

### 脚本示例

```squirrel
class MoveComp extends eve.Component { speed = 1.0 }

class EnemyAI extends eve.SceneEntity {
    move = MoveComp

    function onAttach() {
        node().setPosition(0.0, 0.0, 0.0)
    }

    function update(dt) {
        local p = node().getPosition()
        node().setPosition(p.x + move.speed * dt, p.y, p.z)
    }
}

local e = scene.attachEntity("enemy", EnemyAI)   // 返回 EnemyAI 实例
print(eve.view(EnemyAI).len())                    // 1 —— 天然进脚本 ECS 注册表
```

## 生命周期与所有权

### attach 链

```text
scene.attachEntity(nodeId, cls)
  ├─ 确保 SceneNode 存在（失败返回 null）
  ├─ 已有同 class 实例 → 先 detach
  ├─ 懒创建 SceneObject：SceneObject::create()，写 Meta{hostName,nodeId}，node.objectId = id
  ├─ cls.create()：脚本侧 _ecsCollectSlots / 实例化组件槽 / 注册进 _ecsTypes（view 可见）
  ├─ 写字段：_scene / hostName / nodeId
  ├─ HSQOBJECT 入 ScriptBindings（sq_addref 根引用）
  └─ 调用脚本 onAttach()
```

### detach 链

```text
scene.detachEntity(nodeId, instance)
  ├─ 找到节点 → 在 ScriptBindings 中定位该实例
  ├─ 调用脚本 onDetach()（sq_call，失败恢复栈并继续）
  ├─ 调用脚本 destroy()（_ecsTypes 移除，view 立即不可见）
  ├─ sq_release 释放根引用
  └─ ScriptBindings 空且无其它组件 → 销毁 SceneObject，node.objectId = 0
```

### reconcile / rebuild / 移除

- `applyTreeReconcile` patch 命中（节点保留）→ 绑定原样保留；`patchProps` 修改 id 时同步更新 `SceneObject.Meta.nodeId` 与脚本实例 `nodeId` 字段。
- `applyTree` 全量重建 → 按 id 保留绑定（与现有 renderable link 保留机制同一条路径）；被移除的节点递归执行 detach 链。
- host 销毁 → 整树 detach。
- `SceneLoader` 热重载走的是 `applyTree` 同款路径：挂载节点上的脚本实体在 hot-reload 后仍有效。

### update 时序

```text
eve_update(dt) { scene.update(dt) }     // 引擎约定，与 eve.System.update 一致
  ├─ 1. TransformSystem::updateAll()（P0 全量；P1 改为按 dirty 增量）
  └─ 2. 迭代 ecs::View<SceneObject, Meta, ScriptBindings>，对每个脚本实例：
         有 update 方法则 sq_call(instance.update, dt)；异常恢复栈、跳过该实例继续
```

### 查找与安全

- P0：`SceneObject` 按 `Meta.nodeId + hostName` 用 `ecs::View` 线性查找（节点数量级小，可接受）。
- P1：全局 `hostName/nodeId → SceneObject*` 哈希 + ECS generation 校验，消灭线性查找与句柄失效问题。
- HSQOBJECT 根引用只允许主线程读写（attach/detach/update 都在脚本调用栈内，VM 存活）。
- 重复 attach 同 class：先 detach（Unity 同型组件唯一语义）。
- 脚本异常：`sq_call` 失败必须 `sq_settop` 恢复栈并继续，不得让引擎崩溃。

## 与现有机制的关系

| 机制 | 关系 |
|------|------|
| `eve.view(cls)` / `eve.System` | `SceneEntity` 走 `_ecsTypes` 注册，天然可查、可驱动，无需 C++ 桥接 |
| `registerCppEntityView` | 不注册 `SceneObject` 的 script view（避免与脚本注册表双路径重复返回） |
| `TransformSystem` | 不改；NodeRef 的 world getter 依赖 world 矩阵已更新（`update`/`updateTransforms` 约定） |
| `bindOwner` | 保留（host ↔ ownerId）；`SceneObject.Meta` 提供更细的 node ↔ 实体绑定 |
| `SceneComponent`（声明式 builder） | 命名不冲突，职责分离：`SceneComponent` 负责建树，`SceneEntity` 负责节点上的运行逻辑 |
| `SceneLoader` | 无需改动；导入节点同样可 attach（绑定按 id 保留） |

## 引擎级小改动

`eve.SceneEntity` 必须 `extends eve.Entity`，而 `eve.Entity` 由 `exposeECS` 在所有模块 expose **之后**注入（`ModuleManager::exposeVM` 末尾）。因此：

- `src/engine/common/ECS.h/.cpp` 增加 `registerPostEcsHook(std::function<void(ssq::Table&)>)`，在 `exposeECS` 末尾（`injectEcsScript` 之后）执行已注册钩子。
- `Scene::expose` 注册一个钩子注入 `eve.SceneEntity` 基类脚本（编译方式同 `injectSceneComponentClass`）。
- 该钩子机制通用，未来其它模块需要 extends 脚本 ECS 基类的注入都可复用。

## 风险与缓解

| 风险 | 缓解 |
|------|------|
| 脚本 GC 提前回收实体 | C++ `sq_addref` 根引用持有，detach 才释放 |
| `eve.view` 返回已 detach 实体 | detach 立即调用 `destroy()`（`_ecsTypes` 移除）+ 释放根引用 |
| update 中节点被移除 / 实体被 detach | P1 引入延迟销毁队列（统一帧末执行） |
| ECS id 重用 | P1 用 `ecs::handle_of + try_get`（generation 校验）替代裸 id |
| 全量 rebuild 丢失绑定 | 复用 applyTree 的按 id 保留路径（与 renderable link 同机制） |
| 跨模块职责蔓延 | 一期只动 scene + ECS 钩子，不碰 TransformSystem / SceneLoader 主体 |

## 分期落地

### P0 —— 本次架构落地（设计确认后实施）

- [x] `src/modules/scene/SceneObject.h/.cpp`：新 C++ ECS 实体（Meta + ScriptBindings）。
- [x] `SceneHost.h`：`SceneNode` 增加 `uint32_t objectId`。
- [x] `src/modules/scene/SceneNodeRef.h/.cpp`：节点句柄类 + 变换 getter/setter。
- [x] `Scene.cpp`：原生原语 `rootEntity / unrootEntityAt / forEachEntity / updateScripts / getNodeRefAt*` + expose `eve.SceneNodeRef`。
- [x] `ECS.h/.cpp`：post-ECS 钩子 + `_cppHasView`；`Scene::expose` 注入 `eve.SceneEntity`。
- [x] `NodeDesc.cpp`：applyTree/applyTreeReconcile 的 objectId 保留；孤儿绑定由 `Scene::pruneOrphanObjects` 拆除。
- [x] `test/scene.cpp`：objectId 保留（C++）+ 脚本生命周期/view 集成；`test/ScriptECS.cpp`：view 缓存、O(1) 销毁、默认值缓存回归。
- [x] 示例 `examples/scene-ecs/main.nut` + 更新 `docs/usr/modules/scene.md` 与 `docs/dev/模块设计.md` 清单。

### 实现取舍（相对设计稿）

- 面向脚本的完整 API（`attachEntity / detachEntity / getEntity / hasEntity / entitiesOf / update / getNodeRef`）以**注入的脚本包装方法**实现（通过 Squirrel 运行时给原生类添加成员），C++ 只暴露类型简单的原生原语——避免成员函数绑定 `ssq::Object` 返回值的不确定性。
- `SceneNodeRef` 的实体转发通过原生 `getScene()` 拿回 `eve.Scene` 实例再走包装方法。
- `attachEntity` 返回实例由脚本包装层完成（`cls.create()` → 设字段 → `rootEntity` → `onAttach`）。
- 脚本 ECS 性能优化随本设计一并落地：view 缓存、O(1) swap-remove、槽/组件默认值缓存、`_cppHasView` 门控。

### P1

- [x] 节点事件：`onTransformChanged` / `onParentChanged`（实现为 `node_changed` / `node_moved` / `node_added` / `node_removed`，C++ + 脚本回调）。
- [x] `hostName/nodeId → SceneObject` 哈希：`findById` 惰性 id→index 索引落地（O(1)）。
- [x] TransformSystem 增量 dirty（只重算脏子树、只同步受影响 link；全净树零开销）。
- [x] 生命周期钩子：`SceneComponent.onMount`（C++ + 脚本）。
- [ ] `collectIdsWith(cls)`；`enable/disable` + `onEnable/onDisable`。
- 延迟销毁队列（update 中安全 detach）。

### P2

- [x] 通用 link 系统（2026-08-17）：`renderable2d/3d`、`physics2d/3d`（含 node↔body 双向模式）、`camera3d`、`audio3d`；多 link 共存、reconcile 按 id 保留、`unlinkNodeKind` 定向拆除、NodeRef 转发。**实现取舍**：link 放在 `SceneNode` 的 `std::vector<SceneLink>`（arena 内，随树保留），而不是挂 `SceneObject` 组件——避免 `SceneLoader` 的 renderable 直连路径迁移到实体组件、且保持纯数据节点零开销；C++ 系统如需批量遍历带 link 的节点，走 `findAllLinked` / 按 kind 过滤即可。
- [x] 场景序列化（2026-08-17）：`SceneHost ↔ JSON`（id/key/name/space/TRS/visible/tags/layer/bounds/children），Poco JSON 实现，脚本 `serializeHost / deserializeHost`。
- [x] 节点拾取与裁剪（2026-08-17）：节点级 AABB（`setNodeBounds`）+ `pickRay/pickScreen` + `collectFrustumIds`（Vulkan ZO 视锥）+ `syncSpatialIndex`（接入 spatial 八叉树，id=arena 索引，`nodeIdFromSpatialId` 回映）。**实现取舍**：AABB 挂在节点而不是 Mesh——`Mesh` 不保留 CPU 顶点数据；`SceneLoader::fillSceneBounds` 从 Assimp `aiMesh` 顶点计算网格 AABB 并向上并集，自动填充导入模型（热重载保留）。
- [ ] `AnimLink`：动画是"姿势驱动"而非变换同步（状态机推进 + 骨骼姿势应用），语义不同，单独列为动画挂接 TODO。
- 轻量 instantiate（复制子树 + 重命名 id + 复制绑定）。

## 文件改动清单（P0）

```text
src/engine/common/ECS.h / ECS.cpp          +registerPostEcsHook（~20 行）
src/modules/scene/SceneObject.h/.cpp       新增（~80 行）
src/modules/scene/SceneNodeRef.h/.cpp      新增（~200 行）
src/modules/scene/SceneHost.h               SceneNode +objectId（1 行）
src/modules/scene/Scene.cpp                新 API + expose + SceneEntity 注入钩子（~350 行）
src/modules/scene/NodeDesc.cpp              绑定保留/销毁（~40 行）
test/scene.cpp                             新测试（~200 行）
examples/scene-ecs/main.nut                新示例（~80 行）
docs/usr/modules/scene.md                  新增 API 快查
docs/dev/模块设计.md                        第 20 节清单更新
```

`create_module(EVScene scene)` 自动扫描 `src/modules/scene/` 下新增源文件，无需改 CMake。

## 验收标准

1. `scene.attachEntity("p", MyComp)` 后，`eve.view(MyComp).len() == 1`，实例字段可读写。
2. `scene.update(dt)` 驱动 `MyComp.update(dt)`；`node().getPosition()` 与 TransformSystem 计算一致。
3. `remountBuildAs` / reconcile 后绑定按 id 保留；节点从树中移除后 `eve.view(MyComp)` 不再包含该实例。
4. 同一节点重复 attach 同 class：旧实例收到 `onDetach`，`view` 中数量不变。
5. `SceneLoader` 导入的模型节点 attach 后，`reload()` 前后实例保持存活（绑定保留）。
