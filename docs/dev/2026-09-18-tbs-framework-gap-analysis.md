# Turn Based Strategy Framework 4.2.0 对照分析与 EVEngine 策略组件改进/移植方案

日期：2026-09-18
分支：`codex/tbs-framework-gap-analysis`
worktree：`.worktrees/tbs-gap-analysis`
素材：`C:\baidunetdiskdownload\Turn Based Strategy Framework 4.2.0.unitypackage`（55,913,923 B）

状态：分析文档（只读取证，**未改动任何产品代码**）
方法：解包源码直读 + 关键词全量扫描 + 并行子代理清点；每条结论附 `file:line`

> ## ⚠️ 基线说明（先读这一段）
>
> 本文**主体**（§1–§9 的取证）基于本地 `dev` 的 `dae6a644c`（2026-09-15）。
> 该基线当时**落后 `origin/dev` 199 个提交**。
>
> 事后已把分析 worktree 刷新到 **`origin/dev` 的 `d486eff1e`（2026-09-18）**，
> 并按新基线**逐条重核了全部 headline 结论**——结果见 **§10**。
> 其中 **4 条已失效**，最重要的一条是：`hexmap` 已作为
> **PR #417 合并进 dev**（不再是"未提交"），`combat` 也已获得完整的脚本出口。
>
> **引用本文任何结论前，请先看 §10 的「旧 → 新」对照表**，
> 或直接以 §10 为准。

---

## 0. 结论先行

1. **TBS Framework 的核心异常地薄。** 解包后 1813 个资产里，引擎无关核心
   `Assets/TBSFramework/External/tbsf-common/` 只有 **78 个 .cs**（239 KB），
   Unity 表现层 `Scripts/` 67 个 .cs（156.6 KB），
   编辑器工具 `Editor/` 11 个 .cs（64.7 KB）；剩下 **1544 个是示例工程资源**
   （含 217 MB 的旗舰示例 ClashOfHeroes）。
   它真正值钱的是**接缝（seam）设计**，不是功能数量。
   而且 `External/` 里**没有任何第三方库**——全部是它自己的代码。

2. **我们的仿真内核明显强于它。** 确定性、快照/回放、原子提交、`Result` 错误模型、
   反应栈、ECS 短根、9 个专用测试文件——都是它的短板。它自己的
   `ICommand.Undo` 是**从不被调用的死代码**（0 处调用点），
   范围攻击命令三个方法直接 `throw new NotImplementedException()`，
   AI 用**未播种的 `new Random()`**，唯一的确定性钩子
   `InitializeRng(seed)` **也从未被调用**。

3. **我们真正的缺口有两道。**
   证据：`examples/tactics/main.nut` 是 **31,321 B 的 Squirrel**，
   在里面重新实现了 6 类敌人 AI、每人 3 个技能、鼠标选格、可达/可攻高亮、
   伤害飘字、模型换装。原因不是我们没有这些能力，而是：
   - **(a) 引擎里已有的能力运行时既没有脚本出口，其中一部分甚至没有任何消费者。**
     `combat`、`npc_ai`、`settlement`、`grid` 四个模块的 `.cpp` 里
     **`Module_IMPL` 数量为 0**；`action` 的 1 个只是编辑器时间轴
     （`ActionEditorModule`），**`AbilityRuntime` 不可达**。
     更糟的是好几个组件的**生产代码里根本没有调用方**（只有测试）：
     `npc_ai` 在 `src/` 内零消费者，`NpcAiWorld`/`NpcAiEcsSystem` 无人引用，
     `INavigationProvider` 只有测试替身，
     `ActionNotifyRegistry::dispatch` **连测试都没调用**，
     `combat::standardCombatAbilities()` 只有测试调用，
     `combat` 整体只被 `rts` 消费。
   - **(b) 内核之上没有可复用的交互/表现层。**
     TBS Framework 用 `IAbility` + `GridState` 状态机 + `MarkAs*` 表现状态 +
     `Highlighter` 摊销掉的东西，我们这里一件都没有。

**一句话**：我们不缺能力，缺的是**把已有能力接到脚本、接到真实调用路径上**，
以及**在内核之上组装成战棋的那一层**；而 TBS Framework 恰好只有那一层。
两边互补，不是替代。

**最高风险项（两经更正，最终版见 §10）**：本文曾把 `hexmap` 判为
"完全未落地"，后又更正为"已接线但全未提交"。
**按 `origin/dev` 新基线，正确结论是：它已作为 PR #417 合并进 dev**——
31 个文件受版本控制、manifest 声明齐备、`module_list.nut` 有条目。
两次误判同源，都因为我先按旧 HEAD 建 worktree 再分析（详见 §1.4、§5.12、§10）。

当前真正的同类风险是**主工作区里正在进行、尚未提交的球面六边形工作**
（`HexSphereMap` / `HexSphereTopology` / `examples/hex-planet`，今天 15:54 后新建）。

---

## 1. 取证方式

### 1.1 解包

`.unitypackage` 是 gzip 压缩的 tar，内部每个资产一个 GUID 目录，含
`pathname`（路径）、`asset`（内容）、`asset.meta`。两个坑：

- `pathname` 的内容是 `路径\n00`，末尾带 `\n00` 尾巴，直接读会把 `00` 当成路径一部分；
- **文件夹条目只有 `pathname` + `asset.meta`，没有 `asset`**，当文件读会报错。

所以索引脚本必须 `ReadAllText(...).Split('\n')[0].Trim()` 且容忍缺失的 `asset`。
解包产物落在 worktree 的 `.tbs-research/`（临时取证目录，交付前可删）。

### 1.2 我们这边的一个重要前提

创建 worktree 时（commit `dae6a644c`），主工作区里
**`src/modules/hexmap`（31 文件）、`test/hexmap.cpp`、`docs/usr/modules/hexmap.md`、
`examples/hex-terrain-3d/` 全部是 untracked**，worktree 里一开始没有它们。
本分析已把它们复制进 worktree 后再取证——否则会得出
「我们没有六边形地图层」的错误结论。

> 顺带暴露了本次分析的**最高风险交付问题**：`hexmap` 已实现、已接线、能编译，
> 但整块工作（含 `cmake` 里的模块声明）**全部未提交**。
> 而且这个风险当场发生了一次：我的 worktree 建在 HEAD，
> 因此一度误判为"模块不存在"。详见 §1.4 与 §5.12。

### 1.3 素材规模

| 层 | 文件数 | 说明 |
|---|---|---|
| `External/tbsf-common/**` | 78 .cs / 239 KB | 引擎无关核心（`TurnBasedStrategyFramework.Common.*`） |
| `Scripts/**` | 67 .cs / 156.6 KB | Unity 表现层（`TurnBasedStrategyFramework.Unity.*`） |
| `Editor/**` | 11 .cs / 64.7 KB | Inspector / 网格生成器工具 |
| `Examples/**` | 1544 资源 + 99 .cs | ClashOfHeroes / TilemapExample / LegacyDemos / Features / Tutorial |
| TextMesh Pro + ProjectSettings | ~40 | 第三方与工程配置 |

**关键事实**：`Assets/TBSFramework/External/` 里**只有一个条目**——它自己的
`tbsf-common`。也就是说**没有任何第三方库被捆绑**：没有行为树资产、
没有补间库（动画全部是 `Time.deltaTime` + Unity `Awaitable`）、
没有网络 SDK（传输层是抽象类）。这降低了移植的许可证风险。

### 1.4 方法论教训：按 HEAD 建 worktree 会漏掉工作区的已跟踪修改

**这是本次分析犯过的一个真实错误，值得记录。**

为不打扰主工作区，我把 worktree 建在 `HEAD`（`dae6a644c`）。
主工作区当时有 26 个 `M`（已修改未提交）与 1 个 `A` 状态的**已跟踪**文件，
外加 51 个 untracked 文件。我意识到 untracked 的 `hexmap` 会缺失并做了补拷，
**但没有补拷 `M` 状态的已跟踪文件**——而 `hexmap` 与构建系统的接线
恰恰就在其中一个 `M` 文件里（`cmake/module_manifest/rendering_simulation.cmake`）。

结果：我一度得出"`hexmap` 完全未落地"的错误结论（§5.12 已更正）。
**正确的取证方式是 `git worktree add` 之后同步工作区改动，
或者直接在主工作区只读分析。**

**我据此对本文的 headline 结论回到主工作区逐条复核**：

| 结论 | 在**主工作区**复核结果 |
|---|---|
| `combat`/`npc_ai`/`settlement`/`grid` 的 `Module_IMPL` 数为 0 | ✅ 仍为 0（`action`=1、`sensing`=1、`tactics`=1、`hexmap`=1） |
| `combat` 在 `src/` 内只被 `rts` 消费 | ✅ 不变 |
| `tactics` 的 `actionPoints` 从不递减 | ✅ 不变（`-=`/`--` 0 命中） |
| `npc_ai` 生产代码零消费者 | ✅ 不变 |
| `hexmap` 未落地 | ❌ **错**——已完整接线，只是全未提交（§5.12） |

仍未纳入本文证据基线的主工作区改动（对结论无影响，但需知悉）：
`src/engine/common/Runtime.cpp`、`src/engine/common/SquirrelOwnership.h`、
`src/modules/rpg/RPG.cpp`、`src/modules/ui/*`、
被删除的 `scripts/check_bindings*.{py,json,txt}`、新增的 `test/rpg_script.cpp`。
**被删除的 `check_bindings*` 工具链值得单独注意**——它正是用来检测
"脚本绑定缺口"的，而"绑定缺口"是本文 §5.6/§5.8 的核心议题。

---

## 2. TBS Framework 4.2.0 拆解

### 2.1 三层结构

```text
tbsf-common   (78)   纯 C#，无 UnityEngine 依赖 —— 唯一的可移植部分
├─ controllers/        GridController, IGridController, GridState + 4 个状态
│                      turnResolvers/{ITurnResolver, SubsequentTurnResolverImpl}
│                      gameResolvers/GameResult
├─ units/              IUnit, ICombatant, IMoveable, MoveComponent, CombatComponent
│                      IUnitManager, UnitHelper
│                      abilities/{IAbility, ICommand, Move|Attack|EndTurn|MultipleTarget}*
├─ cells/              ICell, ICellManager, CellHelper, SquareHelper, HexagonHelper
├─ pathfinding/        PathfindingAlgorithm, AStar, Dijkstra, 4 种优先队列
├─ ai/                 30 个：behaviourTrees(10 基节点 + 6 自定义) + evaluators(9) + 2 selector
├─ players/            IPlayer, IPlayerManager, HumanPlayer
├─ network/            INetworkConnection, NetworkException
└─ utilities/          Vector2Int/3Int/3Impl, GridUtilities, IVectorArithmetics

Scripts (67)  Unity 层。字节占比：units 25% / network 16% / highlighters 15.5% /
              ai 12% / cells 11% / controllers 7% / players 6% / gui 0.5%
              这层绝大多数是**零逻辑转发器**：AttackAbility、MoveAbility、
              AttackRangeHighlightAbility、SubsequentTurnResolver、
              SubsequentUnitSelector、HumanPlayer、4 个 utilities/*Extension
              都是薄包装，真逻辑在 tbsf-common。
              核心是 Unit.cs(25,310 B, 573 行) + 21 个 Highlighter(24 文件)

Editor (11)   GridHelper.cs(31,060 B，包里最大的手写文件) / CellBrush / UnitBrush /
              GridCoordinatesGizmo / 5 个 ICellGridGenerator 实现 / GridHelperUtils
```

### 2.2 最值得看的九个接缝

这是本框架**全部的设计智慧**所在，也是唯一该借鉴的部分。

#### (1) `ICommand`：可序列化、可撤销的命令对象

`common/units/abilities/ICommand.cs:10-41` 定义
`Execute / Undo / Serialize() -> Dictionary<string,object> / Deserialize(...)`。
`MoveCommand`（136 行）与 `AttackCommand`（103 行）都实现四件套，
`Serialize` 只写坐标/ID/数值，`Deserialize` 用 `gridController` 把坐标还原成 `ICell`。
**"命令即协议"**：同一个对象既是本地执行单元，也是网络载荷。

#### (2) `IAbility`：把"合法性 + 表现 + 输入"绑成一个单元

`abilities/IAbility.cs:12-124`。一个 Ability 同时回答：

| 关注点 | 方法 |
|---|---|
| 我能做吗 | `CanPerform` |
| 选我时展示什么 | `Display` / `CleanUp` / `OnAbilitySelected` / `OnAbilityDeselected` |
| 输入怎么路由 | `OnCellClicked` / `OnCellHighlighted` / `OnCellDehighlighted` |
| | `OnUnitClicked` / `OnUnitHighlighted` / `OnUnitDehighlighted` |
| 回合钩子 | `OnTurnStart` / `OnTurnEnd` / `OnUnitDestroyed` |

`MoveAbilityImpl`（242 行）里 `OnAbilitySelected` 算可达集、`Display` 高亮、
`OnCellHighlighted` 实时算并画路径、`OnCellClicked` 决定移动或退回
`GridStateAwaitInput`。**`CanPerform` 与"点下去会不会成功"共用同一套判断**——
正是我们设计文档 §6.2 要求的"查询和执行必须共用同一套 validator"。

#### (3) `GridState` 有限状态机：交互状态与仿真状态分离

`controllers/GridState.cs:9-103` + 4 个实现：
`GridStateBlockInput`（动画/结算期间吞输入）、`GridStateAwaitInput`（等待选单位）、
`GridStateUnitSelected`（有单位被选中，把输入转发给它所有 ability）、
`GridStateGameEnded`。关键细节：`MakeTransition(nextState)` 让**状态自己决定迁移**，
且 `GridController.GridState` 的 setter 保证 `OnStateExit` → `OnStateEnter` 成对
（`GridController.cs:31-44`）。

注意它管的是**交互**，不是回合。回合在 `TurnResolver` 里——职责分离得很干净。

#### (4) `MarkAs*` 表现状态族

`IUnitManager.cs:15-155` 定义了一整套具名视觉状态：
`MarkAsSelected / MarkAsFriendly / MarkAsFinished / MarkAsTargetable /
MarkAsAttacking / MarkAsDefending / MarkAsMoving / UnMarkAsMoving /
MarkAsDestroyed / UnMark`。`Unit.cs:50-59` 为每个状态配一组序列化的
`List<Highlighter>`；`Cell.cs:33-36` 另外有 4 个（unMark/highlighted/reachable/path）。
**表现是数据（Inspector 里挂列表），不是代码分支。**

#### (5) `Highlighter`：可组合的异步表现原子

**21 个类型 / 24 个文件**（占 Unity 层字节 15.5%，是该层最大子系统），
签名统一为 `Task Apply(IHighlightParams)`（`highlighters/Highlighter.cs:9-22`）。
覆盖：路径箭头（`ArrowSpritePathHighlighter`，4,775 B，唯一真正的路径可视化器）、
位移补间（`LerpToPositionHighlighter` / `CurveMovementHighlighter` /
`RelocateToDestinationHighlighter`）、朝向（`DirectionRotationHighlighter` /
`FaceDestinationHighlighter` / `FaceEnemyHighlighter` / `BaseRotationHighlighter`）、
缩放/自旋/摇摆、Sprite 颜色与排序、Renderer 材质属性块、对象启停、
`DelayHighlighter`（纯计时原语）、`CompoundHighlighter`（**并行** `Task.WhenAll`）、
`UnitDestructionResponsiveHighlighter`（**串行**且在单位阵亡时中断）、
`DebugHighlighter`。参数是 `IHighlightParams` 标记接口 + 4 种载荷
（`NoParam` / `PathHighlightParams` / `MoveHighlightParams` / `CombatHighlightParams`）。

**但抽象本身有真实缺陷，移植时必须修正：**

- **只有 `Apply`，没有 revert/undo 契约。** 回退靠"再挂一个反向 Highlighter 列表"
  （`_markAsMoving` / `_unMarkAsMoving`），而 `SpinningHighlighter`、
  `SetSpriteOrderHighlighter`、`GameObjectActivatorHighlighter`、
  `ScalingHighlighter` 都会留下**无法还原**的副作用。
- `ScalingHighlighter` 的基线硬编码为 `Vector3.one`，不是对象的原始 scale
  （`ScalingHighlighter.cs:17,30`）。
- 参数是**非受检强转**：把 `FaceDestinationHighlighter` 放进 `_unMarkFn`
  会在运行时抛异常（`NoParam` → `MoveHighlightParams` 失败）。
- 21 个类型全部**只被 Inspector 列表引用**，`Scripts` 里没有任何代码按类型引用它们。
- `SetSpriteOrderHighlighter` 的命名空间是
  `…Unity.Examples.Legacy.Example4.Highlighters`——**示例代码物理上位于运行时程序集内**。

