# NPC AI

**脚本入口：** `eve.NpcAi()`（槽位 `npc_ai`）

The `npc_ai` module is EVEngine's data-oriented orchestration runtime for authored
NPC behavior. It combines a hierarchical state tree with typed blackboards,
event-driven perception, bounded scheduling, task providers and reservable Smart
Objects. It intentionally does not introduce a universal actor base class.

## Squirrel 快速开始

```squirrel
local npc = eve.NpcAi();
local created = npc.newWorld(2048, 128);
assert(created.ok && created.value.ownership() == "owned");
local world = created.value;
assert(world.registerBehavior(behaviorJson).ok);
local agent = world.createAgent("guard").value;
world.setBlackboardString(agent, "enemy", "player");
world.signal(agent, "enemy_seen");
local tick = world.tick(1, 1.0 / 60.0, 64, 8);
local snap = world.snapshot(agent);
```

模块提供 `getName`；`newWorld(traceCapacity, maxMemoriesPerAgent)` 返回脚本 GC
拥有的 `NpcAiWorld`（`ownership` 恒为 `"owned"`）。
`registerBehavior` / `validateBehavior` 接受 BehaviorDefinition JSON（`id`、
`schemaVersion`、`initialState`、`blackboardSchema`、`states`）。Agent 以打包
`int64` handle 表示；`createAgent` / `destroyAgent` / `isAgentStale` 与销毁后的
`snapshot` / `snapshotJson` 失败用于检测失效。黑板写入用类型化 setter：
`setBlackboardBool` / `setBlackboardInt` / `setBlackboardNumber` /
`setBlackboardString`。感知用 `remember` / `forget`，调度用 `tick` / `signal`。
`ITaskService` 可通过 C++ `registerTaskService` 或脚本 `registerTaskProvider` 注册；
无任务 provider 时，含 task 的行为会在 admission 失败。

## Behavior runtime

Create a `BehaviorDefinition`, declare its blackboard schema and states, then publish
it through `NpcAiWorld::registerBehavior`. Publication validates the entire graph
before changing the registry. Agents receive generation-qualified `AgentHandle`
values; resolve operations reject handles after destruction and slot reuse.

The active state is a leaf. Every state from the root to that leaf is active, so
parent tasks such as awareness or hit reaction can run concurrently with leaf tasks
such as patrol or attack. Transitions are evaluated leaf-to-root. A target's complete
enter-condition path is checked before any current task is stopped.

Task implementations derive from `ITaskService`. The world takes unique ownership of
each provider. Navigation, animation, combat and project-specific gameplay remain
outside the core and are connected as providers. Missing providers are detected
before callbacks; if a later task fails to start, tasks started during the same
admission pass are stopped with `StopReason::StartupRollback`.

## Perception and diagnostics

`remember` stores bounded `(subject, sense)` memories with confidence, payload and a
simulation-tick lifetime. Updating a memory queues `perception.<sense>`; expiry queues
`perception.forgotten.<sense>`. No wall-clock time is read by the runtime.

`snapshot` returns an owning copy of the active path, blackboard and perception state.
`trace` returns fixed-capacity lifecycle records suitable for an editor debugger or
automated gameplay diagnosis.

`archive` returns an owning `AgentArchive` with schema id/version and logical ids only.
`restoreAgent` validates the behavior, active path, blackboard types, perception budget
and task ownership before publishing a new generation handle. Incomplete task providers
restart from their opaque memory; process-local navigation tickets are never revived.

## Smart Objects

`SmartObjectWorld` registers world interactions such as cover points, doors, seats or
work stations. Queries filter free slots by activity, required tags, distance and
stable ordering. A successful claim returns a generation-qualified lease handle.

Call `releaseAgentClaims` before destroying an NPC. When an object is removed first,
choose `RejectClaimed` to preserve claims or `CancelClaims` to invalidate them
explicitly. `expire` provides bounded cleanup for abandoned leases.

## Navigation and ECS

`NavigationTaskService` adapts an async `INavigationProvider` to state tasks. A request
factory turns authored task parameters into a provider request; start returns without
blocking, tick polls the ticket, and transition/destruction abandons outstanding work.

`NpcAgentState` stores only a generation-qualified link to `NpcAiWorld`.
`NpcAiEcsSystem` checks links before the AI step and publishes an owning
`NpcAiProjection` afterward. It closes each ECS view before task callbacks run and does
not mutate entity structure during iteration. Call `releaseAgent` for entity-first
cleanup; world-first cleanup is detected as a stale link and clears safely.

## Determinism and threading

All mutation is simulation-thread-affine. Callers inject simulation tick and dt.
Agent scheduling is stable and round-robin within an explicit per-frame budget;
transition loops have a separate per-agent budget. Provider floating-point results
must document their tolerance contract.

The contract suite includes a 2,048-agent population advanced in 128-agent batches;
every agent receives progress after one complete round. This is a deterministic budget
proof, not a hardware performance benchmark.

## DecisionContext（`eve.Decision()`）

轻量黑板 / FSM / utility / 影响图工作区由本模块托管，脚本入口仍是 `eve.Decision()`
（槽位 `decision`）。与层次状态树 / 感知记忆的 `NpcAiWorld` 互补：前者是显式
workspace，后者是行为编排运行时。用法见 [决策](decision.md)。共享条件树见
[条件树](condition.md)。

### 脚本任务提供方

`world.registerTaskProvider(type, providerName)` 把已通过 `eve.provider.publish` 发布的脚本实例
接入 `ITaskService`。注册时捕获 generation；卸载或同名重发布后旧任务返回 stale 错误，
不会自动绑定另一实例。游戏仍决定路径、移动、攻击及感知到行动的策略。

提供方实现 `start(context)`、`tick(context)`、`stop(context)`：

- start 返回 `{state="running", memoryJson="{}"}`；tick 的 state 可以是
  running/succeeded/failed。memoryJson 可省略，提供时必须是有效 JSON 字符串。
- context 是拥有型投影，包含 agent/state/tick/dt/task/parametersJson/memoryJson/blackboard/
  perception；stop 另含 stopReason（completed/transition/startupRollback/agentDestroyed/definitionReplaced）。
- stop 返回值不参与业务决策，错误记入引擎日志；原生清理继续执行。
- 回调不可重入同一 NPC 世界；脚本修改/查询入口返回 Conflict。提供方在脚本线程调用，
  不持锁，不保留任务上下文引用。世界销毁先停止任务，提供方由现有 registry 管理并在 VM 退出前释放。
- 每次任务调用需复制黑板/感知投影，成本随该 agent 数据增长；高频大批量原生行为可继续使用
  C++ `registerTaskService`。两者最终进入同一 NpcAiWorld 调度器。

注册阶段的名称查找只做一次。运行时保存 `ScriptProviderHandle`，复用现有 provider 生命周期和
调用机制（R-COHESION-3/6、R-FACE-5/6/7、R-MECH-1/4），不创建新的 provider 注册表。
