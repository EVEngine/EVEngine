# EVEngine Climbing：跑酷与攀爬系统设计

日期：2026-08-28

状态：生产基线已实现并进入验收；交互式视觉截图仍受本机 filesystem 初始化失败阻塞

目标 profile：3D 动作游戏，第三人称优先，同时支持第一人称与 AI 角色

## 1. 决策摘要

EVEngine 新增 `climbing` 玩法模块，负责角色与环境之间的连续移动交互。模块名优先
使用者一眼能理解的“攀爬”，能力范围仍明确包含 vault、mantle、wall run 等跑酷
动作。它不是另一个通用 `CharacterController`，也不把动画、物理、输入和相机状态
复制进一个大类。系统采用以下闭环：

```text
ClimbingCommand
  -> Environment Probe
  -> Candidate Synthesis
  -> Hard Validation
  -> Deterministic Scoring
  -> ClimbingExecution
  -> Warp + Collision-constrained Motion
  -> Pose Constraints / Events
```

设计吸收 Unity Parkour & Climbing System 的关键体验目标：预测跳跃、按障碍高度
自动选择动作、悬挂与横移、Braced/Free Hang、手脚 IK、烘焙攀爬点；但实现采用
EVEngine 自己的 Definition、Result、SimulationStep、Physics Query、Animation
Graph、Root Motion、ECS 和 Link/Handle 约束。

首个可交付版本只做一条完整生产路径：

- 地面移动与空中移动接入；
- low vault、high vault/mantle、ledge grab、hang idle、drop；
- 运行时探测与少量显式 anchor 混合；
- collision-constrained root motion 与单目标手部 IK；
- Squirrel 定义、调试可视化、确定性评分和 headless contract tests。

Shimmy、内外角、ledge-to-ledge、wall run、pole、beam balance 和 ladder 在核心
契约稳定后分批加入，不在 MVP 中伪造完成度。

## 2. 参考系统中保留与放弃的部分

### 2.1 保留的体验能力

- 根据速度、输入方向、障碍高度、深度、落点和空间余量选择动作。
- 预测可达的目标，而不是只能在碰撞后响应。
- 同一动作动画通过目标匹配适应一段几何范围。
- 攀爬使用 authored animation 提供轮廓和节奏，IK/约束负责手脚贴合。
- 支持 Braced Hang 与 Free Hang，并可在二者之间过渡。
- 普通障碍可运行时探测，复杂路线可由关卡工具烘焙 anchor graph。
- 玩家和 AI 使用同一候选、验证、执行协议。

### 2.2 不复制的 Unity 结构

- 不复制 `MonoBehaviour` 单例、LayerMask 全局状态和 Animator bool/trigger 业务真源。
- 不让每种动作拥有一份独立射线检测和角色移动代码。
- 不以 `ScriptableObject + Animator.MatchTarget` 作为执行契约；EVEngine 使用版本化
  Definition、显式 WarpWindow 和物理约束后的位移结果。
- 不让动画 State Machine 与玩法 State Machine 同时决定角色当前动作。
- 不把关卡攀爬点当作静态世界坐标；动态平台上的 anchor 必须使用 body-local 坐标
  和 generation-checked handle。

## 3. 模块边界

建议在 manifest 中声明：

```cmake
eve_declare_module(NAME climbing LAYER 5 SCRIPT Climbing SLOT climbing
                   DEPS animation definitions physics scene
                   OPTIONAL_DEPS ik attributes game_event
                   GROUP 3d)
```

最终依赖以实现时的真实 include graph 为准。`physics` 不是静默 optional：没有空间
查询和 capsule mover 时，运行时执行返回 `Unsupported`；若未来出现第二个真实
query/mover provider，再提炼 consumer-owned capability，不为单一 consumer 提前
把接口放进 common。

职责分配：

- `climbing`：意图缓冲、环境候选、动作选择、执行生命周期、warp、anchor graph、
  调试快照和领域事件。
- `physics`：ray/capsule/box cast、overlap、closest point、capsule mover、碰撞过滤；
  不理解 vault 或 hang。
- `animation`：clip、graph、root motion、pose、constraint stack、notify；不决定
  动作是否合法。