**结论**：该借鉴的是"具名表现状态 + 可组合参数化表现原子"这个**形状**，
以及必须补上的 **apply / sequence / revert** 显式模型，而不是它的实现。

#### (6) `ITurnResolver` + `TurnContext`

`turnResolvers/ITurnResolver.cs:11-62`：`ResolveStart` / `ResolveTurn` 返回
`TurnContext{ CurrentPlayer, Func<IEnumerable<IUnit>> PlayableUnits }`。
内置实现只有 `SubsequentTurnResolverImpl`（按 PlayerNumber 轮转，跳过无单位玩家）。
**回合顺序是可替换对象**——这正是我们缺的（§4 P0-G）。

#### (7) `MoveComponent`：全图 Dijkstra + 路径缓存

`units/MoveComponent.cs:102-154`：`GetGraphEdges` 把整张图建成
`Dictionary<ICell, Dictionary<ICell,float>>`，`CachePaths` 一次 Dijkstra
得到 `cameFrom` + `costSoFar`，之后 `GetAvailableDestinations` 只做过滤，
`FindPath` 只做回溯。**思路对（一次全展开，多次查询），实现很差**（见 §2.4）。

#### (8) 组合式 AI：行为树 + 位置/目标评估器

`ai/` 30 个文件。行为树是纯 `Task<bool> Execute(bool debugMode)` 的
`ITreeNode`（`ITreeNode.cs:11-15`），基节点 `Selector/Sequence/Inverter/Succeder/
Random/Delay/RealtimeDelay/Func/Debug`；自定义节点
`MoveActionNode / AttackActionNode / AttackSequenceNode / CellTakenNode /
EnemiesInRangeNode / HealthThresholdNode`。

真正的亮点是 **evaluator 组合**：`IPositionEvaluator{ Weight, Initialize, EvaluatePosition }`
（`IPositionEvaluator.cs:10-33`）+ 9 个实现
（`DamageDealtPositionEvaluator` / `DamageReceivedPositionEvaluator` /
`DistancePositionEvaluator` / `EnemyProximityPositionEvaluator` /
`HealthTargetEvaluator` / `DamageDealtTargetEvaluator` / `RandomPositionEvaluator`）。
`DamageDealtPositionEvaluator.cs:45-86` 先对每个可站格算"能打出的最大伤害"，
归一化，再按 `(1-decayRate)^distance` 做距离衰减累加——
**"站这儿能打到谁、顺便威胁谁"** 的经典战棋 AI 评分。

默认行为树（`RegularBehaviourTreeResource.cs:67-110`）是个好参考：
外层 `SequenceNode` = [移动 `SelectorNode` → 延迟 → `SuccederNode(AttackSequenceNode)` → 延迟]；
移动选择器先试**高血量分支**（`HealthThresholdNode(0.5)` 通过 → `MoveActionNode`），
否则走**低血量分支**（`InverterNode(HealthThresholdNode)` → `MoveActionNode`），
两支用**不同 evaluator 权重集**（`DamageDealt`/`DamageReceived`/`Distance`）；
攻击步骤用 `HealthTargetEvaluator` + `DamageDealtTargetEvaluator`。

**但有三处关键缺陷：**

- **行为树的"形状"是 C# 代码，不是数据。** `BehaviourTreeResource` 是抽象
  MonoBehaviour，`Initialize` 里用 `new` 表达式手工搭树
  （`Scripts/ai/behaviourTrees/RegularBehaviourTreeResource.cs:65-111`）；
  只有**调参面**（`_actionDelay` / `_turnFinishDelay` / `_highHealthThreshold`
  与 12 个 evaluator 权重）是 `[SerializeField]` 序列化的。
  **换一套 AI 逻辑必须改代码重编译**，无法数据驱动或热更。
  这一点我们**领先**——`npc_ai::BehaviorDefinition` 已经是
  `schemaVersion` + 黑板 schema + 状态/转移的数据形态（`npc_ai/NpcAi.h:74-81`），
  只是缺战棋语义的 evaluator。
- `AIPlayer.Play` 是 `async void`，回合推进靠**墙上时钟延迟**
  （`_turnStartDelay` / `_unitDelay`），不是注入的逻辑 tick
  （`players/AIPlayer.cs:76-108`），还有**按键闸门**（N 键）做单步调试
  （`:93-97`）。这直接排除了无头确定性模拟与"让 AI 试算三步"——
  而这恰是战棋最需要的。
- `MobilityBasedUnitSelectorImpl`（核心里有）**没有 Unity 适配器**，
  只有 `SubsequentUnitSelector` 可用 → 宣传的"机动力选择单位"策略实际不可达。

#### (9) `INetworkConnection`：TBS 语义的网络抽象

`network/INetworkConnection.cs:11-118`：房间（`CreateRoom/JoinRoomByName/
JoinQuickMatch/GetRoomList`）、`OpCode{TurnEnded,AbilityUsed,PlayerNumberChanged,
IsReadyChanged}`、`SendMatchState(opCode, IDictionary)`、
`AddHandler(handler, opCode)`、以及 **`InitializeRng(int seed)`**——
把"随机种子必须在各端一致"写进了接口。
`NetworkConnection.cs:219,244` 是 `Serialize`/`Deserialize` 的**唯一真实调用点**。

**但这是一具"命令广播骨架"，不是 netcode：**

- 默认只注册 **2 个 opcode**（`TurnEnded` / `AbilityUsed`）；
  房间/就绪由 `NetworkGUI` 另加 2 个（`NetworkGUI.cs:67-68`）。
- 命令重建用 **`Type.GetType(程序集限定名)` + `Activator.CreateInstance`** 反射
  （`NetworkConnection.cs:236-247`），既脆弱又不安全，且**无任何命令校验**。
- 回合结束是**无参消息**：对端直接 `EndTurn`，不带任何状态。
- **没有**棋盘/单位/RNG 状态同步、没有快照、没有回滚、没有序列号、
  没有增量压缩、没有重连、没有状态重同步、没有 host 迁移。
  模型就是"两端重放同一命令流"。
- **唯一的确定性钩子 `InitializeRng` 从未被调用**（grep：仅声明）——
  与"AI 用未播种 `new Random()`"合起来看，
  这个框架**在任何一端都不保证可复现**。
- `EndTurn(bool isNetworkInvoked)` **忽略自己的参数**，永远调 `EndTurn(true)`
  （`NetworkConnection.cs:262-266`）。
- 传输层是抽象类，**包里没有任何具体实现**（文档注释点名 Nakama/Photon）。
  即示例的 `-multi` 场景在本包内无法真正跑起来。

### 2.3 它自己没做的事：都放在示例里

`Examples/Features/` 下是**三个独立示例工程**（不是"一功能一 demo"），
每个 README 都在教你**自己实现**：

| 示例 | README 原话要点 |
|---|---|
| `Deploy` | "Brief example showing how to implement the Deploy feature"：用一个持 `DeployAbility` 的 `DummyUnit` 占住回合，`FinishButton` 调 `UnityGridController.EndTurn` |
| `InitiativeSystem` | ATB/充能回合制：`InitiativeTurnResolver` + `InitiativeComponent{Initiative,Charge}` + `DummyPlayer` 当"时钟" + `ActionBar` 进度条。公式 `charge_added = initiative * multiplier`，达阈值行动 |
| `MultiCellUnits` | 占多格的单位：`MultiCellUnit` 覆写移动/占位/攻击范围；方格用边长、六边形用半径；攻击范围按"最近点对"判定 |

`ClashOfHeroes`（旗舰示例，977 资产 / 217 MB）自己写了：
`Charge/Heal/Relocate/Sweep/PercentageDamage/AreaEffectDamage` 六个技能、
`TargetCellSelector`/`LeapTargetCellSelector`/`TeleportTargetCellSelector` 目标选择器、
`AbilityMenu`/`AbilityCancel`/`AbilityDetailsDisplay`/`TurnTransitionUI`/`CameraPanning` UI、
`BarbarianSkillHighlighter` 等每角色专属高亮、
`HeightPositionEvaluator` + `IHeightComponent`（**高度优势是示例级，不在核心里**）、
`ITurnAbilityLimit` 每回合技能次数。

`LegacyDemos/Example4` 自己写了经济（`EconomyController`/`IncomeGenerationAbility`/
`CheckCostNode`）、建筑占领（`CaptureAbility`/`Capturable`/`StructureCaptureCondition`）、
生产（`SpawnAbility`/`SpawnCommand`）。`TilemapExample` 自己写了
`IMovementRules` + `Land/Sea/AirUnitMovementRules`（**地形域相关移动规则也是示例级**）。

**关键词全量扫描**（78 核心 + 67 Unity + 99 示例 .cs）结果：

| 关键词 | 命中 |
|---|---|
| fog / visibility / line of sight | **0 / 0 / 0** |
| cover / flank / advantage | **0 / 0 / 0** |
| status effect / debuff / resist / cooldown | **0 / 0 / 0 / 0** |
| overwatch / opportunity / interrupt | **0 / 0 / 0** |
| zone of control | **0** |
| elevation | **0** |
| `buff` / `reaction` / `height` | 8 / 22 / 11 —— 全部是 `buffer` / `preAction`·`postAction` / 地图高度的大小写子串误命中 |

即：**战争迷雾、视线、掩体、夹击、状态效果、冷却、监视、机会攻击、控制区、
高程——这个框架一个都没有。**

### 2.4 质量缺陷（**不要照搬**）

| # | 缺陷 | 证据 |
|---|---|---|
| 1 | `ICommand.Undo` 声明了，但**全仓库 0 处调用**——没有 undo 栈，纯装饰 | `grep '\.Undo\s*\('` → 无命中；`ICommand.cs:26` 有声明 |
| 2 | 范围攻击命令的 `Undo`/`Serialize`/`Deserialize` 三个方法全是 `NotImplementedException` | `MultipleTargetAttackCommand.cs:65,70,75` |
| 3 | `EndTurnCommand.Deserialize` 同样未实现 | `EndTurnCommand.cs:30` |
| 4 | `MoveCommand.Undo` 只改 `IsTaken` 与 `CurrentCell`，**没把单位加回 `_source.CurrentUnits`** → 撤销后格子单位列表不一致 | `MoveCommand.cs:78-87` vs `Execute` `:63-65` |
| 5 | `AttackCommand.Undo` 回血、还 AP，但**不撤销阵亡**（`OnUnitDestroyed` 已 destroy gameObject） | `AttackCommand.cs:66-72` |
| 6 | AI 评估器用**未播种的 `new Random()`** → 同局不同端、回放不一致 | `RandomPositionEvaluator.cs:20` |
| 7 | `GetGraphEdges` 每次查询为**全图**建 `Dictionary<ICell,Dictionary<ICell,float>>`（每格一个新字典） | `MoveComponent.cs:102-124` |
| 8 | `CanPerform` 每次 UI 判定**重算** `GetAvailableDestinations`，不复用 `CachePaths` 结果 | `MoveAbilityImpl.cs:206` vs `:66` |
| 9 | 权威状态双写：`ICell.IsTaken`(bool) 与 `ICell.CurrentUnits`(List) 必须手工保持一致；`Undo`/`Cleanup` 已证明会漏 | `MoveCommand.cs:54-65`、`Unit.cs:466-481` |
| 10 | 仿真混 `async Task`/`async void` 与 `Task.WhenAll`，表现与仿真不可分离，无法无头运行 | `GridController.OnAbilityUsed` 是 `async void`；`AttackCommand.Execute` 内 `Task.WhenAll` |
| 11 | 静态共享 `static DijkstraPathfinding pathfinder` | `MoveComponent.cs:29` |
| 12 | 无存档/无回放、无 schema 版本 | 全仓库 `savestate`/`persist` 0 命中 |
| 13 | `Vector2IntImpl`/`Vector3IntImpl` 存在 `NotImplementedException` 分支 | `Vector2IntImpl.cs:36`、`Vector3IntImpl.cs:60` |
| 14 | 命名空间泄漏：`External/tbsf-common/common/units/UnitHelper.cs` 的命名空间是 `...Unity.Units` | `UnitHelper.cs:8` |
| 15 | **唯一的确定性钩子 `InitializeRng(seed)` 从未被调用** | 声明 `network/INetworkConnection.cs:117`；实现 `NetworkConnection.cs:133-136`；`Scripts` 内 0 调用 |
| 16 | 表现层**没有 revert 契约**：只有 `Apply`；自旋/Sprite 排序/对象启停/缩放留下不可还原副作用 | `Highlighter.cs:9-22` 只有 `Apply` |
| 17 | `ScalingHighlighter` 缩放基线硬编码 `Vector3.one`，不是对象原始 scale | `ScalingHighlighter.cs:17,30` |
| 18 | `IHighlightParams` 全部**非受检强转**，放错槽位运行时抛异常 | 各 Highlighter |
| 19 | `ArrowSpritePathHighlighter` 激活新段时**从不关闭旧段** | `ArrowSpritePathHighlighter.cs:50` |
| 20 | `NetworkGUI` 两处"全员就绪"阈值不一致，且字典索引无保护 | `NetworkGUI.cs:176` vs `:197`、`:82` |
| 21 | `CellBrush.Paint` 在 null 检查**之前**解引用 prefab 实例 | `CellBrush.cs:31-32` |
| 22 | `GridCoordinatesGizmo` 的 `GetRectCellsInBounds` 与 `GetHexCellsInBounds` 是**逐字节相同的迭代器** → hex 分支无意义 | `GridCoordinatesGizmo.cs:102-122` |
| 23 | `ICellGridGenerator.ReadGeneratorParams()`（反射式参数 GUI）**从未被调用** | `ICellGridGenerator.cs:49-78` |
| 24 | `ICellManager.CellRemoved` 事件声明了但**从不触发** | `RegularCellManager.cs:18` |
| 25 | 5 个网格生成器里 **3 个（Equilateral/Hexagonal/Triangular）从工具里不可达** | `GridHelper.cs:596-620` |
| 26 | `RemotePlayer.PlayerType` 也是 `AutomatedPlayer` → 远程真人与 AI **类型上不可区分** | `RemotePlayer.cs:14` |
| 27 | `UnityPlayerManager.GetPlayerByNumber` 每次调用**重新遍历层级**，不用已缓存的 `_players` | `UnityPlayerManager.cs:26-29` |
| 28 | `External/` 里**没有任何第三方库**——清单里唯一条目是它自己的 `tbsf-common` | `asset-index.csv` |
| 29 | `Cell.SetColor(...)` 是**空虚函数**，AI 调试着色静默失效，除非示例覆写 | `Cell.cs:118` |

### 2.5 编辑器工具：`GridHelper` 是它真正的"接线参考"

`Editor/GridHelper.cs`（31,060 B，包里最大的手写文件）不是普通工具，
它是**框架全部场景接线知识的唯一载体**。`GenerateBaseStructure()`
（`GridHelper.cs:521-646`）一次性建出：`GridController` + `UnityGridController` +
`SubsequentTurnResolver`、`CellManager` + `RegularCellManager`、
`UnitManager` + `UnityUnitManager`、`PlayerManager` + `UnityPlayerManager`
（按子物体数量编号生成 N 个 `HumanPlayer` + M 个 `AIPlayer`）、`GUIController`、
`GameEndConditions/DominationCondition`、灯光、
`EventSystem` + `InputSystemUIInputModule`、按 `CellShape` 配置的 Unity `Grid`+`Tilemap`
（layout/swizzle 逐形状、逐 2D/3D 分支）、相机与 `PhysicsRaycaster`。

另外三块能力：

- **地形笔刷**（`:273-341, :448-501`）：Ctrl+R 切模式、
  `HandleUtility.AddDefaultControl` 抢占场景输入、半径 1–4、
  `Handles.DrawWireDisc` 光标、哈希去重保证每格只刷一次；
  换 prefab 时"实例化新的 → 拷 `siblingIndex` + `Cell.CopyFields()` →
  `Undo.DestroyObjectImmediate(旧的)`"，整体折叠进一个 undo 组；
  缺 Collider 会报警（因为拾取是 raycast）。
- **单位笔刷**（`:208-271, :403-446`）：同样模式机，尊重 `IsTaken`，
  写 `PlayerNumber`/`CurrentCell`/`CurrentUnits`，注册 undo。
- **Prefab 助手**（`:145-206`）：选中物体转 prefab，以及一个"变体生成器"
  （实例化根 prefab + `EditorUtility.CopySerialized` 逐组件拷贝）。

结构性问题：它**用名字字符串耦合场景**——`OnHierarchyChange` 靠识别
`GridController`/`CellManager`/`UnitManager` 三个名字来重新绑定（`:93-113`）。

**对照价值**：我们 `hexmap` 的笔刷（`editElevation`/`editTerrainType`/
`editFeatureLevel` + `collectBrush` + `pickCell` + 脏块）在地形侧**已等价甚至更强**
（无资源依赖、程序化几何、确定性生成器）；缺的是
**遭遇/编队/刷怪点这一层"战棋关卡"编辑**，以及 `GridHelper` 那种
"一键建出可运行场景"的引导式接线。

