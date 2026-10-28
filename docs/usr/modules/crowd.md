# 群体与 RTS 移动（Crowd）

**脚本入口：** `eve.Crowd()`

Crowd 是 UI/渲染无关的连续流场与群体模拟库。项目负责把地形、Tilemap 或建筑占用转换为
`setCellCost/setBlocked`，并把 `getAgentState` 映射到 ECS、Scene 或自定义渲染器。

## 稳定身份

### 数值运行句柄与批量同步

新 C++ 代码使用 `agentRef(slot)` 或绑定时的 `resolveNamedAgent(name)` 获取 `AgentRef`，
再用 `isAgentAlive/readAgent` 查询。reference 同时保存 Crowd owner、registry index 和 generation；
删除其他代理引起的 swap-pop 不改变 reference，删除自身、clear 或 owner 销毁后旧 reference 不再有效。
运行句柄不是持久身份，不写入存档；重建时通过稳定名称/持久身份重新绑定。

脚本 reference 是 `{ owner, index, generation }` 整数字段表，禁止打包成浮点数。
`agentRef/resolveNamedAgent/readAgent` 返回标准 Result；`isAgentAlive` 是纯真假查询。
`createAgents(agents, options)` 与既有 `applySpawnBatch` 共享同一原子放置内核和三种策略，
每项出生回执增加 `reference`。旧直接 add/slot API 为兼容入口，可保留重叠的历史初始化状态；
需要安全出生的新代码使用 createAgents 批次。

```squirrel
local resolved = crowd.resolveNamedAgent("unit.alpha");
if (!resolved.ok) throw "agent missing";
local ref = resolved.value;
local changed = crowd.updateAgents([{
    reference = ref, maxSpeed = 4.0, avoidancePriority = 2, holdPosition = false,
    intent = { action = "seek", hasTarget = true, targetX = 20.0, targetY = 8.0 }
}]);
if (!changed.ok) throw "update rejected";
local frame = crowd.copyAgentStates([ref]);
if (frame.ok) localState <- frame.value.agents[0].state;
```

`updateAgents` 可选字段为 maxSpeed/maxAcceleration/headingTurnRate/radius/avoidancePriority/data/
holdPosition/interaction/intent。interaction 仍使用 pushability/holdPosition/layer/mask；
同一项不能同时指定 interaction 和独立 holdPosition。intent 的全部四个字段必填，
action 为 idle/flow/seek/boids。所有浮点参数必须有限，运动上限和半径非负。
同批重复 reference、失效或错 owner、非法参数都在修改任何代理前拒绝。

`removeAgents([ref,...])` 先验证全批再删除；重复/失效输入不发布部分删除。
`copyAgentFrame()` 返回全部拥有型行；`copyAgentStates([ref,...])` 按请求顺序返回行。
value 包含 agents 数组（每行 reference/state）和 simulationSeconds；快照不保留内部指针。
C++ 对应 AgentUpdateBatch/AgentRemovalBatch 与可复用 AgentFrame，输出复制随请求代理数线性增长。
脚本版本每次分配拥有型表，不适合作为每单位回调；常规海量帧同步使用 C++ 可复用存储。
所有接口仍限定同一仿真线程，推进期间禁止重入或并发修改。

独立 CPU 裁剪配置使用 `EVENGINE_PROFILE=crowd-core-only` 与
`EVENGINE_BUILD_PROFILE_CHECKS=ON`，只选择 common/crowd 与 Squirrel；
`eve_crowd_check` 运行真实解算/脚本契约，`eve_crowd_benchmark` 测量核心移动。
OBJECT 与 SHARED 都通过相同 profile 接线；SHARED 消费 manifest 派生链接组，测试程序与 DLL 同目录。
构建/运行证据按实现进度记录核对；不要把 CMake 选项本身当成裁剪成功。

已绑定代理的重新放置使用 `relocateAgents([{reference=ref,x=20.0,y=10.0}], maxChecks)`，
对应 C++ AgentRelocationBatch。最多 1024 项，maxChecks 为 1–10000000，统计候选/邻居几何检查；
地形圆形检测沿用出生内核。只接受所有目标位置同时可用的精确放置，不推邻居、不选附近替代点。
失败不移动任何代理；成功保留 identity、交互和目标，清零速度并使空间索引失效。
已绑定 RTS 单位使用 `rts.relocateUnits([{subject=uuid,x=20.0,y=10.0}], maxChecks)`，
C++ 对应 UnitRelocationBatch；该接口同步 ECS 并清除派生路线，不改变订单。
容纳中的单位拒绝独立重新放置；缺少当前 Crowd 投影的链接单位需先完成绑定。

RTS 常规帧已使用缓存数值 reference、一次输入批次和一次选定输出批次；
名称只用于首次绑定或链接变更。已绑定的连续位置以 Crowd 为权威，ECS Motion 是输出投影，
不再每帧反向覆盖。历史单位首次绑定保留其初始几何（可能含重叠），生产出生使用原子放置。
容纳/解除链接移除投影；下车重新绑定当前 ECS 位置。安全下车放置和 provider lifetime lease 仍待后续批次。

紧凑 SOA 存储使用 swap-pop，普通 `addAgent` 返回的 slot 在删除其他单位后可能改变。
编辑器选择、存档、网络实体和 ECS 绑定应使用稳定逻辑 ID：