- `scene`：角色和动态障碍的 transform identity；不保存跑酷状态。
- `ik`：可选的链求解 backend；provider 缺失时使用动画模块内已有的有限 TwoBone
  约束或明确降低 IK 质量。
- `attributes`：可选 stamina authority；`climbing` 不维护第二份 stamina。
- `game_event`：只记录需要 replay、网络或审计的动作事实；每帧 probe 不入日志。
- 游戏项目输入 adapter：把键鼠、手柄或 AI 命令投影为 `ClimbingCommand`；
  `climbing` 不直接依赖 Keyboard/Joystick。

## 4. 领域模型与唯一真源

跑酷是一种可附加能力，不是角色的稳定“是什么”。因此不创建统一
`Character`/`GameplayActor`/`ClimbingAgent` 大根类。系统查询带 climbing 组件的
现有领域实体：

```text
ecs::View<ecs::Entity,
          ClimbingBody,
          ClimbingIntent,
          ClimbingState,
          ClimbingLinks>
```

组件分类如下。

### `ClimbingBody`

热、权威、瞬态、可复制到网络 prediction buffer。

- capsule 半径、半高、up；
- 线速度、面向 yaw、grounded 与 ground normal；
- 最大坡度、skin、ground snap、step height；
- 当前 movement mode：`Grounded`、`Airborne`、`Climbing`。

角色位置和旋转的唯一权威 owner 必须在项目接入时指定。推荐由 climbing motor 在
fixed simulation phase 拥有 kinematic transform，Scene/Avatar 只消费投影。若项目
已有 authoritative locomotion controller，则它实现同一 motor 接入点，禁止双方
每帧互相覆盖 transform。

### `ClimbingIntent`

热、权威、瞬态、local/predicted。

- camera-relative move vector；
- look/facing intent；
- `Jump`、`Climb`、`Drop`、`Sprint`、`Crouch` 命令；
- 每个 edge command 的 pressed tick、expiry tick 和 consumed execution id；
- 输入模式：`Flow` 或 `Precision`。

输入缓存以 tick 表达，不读 wall clock。默认建议 jump/climb buffer 120 ms、
coyote time 100 ms，具体值属于 profile definition。

### `ClimbingState`

热、权威、瞬态、replicated。

- 当前 `ClimbingExecutionId`；
- execution phase、phase elapsed、definition generation；
- 当前 anchor/candidate 的稳定引用；
- collision residual、warp residual、last stable transform；
- cancel window、interrupt priority、terminal reason。

动画参数、相机倾斜和 IK 权重是从此状态导出的 presentation，不是第二权威状态。

### `ClimbingLinks`

冷、权威、可持久、replicated identity。

- `SceneNodeRef`：角色的 Scene 对应物；
- `PhysicsBodyHandle`/shape handle：角色 capsule 或被忽略的自身形状；
- 可选 Avatar/animation binding handle；
- 失效时 action 以稳定错误码取消，不保存跨帧裸指针。

Link 的角色侧 owner 负责创建与销毁。目标先销毁时 generation 解析失败并产生
`climbing.link.stale`；角色先销毁时释放 anchor reservation。restore 先恢复角色
和目标 identity，再重建 Link，最后才允许 execution 恢复。

## 5. Definition 数据

### 5.1 `ClimbingProfileDefinition`

每种角色体型/操控风格一份 profile：

- capsule 与 standing/crouched 几何；
- locomotion 加速度、制动、空中控制、重力、跳跃速度；
- probe 距离、扇区、最大候选数和 query mask；
- 输入 buffer、coyote、auto-assist 强度；
- 候选评分权重和最大 warp budget；
- 默认动作集合与 tag allow/deny；
- stamina adapter policy；
- camera cue profile 引用。

### 5.2 `ClimbingActionDefinition`

动作由数据描述，稳定 identity 使用 `DefinitionRef`，不以 enum 封死动作集合：

