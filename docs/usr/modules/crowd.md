# 群体与 RTS 移动（Crowd）

**脚本入口：** `eve.Crowd()`

Crowd 是 UI/渲染无关的连续流场与群体模拟库。项目负责把地形、Tilemap 或建筑占用转换为
`setCellCost/setBlocked`，并把 `getAgentState` 映射到 ECS、Scene 或自定义渲染器。

## 稳定身份

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