```squirrel
crowd.resizeField(64, 64, 1.0, 0.0, 0.0);
crowd.buildFlowField(32, 32);
crowd.addNamedAgent("unit.alpha", 2.0, 2.0, 0.0, 0.35);

local slot = crowd.getNamedAgentIndex("unit.alpha");
local state = crowd.getAgentState(slot);
```

- 稳定身份：`addNamedAgent/hasNamedAgent/getNamedAgentIndex/getAgentStableId/removeNamedAgent`。
- 匿名/批量模拟：`addAgent/removeAgent/clearAgents/getAgentCount`；适合粒子式、无需持久
  逻辑身份的群体。
- 地形流场：`resizeField/setBlocked/setCellCost/getCellCost`、
  `addFlowGoal/clearFlowGoals/build/buildFlowField`、`flowAtWorld/flowAtCell/costAtWorld`。
- 流场查询：`isFieldBuilt/getFieldWidth/getFieldHeight/getCellSize/getFieldOriginX`
  `/getFieldOriginY/isReachable`；编辑器可据此显示网格、目标可达性与地形覆盖范围。
- 单位控制：`setAgentAction/setAgentTarget/clearAgentTarget/setAgentSpeed/setAgentAccel`
  `/setAgentTurnRate/setAgentRadius/setAgentData/setAgentAvoidancePriority/setAgentPosition`
  `/getAgentState`，以及 `getAgentAction/getAgentData/getAgentAvoidancePriority/getPositions`
  `/getHeadings`。避让优先级越高，重叠解算时该单位让行越少；无效 slot 的 setter 返回
  `false`，getter 返回 `0`。
- `CrowdAgentState` 快照字段：`action`、`data`、`avoidancePriority`、`heading`、`speed`、
  `vx`、`vy`；
  快照仅用于把模拟结果同步到 ECS、动画和渲染状态。
- 容量与默认值：`setMaxAgents/getMaxAgents`、`setDefaultSpeed/setDefaultTurnRate`
  `/setDefaultRadius`、`setArriveRadius`。
- 邻域行为：`setSeparationRadius/setPerceptionRadius`、`setSeparationWeight`
  `/setAlignmentWeight/setCohesionWeight/setGoalWeight/setWanderWeight`。
- 解算约束：`setResolveOverlaps`、`setClampToField`；配置完成后调用 `step(dt)`。

推荐新代码使用 `advance(dt)`，返回统一 Result 表；检查 `ok` 后读取 `value`：
`substeps`、`unresolvedContacts`、`unresolvedWalls`、`maxPenetration`。
解算按同一子步快照计算速度，统一移动后重建邻域，迭代推开并处理墙体。
子步最长 1/60 秒，并按半径、速度与地形格大小进一步限制移动距离。
超过 1024 子步预算、负数或非有限 dt 会在修改状态前拒绝。
空间不足造成的残余碰撞会明确报告，成功推进不代表所有单位都已无重叠。
`advance` 还报告 `gridBuilds/gridCellsPeak`、`cellsVisited/candidatesVisited`、
`gridEntriesPeak/gridSortComparisons/spatialVisits` 及 `contactPairsChecked/contactPasses`。
稀疏索引按 signed int64 cell 坐标排序，
行内二分查询仅枚举 occupied cells，桶内按稳定 tie key 排序；存储与代理/活跃格数量相关，
不与群体之间的空区域面积相关。cell 尺寸按最大身体直径（至少 1 世界单位）选取，
预测与接触仍用各自保守范围。坐标超出 signed int64 cell 范围时拒绝并回滚整个推进。
`maxGridCells` 现在限制 occupied cell 数；`cellsVisited` 计数 occupied cells。
`advanceTick` 的 budget 可选整数 `maxSpatialVisits` 限制行/cell 枚举及二分比较，默认不设额外
限制；超限明确失败，不跳过身体碰撞。旧 budget 表仍可省略新字段。计数同时包含最终残余检查。
候选计数在半径和交互过滤前累加，
因此邻居上限不代表搜索工作量上限。接触 pair 计数仅包含修正轮中接受交互过滤的唯一 pair。
接触候选经计数/前缀分配填入有界 buffer，按有序 tie key pair 排序后计算修正，避免 dense slot
或创建顺序改变求和顺序。`maxContactPairs` 为每轮活跃 pair 上限（默认 1000000，可设 0..10000000），
`maxPairVisits` 为 scratch 初始化、计数、前缀、buffer 初始化、填充和排序工作上限，默认不设
额外限制。两者都是 `advanceTick.budget` 的可选精确整数扩展；超限回滚，不截断接触。
`contactPairsPeak/pairVisits` 报告活跃 buffer 峰值与构建工作；串行实现仍是当前唯一执行路径。
`gridNanoseconds/motionNanoseconds/contactNanoseconds/wallNanoseconds/residualNanoseconds`
分别测量网格、运动（含预测）、接触、墙体与最终质量检查；计时只用于诊断，不改变推进结果。
所有计数与时间按调用重新统计，空场或零 dt 返回零工作量。
`step(dt)` 是仅供兼容的入口，内部调用 `advance`，拒绝输入时抛出异常；
新代码需要读取 Result 来处理失败与残余拥挤。