---

## 3. 逐维度对比

图例：**●** 我方明显更好 · **◐** 各有取舍 · **○** 我方缺失/落后
（"我方"列写的是**实际可用**的能力；脚本可达性单列，因为差距很大）

| # | 维度 | EVEngine | TBS Framework 4.2.0 | 判定 |
|---|---|---|---|---|
| 1 | 回合/轮次调度 | `TurnPolicyKind{SideAlternating,Initiative}`，`BattlePhase` 8 阶段，每次 `advance()` 只走一个可观测迁移 | `ITurnResolver` 可替换对象；内置只有按玩家轮转；ATB 在示例里 | ◐ 机制更好·扩展性更差 |
| 2 | 行动经济 | AP/MP/RP + `round*` 三档，revision 乐观检查，预览/提交同 validator（**但 AP 从不消耗**，§5.7） | 单一 `ActionPoints`(float) + `MovementPoints`(float)，无反应点 | ◐ |
| 3 | 棋盘拓扑 | `BoardTopology{Square4,Square8,HexAxial}` + `Cell{x,y,layer}`；hexmap 另有完整 axial/offset/立方体取整 | `HexGridType{even_q,odd_q,even_r,odd_r}` + `HexagonHelper` | **●**（hexmap 侧；已接线但全未提交） |
| 4 | 寻路/移动 | `PathQuery::reachable/path/cellsInRange`（确定性 tie-break，整数成本）；hexmap 有相位桶队列 A* | Dijkstra all-paths + 缓存 + 回溯；`AStar`/`Dijkstra` 两种 | ◐ 算法更好·**五套并存**（§5.2） |
| 5 | 战斗/伤害 | `combat/{CombatAttributes,Damage}` + `rpg::SettlementPipeline` 可注册阶段（**无脚本出口；`combat` 在 src 内只被 rts 消费**） | `ICombatant` 6 个虚方法 + `CalculateDamageDealt/Taken/Total`，无内置公式 | ◐ |
| 6 | 技能/能力 | `action::{ActionDefinition,AbilityDefinition,AbilityRuntime}`（冷却/实例化策略/激活组/事件触发）+ `combat::standardCombatAbilities()` 20 个（**脚本不可达；生产无调用方，仅测试**） | `IAbility` 12 个钩子 + `ICommand` 四件套；技能全在示例 | ◐ 内核更强·**未接线** |
| 7 | AI | `npc_ai`（数据驱动层次状态机 + 黑板 + 感知记忆 + tick 预算 + trace + 快照，**但 src/ 内零消费者**）、`decision`（FSM/utility/影响图） | 行为树（**形状是代码**）+ 9 个位置/目标 evaluator，**专为战棋评分设计** | ○ 我们的是壳，它的是战棋专用 |
| 8 | 迷雾/可见性 | hexmap：引用计数 `HexVisibility` + `explored` 单向闩锁 + `HexFogMesh` | **无** | **○**（tactics 无视野；hexmap 全未提交） |
| 9 | 胜负/目标 | `ObjectiveKind{EliminateSide,SurviveRounds,OccupyCells}` 内置 3 种，`endsBattle` | `GameResult{Winners,Losers}` + 示例 `DominationVictoryCondition` | **●** |
| 10 | 视线/掩体/高程 | `sensing::ILineOfSightQuery` 接口存在，只有 World3D physics 实现，**引擎内没有 2D/网格 LOS 实现**；候选 provider 只在测试里注册 | **全无** | ○ |
| 11 | 反应/打断 | `ReactionWindow` LIFO 栈 + 深度上限 8 + 同触发器防环 + `causation`/`correlation`（**脚本不可达**） | **无** | **●**（仅 C++ 层） |
| 12 | 持久化 | `SnapshotEnvelope`（schema `tactics:battle` v1）+ 事务式 restore + 哈希注入 + `TacticsReplay` 修订校验（**脚本不可达**） | 无 | **●**（仅 C++ 层） |
| 13 | 确定性/回放 | 注入 `SimulationStep`、命名 RNG 流、命令日志、字节级一致测试（**但脚本无法驱动固定步进时钟**，§5.16） | `InitializeRng(seed)` 在接口里但**从不调用**；AI 用未播种 `new Random()` | **●** |
| 14 | 网络 | `network` 模块 + `BattleCommand` 稳定身份；无 TBS 专用房间/opcode 层 | `INetworkConnection` 房间/opcode/`SendMatchState`；但无传输实现、无状态同步、无重连 | ○ 我方缺适配层·**它也没解决问题** |
| 15 | 表现/反馈 | 无棋盘级抽象；`building::PlacementSystem` 有 `EdgePathPreview`（思路可借） | 21 个 `Highlighter` + `MarkAs*` 状态族 + `GridState` FSM（**无 revert 契约**） | **○ 最大缺口** |
| 16 | 编辑器/授权 | hexmap 有笔刷 + 拾取 + 脏块 + 确定性生成器；`editor`/`editing` 框架（**hexmap 全未提交**） | `GridHelper.cs`(31 KB) + 2 个笔刷 + gizmo + 5 个网格生成器（3 个不可达） | ◐ |
| 17 | 脚本易用性 | `Result{ok,code,status,diagnostics,value}` 统一；**但 tactics 脚本面窄、combat/npc_ai/action-runtime 完全不可达** | Inspector 挂载 + 继承 + `[SerializeField]`；**无脚本层** | ◐ 类型安全更强·**开门程度严重不足** |

---

## 4. 我们缺什么（按优先级）

### P0 — 先让已有能力可用（收益最大，§6 详述）

- **A0. ~~把 `hexmap` 的整块工作提交掉~~ ✅ 已完成（PR #417）**（§10.2 第 1 条）。
  按旧基线它曾是第一优先级；新基线下此条**删除**。
  同类风险转移为：主工作区里未提交的球面六边形工作
  （`HexSphereMap` / `HexSphereTopology` / `examples/hex-planet`）。
- **A. 给剩余能力运行时开脚本出口**（§5.6，**范围已因 `combat` 落地而缩小**）：
  `combat` 现在已有完整 `CombatRuntime` 脚本面（§10.2 第 2 条），
  因此本条只剩 **`npc_ai` / `settlement` / `grid`**（`Module_IMPL` 仍为 0）。
  更重要的新任务变成**把 `tactics` 与新的 `CombatRuntime` 真正接起来**——
  `examples/tactics/main.nut` 至今仍是 31,321 B、自实现 HP/技能/AI（§10.2 第 9 条）。
- **B. 补全 `tactics` 的脚本面**（§5.8）：`reachable` / `cellsInRange` /
  `preview*` / 反应三件套 / `snapshot`·`restore`·`replay`·`commandsFrom` /
  **`endTurn`·`finish`**（现在连结束回合都没绑定，示例只能用
  `battle.wait(actor.id)` 绕），以及让 `addCell` 能设 `height`/`passable`/`tags`。
  否则我们在 §3 判定为"领先"的反应、存档、回放**脚本游戏一项都用不到**。
- **C. 补全行动经济**（§5.7）：`actionPoints` 目前全仓库从不递减，
  `acted` 只写不读。需与 E 一起定义"一次行动消耗什么、何时结束回合"。
- **D. 棋盘交互状态机**（对应 `GridState`）：把"谁在等待输入、选中了谁、
  正在瞄准什么、结算中吞输入"变成 tactics 的一等、**无渲染依赖**的状态，
  只产出**意图（intent）**，不直接改仿真状态。
- **E. 能力/目标选择管线**（对应 `IAbility` + `TargetCellSelector`）：
  把 `action::AbilityDefinition`、`sensing::TargetingSpec` 与 tactics 的
  AP/合法性/revision/反应窗口打通，让 `BattleCommandKind` 从
  `{Move,Face,Wait,...}` 扩到带 `kind` + `targetUnit` + `payloadJson` 的
  通用 `attack/skill/interact`。**同时补上 TBS 有而我们完全没有的
  "回合开始/受击"回调**（`AbilityTrigger` 已有声明但从未在运行时路由）。
- **F. 表现状态族与表现意图**（对应 `MarkAs*` + `Highlighter`）：
  由 tactics 事件派生具名表现状态（selected/friendly/finished/targetable/
  attacking/defending/moving/destroyed），表现层订阅后自行绘制。
  **不引入 graphics 依赖**，只发 intent + typed link；
  **必须带显式 revert 契约**（§2.2 第 5 条）。
- **G. `ITurnPolicy` 扩展点**（对应 `ITurnResolver`）：注释承诺过
  （`docs/dev/战棋系统设计.md:179-191`），实现里只有封闭枚举 `TurnPolicyKind`
  （grep `ITurnPolicy|TurnPolicyRegistry` → 0 命中）。同时补上
  **CTB/ATB 充能模型**（`charge += initiative * multiplier`，达阈值行动）。
- **H. 视线/掩体策略接口 + `ILineOfSightQuery` 接线**：
  `docs/dev/战棋系统设计.md:304-305` 承诺 v2 加 `ILineOfSightPolicy`/`ICoverPolicy`，
  至今 0 命中。同时把 `hexmap::collectVisibleCells` 适配成
  `sensing::ILineOfSightQuery` provider 注册进 capability——现在
  引擎里**唯一的 LOS 实现是 `physics` 的 World3D 版**
  （`physics/TargetingLineOfSightAdapter.h:25-28`），
  真正面向战棋的六边形视线算法反而没注册。

> 依赖顺序：**A0 → A → B/C → D/E/F → G/H**。
> A0 是纯工程债（不落地就没有六边形棋盘）；A 让脚本碰到能力；
> B 让脚本碰到我们已有的确定性能力；C/D/E 才是在内核里补战棋语义；
> F 是表现层；G/H 是扩展点。

### P1

- **I. 部署阶段**（`Features/Deploy`）：本质是"只允许在指定格放置指定单位"的
  特殊阶段 + 一次性回合结束。
- **J. 多格单位 + 占位策略**：`docs/dev/战棋系统设计.md:146-147` 已埋点
  （"占位检查应通过 `OccupancyPolicy`"），实现里 `OccupancyPolicy` 0 命中，
  当前硬编码"每格一个 unit"（`TacticsPath.cpp:84-85`）。
  参考 `Features/MultiCellUnits/README.md` 的方格边长/六边形半径 + 最近点对攻击判定。
- **K. 边区事实（edge facts）**：设计文档 §4.3 承诺
  "edge facts：可通行性、额外成本、墙/门/放逐边等标签"，实现里
  `CellState` 只有格级 `moveCost/height/passable/tags`，
  **成本只看目标格**（`TacticsPath.cpp:89-90`）。
  结果：无法表达斜向代价、方向性墙/门、单向边、代价不对称的坡。
- **L. 战棋 AI 评估层**：不重写 `npc_ai`，在它旁边加
  「快照 → 枚举合法行动 → 位置/目标 evaluator 加权评分 → 选首」，
  对应 `IPositionEvaluator`/`ITargetEvaluator` 的思路。
  TBS 的 evaluator 只在"可站格 + 能打到的敌人"上算，规模可控且天然确定性
  （**但要去掉它的 `new Random()`**，改用我们的命名 RNG 流）。
  我们的 `npc_ai` 已经是数据驱动 + 有 tick 预算，**在这方面领先**；
  但它**在 `src/` 内零消费者**，所以还要先给它一个真实调用路径。
- **M. 组合存档/回放**：`tactics:battle` 快照只覆盖战局；
  一场战棋还要 hexmap 地图、单位朝向、RPG 血量、技能冷却。
  需要组合快照/编排层。注意 `orders` 的脚本绑定也缺 `snapshot`/`restore`。
- **N. TBS 语义的网络适配**：`network` 模块 + `BattleCommand` 日志已具备
  lockstep 所需素材，缺房间/join/opcode/`isNetworkInvoked` 语义层。
  **不要照搬它的反射式 `Type.GetType` 命令重建**，
  应当用稳定 `LogicalId` 注册表 + 版本化 payload。

### P2

- **O. tactics 与 hexmap 的视野打通**：`HexVisibility` 已有引用计数 + 探索闩锁，
  但 tactics 单位没有视野概念。打通后才有真正的"战棋战争迷雾"。
- **P. 迷雾三态与面向视锥**：现在只有"已探索/当前可见"两态
  （`docs/usr/modules/hexmap.md:252-257` 已自述），且是圆形范围不是视锥。
- **Q. 战棋编辑器**：对照 `GridHelper` 的 31 KB——关卡/遭遇编排、
  批量单位放置、坐标 gizmo。我们 hexmap 笔刷只覆盖地形，不覆盖遭遇。
- **R. 表现件**：伤害飘字、镜头聚焦、回合转场 UI（对照 `TurnTransitionUI`、
  `CameraPanning`、`HealthBar`），以及**路径确认双击**
  （对照 `PathConfirmationHighlighter` 与 `MoveAbilityImpl.WithConfirmation` /
  `UseTouchOptimizedControls` 的触屏优化）。

---

## 5. 我们实现得不够理想的地方

### 5.1 tactics 与 hexmap 是两个平行筒仓

`grep '#include "tactics' src/**` 的结果：**只有 tactics 自己的 7 个 .cpp**。
整个 `src/` 里没有任何模块 include 过 tactics；它是纯出口的叶子。

两者各自独立地拥有：坐标模型（`tactics::Cell{x,y,layer}` vs
`hexmap::HexCoordinates{x,z}` + offset 转换）、寻路、单位注册表
（`TacticsUnit` ECS 短根 vs `HexUnitRegistry` 下标 id）、可见性、存档。
一份真正的六边形战棋必须在这两套模型间来回搬运数据，而两边都不认对方。
设计文档也互不引用。

再加 `rpg::Battle`（同时回合 + initiative 排序），引擎里共有**三套回合语义**。

### 5.2 多套并存的网格寻路

> ⚠️ 旧基线是 5 套；按 `origin/dev` 应为 **6 套**
> （新增 `combat/navigation/PathfinderCombatNavigation`，且 `hexmap/HexSearch` 已转正）。
> 见 §10.2 第 4 条。问题本身（应收敛）不变，且更紧迫。

| 位置 | 形态 |
|---|---|
| `map/Pathfinder.h` + `map/Path.h` + `map/FlowField.h` | 脚本友好门面：拓扑名、对角开关、gid 阻挡、每格成本、动态进入惩罚、流场 |
| `hexmap/HexSearch.h` | 相位 + 优先级桶队列、可复用 scratch、`findPath` A*、`collectVisibleCells` |
| `tactics/TacticsPath.h` | `std::map` + `priority_queue` 全展开、整数预算、确定性 tie-break |
| `rts/RTSSystems.h` | `NavigationGrid` + `NavigationSystem` |
| `crowd/CrowdField.h` | 流场 |
| `npc_ai/Navigation.h` | `INavigationProvider` 抽象（唯一做对的：只声明接口） |

这违反仓库规范里「**不要为同一操作制造第二条更便宜的路径**」。
其中 `npc_ai` 的做法（声明 provider 接口 + capability）是正确范式，
其余四套应当收敛。**注意 `INavigationProvider` 只有测试替身，
没有任何生产实现。**

### 5.3 tactics 的寻路实现明显弱于我们自己的 hexmap

| | `tactics::PathQuery` | `hexmap::HexSearchContext` |
|---|---|---|
| 数据结构 | `std::map<Cell,int>` + `std::map<Cell,Cell>` + `priority_queue<FrontierNode>`（比较整个 `Cell`） | 平铺 `vector<HexSearchData>` + 按优先级分桶的侵入式链表 |
| 复用 | 每次查询重新分配 | `beginPhase()` 相位前进，**不付全图重置代价** |
| 单目标 | `path()` **直接调用 `reachable()`**（全量 Dijkstra），无 A* 提前退出 | 真 A*，带启发式 |
| 范围查询 | `cellsInRange` 线性扫全图，且 hex 度量直接跳过非同层格 | —— |

**引擎里已经有一个更快、更省、带相位复用的实现，而战术模块又写了一个更慢的。**

### 5.4 设计文档承诺 vs 实现（4 处硬缺口）

`docs/dev/战棋系统设计.md`（2026-08-28，状态写着"v1 已实现"）承诺但 grep 0 命中的：

| 文档位置 | 承诺 | 现状 |
|---|---|---|
| `:179-191` | `ITurnPolicy` + 第三方 C++ 注册 | 只有 `TurnPolicyKind` 枚举 |
| `:319-321` | `applyOutcome`：`displaceUnit` / `grantActionPoints` / `addTacticalTag` | 只有 `defeatUnit` |
| `:143` | `BoardState` 的 edge facts（墙/门/放逐边、额外成本） | 只有格级 `CellState` |
| `:304-305` | v2 的 `ILineOfSightPolicy` / `ICoverPolicy` | 0 命中 |

