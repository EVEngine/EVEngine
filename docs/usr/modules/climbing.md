# 攀爬与跑酷模块

**脚本入口：** `eve.Climbing()`；默认加载槽位为 `climbing`。

`Climbing` 将跑酷动作拆为“几何探测 → 硬校验 → 确定性选择 → 胶囊体约束执行”。当前能力覆盖矮墙翻越、撑越、登台、空中抓边、Braced/Free Hang、climb-up、drop、沿边横移、内外转角、跨锚点跳跃、向下攀爬，以及梯子的 mount/climb/dismount。动作差异由 definition 描述，不写死在角色控制器里。

## 最小用法

```squirrel
local physics = eve.Physics();
local climbing = eve.Climbing();
local world = physics.newWorld3D(0, -9.8, 0, true);

local created = climbing.newRuntime();
if (!created.ok) throw created.error.message;
local mover = created.value; // owned；模块卸载或 release 后句柄失效

local configured = mover.setProfile(
    0.30, 1.80, 0.03, // capsule radius / height / skin
    1.20, 1.80,       // forward probe / max obstacle height
    0.65, 0.20, -1); // top normal, warp budget, physics mask
if (!configured.ok) throw configured.error.message;

local mantle = mover.upsertAction(
    "core:mantle", 0.55, 1.25, 0.0,
    0.60, 0.60, 0.70, 0); // duration, landing forward, apex, selection bias
if (!mantle.ok) throw mantle.error.message;
```

完整动作可以通过 `upsertActionKind` 指定 `vault`、`mantle`、`ledge_grab` 或
`climb_up`，并提供悬挂偏移、双手间距和取消窗口。该入口会自动建立稳定动画 notify
契约；调用 `validateActionClip(actionId, clip)` 验证 clip 后才能启动动作。缺失
`contact.left_hand`、`contact.right_hand` 或 `land` 会返回
`climbing.animation.notify_missing`，不会在运行时猜测默认帧。

角色请求动作时，将脚底世界坐标、水平朝向、速度、角色刚体 ID 和确定性 tick 传给 `tryBegin`：

```squirrel
local begin = mover.tryBegin(world, feetX, feetY, feetZ,
                             forwardX, forwardZ, speed,
                             characterBodyId, simulationTick);
if (begin.ok) {
    // begin.value.actionId / topPoint / landingFeet 是 owning Value，能安全跨查询读取。
}
```

空中抓边使用 `tryBeginMode`，额外提交垂直速度与 grounded 状态。到达 `hanging`
后可调用 `climbUp(tick)` 或 `drop(tick)`；悬挂会持续验证 body-local 动态锚点、平台
速度和角色 capsule clearance。

执行期间，每个模拟 tick 调一次 `advance`：

```squirrel
local step = mover.advance(world, simulationTick, fixedDt);
if (step.ok) {
    character.setPosition(step.value.feet.x, step.value.feet.y, step.value.feet.z);
    animGraph.setParameter("climbingAction", step.value.actionId);
    animGraph.setParameter("climbingTime", step.value.normalizedTime);
}
local events = mover.drainEvents(); // PostPhysics/PostSimulation 阶段
if (!events.ok) throw events.error.message;
foreach (event in events.value) gameEvents.publish(event);
```

`desiredDelta` 是轨迹/动画希望得到的位移，`actualDelta` 是物理胶囊体真正允许的位移，`warpResidual` 是两者之差。超过 profile 的 `maxWarpResidual` 时 phase 变为 `failed`，不会穿透障碍。

## API 快查