所有调用限定在同一仿真线程，不支持并发访问或重入。固定 dt 序列用于同一
构建的可重复运行，不承诺跨平台逐位一致。流场原点是格 (0,0) 的左下角；
边界约束包含单位半径，圆形单位沿障碍物角落按圆形几何处理。

[`examples/composable-editor/gameplay_components.nut`](../../../examples/composable-editor/gameplay_components.nut)
演示把正在编辑的 Heightmap 转换成移动代价、用稳定 ID 创建 RTS 单位，再逐帧同步回 ECS；
这套桥接属于项目代码，并非固定 RTS 编辑器。


### 独立交互策略

- `setAgentInteraction(id, pushability, holdPosition, layer, mask)` 返回标准 Result。pushability 必须是 [0,1] 的有限值，layer/mask 为非负位掩码；失败不修改状态。
- `getAgentInteraction(id)` 返回标准 Result，value 为上述四个字段的拥有型快照。
- pushability=0 禁止其他单位推挤，但仍可主动行走；holdPosition=true 立即停止速度并暂停移动和邻居推挤，已有目标保留，解除后继续。地形约束仍然生效。
- 双方 mask 都接受对方 layer 时才参与邻居转向、接触消解和残余接触统计；layer=0 可禁用单位间交互。
- 同优先级按 pushability 比例分担接触位移。原有 avoidancePriority 的先后让行偏好只作用于双方均可移动的接触，不覆盖显式坚守或不可推挤策略。
- C++ 接口传入 AgentInteraction 值，脚本传入四个字段；均仅允许仿真线程调用。槽位删除后需重新解析稳定 ID。


### 批量出生事务

`applySpawnBatch(agents, options)` 是带搜索预算的原子操作，返回标准 Result。
C++ 使用拥有型 `SpawnBatch` 构建请求。脚本对应如下（字段均必填）：

```squirrel
local result = crowd.applySpawnBatch([
    { stableId = "soldier-17", x = 20.0, y = 30.0, heading = 0.0, radius = 1.0,
      pushability = 1.0, holdPosition = false, layer = 1, mask = 1 }
], {
    policy = "pushNeighbors", maxDistance = 8.0, searchSpacing = 0.5,
    maxPasses = 64, maxChecks = 100000
});
if (result.ok) {
    local placements = result.value.created; // stableId / x / y
    local moved = result.value.displacedAgents;
}
```

- `reject`：必须在指定位置可放置，否则整批失败。
- `nearestFree`：原位置不合法时从近到远采样同心圆，选择首个可用位置。searchSpacing 控制径向与近似弧长采样；不承诺连续空间中的最近解。
- `pushNeighbors`：固定本批所有新单位的位置，按既有单位的可推挤程度消解相连区域的接触。坚守和零推挤单位不移动，交互层过滤依然生效；此强制安置不使用行军避让优先级。

最多 1024 个新单位；maxPasses 为 1–256，maxChecks 为 1–10000000，maxDistance 非负、searchSpacing 为正，浮点参数必须有限。maxChecks 统计候选点及邻居检查；返回 value.checks 可观测工作量。maxDistance 限制新单位的搜索距离及既有单位相对原位置的位移。

实现先复制当前仿真及流场状态，每批只复制一次；成功后一次性提交。名字冲突、容量不足、固定阻挡、地形阻挡、位移或计算预算耗尽均不发布部分结果。既有速度、目标和仿真时间不变；不承诺在预算内解开任意拥挤布局，失败时调用方可重试较大预算或选择其他出生点。

`addAgent` / `addNamedAgent` 仍是兼容的直接放置接口，可能产生重叠；需要出生安置语义的新代码使用上述批次 API。该 API 仅供仿真线程调用，无回调或重入；返回坐标及稳定 ID 的拥有型快照，不暴露跨帧槽位。


### 预测交叉避障

`configureAvoidance(enabled, horizon, margin, maxNeighbors)` 返回标准 Result；
`getAvoidanceSettings()` 返回上述四个字段的拥有型表。C++ 使用 `AvoidanceSettings`。
独立 Crowd 默认关闭，以保留纯 Boids 场景的计算成本；可显式启用：

```squirrel
local configured = crowd.configureAvoidance(true, 2.0, 0.1, 32);
if (!configured.ok) throw "invalid avoidance settings";
local step = crowd.advance(dt);
if (step.ok) {
    local work = step.value.avoidanceChecks;
    local limited = step.value.avoidanceTruncations;
}
```

horizon 是 (0,10] 的有限秒数；margin 是非负有限的额外圆形间距；maxNeighbors 为 1–128。
非法配置不改变既有设置。每个子步先计算所有单位的期望速度，再基于共同的当前位置和速度快照选取可达速度。
采样综合预测接触时间、目标速度偏差、当前速度变化和右侧通行偏好，受单位加速度与速度上限约束。
坚守单位不参与主动转向；交互层过滤同样适用于预测；优先级影响让行倾向但不会免除碰撞代价。

每个单位/子步最多评价 45 个候选速度和 maxNeighbors 个最近邻居，复用邻居缓冲区。
空间查询本身仍依赖局部密度。advance 的 avoidanceChecks 统计候选-邻居预测次数；avoidanceTruncations
统计邻居数量超限的单位/子步次数。截断意味着没有预测所有潜在交互对象，不能视为完整避障保证。