另外 `:146-147` 的 `OccupancyPolicy` 也是 0 命中。

### 5.5 回合与胜负是封闭枚举

`TurnPolicyKind{SideAlternating,Initiative}`、`ObjectiveKind{EliminateSide,SurviveRounds,OccupyCells}`
都是编译期枚举。加一种 ATB 充能（TBS 示例里的做法）或"护送/生存 N 回合且不损失单位"
都要改内核。而引擎里**已经存在** `policyregistry` 模块与
`docs/dev/战棋系统设计.md:190` 的"数据中只存稳定字符串 ID"约定——没接上。

### 5.6 已有能力没有脚本出口，其中一部分连消费者都没有

> ⚠️ **本节基于旧基线 `dae6a644c`。按 `origin/dev`，其中 `combat` 一条已失效**
> ——`combat` 现在有完整脚本面（`CombatRuntime`）且被示例实际使用，
> `standardCombatAbilities()` 也有了生产调用方。**请以 §10.2/§10.4 为准。**
> 仍然成立的是：`npc_ai` / `settlement` / `grid` 的 `Module_IMPL` 为 0。

`examples/tactics/main.nut` 那 31,321 B 的**直接原因**：

| 模块 | `Module_IMPL` | 脚本可达性 |
|---|---|---|
| `action` | 1（仅 `ActionEditorModule`，编辑器时间轴） | **`AbilityRuntime` 不可达** |
| `combat` | **0** | 完全不可达 |
| `npc_ai` | **0** | 完全不可达 |
| `settlement` | **0** | 完全不可达 |
| `grid` | **0** | 完全不可达（`docs/usr/modules/grid.md` 已声明"无脚本入口"） |

更严重的是**"有实现但生产代码里没有调用方"**（不只是没开门）——
下表每条我都亲自 grep 复核过，"生产"= `src/` 内、排除自身模块：

| 组件 | 状态（已复核） |
|---|---|
| `npc_ai` 整个模块 | **`src/` 内零消费者**；只有 5 个测试文件 include 它（`test/npc_ai*.cpp`、`test/editor_behavior_graph.cpp`） |
| `NpcAiEcsSystem` / `NpcAiWorld` | `src/` 内除自身目录外**无任何引用** |
| `npc_ai/editing/BehaviorGraph.cpp:32-105` | 编译出 `BehaviorInstruction`，**运行时无消费者**（仅 `test/editor_behavior_graph.cpp`） |
| `INavigationProvider` | 无**生产**实现；唯一实现是 `test/npc_ai_navigation.cpp:19` 的 `FakeNavigationProvider` |
| `AbilityRuntime::matchingGrants` | 只被自己的测试引用 |
| `ActionNotifyRegistry::dispatch` | **连测试都没有调用方**——只有声明（`ActionNotifyRegistry.h:71`）与定义（`.cpp:106`）；`ActionRuntime::advance` 只返回 timeline 事件（`Action.cpp:507-517`） |
| `combat::standardCombatAbilities()` | **只有测试调用**（`test/standard_combat_abilities.cpp`、`test/combat_framework_composition.cpp`），`src/` 内无生产调用方 |
| `combat` 模块整体 | `src/` 内**只被 `rts` 消费**（`#include "combat/` 仅出现在 `rts/`） |
| `ISensingCandidateProvider` | 具体实现 `SensingCandidateProvider` 就在 `sensing/Targeting.h:435`，但**只在 `test/targeting.cpp` 里被实例化并 `cap::provide`**；**没有任何生产模块注册它** → 真实游戏里 `TargetingResolver::resolve` 会返回 `Unsupported`（`Targeting.cpp:339-340`） |

后果：脚本作者唯一的选择就是在 Squirrel 里重写。这不是"没做框架层"，
而是"**做了但没开门，而且没接进调用路径**"。

叠加根因之二（缺少交互/表现层，§4 P0-D/F），就是那 31 KB。

**结论**：Phase 1 第一优先级应是**给已有能力运行时开脚本出口并接上真实调用路径**
（并保证它与 tactics 的 AP/revision/反应窗口共享同一 validator）。

### 5.7 tactics 的行动经济只接了一半

| 资源 | 是否被消耗 | 证据 |
|---|---|---|
| `movePoints` | 是 | `TacticsBattle.cpp:391` `unit->turn()->movePoints -= receipt.cost;` |
| `reactionPoints` | 是 | `TacticsBattle.cpp:641` `--reactingUnit->turn()->reactionPoints;` |
| **`actionPoints`** | **否，全仓库从不递减** | 仅赋值/读写/快照：`Tactics.cpp:484`、`TacticsBattle.cpp:272`、`TacticsPersistence.cpp:219,759`；`--`/`-=` 0 处 |

`TurnResources::acted` 同样**只被写、从不被读**（`TacticsBattle.cpp:357,477`）。

后果：

- `TurnResourceSpec::actionPoints` 目前是**装饰性字段**——进快照、进命令、
  脚本可设，但**不影响任何行为**；
- 没有"一次行动消耗 1 AP、AP 归零则自动结束回合"这条最基本的战棋规则，
  也没有"攻击消耗 AP"的落点（因为 `attack` 行动本身还不存在）；
- `acted` 不读意味着"本回合已行动"无法被查询或用于跳过单位。

### 5.8 tactics 的脚本面远窄于 C++ 面

脚本面共 **5 个 `Tactics` 方法 + 25 个 `TacticsBattle` 方法**
（`Tactics.cpp:1017-1021`、`:752-998`）。

**没有**脚本出口（只存在于 C++ API）：

- `reachable` / `cellsInRange`（可达范围与射程枚举）
- `previewMove` / `previewFace` / `previewWait`（预检）
- `openReaction` / `acceptReaction` / `declineReaction`（**整个反应系统**）
- `snapshot` / `restore` / `replay` / `commandsFrom`（**整个存档与回放**）
- **`endTurn`（`Tactics.h:97`）/ `finish`（`Tactics.h:105`）**
  —— 连结束回合都没绑定，所以 `examples/tactics/main.nut:335,411`
  只能用 `battle.wait(actor.id)` 绕过去。

另外两个具体缺口：

- **`CellState` 对脚本是不透明的**：`addCell(x,y,layer,moveCost)`
  （`Tactics.cpp:765-770`）**只设 `moveCost`**，`height` / `passable` /
  单元格 `tags` 脚本无法设置（而 `height` 目前也不被路径与战斗使用，见 §5.9）。
- **`addObjective` 只能走三个硬编码 kind**，通用 `ObjectiveSpec` 不可达；
  `endTurn`/`finish` 只通过 `IGameplayControlProvider` 暴露，
  而它只被 devtools/MCP 调用（`src/engine/devtools/DevTool.cpp:431`、
  `McpServer.cpp:712`），游戏脚本调不到。

后果：**反应窗口、存档、回放这三项在 §3 里判定为"领先"的能力，
脚本游戏一项都用不到**。§3 的"●"是 C++ 层面的领先，
在"能不能做出一款战棋"这个维度上并不成立。

### 5.9 没有覆盖、夹击、朝向加成、高地加成、机会攻击、控制区

全 `src/modules` grep：`ILineOfSightPolicy`/`ICoverPolicy` 0 命中，
夹击/背刺/高地公式 0 命中。设计文档把它们显式列为 v1 非目标
（`docs/dev/战棋系统设计.md:450`）。仅有的近义命中都在无关模块：
`weapon/CombatCarrier.h:244`（弹体遮挡）、`orders/CommandQueue.h:96`（优先级抢占）、
`rts/RTSTypes.h:145`（受击打断）。

`CellState.height` 存在但**路径与战斗都不使用它**；
`hexmap` 侧的高程只服务于渲染与视野高度判定。

### 5.10 第三个回合系统：`rpg::Battle`

除 `tactics`（阵营交替 / initiative）与 `hexmap`（无回合）之外，
`rpg` 还有一套 `Battle`（同时回合 + 按 initiative 排序），
`examples/rpg-classic/main.nut:1021,1188,1195,1201-1203,1257,1261`
是它的真实可玩示例（抽象 JRPG，无网格）。
加上 `examples/tactics`，引擎里有**两个可玩的回合制示例**，
但两者用的是**两套不同的回合系统**，互不共享回合策略与快照能力。

### 5.11 文档债

- `docs/usr/MODULES.md` **没有**链接 `tactics.md` 或 `hexmap.md`；
- `docs/dev/模块设计.md` 没有 `tactics`、`hexmap`、`orders` 章节；
- `docs/dev/测试覆盖.md` 没有 `tactics` 与 `hexmap` 行；
- `docs/usr/modules/rts.md` 不存在（而 `tactics.md` 存在）。
- 好消息：26 个范围内模块与 10 份 TBS 相关文档里
  **`TODO/FIXME/HACK/XXX` 0 命中**。

### 5.12 hexmap 已完整实现并已正确接线，但**整块工作全部未提交**

> ⚠️ **本节也已过期。** 按 `origin/dev`，`hexmap` 已作为 **PR #417 合并进 dev**
> （31 文件受控 + manifest 声明 + `module_list.nut` 条目），"未提交"的风险不再存在。
> 保留本节是为了记录两次误判的经过与方法论教训（§1.4、§10.5）。

> **⚠️ 本节是本文档的一次自我纠错，请连同上一条方法论教训一起读。**
> 本节初稿断言"`hexmap` 完全未落地、既不可构建也不可运行、`test/hexmap.cpp` 会链接失败"。
> **该断言是错的**，原因是我的取证方法有缺陷（见下）。正确结论如下。

**事实（已在主工作区与现有构建产物中核对）**：

- `cmake/module_manifest/rendering_simulation.cmake:53` **有**完整声明：
  `eve_declare_module(NAME hexmap LAYER 4 SCRIPT HexMap SLOT hexmap … DEPS graphics GROUP 3d)`。
  `SCRIPT`/`SLOT` 令牌都是正确的（`SCRIPT HexMap` 与 `HexMapModule.cpp:195` 一致）。
- 生成的模块清单里有它：`build/win32-debug/src/scripts/module_list.nut:50`
  是 `{ slot = "hexmap", cls = "HexMap" }`，`:187` 是完整条目
  （`enabled = true, layer = 4, deps = ["graphics"], profiles = ["3d"]`）。
  这直接证明 `src/scripts/load.nut` **会**创建 `hexmap` 全局——
  我初稿"脚本全局不会被创建"的推断是错的。
- 它**真的编出来了**：`build/win32-debug/src/modules/CMakeFiles/EVHexmap.dir/` 下有
  **17 个 `.obj`**（含 `HexMapModule.cpp.obj`），`hexmap_src.txt` 列了 14 个源文件。
- **它的测试真的跑过并且通过**：`build/win32-debug/Testing/Temporary/CTestCostData.txt`
  里有 **34 条 `hexmap.*` 用例**（带耗时），覆盖
  `coordinates / features / generate / grid / mesh / metrics / pick / search /
  serializer / units / visibility` 十一组，例如
  `hexmap.mesh.terrainTriangulatesEachOwnedBoundaryOnce`、
  `hexmap.serializer.rejectsCorruptPayloadWithoutMutatingTheOutputs`、
  `hexmap.visibility.countsViewersAndLatchesExplored`。
  （同一份记录里 `tactics.*` 有 20 条，全库共 3665 条。）

所以 `hexmap` 不是"未落地"，而是**实现完成度高、接线正确、编译通过、34 个用例全绿**。

**真正的问题不是"没落地"，而是"整块工作一行都没提交"**：

| 内容 | git 状态 |
|---|---|
| `src/modules/hexmap/`（31 文件） | **untracked** |
| `test/hexmap.cpp` | **untracked** |
| `docs/usr/modules/hexmap.md` | **untracked** |
| `examples/hex-terrain-3d/`（含 shaders） | **untracked** |
| `scripts/capture_hex_terrain_3d.sh` | **untracked** |
| **`cmake/module_manifest/rendering_simulation.cmake` 里的 hexmap 声明** | **`M`（已修改未提交）** |

所以风险是**提交卫生**，不是构建：`git clean -fd`、一次 fresh clone、或**任何基于 HEAD 的
worktree/CI** 都会同时丢掉模块本体**和它的接线**。

**这条风险已经在本次分析中真实发生了。** 我的取证 worktree 建在 HEAD
（`dae6a644c`），那里既没有 hexmap 源码，也没有那行 manifest 声明——
于是我得出了"模块不存在/未落地"的错误结论，直到回头检查
`build/win32-debug` 才发现 `EVHexmap` 早已编出来。

**方法论教训（已写入 §1.4）**：只按 HEAD 建 worktree 会**系统性漏掉工作区里已修改的已跟踪文件**。
我补拷了 untracked 文件，却没有补拷 `M` 状态的跟踪文件——
而 hexmap 的接线恰恰就在一个 `M` 文件里。
主工作区还有一批 `M` 文件（`src/engine/common/Runtime.cpp`、
`SquirrelOwnership.h`、`src/modules/rpg/RPG.cpp`、`src/modules/ui/*` 等）
以及被删除的 `scripts/check_bindings*.py`，同样不在我的 worktree 里。
**我已针对本文的 headline 结论回到主工作区逐条复核**（§1.4 列出复核结果），
结论未变；但读者仍应知道本文的证据基线是"HEAD + untracked"，不是完整工作区。

### 5.13 有机件但没接线，而且引擎里没有网格 LOS

- `sensing::ILineOfSightQuery`（`sensing/Targeting.h:392-406`）是设计良好的
  capability 接口，但**引擎里唯一的实现是 `physics` 的 World3D 版**
  （`physics/TargetingLineOfSightAdapter.h:25-28`）——
  **没有任何 2D / 网格 LOS 实现**；
- 它的候选提供方 `ISensingCandidateProvider` 有一个最小实现
  `SensingCandidateProvider`（就在 `sensing/Targeting.h:435`），
  但**只在 `test/targeting.cpp` 里被注册**（`:122,158,173`），
  **没有任何生产模块注册它**；缺失时 `TargetingResolver::resolve`
  返回 `Unsupported`（`Targeting.cpp:339-340`）。
  仓库自己的边界台账也把它记为零提供方
  （`docs/dev/2026-09-15-模块边界审查台账.md:61,149`）；
- `hexmap::collectVisibleCells`（面向六边形战棋的视线）**没有**适配成 provider；
- `tactics` 完全不使用 `sensing`（它只 `DEPS action`，
  `cmake/module_manifest/orchestration.cmake:73`）。

于是"六边形视线"的算法在 `hexmap`，"视线接口"在 `sensing`，两者互不相识。

### 5.14 hexmap 的可见性是"距离 + 高程"启发式，不是真视线

`hexmap::collectVisibleCells`（`HexSearch.h:208-225`）判定可见的条件是
"最短路径距离 + 目标格 viewElevation ≤ range + 起点 viewElevation"，
且距离不超过直接六边形距离。这便宜、可复现，但**没有真正的射线遮挡**：
一堵薄墙只能通过 `moveCost` 阻断搜索来间接生效，
无法表达"看得见但走不过去"或"能射到但看不见"——战棋常需要区分这两种。

另外：**全仓库没有任何 FOV 对称性断言**。`test/map_fov.cpp` 的 24 个用例
从不检查 a↔b 互相可见。对需要对称视野的战棋这是一个真实缺口。

### 5.15 迷雾只有两态

`docs/usr/modules/hexmap.md:252-257` 自述：不区分"当前可见"之外的中间态透明度，
迷雾柱顶取自身与邻居最高面导致高差大时略鼓。对文明类可接受，对战棋
（需要"记忆中的地形 / 上次见到的敌人 / 当前可见"三态）不够。

### 5.16 脚本无法驱动确定性固定步进时钟

`Timer` 的脚本面只有 `getName/getTime/getDelta/step`（`Timer.cpp:83-86`），
**`Timer::stepSimulation()` 与 `simulationClock()` 没有绑定**。
而固定步进时钟正是回合制 tick 最自然的基底。
此外 `hexmap` 的脚本面（`eve.HexMap`）在测试里出现 **0 次**，
`eve.Tactics` 只在 2 个测试用例里出现过——**脚本出口的测试覆盖极薄且不对称**。

### 5.17 `combat` / `attributes` 的局部质量缺陷

在评估"把 combat/attributes 接到 tactics"之前，需要知道它们自身的问题：

- `CombatAttributeRuntime::advance`（`CombatAttributes.cpp:100-121`）
  先算候选再**逐个提交并提前返回**，与它自己的头文件契约
  "所有改动先 clamp 再一次提交"（`CombatAttributes.h:48-52`）**自相矛盾**——
  存在部分变更路径。
- `AttributeSet::getFinal` 从 const 方法写 `mutable` 缓存，
  且**传入自定义算子注册表时会绕过缓存**（`AttributeSet.cpp:212-216`），
  于是 `Custom` 修饰符的缓存与非缓存结果**不一致**。