- `Climbing.getName()` → `"Climbing"`
- `Climbing.newRuntime()` → `Result<owned ClimbingRuntime>`
- `Climbing.newAnchorGraphJson(json, world, body)` → `Result<owned ClimbingAnchorGraph>`
- `Climbing.bakeAnchorGraphJson(requestJson)` → `Result<{ graphJson, buildSettingsHash, ledgeNodeCount, ladderNodeCount }>`
- `Climbing.inspectAnchorGraphJson(graphJson)` → renderer-neutral owning editor overlay
- `ClimbingAnchorGraph.node(nodeId)` / `generation()` / `reservationCount()`
- `ClimbingAnchorGraph.planRoute(startNodeId, goalNodeId, avoidFull, agentId, executionId, maxVisitedNodes)`：在当前 graph generation 上返回确定性最短 owning route；owner id 必须同时为零或同时非零
- `ClimbingAnchorGraph.reloadJson(json)`：原子发布新图并返回 `invalidatedOccupants`
- `ClimbingAnchorGraph.release()` / `isStale()`
- `ClimbingRuntime.ownership()` → `"owned"`
- `ClimbingRuntime.setProfile(radius, height, skin, probeDistance, obstacleHeight, minTopNormalY, maxWarpResidual, maskBits)`
- `ClimbingRuntime.setProfileJson(json)` / `reloadProfileJson(json)`：规范 profile JSON 的初始安装与原子热重载
- `ClimbingRuntime.upsertAction(id, minHeight, maxHeight, minSpeed, duration, landingForward, apexHeight, selectionBias)`
- `ClimbingRuntime.upsertActionKind(id, kind, ..., hangBodyOffset, hangFeetBelowLedge, handSpacing, cancelStart, cancelEnd)`
- `ClimbingRuntime.validateActionClip(actionId, clip)`
- `ClimbingRuntime.tryBegin(world, feetX, feetY, feetZ, forwardX, forwardZ, speed, ignoredBodyId, tick)`
- `ClimbingRuntime.tryBeginMode(world, ..., ignoredBodyId, verticalSpeed, grounded, tick)`
- `ClimbingRuntime.tryBeginAnchor(graph, world, nodeId, agentId, actionId, ..., tick)`
- `ClimbingRuntime.transitionAnchor(graph, world, targetNodeId, edgeKind, actionId, tick)`
- `ClimbingRuntime.currentAnchor()` → `{ graphId, nodeId, graphGeneration }`
- `ClimbingRuntime.advance(world, tick, fixedDt)`
- `ClimbingRuntime.climbUp(tick)` / `drop(tick)`
- `ClimbingRuntime.cancel(reason, tick)`：稳定值包括 `player_request`、`drop_requested`、`link_stale`、`anchor_stale`、`motion_blocked`、`warp_budget_exceeded`、`definition_reloaded`
- `ClimbingRuntime.drainEvents()`：仿真后原子取走 owning 事件批次；事件含稳定 kind、actionId、tick 与 executionId
- `ClimbingRuntime.inspect()` → owning debug snapshot
- `ClimbingRuntime.setDebugCapture(enabled)`：关闭时立即释放候选、query、cost 与 motion evidence
- `ClimbingRuntime.snapshotJson()` / `restoreJson(json, world)`
- `ClimbingRuntime.definitionGeneration()` → 十进制字符串（避免脚本整数溢出）
- `ClimbingRuntime.release()` / `isStale()`

## C++ definition 与存档契约

`ClimbingProfileDefinition` 和 `ClimbingActionDefinition` 使用规范化 `eve::Value`
codec：schema 分别为 `evengine.climbing-profile` v4 与
`evengine.climbing-action` v4；解码同时接受 N-1（v3）。解码会先构造并验证完整 owning candidate，再一次性返回；
已知字段有误时整份拒绝。未知根字段保存在 `extensionMetadata` 并在再次编码时原样输出，
方便编辑器和插件往返处理更高层 metadata。

`inputBufferTicks` 与 `coyoteTicks` 是 profile 的版本化权威配置，默认分别为 8 和 6
tick。输入窗口只使用 `SimulationTick`，不读取 wall clock。普通 Grounded/Airborne
移动也由同一 profile 的加速度、制动、空中控制、重力、跳跃速度、最大坡度、step height
和 ground snap 驱动，并通过 Physics owning capsule mover 写回实际位移。

C++ 输入适配层提交 `ClimbingCommand::Climb`，所有公开模块与类型统一使用 `climbing` 命名；
`parkour` 仅作为动作分类和 action id/tag 使用，不存在公开 `traversal` 模块或 API。
`ClimbingInputMode::Flow` 偏向速度连续性和 combo，`Precision` 提高显式朝向、镜头意图与
目标距离的权重。评分在量化后以 action/body/shape identity 稳定决胜，并对刚执行过的
动作施加 definition 中声明的重复惩罚。
候选准备成功后才消费输入并提交 `ClimbingExecutionId`，所以预检失败不会吞键或部分改变
权威状态。