这是局部速度采样启发式，不是 ORCA 实现，也不承诺任何密度和加速度条件下都无碰撞、无死锁。
接触修正仍是最终约束，地图寻路与 RTS 通行协调仍负责绕过复杂障碍及解决窄口拥堵。
预测配置目前属于运行时状态，完整保存/恢复仍需集成。RTS 的 configureScriptWorld 默认启用预测避障，
将到达减速距离设为一个导航格，并关闭旧的远距离 Boids 分离力，由预测避障和接触修正保持间距。
外部注入的 Crowd 仍由提供方配置，不会自动覆盖其设置。


### Crowd 移动投影

链接到 Crowd 的单位由 CrowdMotionSystem 同步订单目标并推进。HoldPosition 订单会禁止主动移动和邻居推挤；替换为移动订单后释放坚守。底层已配置的可推挤程度与交互层保持不变。

到达表示进入 arrivalRadius 范围，不再把单位强行移动到精确目标点；ECS 中的位置保持与 Crowd 接触处理结果一致。步进预算拒绝等错误通过系统的 Result 向调用方传播。

需要预测避障时在注入的 Crowd 上调用 configureAvoidance；该设置由提供方管理。生产出生与保存恢复的新策略接入仍在开发，不能把直接放置的旧路径当成原子出生事务。

### Production placement ownership

The native RTS `ProductionSpawn` callback receives `(Building&, ProductionTask const&,
WorldPosition requested)`. The position probe selects the requested exit, or the
producer world position is used when no probe is installed. The factory owns final
placement; production settlement does not overwrite its resolved coordinates.
Native custom factories must initialize the returned unit's motion explicitly.
The callback returns `ProductionSpawnOutcome`: `Created` contains a borrowed
unit, while `Blocked` contains no unit and leaves the completed task retryable.
The script-world factory uses an atomic `PushNeighbors` batch at the requested
position. Displaced peers are copied back to ECS immediately. A fixed blocker,
invalid terrain placement, capacity exhaustion or bounded relaxation failure
leaves production blocked; it retries on subsequent ticks without a second debit.
Place production exits inside the walkable field with room for the full agent
radius. No nearest-free fallback is silently selected. Creation errors remain
structured failures. The default batch limits apply (64 world units of movement,
64 passes and one million candidate checks).

Before production, RTS currently reconciles Crowd projections with a zero-duration
CrowdMotionSystem pass, including same-frame containment/order changes. This adds
a second projection traversal and does not advance simulation time. On a rejected
placement, tentative ECS roots/weapons are released while payment and subject
reservation remain. Internal ECS generation allocation is not rolled back.

Contained RTS units are removed from Crowd at the next `CrowdMotionSystem` step.
Disembarking recreates their projection from the current ECS position. This prevents
passengers from retaining invisible collision footprints; it does not itself choose
a collision-free disembark location.

### RTS formation spacing

Command fan-out treats formation spacing as a minimum center distance. For a
multi-unit selection it expands that distance to at least the sum of the two
largest unit radii, so line/grid/wedge slots can accommodate every selected pair.
This is a conservative uniform layout, not dense variable-radius packing. Slot
assignment is deterministic under selection reordering for the same live units
and positions. Duplicate handles, invalid radii and nonfinite positions reject
the request before changing any order. Pure FormationPlanner remains independent
of unit geometry; direct callers supply their own spacing.

Before issuing orders, fan-out performs up to four deterministic pair-exchange
sweeps over its initial slot assignment. A swap must reduce total straight-line
travel distance beyond a relative numerical tolerance. Slot identity moves with
its position. This reduces avoidable slot crossings while keeping quadratic
command-admission work; it is neither a global assignment optimum nor a guarantee
of collision-free trajectories through terrain. Equal-cost slots retain their
previous assignment rather than oscillating between equivalent choices.

### Oriented formation layouts (native API)

`FormationSpec::rotationRadians` rotates the generated layout counterclockwise
around its anchor. Zero keeps the previous layout convention: line along X,
column along Y, wedge extending toward positive Y. Column uses one centered row
of units along Y. Dispersed uses deterministic concentric rings with 1.5 times
the resolved minimum spacing; it consumes no random stream. These remain target
layouts, not persistent moving groups or obstacle-aware formation recovery.

`EVERTS_COMMANDS` v4 stores rotation and the Column/Dispersed enum values. Imports
of v1-v3 use zero rotation; exporters retain v1-v3 for inputs using only legacy
layouts and zero rotation. Unsupported versions and malformed rotation are
rejected before changing the command log. Squirrel's existing convenience move commands keep their grid layout; the explicit
formation methods below accept the complete layout specification.

### Squirrel formation movement

`rts.moveFormationUnits(subjects, x, y, append, kind, spacing, columns, rotationRadians)`
issues a Move command immediately and returns the canonical Result with the fan-out
receipt. `kind` is `line`, `grid`, `wedge`, `column`, or `dispersed`. `columns = 0`
lets grid choose its width; rotation is in radians, positive counterclockwise.
Spacing is a minimum and automatically expands to fit the selected units.

`rts.queueScriptFormationMove(tick, subjects, x, y, append, kind, spacing, columns,
rotationRadians)` records the same command for a future simulation tick. It requires
a configured script world and uses the existing command log, including v4 when
needed. Invalid subjects/layouts/geometry return failed Results before changing
unit orders or queued input. These methods are Move commands; the existing
`attackMoveUnits` convenience method retains its grid behavior.