- `removeModifier(id)` 取首个匹配、`modifierCount` 遍历 `unordered_map`
  （`AttributeSet.cpp:278-287,348-352`）→ 不确定性。
- `priority` 对可交换的 Add/AdditivePercent/MultiplicativePercent 传递**无效果**。
- `Attributes::newSet` 返回**裸 `AttributeSet*` 且没有 delete/release 绑定**
  （`Attributes.h:18`、`Attributes.cpp:9,15-16`）→ 脚本侧泄漏。
- `AttributeSetStateAdapter` 对 `hasTag`/`resource`/`authority` **故意返回 `nullopt`**
  （`StateAccessAdapter.h:29,33,39`），所以任何用
  `HasTag`/`HasResource`/`AuthorityCheck` 的 `decision::Condition` 对它都会
  报"不可用"。

这些不影响 headline 结论，但会影响 Phase 1/2 的实现顺序：
**把 combat/attributes 接进 tactics 之前应先修掉 `advance` 的部分变更路径**
（否则违反"原子提交"这条仓库强制规范）。

### 5.18 hexmap 生成器有一个被测试记录下来的已知缺陷

`test/hexmap.cpp:729-733` 明确记录：程序化生成器**不产生河流**，
`CHECK(rivers > 0)` 是**刻意的建议性断言**并附了解释注释。
这是本次扫描中唯一"已知失败被写进测试"的地方，
说明生成器的水文步骤尚未完成（与 `docs/dev/hex-terrain-capability-audit.md:83-85`
的"production hydrology 仍缺"一致）。

**独立佐证**：已运行过的 34 条 `hexmap.*` 用例里，
生成器那条叫 `hexmap.generate.isDeterministicAndProducesLandWaterAndPlants`
——**名字里没有 river**；而 `hexmap.grid.riversMirrorAndValidate` 测的是
河流**数据模型**（镜像与校验），不是生成器。两条证据一致指向：
河流的数据结构与编辑/渲染是完备的，**缺的只是生成器里的水文步骤**。

---

## 6. 改进与移植方案

### 6.1 总原则

1. **只移植接缝，不移植形态。** 拿 `IAbility` 的钩子划分、`GridState` 的
   交互/仿真分离、`MarkAs*` 的具名表现状态、`IPositionEvaluator` 的评分组合；
   丢掉 MonoBehaviour / Inspector / Coroutine / `async void`。
2. **不平移它的实现缺陷。** 不引入 undo 栈、不做 `IsTaken` 双写、
   不用未播种 RNG、不做 per-query 全图字典、不用反射重建网络命令。
3. **表现意图 ≠ 仿真状态。** tactics 只发事件与意图；表现层
   （`animation`/`effects`/`particles`/`ui`）订阅。tactics 必须继续能在无
   `graphics` 的进程里跑（`docs/dev/战棋系统设计.md:28` 的硬约束）。
4. **单一真值来源。** 新框架层不得复制棋盘/回合/占位事实；
   只读 tactics 快照 + 产出意图。寻路收敛到一处（§6.3）。
5. **先接线，再造轮子。** 引擎里已有一批"有实现但生产代码无调用方"的组件
   （§5.6）；任何新功能提案都应先回答"能不能用已有的"。
6. **对齐仓库强制规范**：`Result` + `[[nodiscard]]`、短根 ECS + 类型化 Link、
   六面边界 + `module-interface` 目录条目、注入时间与命名 RNG、
   持久化 schema/version/migration、`@cost` 标注、`check/architecture-contracts`。

### 6.2 分阶段实施（每阶段可独立成 PR 并独立验收）

#### Phase 0：先修地基（风险最低、收益立竿见影）

| 项 | 动作 | 验收 |
|---|---|---|
| 0.1 | ~~提交 `hexmap`~~ ✅ **已完成（PR #417，已合入 dev）**。新基线下的替代条目：提交主工作区的球面六边形工作 | 从 HEAD 新建的 worktree 也能编译 hexmap（**已验证**：`origin/dev` 含 31 文件 + manifest 声明） |
| 0.2 | `TacticsPath` 换用相位桶队列 scratch，`path()` 改真 A*，`cellsInRange` 走索引 | `test/tactics_path.cpp` 现有断言不变；新增规模基准 |
| 0.3 | 补 `BoardState` 的 edge facts（`EdgeKey{from,to}` + `EdgeState{passable,extraCost,tags,oneWay}`） | 新测试：斜向代价、方向性墙、单向边 |
| 0.4 | 修行动经济：实现 `actionPoints` 消耗、让 `acted` 可读、明确 `endTurn` 自动触发条件 | 新测试：AP 归零自动结束回合；`acted` 查询 |
| 0.5 | 修 `CombatAttributeRuntime::advance` 的部分变更路径，使其符合自己的头文件契约 | 失败注入测试：中途失败不留部分变更 |
| 0.6 | 修 `Attributes::newSet` 的裸指针泄漏（加 release 绑定） | 脚本侧可释放；泄漏测试 |

#### Phase 1：让已有能力可用（本方案的核心）

- **1.0 给已有能力运行时开脚本出口**（P0-A，**先做这一步**）
  为 `action::AbilityRuntime`、`combat::DamageRuntime`、
  `combat::standardCombatAbilities()`、`npc_ai::NpcAiWorld` 增加
  `Module_IMPL`/`expose` 绑定，遵守既有 `RuntimeHandleRef` + `Result` 投影范式
  （照 `Tactics.cpp` 的脚本类写法）。**不新增算法，只开门。**
  同时接上真实的运行时路由：让 `ActionNotifyRegistry::dispatch` 与
  `AbilityTrigger` 在 `ActionRuntime::advance` 里被调用
  （现在 `advance` 只返回 timeline 事件）。
  验收：Squirrel 里注册一个 `AbilityDefinition`、授予单位、激活一次并读回冷却；
  一个 `on-damage`/`on-turn-start` 触发的技能能被真实触发；
  `examples/tactics` 的 HP/技能部分改用引擎能力后行数下降。

- **1.1 补全 `tactics` 脚本面**（P0-B）
  暴露 `reachable` / `cellsInRange` / `previewMove|Face|Wait` /
  `endTurn` / `finish` /
  `openReaction|acceptReaction|declineReaction` /
  `snapshot|restore|replay|commandsFrom`；并让 `addCell` 接受
  `height`/`passable`/`tags`（或提供分步 setter）。
  验收：`test/tactics_script.cpp` 覆盖每条新绑定；反应窗口与回放可在
  Squirrel 里端到端跑通；`examples/tactics` 不再需要 `wait()` 代替 `endTurn`。

- **1.2 `ITurnPolicy` 扩展点**（P0-G）
  ```cpp
  // tactics/TurnPolicy.h
  class ITurnPolicy {
  public:
      virtual ~ITurnPolicy() = default;
      virtual Result<void> beginRound(Battle&) const = 0;
      virtual Result<std::optional<SubjectRef>> nextActor(const Battle&) const = 0;
      virtual Result<bool> mayAct(const Battle&, SubjectRef) const = 0;
  };
  // 注册表：稳定字符串 ID -> 工厂；Battle::TurnState 存 id 而非枚举
  ```
  内建 `side_alternating` / `initiative` 改为实现；新增
  `charge_time`（ATB：`charge += initiative * multiplier`，达阈值行动，
  charge 进快照）。持久化里只存稳定 ID 字符串。

- **1.3 通用行动协议**（P0-E）
  把 `BattleCommandKind` 从固定枚举扩展为
  `{ Move, Face, Wait, EndTurn, UseAbility, ... }` + `LogicalId action` +
  `SubjectRef targetUnit` + `Cell targetCell` + `payloadJson`，
  由 `action::AbilityDefinition` 提供合法性（AP/RP/冷却/revision），
  由 `sensing::TargetingSpec` 提供目标约束。
  **查询与提交共用同一 validator**（既有 `previewMove`/`previewFace`/`previewWait`
  的模式必须延续到 `previewAbility`）。

- **1.4 棋盘交互状态机**（P0-D）
  ```cpp
  // tactics/Interaction.h —— 无渲染依赖
  enum class InteractionState : std::uint8_t {
      Blocked, AwaitSelection, UnitSelected, Targeting, Resolving, Ended
  };
  struct InteractionIntent {           // 只表达"想做什么"，不改仿真
      InteractionIntentKind kind;      // SelectUnit, MoveTo, UseAbilityOn, Cancel, Confirm, EndTurn
      SubjectRef actor; Cell cell; LogicalId action; SubjectRef targetUnit; Revision expected;
  };
  class InteractionSession {           // 会话，非权威；可丢弃重建
      Result<InteractionIntent> onCellClicked(Cell) const;
      Result<InteractionIntent> onCellHovered(Cell) const;
      // 状态迁移由状态自身决定（借 GridState::MakeTransition 的思路）
  };
  ```
  与 `GridState` 的关键差异：**返回意图而不是直接改 controller**，
  因此仿真线程与输入/UI 完全解耦，且可无头重放。

- **1.5 表现状态与意图**（P0-F）
  ```cpp
  // tactics/Presentation.h —— 仅事件 + 具名状态，不依赖 graphics
  enum class UnitVisualState : std::uint8_t {
      Idle, Friendly, Selected, Finished, Targetable, Attacking, Defending, Moving, Destroyed
  };
  struct PresentationIntent {
      std::uint64_t sequence;           // 复用 BattleEvent::sequence
      SubjectRef subject; SubjectRef other;
      UnitVisualState state; Cell from; Cell to; std::vector<Cell> path;
      std::uint64_t expiresAtTick;      // 表现不是权威，必须可过期
  };
  struct PresentationRevert {           // TBS 缺的就是这个
      UnitVisualState from; UnitVisualState to; std::uint64_t sequence;
  };
  ```
  `Battle::Events` 之后追加一个「表现意图投影」，由 `game_event`/`rx`
  或专门的 sink 消费。表现层订阅后自行映射到 `Renderable3D` 高亮、箭头、
  补间、飘字——等价于 `Highlighter` + `MarkAs*`，但**是数据不是 MonoBehaviour**，
  且**显式区分 apply 与 revert**。

- **1.6 视线/掩体策略 + capability 接线**（P0-H）
  ```cpp
  // tactics/LineOfSight.h
  class ILineOfSightPolicy {
      static constexpr const char* capabilityName = "eve.tactics.ILineOfSightPolicy";
      virtual Result<bool> visible(const BoardState&, Cell from, Cell to) const = 0;
  };
  class ICoverPolicy {
      static constexpr const char* capabilityName = "eve.tactics.ICoverPolicy";
      virtual Result<CoverLevel> cover(const BoardState&, Cell attacker, Cell target) const = 0;
  };
  ```
  并在 `hexmap` 里把 `collectVisibleCells` 适配成
  `sensing::ILineOfSightQuery` provider（`cap::provide`）——**这是引擎里
  第一个 2D/网格 LOS 实现**——让 `sensing::TargetingResolver` 的
  `LineOfSightMode::Required` 真正可用于战棋。
  **provider 缺失时必须返回可观测的 `Unsupported`**，而不是空成功集
  （`sensing/Targeting.h:455-461` 已立下这个好榜样）。
  同时补 FOV 对称性测试（目前全仓库 0 个，§5.14）。

**Phase 1 交付面**（按仓库"六面边界"要求）：
接口头文件 + `module-interface` 目录条目 + 每项独立测试文件
（`test/tactics_interaction.cpp`、`test/tactics_presentation.cpp`、
`test/tactics_turn_policy.cpp`、`test/tactics_line_of_sight.cpp`、
`test/action_ability_script.cpp`、`test/fov_symmetry.cpp`）
+ 快照 schema 升到 v2 并给 v1→v2 迁移 + `docs/usr/modules/tactics.md` 更新
+ 裁剪 profile 验证（tactics 仍可在无 graphics 构建里通过）。

#### Phase 2：战棋必备玩法（P1）

| 项 | 内容 | 对照物 |
|---|---|---|
| 2.1 | 部署阶段：部署状态 + 部署区/部署表 + 每方一次性结束 | `Features/Deploy` |
| 2.2 | `OccupancyPolicy` + 多格单位（方格边长 / 六边形半径；攻击按最近点对） | `Features/MultiCellUnits` |
| 2.3 | 战棋 AI 评估层：快照 → 合法行动枚举 → 位置/目标 evaluator 加权（含高度、威胁、受击）；**先给 `npc_ai` 一个真实调用路径** | `IPositionEvaluator` 9 实现、`HeightPositionEvaluator` |
| 2.4 | 组合存档/回放：tactics + hexmap + 单位/RPG 血量 + 冷却 的单次事务快照；补 `orders` 的 `snapshot`/`restore` 绑定 | 无对照（我方领先项，需自建） |
| 2.5 | 网络适配：房间/opcode/`isNetworkInvoked` 语义层，坐落在既有 `network` 模块上；**用注册表而非反射** | `INetworkConnection`、`OpCode` |
| 2.6 | 技能目录：把 `combat::standardCombatAbilities()` 与 tactics 的 AP/反应/目标约束正式对接（含 `targetCellSelector` 概念） | `IAbility` + `TargetCellSelector` |
| 2.7 | 统一回合语义：让 `tactics` / `rpg::Battle` 共享 `ITurnPolicy` 与快照能力 | 无（我方自建） |

#### Phase 3：表现与工具（P2）

| 项 | 内容 | 对照物 |
|---|---|---|
| 3.1 | tactics 单位视野 + hexmap 迷雾打通（复用引用计数 + 探索闩锁） | 无（我方 hexmap 领先） |
| 3.2 | 迷雾三态（未探索 / 记忆 / 当前可见）+ 可选面向视锥 | 无 |
| 3.3 | 战棋遭遇编辑器：部署区、编队、目标、刷怪点、地形批量笔刷 | `GridHelper.cs`(31 KB)、`CellBrush`、`UnitBrush`、`GridCoordinatesGizmo` |
| 3.4 | 表现件：伤害飘字、镜头聚焦/平移、回合转场 UI、路径确认双击、触屏优化控制 | `HealthBar`、`CameraPanning`、`TurnTransitionUI`、`PathConfirmationHighlighter`、`MoveAbilityImpl.WithConfirmation` |
| 3.5 | 补 `hexmap` 生成器的水文步骤（河流），把 `test/hexmap.cpp:729-733` 的建议性断言变成强制断言 | `docs/dev/hex-terrain-capability-audit.md:83-85` |
| 3.6 | 绑定 `Timer::stepSimulation()`/`simulationClock()`，让脚本能驱动确定性固定步进时钟 | 无（我方自建） |

### 6.3 寻路收敛的具体建议

> **⚠️ 更正（2026-09-18，实施 Phase 0.2 时发现）**：本节初稿建议"把 `TacticsPath`
> 换成 `hexmap` 的相位 + 桶队列内核"。**该建议不可行**，两个独立原因：
>
> 1. **算法不匹配**：hexmap 的桶数组按**优先级**索引，前提是优先级小而有限
>    （格数 + 步数）。tactics 的代价来自 `CellState::moveCost`（`int`）与调用方预算，
>    **上界不可预测**——按绝对代价开桶会退化成无界分配。tactics 需要的本来就是
>    `(cost, cell)` 二叉堆，即它原有的结构。
> 2. **依赖不可用**：`hexmap` 的 manifest 依赖是 `DEPS graphics`，而 `tactics`
>    的硬约束是"无渲染依赖、可在无 graphics 进程内运行"。**即使算法匹配也不能复用。**
>
> 因此 Phase 0.2 的真实收益是**单目标查询提前退出 + 消除重复的校验/展开逻辑**，
> 而不是"换成桶队列"。已按此实施（见 `TacticsPath.cpp` 的 `expand` / `resolveQueryOrigin`），
> 157/157 回归测试证明行为零变化。

第二步（下沉为 capability）仍然成立，因为它交换的是**接口**而非算法：

1. ~~换上 hexmap 内核~~ → 改为：**保序的实现优化**（提前退出、单一校验源、减少分配），
   由既有的 tie-break 与快照字节一致测试作为等价性判据。
2. 把「搜索内核」下沉为一个 capability：
   `eve.grid.IGridSearch`（声明在 `common/`，由 `map` 提供实现——**由 `map` 而非
   `hexmap`**，因为 `hexmap` 带 graphics 依赖，会污染 `tactics` 的无渲染契约），
   `tactics`/`rts`/`npc_ai`/`crowd` 通过 `cap::query` 消费。
   `npc_ai/Navigation.h` 的 `INavigationProvider` 已经是这个模式，照它做
   （并顺便给它补上第一个实现）。

### 6.4 明确**不**移植的清单