`ClimbingRuntime::snapshot()` 输出 `evengine.climbing-runtime` v4，并包含当前 profile、
definition generation、活动 action snapshot 与尚未派发的事件队列。`restore(value, world)` 支持
v4 与 N-1（v3；历史 v2/v1/v0 仍可迁移），
并在发布前重新解析 profile、pinned action、candidate、计时、动态平台 body/shape/world 句柄；
未知新版本或 stale link 都不会部分改变当前 runtime。Physics 句柄是
进程内身份，跨进程存档需要场景层先通过持久 ID 重建 Physics world，再调用 restore。

活动 execution 固定开始/分支时的不可变 action snapshot。`reloadProfile`、
`reloadProfileJson` 与 `upsertAction` 都先完整验证候选定义，再递增 generation 并一次发布；
活动动作继续使用旧 snapshot，新动作使用新 generation。普通 `setProfile` 仍是初始配置入口，
活动期调用返回 `conflict`。

## 动画与姿态适配

Climbing 直接验证 `AnimClip` 的语义 notify，但不会跨帧持有 clip 指针。
`applyClimbingPose` 是同步 C++ adapter：把 `ClimbingAdvance` 的左右手 anchor 与
`contactWeight` 交给 Animation 内置 `TwoBoneIK` constraint stack。它不保存 skeleton/pose
裸指针；游戏应在动画图输出 base pose 后、skinning 前调用。最终角色位移始终使用 Physics
返回的 `actualDelta`。

`ClimbingMotionInput` 可为每个 fixed tick 提交动画 root translation、root yaw、朝向与
pelvis offset。Definition 用 `warpWindows` 显式开启 horizontal、vertical、facing
通道，并分别限制逐 tick 修正和整段动作预算。Mantle/climb-up 的完整胶囊路径采用
“先抬升到台面净空、再越过边缘、最后落脚”的确定性轨迹；预检与实际执行共享同一函数。
pelvis 超出 `maxPelvisDeviation` 时本 tick 原子拒绝，warp 在最后窗口仍不能收敛时以
`climbing.warp.budget_exceeded` 结束，不会传送穿墙。

`collision.compact` notify 在同一 simulation tick 把物理 mover 切换到 profile 的
`compactCapsuleHeight`，状态保持到 `land`；`branch.open` / `branch.close` 按 notify 顺序
更新 `branchWindowOpen`。这些状态进入 runtime snapshot，而不只是一帧动画提示。

Animation binding 会把稳定 clip identity、graph node id、root bone 和 mirror policy 固定进
活动 execution；`graphNodeId` 是由游戏动画 adapter 解析的外部稳定 ID，Climbing 不持有
动画图节点指针。`ClimbingAdvance` 输出已解析的 clip/graph/root/mirror 信息与
`branchComboTag`，镜像 pose 的真正生成仍由 presentation adapter 负责。使用 MotionMatcher
的角色在动作开始时显式让出 locomotion provider 给 AnimGraph one-shot，动作结束后再按
fixed tick 恢复 MotionMatcher，不让两套系统同时拥有姿态。

`inspect()` 的 bounded owning snapshot 包含 probe/query 线段与胶囊、每个 action 的 hard
reject code、量化 bias/height/distance cost，以及 planned/actual/residual capsule motion。
它还包含 broad-phase/query/mover 计数与预算状态。探测先执行固定容量、稳定排序的 owning
broad phase，再进行窄相查询；热路径不读取 Physics 的“最近查询”缓存。关闭 HUD 时调用
`setDebugCapture(false)`，稳定预热后的 debug-off probe 路径不会产生逐 tick heap allocation。

## 显式 Anchor Graph

复杂 ledge、转角、梯子、pole 与 beam 使用版本化 C++ `ClimbingAnchorGraphDefinition`
（schema `evengine.climbing-anchor-graph` v2，接受 N-1 v1）。节点保存 body-local position、法线/切线、
左右手与脚 socket、语义 kind、tags 和 1–8 个占用 slot；有向边描述 shimmy、corner、
jump、drop、mount、dismount 或 climb。codec 会保留未知 metadata，并规范化节点、边和
tag 顺序；重复 identity、悬空 edge、非正交 frame 或越界 slot 会整图拒绝。