- `id` 与 tags，例如 `parkour:vault_low`、`climb:hang_free`；
- 允许的 source mode、required command、minimum speed；
- `ProbeRecipe`：前向墙面、上表面、落点、头部 clearance、侧面连续性等 query；
- `GeometryConstraint`：高度、深度、距离、法线、坡度、曲率、支撑类型范围；
- `TrajectorySpec`：起点、contact、apex、landing 等语义控制点；
- `AnimationBinding`：clip/graph node、mirror policy、root bone、notify contract；
- `WarpWindow[]`：归一化时间窗、平移/旋转通道、目标语义、最大缩放和角速度；
- `ContactConstraint[]`：left/right hand、foot、pelvis 的 anchor 与权重曲线；
- cancel/branch windows、landing policy、terminal velocity policy；
- selection bias、连续动作 combo tags、重复惩罚；
- 可选 Condition 和 CostSpec；
- camera/event metadata。

Definition 必须有 schema id/version。未知字段按 schema policy 保留在 extension
metadata，核心已知字段验证失败则整份拒绝，不产生部分注册。活动 execution 固定
使用开始时解析出的 immutable definition snapshot；热重载只影响新动作，默认
`KeepInstanceValues`。修改 capsule、轨迹拓扑或 notify contract 的破坏性变更可
声明 `RejectWhileActive`。

## 6. 环境表示：运行时探测与 Anchor Graph 混合

### 6.1 运行时候选

普通 box、墙、平台和自然几何由 Physics 查询生成临时候选，不要求设计师标记每个
障碍。候选至少包含：

- surface body/shape generation handle；
- obstacle contact、top、landing、surface normal/tangent；
- body-local anchors 和采样 tick；
- height/depth/gap/clearance；
- 支撑 material/tag；
- 生成它的 probe recipe 和证据。

Physics 当前把查询结果缓存在 `World3D` 上。实现 climbing 前应先增加 owning
`QueryResult` 或在单个同步 probe scope 中立即复制结果，不能把 `getRayHit*()` 的
隐式缓存跨 query、跨帧保存。

### 6.2 显式 `ClimbingAnchorGraph`

复杂攀爬面、ladder、pole、beam、设计师指定路线使用可烘焙 graph：

- node：语义类型、body-local transform、左右手/脚 socket、占用 slot、tags；
- edge：shimmy、corner、jump、drop、mount、dismount、换手及允许方向；
- graph asset：schema/version、source geometry content id、build settings/hash；
- runtime instance：scene/physics target handle、generation、world transform cache。

图只拥有 climbing 拓扑和语义，不复制碰撞几何。运行时每次进入关键 phase 前用
Physics 重新验证 clearance。动态平台移动时从 body-local anchor 重建 world pose；
target handle stale 或拓扑 generation 改变时安全取消。

### 6.3 Braced 与 Free Hang

不是两个互斥代码路径，而是同一 `HangSupport` 证据：

- 手部 ledge anchor 必须成立；
- 在胸部以下对墙面做 foot support probe；
- 有稳定足部支撑且 pelvis clearance 合法为 `Braced`；否则为 `Free`；
- 支撑在执行中变化时，仅在有对应 transition edge 和动画时切换；否则保持当前
  pose 并在安全窗口重选/取消。

## 7. Probe、验证和确定性选择

### 7.1 固定阶段

一个 fixed tick 内按稳定顺序执行：

1. 采样 `ClimbingCommand` 并更新 input buffer。
2. 依据 movement mode、速度和 command 选择有限的 `ProbeRecipe` 集合。
3. 批量执行 broad phase，再对近邻进行 ray/capsule/box casts。
4. 合成候选的局部 frame 和语义 anchors。
5. 硬验证完整角色 capsule path、落点、头部空间、法线、tag 和 action condition。
6. 对合法候选计算 cost，以稳定 key 排序。
7. 仅在动作真正启动时 consume 输入并创建 execution。

探测不修改角色、anchor 或 stamina。任何失败都留下可观察 diagnostic evidence。

### 7.2 评分

建议 cost 项：

```text
cost = directionError
     + approachSpeedError
     + heightCenterError
     + distanceError
     + warpTranslationCost
     + warpRotationCost
     + landingVelocityCost
     + intentMismatch
     + repetitionPenalty
     + assistPenalty
```

碰撞、clearance、超 reach、超 warp budget 和禁用 tag 属于 hard reject，不应靠高
cost 勉强通过。浮点 cost 量化到固定精度后比较；完全相同时依次以 action
DefinitionRef、target stable identity、anchor index、mirror side 排序，确保 replay
中不依赖容器迭代顺序。