| 不移植 | 原因 |
|---|---|
| `ICommand.Undo` 及其 undo 栈形式 | 死代码（0 处调用）；我们用 revision + 事务回滚 + 命令日志，撤销由游戏层用快照实现 |
| `ICell.IsTaken` + `CurrentUnits` 双写权威 | 已被证明会漏（`MoveCommand.Undo`）；保留 `BoardState` 双向索引 + `validateInvariants` |
| `RandomPositionEvaluator` 的 `new Random()` | 破坏确定性；必须走命名 RNG 流 |
| `static DijkstraPathfinding pathfinder` | 全局可变状态 |
| `async void` / 表现耦合进仿真 | tactics 必须能无头确定性运行 |
| 21 个 `Highlighter` 具体类 | 只借抽象（且要补 revert）；具体表现用 `animation`/`effects`/`particles` |
| `GridState` 直接持有并改写 controller | 改为返回意图 |
| MonoBehaviour / `[SerializeField]` / Coroutine 形态 | 引擎无此概念；数据用 schema 描述 |
| 行为树"形状即代码" | 我们的 `npc_ai::BehaviorDefinition` 已是数据 + schema 版本，保留优势 |
| `Type.GetType` + `Activator` 网络命令重建 | 脆弱且不安全；用 `LogicalId` 注册表 + 版本化 payload |
| Unity Tilemap / uGUI / InputSystem / `Awaitable` 具体 API | 平台耦合 |
| 编辑器工具的**名字字符串耦合**（`OnHierarchyChange`） | 反模式；用显式引用或 ECS 查询 |

---

## 7. 验证方式

1. **确定性**：新框架层不得引入非注入时间/随机。
   快照字节级一致 + 命令回放一致。既有锚点可复用：
   `test/tactics_turn.cpp:72-105`（精确的 `RoundStart → TurnStart → Acting`
   序列、initiative 20 先于 10、重复同 tick `advance` 返回 `Rejected`、
   no-op `faceUnit` 返回 `NoOp` 且 revision 不变）、
   `test/tactics_replay.cpp`（回放后快照字节一致）、
   `test/rts_composition.cpp:6124,6160`（lockstep 与规范状态哈希）。
2. **无头可运行**：tactics 相关测试全部在无窗口进程内跑
   （`test/tactics_*.cpp` 已是这个形态，新测试保持）。
3. **契约测试**：`ILineOfSightPolicy` / `ICoverPolicy` / `ITurnPolicy` /
   `IGridSearch` 的每个实现共享同一组契约测试（仓库规范要求）。
4. **可选依赖双态**：provider 存在/缺失两种配置都要测
   （例：未注册 LOS provider 时 `LineOfSightMode::Required` 必须返回
   可观测的 `Unsupported`，而不是空成功集）。
5. **脚本出口回归**：每个新 `expose` 绑定都要有 `test/*_script.cpp` 覆盖，
   并验证 `Result` 投影（`ok`/`code`/`status`/`value`）在脚本侧可达。
   现状是 `eve.Tactics` 仅 2 个用例、`eve.HexMap` 0 个用例——需要显著加强。
6. **网格不变性**：参照 `test/map_path.cpp:508-520`（进入惩罚不修改格子成本）
   与 `:252-274`（流场与 A* 在封/解封前后一致）的做法。
7. **架构门禁**：`ARCHITECTURE_BASE=HEAD make check/architecture-contracts`；
   `python3 scripts/module_depgraph.py --check`；裁剪 build profile。
8. **可视化人工验收**：用 `examples/tactics` + MCP 截图，与
   `ClashOfHeroes` 的同类场景做观感对照（两者素材同源，可比性强）。

---

## 8. 未验证 / 待补充

- **版本号 4.2.0 未经验证（重要）。** 该版本号仅来自文件名
  `Turn Based Strategy Framework 4.2.0.unitypackage`。
  我对解包产物的全部文本（`*.cs` / `*.asmdef` / `*.json` / `*.txt` / `*.md` / `*.shader`）
  做过版本串扫描：**包里没有任何版本声明**（`versionDefines` 一律为空数组，
  README 只写素材版本如 `v1.0`）。
  联网侧独立检索到的最高版本证据只到 **4.0.2**（v1.0.1 2016 / FAQ v2.0.1 / FAQ v2.2 /
  3.0.5 / 4.0 / 4.0.1 / 4.0.2），**没有找到任何 4.2.x 的证据**。
  因此：本文所有结论针对的是**这份具体文件的内容**，
  不能断言它们等同于厂商文档里名为"4.2.0"的版本。
- 框架侧结论**全部来自解包源码直读**，未依赖厂商宣传资料；每条有 `file:line`。
  厂商官网/Asset Store 的功能声明、4.2.0 changelog、社区评价由并行联网调研补充；
  若与本文冲突，**以源码为准**。
- **示例工程的"功能清单"部分是从资产名（prefab/scene/script 名）与
  `README.txt`/`README.md` 推断的**，因为解包时只提取了 `Scripts/`、
  `Editor/`、`External/tbsf-common/` 与示例 `.cs`，未提取 `.unity`/`.prefab` 内容。
  已在 §2.3 标注为推断。
- `HeightPositionEvaluator.cs` 不是合法 UTF-8（疑 UTF-16LE），未能直读实现，
  只从文件名与 `IHeightComponent.cs` 推断作用。
- **未在本次分析中运行任何构建或测试**。§5.12 关于 `hexmap` 的结论**已实测修正**：
  不是"链接失败"，而是"能编译但全未提交"——依据是
  `build/win32-debug/src/modules/CMakeFiles/EVHexmap.dir/…/HexMapModule.cpp.obj`
  与 `build/win32-debug/src/modules/hexmap_src.txt` 确实存在。
- **证据基线是 `HEAD + untracked`，不是完整工作区**（§1.4）。
  headline 结论已在主工作区逐条复核通过，但主工作区仍有
  `Runtime.cpp` / `SquirrelOwnership.h` / `RPG.cpp` / `ui/*` 等 `M` 文件
  与被删除的 `scripts/check_bindings*.py` 未纳入基线。
- **本文档自身经历过一次编码事故与一次结论纠错**（详见 §1.4、§5.12）：
  报告已被完整重写并通过严格 UTF-8 校验；`hexmap` 的初稿结论已更正。
  两次事故同源，都来自本环境的两个陷阱——
  **用 PowerShell 做文本往返会破坏 UTF-8**，以及
  **按 HEAD 建 worktree 会漏掉工作区的已跟踪修改**。
- **联网调研产物中 `t2b-determinism-networking.md` 仅为部分重建**：
  原件第 1–819 行及其中约 362 条 URL 因我的文件搬运失误而**永久丢失**
  （完整说明见 §9.2b）。该象限的细粒度引证不可信，需重新调研。
- `Packages/manifest.json`（194 B）内容未提取，Input System / Tilemap / TMP
  的依赖只能从 `using` 指令推断。

---

## 9. 外部资料（联网部分）与已知缺口

### 9.1 本环境的联网限制（重要）

本次分析运行在受限沙箱中：`web_fetch` 对 `github.com`、`raw.githubusercontent.com`、
`assetstore.unity.com` 等目标一律返回
`URL hostname resolves to a non-public IP address`，即**无法直接抓取页面正文**。
因此联网部分只能依赖 `web_search` 的检索结果清单，**拿不到可引用的原文片段**。

结论：**本文的全部事实性结论均来自解包源码直读**（每条附 `file:line`），
联网部分仅用于定位入口与交叉验证，未用于支撑任何论断。
厂商宣传口径与本文若有冲突，**以源码为准**。

### 9.2 已定位到的权威入口（供后续补充）

| 用途 | URL |
|---|---|
| 官方文档仓库（Unity 版 TBSF 文档，作者个人账号 `mzetkowski`；**不存在名为 `Crooked-Head` 的组织**） | https://github.com/mzetkowski/tbsf-unity-docs |
| Unity Asset Store 商品页（功能声明、版本、评价） | https://assetstore.unity.com/packages/templates/systems/turn-based-strategy-framework-50282 |
| Asset Store 评价页 | https://assetstore.unity.com/packages/templates/systems/turn-based-strategy-framework-50282/reviews |
| Unity Discussions 官方发布/讨论帖（36 页，社区评价与痛点） | https://discussions.unity.com/t/released-turn-based-strategy-framework/749240 |
| **Nakama 客户端**（补足 §2.2(9)：传输层实现**存在**，但不在本包内） | https://github.com/mzetkowski/tbsf-nakama-client |
| **Nakama 服务端**（TypeScript + Docker） | https://github.com/mzetkowski/tbsf-nakama-server |
| 厂商 ReleaseNotes.txt（**未读取，最高价值**） | https://drive.google.com/file/d/1PkCrQRNQ29T7id5Fbx0--dnP55U2p94o/view |
| 厂商 FAQ v2.2（未读取） | https://drive.google.com/file/d/1k-OsykcSm5XaWIfVgO75zU2tPZpyAHLp/view |
| 厂商 FAQ v2.0.1（未读取） | https://drive.google.com/file/d/1GSFdmdsN7tYSYjiU2ClqFwMJZVHiQWKW/view |
| 旧版官方文档 v1.0.1（2016，PDF） | https://www.fichier-pdf.fr/2016/07/01/turn-based-strategy-documentation-v1-0-1/turn-based-strategy-documentation-v1-0-1.pdf |
| 厂商身份的唯一硬证据（demo APK 包名 `com.crookedhead.tbsframework.clashofheroes`） | https://apkcombo.com/clash-of-heroes-a-tbsf-demo/com.crookedhead.tbsframework.clashofheroes/ |
| 第三方教程（日文，10 章系列；仅验证存在，正文未读） | https://note.com/unity_note/n/n86ebdf60ef93 |
| 第三方深度评测（中文；仅验证存在，正文未读） | https://blog.csdn.net/2403_88403568/article/details/157093957 |
| 版本号镜像（转载站，可信度低，仅证明版本号存在过） | 3.0.5 `cgioo.com/...tid=35531`、4.0 `cgioo.com/...tid=38326`、4.0.1 `psdly.co.uk/turn-based-strategy-framework`、4.0.2 `cgioo.com/...tid=39273` |

> **关于 §2.2(9) 的一处修正**：我写"传输层是抽象类，包里没有任何具体实现"——
> 就**本包**而言成立，但 Nakama 的客户端与服务端是**独立仓库**。
> 也就是说这个"命令广播骨架"是**有意设计成配外部后端**的，
> 这一点应视为设计选择而非缺陷。**但 `InitializeRng` 从未被调用仍然是缺陷**：
> 后端就位也不会让随机数在各端一致。

### 9.2b 子代理的联网调研产物

详细的联网检索记录与 TBS 通用架构模式（回合调度 IGOUGO/CTB/ATB/WEGO、
反应与打断、确定性/回放/回滚、AI 选型、网格几何与 LOS、迷雾、网络、持久化，
外加一份 60 项"完整 TBS 框架能力清单"）在 `docs/dev/tbsf-research/`：

| 文件 | 行数 | 状态 |
|---|---|---|
| `tbs-gap-analysis-research.md` | 587 | 汇总（两个 target + 置信度/缺口 + 60 项清单） |
| `t1-tbsf-unity.md` | 266 | 完整 |
| `t2a-turn-scheduling.md` | 760 | 完整（字节数与原件一致：71,458） |
| `t2c-ai-grid-fog-persistence.md` | 385 | 完整（字节数与原件一致：78,139） |
| `t2-mine.md` | 288 | 完整 |
| `t2b-determinism-networking.md` | 458 | ⚠️ **仅部分重建** |

**关于 `t2b` 的损失（必须知道）**：该文件原件 1,162 行，
其中第 1–819 行（Topic A/B/C 的详细证据段与 **约 362 条仅存于此文件的 URL**）
在文件被误删后**无法从上下文恢复，属永久丢失**。
重建版已把缺失正文替换为**明确标注 "BODY NOT RECONSTRUCTED" 的标题索引 + 分节摘要**，
并在 §5 列出损失清单。**该象限（确定性/反应/网络的细粒度引证）如需使用，
必须从头重新调研**（约 25 次定向检索，或等有可用抓取通道）。

**保真度核验**：我按删除前记录的文件字节数对照——
`t1`（20,563）、`t2a`（71,458）、`t2c`（78,139）三者**与原件字节数完全一致**；
`t2-mine` +804 B、汇总 −22 B（应为主题免责声明的措辞微调）；
`t2b` 由 99,215 → 34,137 B，与"部分重建"一致。
六者均经严格解码器校验为合法 UTF-8、无 BOM、无替换字符。

⚠️ 该目录的内容**全部基于搜索结果片段**（每条标注 SNIPPET / TITLE / WEAK /
INFER / UNVERIFIED），**没有任何一个页面正文被成功打开**，
因此**不得**作为事实依据引用，只能作为"下一步该抓什么"的地图。

### 9.3 仍需补做的联网工作（建议在有网络的环境按此顺序执行）

已定位到**未读取的一手资料**，按价值排序：

1. **厂商 `ReleaseNotes.txt`** —— 一次性解决 changelog、4.x vs 3.x 差异、
   roadmap、已知限制、支持版本（本项目里其他所有版本相关问题都由它回答）。
2. **厂商 FAQ v2.2 / v2.0.1（Google Drive PDF）** —— 定价、许可证、
   是否含完整 C# 源码、支持的管线与 2D/3D。
3. **Asset Store 页的 "Version changes" 折叠区** —— 可独立交叉验证第 1 项，
   并确认是否存在 4.2.x。
4. **`git log` on `mzetkowski/tbsf-unity-docs`** —— 文档随版本演进的轨迹，
   能反推厂商自己认为"之前缺什么"。
5. **Unity Discussions 帖最后约 5 页** —— 社区真实痛点
   （本次**一条用户抱怨都未能验证**，这一项是唯一的来源）。
6. **`mzetkowski/tbsf-nakama-*` 两个仓库** —— 补足 §2.2(9) 的联机设计细节，
   并检查它们是否真的调用了 `InitializeRng`。
7. 文档仓库目录逐节对照本文 §2.2 的九个接缝，找出**文档承诺但源码未实现**的能力
   （本文只做了源码侧判断）；并从 `Features/*` 之外的教程补齐
   §2.3 中标注为"从资产名推断"的示例功能清单。

### 9.4 与本次结论的交叉验证状态

| 本文结论 | 源码证据 | 外部交叉验证 |
|---|---|---|
| 核心薄、示例重 | ✅ 78/67/11/1544 文件计数 | ⏳ 待补（ReleaseNotes / Asset Store 功能列表） |
| 无存档/无回放 | ✅ `savestate`/`persist` 0 命中 | ⏳ 待补 |
| 网络只是命令骨架 | ✅ 2 个 opcode；实现见 `tbsf-nakama-*` 独立仓库 | ⏳ 待补（Nakama 仓库是否调用 `InitializeRng`） |
| AI 行为树形状即代码 | ✅ `RegularBehaviourTreeResource.cs:65-111` | ⏳ 待补（文档如何描述 AI 扩展） |
| 无迷雾/LOS/掩体/夹击 | ✅ 关键词 0 命中 | ⏳ 待补（文档是否列为 roadmap） |
| 文件名版本 4.2.0 | ✅ **包内无任何版本串** | ❌ 最高只查到 4.0.2，**4.2.x 无证据** |
| 厂商名为 Crooked Head | ✅ 命名空间 `com.crookedhead.tbsf.*` | ✅ APK 包名 `com.crookedhead.tbsframework.clashofheroes` |
| 文档仓库为官方 | —— | ⚠️ 在个人账号 `mzetkowski` 下，**无 `Crooked-Head` 组织** |

联网侧**一条厂商功能声明、一条 changelog、一条用户抱怨都未取得原文**；
本文所有论断均不依赖它们。

---

## 10. 基线刷新：`dae6a644c` → `origin/dev` `d486eff1e`

> 本节是全文档中**时效性最强**的部分。§1–§9 的取证基于旧基线，
> 本节按新基线逐条重核，**凡与本节冲突者以本节为准**。

### 10.1 刷新过程与四个环境发现

1. **HTTPS 拉取失败可绕过**：`origin` 的 `http.sslbackend=schannel` 会报
   `schannel: AcquireCredentialsHandle failed: SEC_E_NO_CREDENTIALS`。
   改用 `git -c http.sslBackend=openssl fetch origin --prune` **成功**。
   （这只是命令行覆盖，**我没有改动仓库配置**；若想常态化，
   可自行 `git config http.sslBackend openssl`。）
2. **`ghsync`（ssh over 443）不可用**：`ssh.exe: fatal error - couldn't create
   signal pipe, Win32 error 5` —— 沙箱禁止创建命名管道。
3. **主工作区正在被并发修改（重要）**：分析期间我发现
   `src/modules/hexmap/HexSphereMap.{h,cpp}`、`HexSphereTopology.{h,cpp}`
   于**今天 15:54–16:00 新建**，`examples/hex-planet/` 新出现，
   未提交条目由 27 增至 35。**因此我只刷新了分析 worktree，完全没有触碰主工作区**
   ——否则会毁掉正在进行的球面六边形工作。