`ClimbingAnchorGraphInstance::bind` 只保存 Physics world/body 的代际句柄，不跨帧持有
`World3D`/`Body3D` 指针。`nodeRef` 返回 graph-generation-qualified reference；
`resolveNode` 每次从 body-local frame 重建世界 socket 与 point velocity，所以运动平台
平移/旋转后仍使用当前锚点，body 销毁或旧 generation 会返回 `stale_handle`。

`ClimbingAnchorNodeRef` 同时带 `graphId`、node id 与 graph generation，不同图即使节点同名、
代数相同也不能混用。`reserve` 为 agent + execution 确定性选择最低空闲 slot，返回不可伪造的 owning credential；
`release` 校验 reservation id、generation、node、slot、agent 和 execution。图热重载先完整验证
新候选，再一次发布并返回排序后的 `invalidatedOccupants`，由 gameplay 在安全窗口取消
或显式迁移；旧 node reference 与 reservation token 均立即 stale。

runtime v4 snapshot 保存 graph handle/epoch、node identity、slot、occupant 与 reservation claim
generation。恢复到另一个 runtime 时，仍存活的 claim 会原子转移并使旧 runtime 的凭证 stale；
旧 runtime 已析构时只会重获原 slot，不会挤掉已有占用。重复恢复同一旧 snapshot 返回 conflict，
因此任意时刻只有一个 runtime 能释放该 slot。graph 与 Physics handle 仍是进程内 identity；
跨进程读取前必须由场景层按持久资源 ID 重建并重映射这些 link。

编辑器或关卡工具使用 `evengine.climbing-anchor-bake-request` v1 提交 body-local 语义几何：
ledge 折线可带逐点墙面法线，ladder 提交底部、up/normal、宽度、间距和 rung 数量。
`bakeClimbingAnchorGraph` 确定性生成 ledge/corner、shimmy 边、ladder rungs 以及
mount/climb/dismount 边，并将 source geometry content id 与全部 build settings/source
采样纳入稳定 `fnv1a64` build hash。输入候选完整验证成功后才返回 graph，不会发布半张图。

`inspectClimbingAnchorGraphAuthoring`（脚本入口 `inspectAnchorGraphJson`）返回完整 owning、
渲染无关的 node/edge overlay，包含 frame、手脚 socket、slot 和语义类型。高层 Editor 或
Graphics adapter 可以选择颜色与绘制方式；`climbing` 本身不反向依赖这两个高层模块。

## 生命周期与确定性

- Runtime 由 `Climbing` 模块独占，脚本代理是 owned；不要保存 C++ 裸指针。
- Runtime 不持有 `World3D` 指针。候选保存 world/body/shape 的代际句柄，跨帧使用时重新验证。
- 探测会临时设置 Physics query filter 和忽略角色自身，返回前恢复原状态。
- 选择分数按毫米量化，并以 action ID、body ID、shape ID 稳定决胜；同一输入与世界快照产生同一排序。
- 时间只来自调用方注入的 tick 与 fixed dt；重复或倒退 tick 返回 `conflict`。
- started/contact/hanging/drop/land/completed/cancelled/failed 由 runtime 的单一有界队列拥有；
  `advance`、`drop`、`cancel` 不调用未知代码。游戏必须在仿真阶段结束后调用 `drainEvents()`，
  再派发脚本或回调。批次会先从 runtime 脱离，因此回调重入产生的新事件留给下一次 drain；
  队列达到 64 条时，新状态变更以 `conflict` 原子退回，不会静默丢事件。
- 游戏适配层把 `actionId + normalizedTime + desired/actualDelta` 喂给动画图，并在同一帧应用可选手部 pose constraint；物理位移始终以 `actualDelta` 为准。
- stamina 不在 Climbing 内复制。profile 采用 `RequireProvider` 时必须声明稳定
  `staminaAdapter`，运行时只接受 identity 完全匹配的 authority；不匹配会在输入消费和
  execution 创建之前返回错误。
- action 的 `eventMetadata` 随 started/contact/land/terminal 事件一同进入 owning 队列和
  snapshot；事件消费者不必再反查可能已热重载的 definition。