`Flow` 模式更偏向保持速度和连续性；`Precision` 模式提高 camera/facing 与显式
target 权重，并允许短暂 target preview。两者共享同一 hard validation。

## 8. `ClimbingExecution` 生命周期

```text
Requested -> Aligning -> Launching -> Climbing -> Landing -> Recovering -> Completed
                    \-> Hanging -> Climbing/Shimmy/Jumping/Drop
任何非终态 -> Cancelled | Failed
```

这是 climbing 的唯一动作生命周期权威。Animation Graph 只由它驱动，不能反向
根据 clip 名称决定玩法 phase。

不直接复用当前 `action::ActionRuntime` 的原因：现有 Action Active operation 在
prepare 后一次 commit，适合技能/武器/卡牌原子效果；跑酷 Active 是多 tick、每 tick
有碰撞反馈和分支的 locomotion authority。强行复用会制造第二套 phase/timer 或把
持续移动伪装成一次 effect。Climbing 可以复用 common Condition、Cost、Result、
Transaction 和 EventEnvelope，但只有在另一个连续动作领域出现相同契约后，才评估
扩展通用 Action runtime。

关键规则：

- 创建返回 `[[nodiscard]] Result<ClimbingExecutionId>`；
- `advance(SimulationStep)` 返回拥有本 tick transitions、motion result 和 diagnostics
  的 `Result<ClimbingAdvance>`；领域事件进入 runtime-owned 有界队列，并在仿真后由
  `drainEvents()` 原子取走；
- 同一 tick 重复 advance 拒绝；不读 wall clock；
- anchor reservation、stamina debit 与动作 publication 通过 prepare/commit 保证
  原子性；失败时 input 不 consume、角色不移动；
- cancel 使用强类型 `ClimbingCancelReason`，并受 definition 的 cancel window 限制；
- 未知 callback/script 在 simulation phase 后派发，不持锁调用。

## 9. 运动、Motion Warping 与碰撞

每个 fixed tick 的位移顺序固定为：

```text
clip root delta / authored trajectory
  -> time and target warp
  -> facing correction
  -> capsule cast + moveCapsule
  -> actual motion
  -> warp residual accumulation
  -> pose constraints
```

`WarpWindow` 只在指定归一化动画区间修正指定通道。例如 vault 的第一窗口对齐双手
contact，第二窗口对齐落点；hang 的窗口对齐手部 ledge frame。修正必须受限：

- 每 tick 最大平移、旋转；
- 动画总位移缩放范围；
- vertical/horizontal 分通道 budget；
- pelvis 与 capsule 的最大允许偏差；
- 目标在 body-local frame 中随动态平台更新。

绝不为了“动画对上”直接 teleport 穿过几何。warp 后的 desired delta 必须通过
capsule mover；实际位移与 desired 的差成为 residual。若 residual 在窗口结束前
不可收敛，则按 definition 进入 hang、短落地、safe cancel 或 structured failure。

动态障碍在每个关键 contact window 前重新验证。Physics handle stale、平台速度超过
profile 上限或 landing 消失时不继续使用旧 world 坐标。

## 10. 动画、IK 与相机

### 10.1 动画契约

- locomotion 可继续使用 MotionMatcher 或 BlendSpace；ClimbingExecution 暴露
  `mode/action/phase/normalizedTime/speed/side/support` 参数。
- climbing clip 的 notify 使用稳定语义：`contact.left_hand`、`contact.right_hand`、
  `collision.compact`、`branch.open`、`branch.close`、`land`。
- clip 缺少 definition 要求的 notify 时注册失败，不在运行时猜默认帧。
- root motion 是 desired motion，最终角色 motion 由 physics mover 决定。

### 10.2 Pose constraints

约束顺序建议：

1. 动画图输出 base pose；
2. pelvis/body warp correction；
3. hands contact IK；
4. feet/braced support IK；
5. head/look constraint；
6. skinning 与 attachments。

MVP 使用 Animation 的 TwoBoneIK 完成单手或双手接触。需要 FABRIK 多骨链时接可选
IK provider。provider 缺失必须在 profile 状态和 debug HUD 中可见，不静默宣称
完整攀爬质量。