4. 本地 `dev` 比 `origin/dev` **多 1 个提交**：`dae6a644c` 并不在 `origin/dev` 上。
5. 刷新方式：删除 worktree 中我早先补拷的 untracked 副本
   （`src/modules/hexmap`、`test/hexmap.cpp`、`docs/usr/modules/hexmap.md`、
   `examples/hex-terrain-3d`，上游均已提供权威版本），
   然后 `git reset --hard origin/dev`。分支无独有提交，分析产物（untracked）完好。
   跨 199 个提交的总体积：**1403 文件 / +178,908 −29,693**；
   其中策略相关模块 **104 文件 / +19,283 −266**。

### 10.2 旧 → 新 对照（逐条复核）

| # | 旧基线（`dae6a644c`）结论 | 新基线（`d486eff1e`）实测 | 判定 |
|---|---|---|---|
| 1 | `hexmap` 未提交、有丢失风险 | **已作为 PR #417 合入 dev**：31 文件受版本控制，`cmake/module_manifest/rendering_simulation.cmake:53` 有声明，`module_list.nut` 有条目 | **失效** |
| 2 | `combat` 的 `Module_IMPL` = 0，能力运行时脚本不可达 | **`combat` = 1**，且 `Combat::expose` 暴露了完整 `CombatRuntime`：`registerFighter` / `setMoveIntent` / `navigateTo` / `applyDamage` / `applyTimelineDamage` / `grantAbility` / `registerTimelineAbility` / `activateAbility` / `advanceAbilities` / `cancelAbility` / `matchingAbilities` / `abilityGrant` / `state` | **失效（大幅改善）** |
| 3 | `combat::standardCombatAbilities()` 只有测试调用 | **已有生产调用方** `src/modules/combat/Combat.cpp` | **失效** |
| 4 | 只有 5 套网格寻路 | 新增 `combat/navigation/PathfinderCombatNavigation.{h,cpp}`，且 `hexmap/HexSearch` 已转正 → **6 套** | **需更新** |
| 5 | `npc_ai` / `settlement` / `grid` 的 `Module_IMPL` = 0 | **仍为 0** | 成立 |
| 6 | `npc_ai` 在 `src/` 内零消费者 | 仍为 0（`combat/navigation` 是另一条路径） | 成立 |
| 7 | `tactics` 的 `actionPoints` 从不递减 | **仍然从不递减**（只有 `reactionPoints`） | 成立 |
| 8 | `tactics` 脚本面窄（无 `endTurn`/`finish`/反应/快照/预检） | **仍然窄**：绑定仍是 ~25 个 battle 方法，无 `endTurn`/`finish`/`openReaction`/`snapshot`/`replay`/`reachable`/`preview*` | 成立 |
| 9 | `examples/tactics/main.nut` = 31,321 B | **仍是 31,321 B / 647 行，未变** | 成立 |
| 10 | 无 `ILineOfSightPolicy` / `ICoverPolicy` / `ITurnPolicy` | **仍为 0**（tactics + hexmap 内） | 成立 |
| 11 | 无 `OccupancyPolicy`（tactics） | **tactics/hexmap 内仍为 0**；全仓 5 处命中都在 `climbing`（`ClimbingRouteOccupancyPolicy`），与战棋无关 | 成立（已澄清） |
| 12 | 无 cover / flank / facing 加成 / 高地加成 / 机会攻击 / ZOC | tactics+combat+rpg 内仍全为 0；`facing` 仍只被存取、**不被任何伤害公式读取** | 成立 |
| 13 | `MODULES.md` 不链接 `tactics.md`/`hexmap.md` | **仍不链接** | 成立 |
| 14 | `docs/usr/modules/rts.md` 不存在 | 仍不存在 | 成立 |
| 15 | 引擎内无 2D/网格 LOS，唯一实现是 physics World3D | 仍无 `ILineOfSight` 网格实现；但**新增 `sensing/TargetingPipeline`**（Select/Filter/Sort 任务管线，含锥角与 facing 来源）——目标筛选能力显著增强，**但不等于视线遮挡** | 部分更新 |

### 10.3 新基线新增的、方案应纳入的设施

| 新设施 | 规模 | 对战棋的意义 |
|---|---|---|
| `combat/Combat.cpp` 的 `CombatRuntime` 脚本面 | 33 KB | **P0-A 的主要部分已完成**：技能授予/激活/冷却、伤害、位移都已对脚本开放，且被 `examples/combat-action-editor` 实际使用 |
| `combat/CombatLocomotion` + `combat/navigation/PathfinderCombatNavigation` | 11 KB + 5 KB | 战斗位移与导航成为组合模块；但**又添了一套寻路**（§10.2 第 4 条） |
| `combat/ActionDamageSink`、`ActionWindowState` | 3.3 KB + 5 KB | 把动作时间轴与伤害/窗口状态接起来——正是 §4 P0-E「通用行动协议」所需的中间件 |
| `sensing/TargetingPipeline` | +451 行 | Select/Filter/Sort 可组合目标筛选 + 锥角 + facing —— 战棋目标选择的现成底座 |
| `sensing/Sensing.*` 大幅扩展 | +793 行 | 感知层显著增强 |
| `rpg/RpgDialect` | +859 行 | RPG 脚本方言；`rpg` 另有 `StatusSystem`/`VitalsSystem`/`BattleControl` |
| `map/DualGrid` 扩展 | +313 行 | 双网格材质过渡 |

### 10.4 方案（§4 / §6）据此修订

- **Phase 0.1「提交 `hexmap`」已完成**（PR #417）→ **删除该条目**。
  新出现的同类风险反而是**主工作区里未提交的球面六边形工作**
  （`HexSphereMap` / `HexSphereTopology` / `examples/hex-planet`）。
- **P0-A「给已有能力运行时开脚本出口」已大部分完成**（`combat`）。
  剩余范围缩小为：`npc_ai` / `settlement` / `grid`（仍为 0），
  以及**把 `tactics` 与新的 `CombatRuntime` 真正接起来**
  ——现在两边都有门，但 `examples/tactics` 依然自己实现 HP/技能/AI。
- **P0-E「通用行动协议」的落点更明确了**：应优先复用
  `combat/ActionDamageSink` + `combat/ActionWindowState` + `sensing/TargetingPipeline`，
  而不是新造。
- **P0-H「视线/掩体」优先级上升**：目标筛选（`TargetingPipeline`）已经很强，
  但**视线遮挡仍然缺失**，两者搭配才是完整的战棋目标系统。
- **新增待办**：`combat/navigation/PathfinderCombatNavigation` 使 §5.2 的
  "寻路收敛"从 5 套变成 6 套，收敛方案的收益更大了（§6.3 不变，但更紧迫）。
- **P1-L「给 `npc_ai` 一个真实调用路径」优先级下降**：
  `combat` 已经提供了脚本可达的战斗运行时，战棋 AI 更可能建在它上面，
  `npc_ai` 是否参与需要重新判断。

### 10.5 本节的方法论说明

§1.4 记录的教训（按 HEAD 建 worktree 会漏掉工作区已跟踪修改）**再次生效**：
我最初把 `hexmap` 判为"未落地"，正是因为 worktree 停在旧 HEAD。
**正确的取证顺序应是：先 `fetch` 并把基线对齐 `origin/dev`，再开始分析。**
本次刷新已把这一步补上；若后续还要继续这项分析，
请把 worktree 保持在 `origin/dev` 基线上，并在每次开始前
`git -c http.sslBackend=openssl fetch origin` 复核。

---

## 11. 实施进度（截至 2026-09-18，worktree `codex/tbs-framework-gap-analysis`）

本节记录 §6.2 计划**实际落地并已验证**的部分，避免本文继续推荐已完成的工作。
基线 `origin/dev` `d486eff1e`；§11.1 记录的 Phase 0.2–1.2a 累计 **11 文件 / +1214 −113** + 4 个新文件
（提交 `bdcb98b25`）；其后 Phase 1.3 声明切片与 Result 短路修复见同表后续行。

### 11.1 已完成并验证

| 计划项 | 内容 | 验证 |
|---|---|---|
| Phase 0.1 | `hexmap` 落地——**上游 PR #417 已合并**，无需本分支处理 | 上游 31 文件受控 + manifest 声明 |
| Phase 0.2 | 寻路内核：单目标查询提前退出 + 单一校验源（`resolveQueryOrigin`）+ 单一展开源（`expand`）。**未**改用桶队列（§6.3 已说明为何不可行） | 行为零变化，确定性 tie-break / 快照字节一致测试全绿 |
| Phase 0.3 | **有向边事实**：`EdgeState{passable,extraCost,tags}` + `addEdge/edge/tryEdge/edgeRecords`；寻路按方向阻挡与叠加非对称代价；**快照 v2** + v1 迁移；脚本 `addEdge/edge/hasEdge` | 3 个 C++ 用例 + 1 个脚本用例；单向性、非对称代价（出 150 / 回 350）、往返保真 |
| Phase 1.1a–c | 脚本面补全：`endTurn`/`finish`/反应三件套/`previewMove·Face·Wait`/`reachable`/`cellsInRange`/`unitResources` | 4 个脚本用例；预检与提交 cost 一致 |
| Phase 1.2a | **`ITurnPolicy` + `TurnPolicyRegistry`**：稳定字符串 id、值投影 `UnitOrder`、三值 `TurnOrder`；内建 `side_alternating`/`initiative`；**快照 v3** + v1/v2 冻结映射迁移；脚本 `start(id)`（诊断列出已注册集合）+ `policyIds()` + `policyId()`（读取侧） | 3 个策略用例（含**自定义策略反转行动顺序**，证明 id 真被解析）+ 3 个迁移用例 |
| Phase 1.3 | **通用行动协议**：`UseAbility` 命令种类 + `targetUnit`（可与 `targetCell` 并存，二者独立）+ `payload`（不透明、调用方自有、上限 4096，原样持久化与回放）+ `previewAbility`（**与提交共用同一 validator**）+ 回执 `AbilityReceipt`；**效果结算刻意留在 RPG/游戏适配层**（tactics 保持 render-free 且不拥有伤害），消费者用事件 `causationCommand` 关联命令记录取得声明；**快照 v4/v6** + 版本门槛；MCP/devtools 控制面新增 `tactics:use-ability`（参数 `{action,x,y,layer,targetUnit,payload}`）；**并修好回放缺口**——`TacticsReplay::apply` 之前没有 `UseAbility` 分支，含技能声明的战局无法回放 | 6 个 C++ 用例（扣点+记日志+事件+事件与命令的关联、预检不改状态且与提交诊断码一致、行动力耗尽/错行动者/错层/非法 action 拒绝、外来单位 `NotFound`、已击败目标拒绝、payload 越界拒绝且恰好等于上限被接受）+ 脚本用例（含 `previewAbility` 不扣点、`targetUnit`/`payload` 读回）+ **回放字节一致用例**（声明含目标与 payload）+ 3 个 v6 迁移用例 |
| Phase 1.2b | **充电时间轴（CTB/ATB）**：`ITurnPolicy::chargeModel` 声明 `ChargeModel{initiativeGain, cost, threshold}`（默认全 0 = 无充电模型，行为与纯排序策略**逐位一致**）；充电状态归 `TurnResources::charge` 所有；回合机负责"累积 → 阈值过滤 → 扣除"；**本轮无人就绪**报告为 phase 不变 + `NoOp`（轮数与每轮资源刷新照常）；永远无法达标的规则在 `start` 被拒绝（`PreconditionViolation`，战局留在 `setup`）；`TurnState::schedule` 成为本轮行动队列（与名册 `units` 分离，单一真值）；**快照 v5** + v1–v4 迁移（充电 0、名册即队列） | 5 个新用例：内建 CTB 下 20/10 initiative 单位为 **2:1 行动频率**（手算 18 次 advance 的精确行动序列 + 8 个空轮）、只扣行动者且扣满阈值、全部 initiative 为 0 时被拒且仍可用 `initiative` 启动、排序策略仍每轮各动一次且 `charge` 恒为 0、项目自定义模型（阈值 40）被真正采用；另有 2 个 v5 迁移用例（v4 迁移后充电归零并报告空轮、v4 载荷携带 v5 字段被拒） |
| Phase 1.4–1.5 | **棋盘交互状态机**（`tactics/Interaction.h`）：`InteractionState{blocked,await_selection,unit_selected,targeting,resolving,ended}` + `InteractionIntentKind{none,select_unit,move_to,use_ability_on,cancel,confirm,end_turn}`；会话只读**不可变投影**（`InteractionContext`：phase/activeUnit/controlledUnit/reachableCells/targetableCells/unitsByCell/revision），**返回意图而不改仿真**，意图带回 `expected` revision；"不是我的回合"用 `state() == blocked` 表达（不因跨回合而失败），不可达格返回 `none`，结构性非法才失败；**表现意图**（`tactics/Presentation.h`）：`UnitVisualState` 9 态 + `PresentationIntent`（含 `expiresAtTick`）+ **显式回退契约** `PresentationRevert{restingState, trigger}`（`never/on_expiry/on_next_turn/on_round_start`），几何来自事件指向的**已接受命令**而非猜测，撤销持久状态返回 `Unsupported` | 9 个新用例：阻塞/结束/非当前回合三态、意图不改状态且 `expected` 正确、待提交意图期间的二次点击为 `Conflict`、格子→单位解析（含空格子无目标）、取消与结束回合、非法 action 与空 pending 拒绝、事件→意图投影（turn.pending/turn.completed/round.pending）、几何取自命令且缺命令时**不猜**、持久状态撤销被拒 + 瞬时状态逆意图仍带过期时间 |

**交付检查点**：提交 **`bdcb98b25`**（分支 `codex/tbs-framework-gap-analysis`，领先 `origin/dev` 1 个提交），
23 文件 / **+6292 −113**，工作区干净。
**已验证**：`ctest` **264/264 通过**（tactics 33 + 邻接 231）；`module_depgraph --check`、
`check_architecture_contracts --base HEAD`、`check_quality_metadata`、`check_bindings --strict`
**全部 exit 0**。
**未验证**：`clang-format` 在本环境不可用（PATH 无 `clang-format`/`clang-format-18`），
故 CI 的 `format` 作业未在本地跑过——这是本提交最可能被 CI 拦下的点，且只涉及格式。

**门禁状态（Phase 1.6 tactics 侧之后）**：
`module_depgraph --check`、`check_architecture_contracts --base HEAD`、
`check_quality_metadata`、`check_bindings --strict` **全部 exit 0**
（`check_bindings`：8252 个绑定、tactics 49 个绑定有文档、无 gap）；
tactics 模块用例 **71/71 通过**（每次提交前跑该模块），
Phase 1.4/1.5 之后曾跑过 `tactics|gameplay` 子集 **91/91**。

**本地验证原则（用户 2026-09-18 明确要求）**：**全量用例交给 CI，不要在本地整套跑**。
本地只做三件事：编译受影响 target、跑覆盖本次改动的用例（`ctest -R "^tactics\."` 或单个用例）、
跑四个源码门禁。因此本分支最后一次全量本地运行是 Phase 1.3 之前的记录
（`ctest 5219/5219`，`build/win32-debug`，`-j 8 -E '^bundle/' -LE benchmark`，约 6 分钟）——
**Phase 1.4/1.5/1.6 与 v5/v6 之后没有本地全量运行**，这段回归覆盖由 CI 承担，不再由本地重复。

**仍未验证**：`clang-format` 在本环境不可用（PATH 无 `clang-format`/`clang-format-18`），
故 CI 的 `format` 作业未在本地跑过——涉及本次改动的所有行都按仓库风格手工排版，但未经工具确认；
`check_nodiscard.py` 因无宿主 C++ 编译器而跳过了它的 compile-fail 部分。
**构建配方补充**：`test/native_test_plugin.dll` 是独立 target，`--target unit_test` 不会构建它；
若确实要在本地跑全量，不先构建它会让 `plugins.load.nativeLibraryAndInstantiateCppModule` 以
`LoadLibrary failed ... (err=126)` 失败（构建配方问题，非代码回归），已记入 `build/RECIPE.md`。

### 11.2 实施中发现的、对本文的修正

除 §6.3 的桶队列更正外，还有两条：

1. **`bool` 返回的比较器会被架构门禁拒绝**（`api-shape ... returns bool`）。
   我最初把 `ITurnPolicy` 写成 `bool precedes(...)`，用"两个方向都 false"表示并列。
   改为显式三值 `TurnOrder{LeftFirst,RightFirst,Equivalent}` 后门禁通过——
   而且这本来就是更好的设计：并列是一个有含义的结果，不该靠隐性约定表达。