```squirrel
local moved = sim.moveFormationUnits(ids, 20.0, 12.0, false,
    "wedge", 1.5, 0, 1.57079632679);
local queued = sim.queueScriptFormationMove(120, ids, 30.0, 12.0, true,
    "column", 1.5, 0, 0.0);
```

### Coordinated movement groups

`RTS::submitMovementGroup(MovementGroupBatch)` accepts an immediate Move batch with
at least two distinct live owned units. The batch supplies stable unit identities,
target, formation layout and positive `leadDistance` in world units. This creates
movement membership independently of player selection. Members bind to the exact
issued order, so replacement/completion, death, containment or stale generation
remove them; a single remaining member resumes its ordinary movement speed.

The current coordinator compares remaining navigation-route distances each step.
Leaders begin slowing when their remaining route is shorter by more than
`leadDistance`; the next `leadDistance` smoothly reduces their speed factor to zero.
Lagging members retain their own speed limit, including morale/command/effect
modifiers. Native motion and injected Crowd consume the same derived factor.
Traffic-yielding, convoy-waiting and retreat-covering members temporarily do not
set the group's pacing limit. They retain membership and rejoin pacing when their
wait ends, allowing the traffic winner to clear a passage instead of waiting for
the unit that is yielding to it.

The coordinator also translates the assigned slots around a
moving anchor derived from the members' current positions. Its lookahead is
`leadDistance`, capped at the destination. Members assemble during travel and
retain their assigned offsets. Reaching an intermediate slot does not complete
the Move order. The formation target is refreshed each step and removed when the
group dissolves. Any yielding member suspends this shared target for that step.

With a pathfinder, shared steering requires radius-clear segments from every
member to both its final destination and its temporary slot. A conservative
swept-box test respects grid origin and scale, including out-of-grid boundaries.
Each query inspects at most 4096 grid cells; larger queries retain path steering.
When clearance fails, members follow their existing paths at their own speed
limits, subject to traffic reservations. This releases the formation through
constrained passages and restores it automatically when all routes clear again.
Validated direct routes consume the remaining cached waypoints so units do not
backtrack after reforming. This is loose passage traversal; explicit ordered
column compression, shared-path turning slots, appended group commands and
AttackMove groups are still required extensions.

Traffic reservations cover connected narrow corridors, rather than only the next
cell. A moving occupant's exit direction takes precedence over outside entrants;
movement priority and stable subject identity break ties. Opposing entrants wait,
and same-direction candidates still arbitrate their next narrow cell. The system
looks ahead up to three route waypoints and rebuilds reservations each step, so
removing an occupant releases its reservation without a persistent lease. It uses
the planned navigation goal for moving entity targets and patrol legs.
Corridors over 1024 cells return a structured unsupported diagnostic. This admission
policy prevents the tested opposing squads from entering together. If opposing
units are already inside the same corridor, the losing direction receives a
temporary evacuation target toward the selected exit and then a nearby clear side
bay when one exists. This target does not replace the original order, is excluded
from snapshots, and route planning is invalidated after the evacuation step so
the unit resumes from its new position. If no radius-clear exit or side bay exists,
the unit keeps waiting rather than publishing an unsafe retreat segment.

Squirrel exposes `moveGroupUnits(subjects,x,y,kind,spacing,columns,rotation,leadDistance)`
and `queueScriptGroupMove(tick,subjects,x,y,kind,spacing,columns,rotation,leadDistance)`.
The queued form records `EVERTS_COMMANDS` v5 and applies through the same admission
boundary. Versions 1–4 retain their previous format for non-group inputs.
`RTSStateSnapshot` v2 includes stable member subjects, exact order ids and lead
distance; v1 restores with no groups. Group references and order bindings are
validated before publishing restored state. Runtime ECS handles are rebound and
derived pacing factors and moving targets are reset rather than treated as
persistent authority. Neither derived value changes the snapshot schema.
# 显式推进与交互策略

原生地图适配边界为拥有型 NavigationSurfaceSnapshot：强类型 surface/movement ID、
表面/运动类别/成本模型 revision、clearanceRadius、网格几何及两套成本数组。
terrainCosts 为原始物理地形，navigationCosts 为按类别预膨胀后的导航成本；导航不能解除原始墙体。
prepareNavigationSurface 先验证与复制，commitNavigationSurface 原子发布，拒绝过期或错目标计划。
同身份/同版本的矛盾内容被拒绝，完全相同投影为 no-op；手动地形修改清除导入导航投影的来源声明。
Flow 代理半径超过导入类别的 clearanceRadius 时停止导航驱动，StepReport 报告 navigationClassMismatches。
当前导航输入仍只有一个活动运动类别投影；每代理类别绑定与原生多目标共享缓存已提供，多类别投影共存仍待实施。
地图侧 captureNavigationGrid 返回不借用 Pathfinder/TileLayer 的不可变捕获，内容变化推进捕获版本。
RTS 上层 projectCrowdSurface 将捕获转换为独立物理/导航成本，按预算进行圆足迹膨胀，
保留四/八邻域和目标进入成本；hex、方向门控及小于 1 的正成本会明确拒绝。
该适配器在显式导航准备边界调用，尚未自动挂接每帧 RTS 更新。