### 10.3 Camera cues

Climbing 只发布 `CameraCue` 数据，例如 FOV impulse、roll、look target、distance
limit、shake tag；Camera 模块/游戏相机决定如何混合。Wall run 的 roll、landing
shake、ledge framing 不写入 climbing transform，也不让相机反向决定动作合法性。

## 11. 动作集合与检测证据

### MVP

- `vault_low`：前墙 + top + far landing + 全 capsule 弧线 clearance；保持水平动量。
- `vault_high`：更高 obstacle，较大手部 contact window，落点必须 walkable。
- `mantle`：墙面、ledge top、站立 capsule clearance；结束为 grounded。
- `ledge_grab`：空中下降/接近 ledge，双手 anchor、hang capsule clearance；结束为 hang。
- `hang_idle`：持续验证手部 ledge 与 capsule；支持 drop、climb up。
- `drop`：释放 reservation，继承平台速度和定义的下落初速。

### 第二批

- `shimmy`：沿 ledge tangent 生成连续 anchor，capsule sweep 验证身体路径。
- `corner_outer/inner`：法线夹角、corner socket、完整 body sweep 与手部交替。
- `ledge_jump`：预测弹道 + landing/hang target，输入方向决定候选扇区。
- `climb_down`：脚前方 top probe、下方 ledge 与 hang clearance。
- `ladder`：显式 graph、step spacing、mount/dismount edges。

### 第三批

- `wall_run`：侧向 wall cast、切向速度、墙面连续性、头部 clearance、持续时间和
  退出预测；重力/adhesion 是 climbing body policy，不是动画位移。
- `pole`/`bar_swing`：显式 anchor、角动量或约束 solver、release trajectory。
- `beam_balance`：窄面识别、局部宽度、balance state 和 camera cue。

## 12. Squirrel API 草案

脚本入口保持窄而结构化：

```squirrel
local climbing = eve.Climbing();

local profileResult = climbing.loadProfile("project://climbing/hero.profile.json");
if (!profileResult.ok) throw profileResult.status.summary;

local bindResult = climbing.bindAgent(playerEntity, profileResult.value, {
    sceneNode = "hero",
    physicsBody = heroBody.getHandle()
});
if (!bindResult.ok) throw bindResult.status.summary;

climbing.submitCommand(playerEntity, {
    moveX = input.moveX,
    moveY = input.moveY,
    jumpPressed = input.jumpPressed,
    climbHeld = input.climbHeld,
    mode = "flow"
}).expect("submit climbing input");
```

脚本返回使用公共 Result 投影，不新增 `lastError()`、`bool + getter cache` 或
`*Checked` 双 API。候选调试使用 owning snapshot：

```squirrel
local debug = climbing.inspectAgent(playerEntity);
foreach (candidate in debug.value.candidates) {
    print(candidate.actionId + ": " + candidate.status.code + "\n");
}
```

## 13. ECS System 契约

### `ClimbingInputSystem` — Input phase

- 类型范围：带 `ClimbingIntent` 的实体；
- read：项目提交的 owning command queue；
- write：`ClimbingIntent`；
- structural mutation：无；
- event：无持久事件；
- services：SimulationStep；
- 确定性：相同 command stream bit-exact。

### `ClimbingProbeSystem` — PrePhysics

- View：`ecs::Entity + ClimbingBody + ClimbingIntent + ClimbingLinks`；
- read：body/intent/link/profile、Physics world；
- write：每 tick owning `ClimbingCandidateBuffer`（derived/transient/local）；
- structural mutation：无；
- services：Physics query；
- 确定性：候选 identity/order stable，几何数值 tolerance-bounded。

### `ClimbingSelectionSystem` — PrePhysics

- read：intent/body/candidates/definitions；
- write：`ClimbingState`；
- structural mutation：创建 execution/anchor reservation 使用 deferred command；
- event：`ClimbingStarted` 在 commit 后排队；
- services：Condition、ResourceAccount、definition registry；
- 确定性：量化 cost 后 stable tie-break。

### `ClimbingMotionSystem` — Physics