2. **版本门槛是最容易写错、也最需要测试的地方**。加 v3 时我把
   `hasEdges` 写成 `sourceVersion == SchemaVersion(2)`，于是 v3 载荷被当作 v2 解析、
   期望 board 只有 2 个字段，**4 个快照/回放测试立刻变红**。正确写法是
   `sourceVersion != SchemaVersion(1)`。
   **教训：每次加版本，门槛条件都要写成"某版本及其之后"，而不是"恰好等于某版本"**，
   并且必须有一组覆盖全部历史版本的往返测试。
3. **一个真实的生产缺陷被新用例暴露：`!a || !b` 短路链会漏检 Result。**
   为 v4 写"v3 载荷携带 `use_ability` 必须被拒绝"的用例时，整个测试进程**直接以退出码 3
   死掉，没有任何断言输出**。逐段插桩定位到
   `TacticsPersistence.cpp` 的 `parseCandidate`：`parseCommands` 已经按预期返回失败，
   但失败传播语句本身崩了。根因是

   ```cpp
   if (!events) return Result<Candidate>::failure(events.status());
   if (!reactions) ...
   if (!commands) ...          // ← 在这里返回
   if (!objectives) ...        // ← objectives / random 从未被观察
   if (!random) ...
   ```

   `commands` 失败时提前返回，**`objectives` 与 `random` 这两个 Result 从未被 `ok()/status()` 观察**；
   Debug 下未观察的 Result 在析构时 `EV_ASSERT`，于是
   `random` 的析构抛出 → 栈展开 → `objectives` 的析构**再次抛出** →
   `std::terminate` → `abort`（Windows 退出码 3）。单个未观察只会变成一条"内部断言失败"，
   两个及以上就变成无输出的硬崩溃——这就是它长期没被发现的原因。
   `TacticsPersistence.cpp` 里同形状的 `!a || !b || ...` 守卫共 **22 处**（事件、反应、
   命令、objective、random、棋盘格/边、单位记录），全部是同类地雷。
   **修复**：新增引擎级 `eve::everyResultValid(...)`（`Result.h`，用 `&` 折叠而非 `&&`，
   保证每个实参都被观察且不短路），并把该文件全部多 Result 守卫改写为它；
   在 `Result检查与不得丢弃返回值规范.md` 增加"多个 Result 的组合条件（短路陷阱）"一节与测试清单项。
   **教训：Result 的"必须检查"契约与 C++ 的短路求值天然冲突；任何把多个 Result 放进同一个
   条件表达式的代码都必须显式全量观察，否则失败路径会以崩溃而非诊断的形式呈现。**
   剩余风险：其他模块（尤其各 `*Persistence.cpp`/`*Snapshot.cpp` 解析器）可能仍有同形状代码，
   本分支只修了 tactics 这一个文件（见 §11.3）。

### 11.3 未完成（按剩余价值排序）

| 项 | 阻塞/说明 |
|---|---|
| Phase 1.2b CTB/ATB | ✅ **已完成**（见 §11.1）。若将来要让"充电"跨战斗继承或做成回合制/半即时可切换参数，需要把 `ChargeModel` 从策略常量提到战局数据（那是又一次 schema 变更 + 迁移） |
| Phase 1.3 通用行动协议 | ✅ **已完成**（声明 / 目标 / payload / 预检 / 回放 / 控制面，见 §11.1）。**刻意不做**：把 `payloadJson` 解释成技能参数、或在 tactics 内结算效果——`rpg`/`combat` 已经拥有技能定义与 `CombatRuntime`，tactics 再解释一遍就会变成第二个真值来源。若将来要让 tactics 直接约束目标合法性（`action::AbilityDefinition` 的 AP/冷却、`sensing::TargetingSpec` 的射程/视线），应做成**可注入接口或 capability**（同 §1.6 的做法），不要给 tactics 加 `sensing`/`rpg` 依赖 |
| Phase 0.4 行动经济 | ✅ 已解除阻塞：`useAbility` 现在是 `actionPoints` 的真实消费者（1 次声明 = 1 点，耗尽后拒绝）。**未完成**：自动结束回合语义（行动力为 0 时是否自动 `endTurn`）需要产品决策，暂不默认 |
| `Result` 短路陷阱（新发现） | tactics 的 `TacticsPersistence.cpp` 已全量修复（22 处守卫 + 新增 `eve::everyResultValid`）。**未完成**：**其他模块未审计**——建议用 `rg '!\w+ \|\| !\w+' src/modules` 加人工筛"操作数是否为 Result"扫一遍其余解析器 |
| Phase 1.6（tactics 侧） | **`ILineOfSightPolicy` / `ICoverPolicy`**（具名 capability `eve.tactics.ILineOfSightPolicy` / `ICoverPolicy`）+ 内建网格实现：阻挡读持久化 **tag**（`sight_blocker`）而非 `passable`（视线与通行是两件事，且**不占新 schema 版本**）；追踪只算两端点之间、棋盘外格按阻挡、三种拓扑（supercover / hex cube 插值）；**从 canonical 格序计算后翻转 → 对称性由构造保证**；`visibleCellsInRange` 组合既有 `cellsInRange` 与视线（射程与视线各自单一所有者） | 6 个新用例：端点不挡/线上挡/线外不挡/棋盘外挡四点、**625 个有序对的对称性扫描**（补 §5.14 空白）、hex 轴向与反对称方向阻挡 + 跨层 `Unsupported` + 未知格 `NotFound`、掩体 full/half/none 与方向无关性、组合查询与射程一致且被挡格消失、capability `provide/query/revoke`（含 provider 缺失 == 空指针） |
| Phase 1.6（hexmap 接线） | ❌ **未做**，本轮把设计约束查清并记录如下（3 条，全部已验证）：**① capability 是单槽位**——`physics/PhysicsCapabilities.cpp:163` 已经 `cap::provide<eve::sensing::ILineOfSightQuery>(&targetingLineOfSightAdapter())`（世界坐标 3D 实现），而 `cap::provide` 是**替换语义**，所以 hexmap 直接再 provide 会互相顶掉，且谁生效取决于模块加载顺序。接口自己的文档早就写明"provider 可以只支持部分坐标空间，不支持的必须返回 `Unsupported`"（`sensing/Targeting.h:420-425`），因此正确做法是**一个按坐标空间分发的 dispatcher provider**（world → physics，grid → hexmap），只注册一次，而不是两个 provider 抢槽位。**② hexmap 没有"段可见"谓词**——`HexVisibility` 是多观察者 FOV 计数器（`isVisible(cellIndex)`），`HexSearch::collectVisibleCells` 一次算出整片 FOV；而 `ILineOfSightQuery::query(from,to)` 问的是**一段**。所以要么给 hexmap 加一个段谓词（新 API），要么用"`to` ∈ FOV(from) 且 range = 两点距离"近似并在文档里写明**阴影投射 FOV 天然不对称**这一后果——这是需要先定语义的设计决策，不能顺手做。**③ 分层没有障碍**：`hexmap` 是 LAYER 4、`sensing` 是 LAYER 1，加依赖是合法向下边。 | 待办：dispatcher provider + hexmap 段谓词语义决策 + provider 存在/缺失双态与 `LineOfSightMode::Required` 集成测试 |
| Phase 1.1d | **快照/回放脚本出口**：新增引擎级 `ISnapshotContentHasher` capability（`common/SnapshotHash.h`）+ **如实命名**的内建摘要 `fnv1a64x2-noncrypto`（`NonCryptographic`，用于完整性/身份而非安全）+ 宿主启动注册点（`main.cpp`，**已有 provider 时不覆盖**，返回 `NoOp`）+ `snapshotContentHashProvider()` 显式回退 + `activeSnapshotHashAlgorithm()` 可观察；tactics 侧 `snapshotJson`/`restoreJson`/`commandLogJson`/`replayJson`/`snapshotAlgorithm` 五个脚本绑定；命令日志编解码器加**锚定端**规则（payload 内日志锚定末尾，回放日志锚定起点），因此回放与恢复共用同一个解析器 | 5 个新用例：内建摘要的命名/确定性/幂等注册（Applied→NoOp）、**provider 存在/缺失双态**（注册后 `snapshotAlgorithm` 变化且确被调用，revoke 后回到内建）、存档往返 + **改一个字节即 `HashMismatch` 且状态不变** + 非信封文本 `ParseError`、命令日志经 restore+replay 后快照**逐字节相同** + 跨 revision 日志与垃圾输入被拒、**Squirrel 端到端往返**（存档→前进→恢复→回放→状态一致 + 算法 id 可读） |
| Phase 1.4–1.6 | ✅ **1.4 交互状态机、1.5 表现意图与回退契约、1.6 tactics 侧视线/掩体策略已完成**（见 §11.1）；**三者都已可从脚本使用**：交互会话作为**脚本自有的值对象**（`battle.newInteraction(unitId)` → `TacticsInteraction`，方法 `state`/`click`/`armAbility`/`cancel`/`endTurn`/`resolve`/`reachableCells`/`expectedRevision`/`armedAction`），投影上下文由 C++ 侧 `Tactics::interactionContext` 构建（phase/activeUnit/controlledCell/reachable/unitsByCell/revision，单一权威）；表现 `presentationIntents`、视线 `visibleCellsInRange`/`lineOfSightAlgorithm`。**会话归属已定为客户端**：会话状态不进快照，不是自己回合时 `newInteraction` 仍成功但 `state() == blocked` | 3 个新用例（脚本点击→意图→提交链、非当前回合得 `blocked`、投影格子可读）+ 交互/表现的 C++ 用例见上 |

---

## 附录 A：关键文件证据索引

### 我方

| 主题 | 位置 |
|---|---|
| 战局/棋盘/单位类型 | `src/modules/tactics/TacticsTypes.h:27-378` |
| 战斗系统与阶段 | `src/modules/tactics/TacticsBattle.h:31-109` |
| 寻路查询 | `src/modules/tactics/TacticsPath.h:53-82`、`TacticsPath.cpp:60-139` |
| 移动成本只看目标格 | `src/modules/tactics/TacticsPath.cpp:89-90` |
| `movePoints` 消耗 | `src/modules/tactics/TacticsBattle.cpp:391` |
| `actionPoints` 只赋值不消耗 | `src/modules/tactics/Tactics.cpp:484`、`TacticsBattle.cpp:272` |
| `acted` 只写不读 | `src/modules/tactics/TacticsBattle.cpp:357,477` |
| Squirrel 脚本面（窄） | `src/modules/tactics/Tactics.cpp:752-998`、`:1017-1021` |
| `addCell` 只设 moveCost | `src/modules/tactics/Tactics.cpp:765-770` |
| `endTurn`/`finish` 未绑定 | `src/modules/tactics/Tactics.h:97,105` |
| 模块门面与 C++ API（宽） | `src/modules/tactics/Tactics.h:46-164` |
| 快照/回放 | `src/modules/tactics/TacticsPersistence.h`、`TacticsReplay.h` |
| 设计文档（含未实现承诺） | `docs/dev/战棋系统设计.md:143,146,179-191,304-305,319-321,450` |
| 用户文档 | `docs/usr/modules/tactics.md`、`docs/usr/modules/hexmap.md`、`docs/usr/modules/grid.md` |
| hexmap 搜索/相位桶队列 | `src/modules/hexmap/HexSearch.h:66-225` |
| hexmap 已知缺陷（无河流） | `test/hexmap.cpp:729-733` |
| hexmap 可见性（启发式） | `src/modules/hexmap/HexVisibility.h`、`HexSearch.h:208-225` |
| hexmap 已接线但全未提交 | `cmake/module_manifest/rendering_simulation.cmake:53`（`M`）；`build/win32-debug/.../EVHexmap.dir/*.obj` 存在；31 个源文件与 `test/hexmap.cpp` 为 untracked |
| 模块脚本名机制 | `cmake/modules.cmake:105-106,451`；`src/scripts/load.nut:93-99` |
| 能力系统（无脚本出口） | `src/modules/action/AbilitySystem.h:29-126` |
| notify 未路由 | `src/modules/action/Action.cpp:507-517` |
| 标准战斗技能（生产无调用方，仅测试） | `src/modules/combat/StandardAbilities.h:19` |
| combat 部分变更缺陷 | `src/modules/combat/CombatAttributes.cpp:100-121` vs `CombatAttributes.h:48-52` |
| attributes 缺陷 | `src/modules/attributes/AttributeSet.cpp:212-216,278-287,348-352`；`Attributes.h:18`；`StateAccessAdapter.h:29,33,39` |
| 伤害 | `src/modules/combat/Damage.h` |
| 目标选择 | `src/modules/sensing/Targeting.h:276-470` |
| LOS capability | `src/modules/sensing/Targeting.h:392-406` |
| 唯一 LOS 实现（World3D） | `src/modules/physics/TargetingLineOfSightAdapter.h:25-28` |
| candidate provider 孤儿 | `docs/dev/2026-09-15-模块边界审查台账.md:61,149` |
| AI（数据驱动但 src/ 内零消费者） | `src/modules/npc_ai/NpcAi.h:74-140`、`npc_ai/editing/BehaviorGraph.cpp:32-105` |
| 决策 | `src/modules/decision/Decision.h` |
| 通用寻路门面 | `src/modules/map/Pathfinder.h:18-68` |
| 导航 provider 抽象（仅测试替身） | `src/modules/npc_ai/Navigation.h:44-73`、`test/npc_ai_navigation.cpp:19` |
| Timer 脚本面 | `src/modules/timer/Timer.cpp:83-86` |
| orders 缺 snapshot 绑定 | `src/modules/orders/CommandQueue.cpp:849,865` |
| 示例（31 KB 脚本成本） | `examples/tactics/main.nut`、`examples/tactics/README.md` |
| 第二个可玩回合示例 | `examples/rpg-classic/main.nut:1021,1188,1195-1203,1257,1261` |
| 唯一 FOV 消费者 | `examples/hex-levels/main.nut:581-600` |
| 测试锚点 | `test/tactics_turn.cpp:72-105`、`test/map_path.cpp:508-520,252-274`、`test/rts_composition.cpp:6124,6160` |

### TBS Framework（路径相对 `.tbs-research/`）

| 主题 | 位置 |
|---|---|
| 命令协议 | `External/tbsf-common/common/units/abilities/ICommand.cs:10-41` |
| 能力契约 | `.../abilities/IAbility.cs:12-124` |
| 交互状态机基类 | `.../controllers/GridState.cs:9-103` |
| 交互状态机实现 | `.../controllers/gridStates/GridStateUnitSelected.cs:50-96` |
| 主控制器 | `.../controllers/GridController.cs:16-213` |
| 回合解析 | `.../controllers/turnResolvers/ITurnResolver.cs:11-62`、`SubsequentTurnResolverImpl.cs:16-43` |
| 表现状态族 | `.../units/IUnitManager.cs:15-155` |
| 移动组件 | `.../units/MoveComponent.cs:102-154` |
| 移动/攻击命令 | `.../abilities/MoveCommand.cs:52-134`、`AttackCommand.cs:48-101` |
| 未实现的命令方法 | `.../abilities/MultipleTargetAttackCommand.cs:63-76`、`EndTurnCommand.cs:30` |
| 寻路算法 | `.../pathfinding/algorithms/{PathfindingAlgorithm,AStarPathfinding,DijkstraPathfinding}.cs` |
| AI 评估器接口 | `.../ai/evaluators/IPositionEvaluator.cs:10-33` |
| AI 评估器实现 | `.../ai/evaluators/DamageDealtPositionEvaluator.cs:45-100` |
| 未播种随机（缺陷） | `.../ai/evaluators/RandomPositionEvaluator.cs:20` |
| 行为树节点 | `.../ai/behaviourTrees/baseNodes/ITreeNode.cs:11-15` |
| 网络契约 | `.../network/INetworkConnection.cs:11-118,229-235` |
| 六边形几何 | `.../cells/HexagonHelper.cs:19-162` |
| 战斗契约 | `.../units/ICombatant.cs:12-143` |
| Unity 层单位 | `Scripts/units/Unit.cs:27-535`（高亮槽位 `:50-59`） |
| 高亮器基类 | `Scripts/highlighters/Highlighter.cs:9-22`（21 类型/24 文件） |
| 行为树"形状即代码" | `Scripts/ai/behaviourTrees/RegularBehaviourTreeResource.cs:65-111` |
| AI 回合（墙上时钟 + 按键） | `Scripts/players/AIPlayer.cs:76-108` |
| 网络实现（反射重建） | `Scripts/network/NetworkConnection.cs:203-277` |
| 编辑器一键接线 | `Editor/GridHelper.cs:521-646` |
| 示例功能说明 | `Examples/Features/{Deploy,InitiativeSystem,MultiCellUnits}/README.md` |
| 旗舰示例 | `Examples/ClashOfHeroes/README.txt`、`Examples/ClashOfHeroes/Scripts/**` |