原生单目标建场使用 `prepareFlowBuild(gx,gy)` 返回 move-only FlowBuildPlan，
准备期间 live field 保持不变；`commitFlowBuild(plan)` 在 owner/来源 revision 匹配时无分配发布。
计划从 Pending 开始；`advanceFlowBuild(plan,maxWork)` 每次最多消费 maxWork 个堆弹出/方向格单元，
报告分别统计两类工作。来源版本变化时取消工作；完成前不能发布。
完成状态区分 Ready 与 Unreachable；显式发布 Unreachable 会替换为不可达场，不代表代理已到达。
计划可显式 cancel，重复取消为 no-op；错目标、取消和重复提交均明确失败。
旧 buildFlowField 驱动同一分步内核至完成。
脚本 requestFlowBuild(gx,gy) 返回标准 Result 中的 token，随后使用 getFlowRequest、
advanceFlowRequest(token,maxWork)、commitFlowRequest、cancelFlowRequest 和 releaseFlowRequest。
token 为 kind="crowd.flowRequest"、schema=1、整数 owner/index/generation；未知 schema 拒绝，
额外字段忽略。token 仅在本进程有效，不能持久化或跨进程复用。
最多保留 64 项请求；取消释放工作缓冲，release 释放注册表槽并使旧代次失效。
checkpoint 不保存瞬态请求，恢复会清除记录并使旧 token 失效；调用者重建声明式请求。

`getFieldStatus()` 原生返回拥有型 FieldObservation；脚本返回标准 Result envelope，读取 `.value`。
state 为 unconfigured/unbuilt/ready/stale，revision 和 builtRevision 在脚本中使用十进制字符串，
goals 为去重后的目标格数。重复设置同一地形值或已有目标不使场失效。
推进报告携带 navigationState（上述状态依次为 0/1/2/3）和 navigationRevision；
正时长空世界及零时长同步也提供同一观测。ready 只表示构建版本有效，不保证查询位置可达。
过期场默认不再提供方向；方向为零不能据此宣布订单已到达。

热路径使用出生时确定的数值 tieKey。具名代理按稳定 ID 的 UTF-8 字节哈希，
匿名代理使用不回绕创建序号；两者分属独立命名空间。swap-pop、批量复制和快照保留键。
具名哈希碰撞在准入时明确拒绝：createAgents 返回 Conflict，旧 addNamedAgent 返回 -1。
不以 dense slot 或每帧字符串比较作为平局回退。wander 初相位由同一身份派生，
具名创建顺序不改变初相位；这尚不等同完整 named RNG stream 协议。

原生 `captureSnapshot()` 返回拥有型、不可变的进程内 checkpoint，覆盖代理、交互策略、场和仿真时钟。
`prepareRestore(snapshot)` 先复制完整运行态并分配新 owner，`commitRestore(plan)` 无分配发布，旧 AgentRef 全部失效。
计划只能提交给准备它的 Crowd；提交成功后被消费，另一轮恢复也使旧计划失效。
消费者按稳定名字重新绑定。该 checkpoint 尚不是持久化格式，版本化序列化和导航 revision 恢复仍待实施。

`advance(CrowdStep)` 接收下一连续 `SimulationTick`、纳秒 `Duration` 和确定性的操作预算。
正时长即使世界为空也消费 tick；零时长只接受当前 tick，不消费时间。
`advance(float)` 是兼容入口，按纳秒量化时长并内部生成 tick。一个 Crowd 首次正时长推进后
不能混用兼容时钟和外部时钟。预算耗尽、非法输入或求解失败保留位置、速度、朝向、wander 和时钟，
调用者可修正输入后用同一个 tick 重试。成功报告的 quality 是位标志，预测截断和残留接触可同时存在。
`maxContactChecks` 同时约束投影中的接触测试和最终残留检查；报告分别统计二者。

脚本 `advanceTick({tick=1,deltaNanoseconds=16666667})` 使用整数输入；可选 budget 必须包含全部六项预算字段。
脚本 tick 输入范围为非负 int64；报告及 AgentFrame 的 tick 用十进制字符串无损输出。
脚本报告 elapsedNanoseconds 为整数。帧快照是拥有型复制，不借出跨帧指针。

`InteractionPolicy` 独立设置 avoidanceLayer/avoidanceMask 和 contactLayer/contactMask。
出生、重定位、身体投影使用 contact 筛选；预测避让和 Boids 邻居使用 avoidance 筛选。
旧 `AgentInteraction` setter 同时设置两套筛选，getter 返回 contact 字段；需要保持独立策略时使用现代 API。


### 每代理运动类别绑定

原生 SpawnRequest::movement 与 AgentUpdate::movement 使用拥有型 MovementBinding，包含
MovementClassId 和类别 revision。AgentState/AgentFrame 和进程内快照保留此选择；交换删除
不会把类别绑定到错误的代理。updateAgents 对整个批次先验证，再发布；ID/revision 必须同时为
正数，或同时为零。零/零显式采用兼容模式，不是自动选择其他类别。

Flow 只使用与绑定身份、版本及半径相符的导入场。类别缺失、版本过时、类别不同或半径超出
clearance 时暂停 Flow 方向，并累计 StepReport.navigationClassMismatches；局部分离、接触、
墙约束仍执行。Seek/Boids 是局部意图，不使用该 Flow 类别匹配。这一批不提供多类别流场缓存，
也不宣称 RTS 的单代理 A* 已改为运动类别导航。