- read：execution、body、anchor、animation desired root delta；
- write：authoritative body transform/velocity、execution residual；
- structural mutation：无；
- event：contact/land/cancel 进入 runtime-owned bounded queue；
- services：Physics mover、SimulationStep；
- callback：本阶段不调用脚本。

### `ClimbingPoseSystem` — PostPhysics

- read：actual motion、execution、contact anchors；
- write：derived animation params/pose constraints；
- event：仿真后 drain owning 事件批次，再交给脚本/game_event adapter；
- services：Animation，可选 IK；
- 确定性：玩法状态不依赖最终 skin pose。

所有 View 内结构变化使用 `ecs::ScopedDefer`。系统不得缓存 component 地址到下一 tick。

## 14. 错误、取消与调试

稳定错误码至少包括：

- `climbing.profile.invalid`
- `climbing.definition.invalid`
- `climbing.provider.physics_missing`
- `climbing.link.stale`
- `climbing.anchor.stale`
- `climbing.candidate.no_match`
- `climbing.candidate.clearance_blocked`
- `climbing.candidate.out_of_reach`
- `climbing.warp.budget_exceeded`
- `climbing.motion.blocked`
- `climbing.animation.notify_missing`
- `climbing.tick.duplicate`
- `climbing.restore.version_unsupported`

`no_match` 是正常决策结果，可作为 debug evidence 而不是每帧错误日志。真正启动失败、
stale link、事务失败和 active execution 失败必须返回 Result/terminal status。

Debug draw 显示 probe 形状、候选 frame、hard reject code、cost breakdown、warp path、
实际 capsule path、residual 与当前 anchors。每帧数据是 bounded owning snapshot，避免
调试 UI 读取 World3D 的“最近一次查询”缓存。

## 15. Replay、网络、存档与热重载

- 离线/单机：固定 tick 下 command、definition generation、anchor identity 和量化
  candidate order 可重放；Physics 接触数值声明 tolerance-bounded。
- 网络：服务器权威选择与执行。客户端预测携带 selected candidate key；服务器可
  接受或返回 corrected execution snapshot，不信任客户端 world position。
- 需要复制的状态：execution id、action ref/generation、phase/elapsed、target handle/
  persistent id、body-local anchors、velocity、support/mirror、reservation token identity。
- 不复制：最终骨骼 pose、IK trace、debug candidates、相机 cue 混合状态。
- Snapshot schema `evengine.climbing-runtime` v4，支持当前/N-1（v3），并保留历史迁移；未派发事件随快照保存，
  未知新版本拒绝。
- restore 先解析与验证 owning candidate state，再解析 links/definition generation，
  最后一次发布；失败保持原角色状态。
- active definition reload 默认固定 snapshot 到完成。anchor graph hot reload 使旧
  generation stale；仅在显式 migration 能映射 node 时继续，否则 safe cancel。

## 16. 测试与验收

### Source-only 与 API 形状

- `ARCHITECTURE_BASE=HEAD make check/architecture-contracts`
- `python scripts/module_depgraph.py --check`
- `python scripts/check_bindings.py --strict`
- manifest/layer/profile、public header self-containment、`git diff --check`

### 单元与契约

- 每个 probe recipe 的 hard validation 与 evidence code；
- candidate cost 量化和 stable tie-break；
- input buffer/coyote/consume exactly once；
- warp window 边界、budget、mirror 和 residual；
- duplicate tick、cancel window、stale handle；
- provider present/absent；
- definition reload active/new execution；
- snapshot round-trip、N-1 migration、未知版本拒绝；
- callback 重入不破坏 execution registry。

### 组合路径

`test/climbing_vault.cpp` 建立真实 World3D box、真实 capsule mover、真实 animation
clip/root motion，并验证：

1. running jump 只选择 `vault_low`；
2. landing capsule 无穿透；
3. root desired delta 被 geometry constraint 修正；
4. started/contact/land/completed 顺序稳定；
5. 移除目标 body 后产生 stale cancel；
6. 相同输入运行两次的量化轨迹与事件 hash 相同。

再增加 `examples/climbing-playground`，包含 0.6/1.0/1.4 m 障碍、可移动平台、
ledge grab、blocked clearance 和 debug HUD。视觉验收必须使用 engine-owned screenshot，
不能用 Xvfb framebuffer 代替。

## 17. 分批实施

### Batch 0：先补齐底座

- Physics owning query result/scope，消除 climbing 对 World3D 最近结果缓存的跨调用依赖；
- animation clip notify contract validator；
- `ClimbingProfileDefinition`/`ClimbingActionDefinition` schema 与 codec；
- Squirrel Result 投影与最小 module binding。

### Batch 1：可玩的 MVP

- components、command buffer、probe/selection/execution/motion/pose systems；
- vault low/high、mantle、ledge grab/hang/drop；
- collision-constrained warp、手部 contact IK、debug snapshot；
- `climbing-playground` 与 headless integration test。

### Batch 2：完整攀爬

- anchor graph baker/runtime、shimmy、corners、climb up/down、ledge jump、ladder；
- braced/free support transition、moving platform、occupancy reservation；
- editor authoring/validation overlay。

### Batch 3：高速跑酷与生产化

- wall run、slide、beam、pole/bar；
- AI route adapter、network prediction/reconciliation；
- motion-matching integration、性能预算、批量 queries 和 telemetry。

每一批都必须落到真实 producer、consumer、入口和组合测试。设计文档本身不表示
功能已经实现。

## 18. 性能预算建议

以 60 Hz、单个主角为初始目标：

- Ground/Air 普通 tick：不超过 4 个 narrow-phase queries；
- 有 climb intent 的 tick：不超过 16 个，候选最多 8 个；
- climbing active：每 tick 不超过 8 个 revalidation queries；
- debug off 时零字符串格式化、零每帧 heap allocation；
- candidate buffer 使用固定容量或 frame arena；
- query 数量、reject 原因、selected cost、warp residual、mover iterations 进入 profiler。

Windows MSVC Debug 的 120 tick 实测快照（单次本机运行，不代表 Release 或其他硬件）：

- 普通移动：p50 34.8 us、p95 38.7 us，query p50/p95/max = 1/1/1；
- 候选密集（8 candidates 与周边 broad-phase shapes）：p50 665.0 us、p95 792.6 us，
  query p50/p95/max = 10/10/10；
- active climbing：p50 71.8 us、p95 81.8 us，query p50/p95/max = 1/1/1；
- 三种 workload 的 query budget exceeded 均为 0；debug-off probe 热路径经 MSVC CRT
  allocation hook 验证，预热后 0 次逐 tick heap allocation。

这些数字由 `climbing.performance.reportsOrdinaryCandidateDenseAndActiveP50P95` 输出，
用于发现回归而非跨机器硬门槛；发布构建仍应在目标平台重新采样。

## 19. 架构规则适用结论

- 未创建统一 GameObject/GameplayActor；Climbing 是正交组件与系统。
- Physics/Scene/Animation 通过现有身份和模块边界组合，不保存跨帧裸指针。
- 新操作使用 Result/强类型状态，不新增含混 bool 或 lastError。
- 时间由 SimulationStep 注入；评分顺序、tie-break、replay contract 明确。
- active definition/anchor 的 generation、stale、双销毁顺序和 restore 顺序已定义。
- 持久格式要求 schema/version/migration 和事务恢复。
- optional IK/Attributes/GameEvent 的 present/absent 行为必须测试且可观察。
- 当前没有批准任何架构例外。

## 20. 外部参考

- Fantacode Parkour & Climbing System 文档：预测跳跃、悬挂/横移、IK、Braced/Free
  Hang、自动按障碍类型选动作和 climb-point baking。
  <https://fantacode.gitbook.io/parkour-and-climbing-system/>
- Unity Technologies Character Controller Platformer Sample：把复杂角色移动拆为
  Ground/Air/Wall Run/Climbing/Ledge Grab 等状态，并分 fixed/variable update。
  <https://github.com/Unity-Technologies/CharacterControllerSamples/blob/master/_Documentation/Samples/PlatformerSample/character.md>
- Dynamic Parkour System：vault、slide、auto-step、motion warping、ledge/pole 与
  jump prediction 的动作范围参考。
  <https://github.com/knela96/Dynamic-Parkour-System>

这些资料只用于能力和问题空间参考；本设计不复制第三方代码或资产。