脚本 createAgents 的每项可提供 `movement = { classId = 7, revision = 2 }`；updateAgents 同样
支持该可选表。两个输入必须是精确整数，禁止浮点身份和不完整表；输入范围为非负 int64。
readAgent/copyAgentStates/copyAgentFrame 的 state.movement 用十进制字符串返回 classId/revision，
保留原生 uint64 精度。旧 getAgentState 类投影保持兼容；新身份信息使用上述拥有型批量接口读取。
RTS 普通数值更新不覆盖此字段，因此原生 game 在绑定后设置的代理类别会保留。


原生 prepareFlowBuild(span<FlowGoal>) 接受 1..1024 个目标格，复制并按坐标排序去重，
通过同一 prepare/advance/commit 内核构建最近目标共享场。所有坐标先验证；空集合或越界目标
拒绝且不改变当前场。阻挡目标允许保留：只有全部目标都不可达才返回 Unreachable。
单目标 prepareFlowBuild(gx,gy) 委托这一入口。调用方可在 prepare 返回后释放/修改输入集合。
规范化目标集用于原生缓存键；共享缓存/lease 入口见下节。不同运动类别投影共存与按类别自动请求仍待实施。


导航输入与场发布使用不同版本：getFieldStatus 的 inputRevision 仅随几何、地形代价、
导入导航成本/规则变化递增，脚本仍使用十进制字符串返回。目标集编辑和其他 FlowBuildPlan
的提交不改变这一输入版本，因此同一输入上的不同目标请求可分别推进/发布。
revision/builtRevision 继续描述当前默认场的状态；每次 commitFlowBuild 都分配新的单调发布
版本，避免旧准备态把观察版本倒退。显式提交某目标请求就是选择新的默认场；场的目标变化
不会自动取消其他拥有型请求。实际地形或类别投影改变仍会在推进前取消旧输入请求。
快照包含两个版本；restore 的新 owner 继续使所有旧请求身份失效。


### 原生多目标共享缓存与 lease

先用 acquireFlowField(span<FlowGoal>) 查询当前输入的 ready 缓存。NotFound 时调用已有
prepareFlowBuild/advanceFlowBuild，再用 acquireFlowField(std::move(plan)) 发布进缓存；它不改变
默认场。两条入口都返回 FlowLease。缓存精确比较规范化目标坐标、输入版本、surface/class/cost
来源和算法版本，不只比较 hash。等价请求共用 generation；未完成、过期或外来计划拒绝。

AgentUpdate::flow 绑定某个 lease；空 lease 显式恢复默认场选择。Flow 代理直接读取它的不可变场，
无需帧内 key 查找。输入过期后停止该场导航驱动，不回退到其他目标；最新地形仍约束身体。
源类别/半径不匹配和过期 lease 均计入 navigationClassMismatches。Seek/Boids 不使用缓存场。
默认场 getFieldStatus 不能代表代理选中的缓存场；sampleFlowField 返回明确 Reachable/Unreachable，
过期/外来 lease 则返回失败，Unreachable 不等同游戏到达。readAgent/frame 的 flowGeneration
是只读运行时观测（脚本为十进制字符串），不能从数字重建 lease。

configureFlowCache 设置 0..8 GiB 的保留存储预算（默认 64 MiB，最多 10000 项）。计费包含各场向量
capacity 与资源/条目元数据，不含 allocator bookkeeping、在建请求、独立 checkpoint 持有的资源
或 cache 元数据容器的有界空余容量。只淘汰没有实际 lease 的记录；按最后 acquire 的仿真 tick
排序，平局按完整稳定 key 排序。计时不参与顺序。所有条目被 pin 时返回压力失败，保留预算、
缓存和原准备计划；可以显式释放/替换 lease 后重试，不静默超预算。

lease 复制会保持 pin，释放最后一份才解除。复制、查询和释放都在仿真 owner 线程进行；lease
不保持 Crowd 存活，provider 先销毁也能安全释放字段资源。快照的无 lease 缓存记录仅共享数据，
不会伪装成实际 pin；恢复更新世界 owner 并重建代理绑定，外部旧 lease 失效。模块卸载前释放
这些进程内 C++ 值；它们不是持久化格式。当前入口为原生，脚本 lease 协议、RTS 共享订单选场、
多运动类别投影共存及 pending 构建去重仍待完成。


脚本可在既有 requestFlowBuild/advanceFlowRequest 完成后调用 acquireFlowRequestLease(token)。
该请求记录持有一个原生 lease，不改变默认场；返回 generation 字符串。随后
sampleFlowRequestLease(token,x,y) 返回拥有型 {state="reachable"/"unreachable",x,y}，
过期地形、外来或释放后的 token 返回失败。releaseFlowRequest 同时释放 pin；restore 清空请求
记录并释放 pin。重复 acquire 拒绝；原生缓存采样与脚本采样使用同一可达性实现。
这一入口不是独立脚本 lease 身份，脚本代理绑定与 RTS 订单共享场迁移仍待接入。


脚本 updateAgents 的每项可提供 flowRequest=已 acquire 的 request token，把同一个 lease
复制到代理；releaseFlowRequest 只释放请求记录，代理仍持有 pin 并继续使用该场。
useDefaultFlow=true 显式解除该代理的 lease，恢复默认场选择；不能与 flowRequest 同时提供。
坏 token、外来 owner、没有 lease 或过期输入会使整个更新批次失败，不发布部分代理参数。
请求 token 字段用 raw 读取，不调用 delegate/_get；释放后的 token 不能再次绑定，但已绑定的
代理 lease 保持有效，直到实际输入变化或显式解除。


接触暂存按 canonical pair 生成 delta，再通过 incident CSR 按 agent 归并。CSR 每轮有
N+1 个 offset 和 2P 个索引，P 受 `maxContactPairs` 限制；初始化和归并也计入
`maxPairVisits`。默认执行为串行；可选同步执行器现已调度接触 delta 和固定顺序归并。
期望方向数学与预测速度评分均已并行，不能将接触并行作为完整速度阶段或大规模战斗验收。


原生 composition root 可通过 `Crowd::setWorkScheduler` 选择 `CrowdExecution::Parallel` 并注入
`CrowdWorkScheduler` 共享 lease；缺少 provider 明确拒绝。默认 Serial 使用相同 256 项逻辑
partition，当前速度评分/加速度/朝向与接触 delta、incident 归并进入执行器；owner 准备完整邻居 CSR 与 Flow 数值，worker 计算期望方向。
启用 `crowd_thread` 卫星后，`makeJobSystemCrowdScheduler` 适配已有 JobSystem。
用 `std::shared_ptr<thread::JobSystem>(thread::createJobSystem(workers))` 建立真实 pool owner，
工厂只保存 weak owner；禁止对借用裸指针加空 deleter 冒充共享生命周期。调度中禁止 stop。
调度锁住 pool owner，全部工作 join 后才返回；释放外部 owner 不会破坏在途调用，下一调用
显式失败。worker 失败整步回滚，不默默重跑串行路径。快照不持有执行器，恢复保留当前配置。
这是原生线程资源接线，未增加脚本线程配置入口。


预测邻居在 owner 线程按距离/tie-key 准备为各 agent 独占数值列表，每列表活跃长度不超过
maxNeighbors，选择过程活跃项数至多 maxNeighbors+1（设置上限 128），容量可保留历史峰值。评分仅在固定快照上
读取旧位姿/速度；worker 写自身 next velocity、speed、heading，join 后统一交换速度列并积分。
预算用与内核同源的采样数组精确预检（当前每邻居 45 次评分），不使用浮点 atomic 或 worker
共享计数器。初始化 O(N) 与保留 scratch 容量不是完整准备 CPU 预算的完成证据。


完整期望方向邻居使用 count/prefix/fill CSR，不复用截断的预测邻居。
maxPreferredNeighbors 默认 1000000、允许 0..10000000；maxPreferredVisits 限制工作列初始化、
count/prefix/fill、Flow 数值准备与三力逐项计算；底层查询仍由 maxNeighborVisits/maxSpatialVisits 限制。
耗尽整步回滚，不跳过邻居。两个预算字段在 advanceTick 的 budget 中可省略，但提供时须为精确整数。
preferredNeighborsPeak/preferredVisits 在 advance 与 advanceTick 两条报告中可观察。
原有保守查询范围和 float 三力边界保留；不贡献启用三力的索引不进入 CSR。
Flow 的 owner-only 借用只在准备阶段使用，任务仅消费拥有型方向/减速数值，不保存 Field 指针。


需要在后续提交阶段避免分配时，先用 prepareAgentUpdates 校验/复制批次，再将 move-only token 交给
commitAgentUpdates。selection 为空表示全部；否则每个预备项对应零/一字节。成功路径不分配工作空间，
所有身份/flow lease 与掩码在写入前重查；即使未选择的身份失效也整批拒绝，失败保留 token。
计划不保留 slot 或 Crowd 指针，因此压缩可重新解析、provider 先销毁安全、restore 使旧 owner token 失效。
这是原生 composition API；脚本 updateAgents 与旧原生同名操作保留为 compatibility-only facade，
均委托同一个 canonical prepare/commit 内核，没有第二条廉价更新实现。


### 终点让位与窄口圆形足迹

Flow 转向在静态障碍边缘先对齐单元格入口的垂直轴，再跨越相邻可达单元格；半径与预测 margin 共同确定可用入口宽度。这是 owner 阶段准备的数值方向，串行和并行工作态消费相同数据。

Seek 行进时，只有开启实体接触、双方接触掩码匹配、邻居可推动且未 HoldPosition，才会考虑通过实体接触让位。可让位状态包括 Idle、最大速度低于来者的 5%，或已在自己的 arriveRadius 内且速度低于自身 maxSpeed 的 5%。这使被编队 pacing 暂停的成员仍能让后排追上。实体接触和墙体约束继续生效；Hold、不可推动、无接触的邻居仍参与预测避让。该规则没有新增保存状态。


### 2026-10-14 CPU 性能优化

预测避让保留原来的速度样本、邻居上限、身体接触和墙体约束。停稳状态每个邻居每子步计算一次，复用于所有查询；不可能进入已满最近集合的候选省去插入排序。最终仍按距离和稳定 tie key 排序，截断质量标记保留。候选评分的各项代价均非负，累计值已经无法胜出时结束该候选；零代价结果直接保留。`avoidanceChecks` 继续作为保守的预测检查预算预留上界，不能当作剪枝后实际执行的检查次数。
