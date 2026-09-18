# Tactics（战棋）

`tactics` 是无渲染依赖的回合制战棋领域模块。它负责战局、阵营、单位占位、回合阶段、
行动资源、确定性寻路、移动提交、反应窗口和胜负目标；生命、伤害、技能、动画与 UI 由其他模块组合。

## Squirrel 快速开始

```nut
local created = eve.Tactics().newBattle(
    "00000000-0000-0000-0000-000000001000", 42);
if (!created.ok) throw created.status.summary;
local battle = created.value;
battle.addCell(0, 0, 0, 100);
battle.addCell(1, 0, 0, 100);
battle.addSide("00000000-0000-0000-0000-000000001001");
battle.addUnit("00000000-0000-0000-0000-000000001002",
               "00000000-0000-0000-0000-000000001001",
               "game:hero", 0, 0, 0, 1, 100, 1, 20);
battle.start("initiative");
battle.advance(1, 1);
battle.advance(2, 1);
battle.advance(3, 1);
local moved = battle.move("00000000-0000-0000-0000-000000001002", 1, 0, 0);
```

`newBattle` 返回统一 Result table，其 `value` 是 script-owned `TacticsBattle` 代理。
代理内部只保存带 module epoch 的 generation handle；`release()` 或模块卸载后，后续写操作返回
`StaleHandle`。脚本不会接触 ECS handle 或跨帧裸指针。

`Tactics.getName()` 返回模块名；`battleCount()`、`sideCount()` 和 `unitCount()` 返回当前存活的
战局、阵营与单位数量。战局代理的 `ownership()` 返回 `"owned"`，`isStale()` 用于在调用其他
战局方法前显式检查代理是否已因 `release()` 或模块 epoch 变化而失效。

可用的战局操作包括
`setTopology/addCell/addEdge/addSide/addUnit/start/advance/move/face/wait/endTurn/useAbility/finish/defeatUnit`，
预检与查询包括
`previewMove/previewFace/previewWait/reachable/cellsInRange/unitResources/edge/hasEdge`，
反应包括 `openReaction/acceptReaction/declineReaction`，
状态查询包括 `status/phase/activeUnit/unitCell/revision/eventCount/eventAt`。所有可能失败的写操作都返回统一
`{ok, code, status, diagnostics, value}` 结构。

`endTurn(actor)` 在不产生 `wait` 行动的前提下结束当前行动者的回合并进入 `turn_end`（事件
`turn.completed`）；`finish()` 结束一个仍在 `running` 的战局并进入 `battle_end`
（事件 `battle.ended`）。已结束的战局再次 `finish()` 会返回失败，不会静默成功。

### 预检、可达范围与行动资源（只读）

**预检与提交共用同一套 validator**，因此 UI 显示的"可点"与提交结果一致：

```squirrel
local preview = battle.previewMove(actorId, 1, 0, 0);   // 与 move 同形：{cost, path, remainingMovePoints}
battle.previewFace(actorId, 2);
battle.previewWait(actorId);

// 可达范围（纯查询：不预留资源、不推进随机流）
local reach = battle.reachable(actorId, 100);
// reach.value.cells = [{ cell = {x,y,layer}, cost }, ...]

// 逻辑距离枚举；metric 取 "manhattan" / "chebyshev" / "hex"
local range = battle.cellsInRange(0, 0, 0, 1, 2, "hex");
// range.value = [{x,y,layer}, ...]

// 每回合资源的读取侧（含此前只写不可读的 acted，以及调度充电 charge）
local r = battle.unitResources(actorId).value;
// r = {actionPoints, movePoints, reactionPoints, roundActionPoints, roundMovePoints,
//      roundReactionPoints, initiative, alive, acted, charge}
```

`reachable` / `cellsInRange` 的返回值是**该次查询所观察到的棋盘版本的投影**，
不代表预留；状态改变后必须重算。`preview*` 成功也不代表占用或资源已被预留。

### 行动经济与技能声明

`useAbility(actor, action, x, y, layer, target, payload)` 只做**声明**：它扣一点行动力、发出
`action.declared` 事件、并把 `use_ability` 命令写入命令日志。**伤害与效果结算不属于 tactics**——
由 RPG 或游戏适配层接收声明后处理，因此脚本无法借这个调用夹带伤害：

```squirrel
// target 传 "" 表示只指定格子；payload 是不透明字符串，tactics 原样保存与回放
local declared = battle.useAbility(actorId, "tactics:strike", 1, 0, 0, targetUnitId, "{\"power\":3}");
// declared.value = {actor, action, targetCell, targetUnit, remainingActionPoints}

// 提交前预检：与 useAbility 共用同一个 validator，拒绝时诊断码完全一致
local preview = battle.previewAbility(actorId, "tactics:strike", 1, 0, 0, targetUnitId, "{\"power\":3}");
// preview 不扣点、不发事件、不记命令；preview.value.remainingActionPoints == 本回合剩下的点数 - 1

// 行动力就是可用次数：耗尽后被拒绝（PreconditionViolation），且不扣不记
local resources = battle.unitResources(actorId).value;   // actionPoints 已减 1
```

`target`（被指定的单位）与 `cell`（目标格）**互相独立**：一个技能可以指向范围中心格 + 主目标单位，
框架不会替你二选一。`payload` 是**调用方自己的效果数据**，tactics 只做两件事：原样存入命令日志
（回放时原样交回）与限制大小（`kMaxAbilityPayloadBytes = 4096`）。它的 schema 与版本属于调用方契约——
在 tactics 里再解释一遍就会变成第二个真值来源。

`action.declared` 事件只带行动者；**技能 id、两个目标与 payload 的唯一权威是它写入命令日志的那条记录**，
消费者用事件里的 `causationCommand`（形如 `"tactics:<sequence>"`）去 `commandsFrom` 里取。这样声明
只有一份，不会被复制成第二种形状。

拒绝条件与诊断码（拒绝**不改变任何状态**）：

| 条件 | DiagnosticCode |
| --- | --- |
| 不在 `running` / `acting`，或没有 active unit | `PreconditionViolation` |
| `action` 不是合法 `LogicalId` | `InvalidArgument` |
| `actor` 不拥有当前回合 | `PreconditionViolation` |
| `actionPoints` 已为 0 | `PreconditionViolation` |
| 目标层与行动者所在层不同，或该格不存在 | `InvalidArgument` |
| `target` 不是本战局单位 | `NotFound` |
| `target` 已被击败 | `PreconditionViolation` |
| `payload` 超过大小上限 | `InvalidArgument` |

`use_ability` 命令因此进入快照 schema **v4**（v6 起还携带 `targetUnit` 与 `payload`）；v1–v3 快照仍可
恢复，但 v3 及更早的快照**不得**携带 `use_ability`（会被判为 `ParseError`），否则旧版本读者会读到
无法解释的命令种类。

MCP / devtools 控制面同样暴露了这个协议：`availableGameplayActions` 在 `acting` 阶段额外给出
`tactics:use-ability`，参数为 `{action, x, y, layer, targetUnit, payload}`（`targetUnit` 与 `payload`
可省略），提交后回执的 `details` 是 `{action, target, remainingActionPoints}`。

### 命令日志（只读）

`commandsFrom(revision)` 返回**该 revision 之后被接受的命令**——这是回放的基质，
脚本可以读、不能追加，因此重放权威始终留在仿真侧：

```squirrel
local all = battle.commandsFrom(0);
// all.value = [{ sequence, kind, actor, cell, facing, policyId, action, triggerSequence }, ...]
// kind 是稳定协议字符串：start / advance / move / face / wait / end_turn / finish /
//   open_reaction / accept_reaction / decline_reaction / defeat_unit / roll_random / use_ability

local newer = battle.commandsFrom(battle.revision().tointeger());
```

`revision` 为负会返回 InvalidArgument；大于当前 revision 时返回空数组（不是错误）。
`kind` 与 `eventAt` 的事件 `type` 都是**稳定协议拼写**，脚本不应依赖枚举的数值。

### 有向边（方向性通行事实）

格描述"从任意方向进入这一格的代价"；**边**只细化**某一个方向**。因此"单向落差""只往一边开的门"
"比正交更贵的对角"都不需要复制格事实：

```squirrel
// spec 省略或传 "" 表示 passable=true / extraCost=0 / tags=[]
local wall = battle.addEdge(0, 0, 0, 1, 0, 0, "{\"passable\":false,\"tags\":[\"door\"]}");
// 反向没有声明 —— 这就是"单向"
local oneWay = battle.hasEdge(0, 0, 0, 1, 0, 0);   // true
local back   = battle.hasEdge(1, 0, 0, 0, 0, 0);   // false

// 非对称代价：出向 +50，回向 +250
battle.addEdge(2, 0, 0, 3, 0, 0, "{\"extraCost\":50}");
battle.addEdge(3, 0, 0, 2, 0, 0, "{\"extraCost\":250}");

local declared = battle.edge(0, 0, 0, 1, 0, 0);
// declared.value = {passable, extraCost, tags}
```

规则：

- 两个端点都必须已存在且**相邻**（按当前 topology），否则失败；重复声明同一方向返回 Conflict。
- 边的 `extraCost` 与目标格的 `moveCost` **相加**，且只在该方向生效；`reachable`、`pathTo`
  与 `move` 提交读的是同一份边事实。
- 只能在该格**方向性**地阻断；要禁止一个格被从任何方向进入，应改用 `addCell(..., passable=false)`。
- 与格一样**只能在 setup 阶段声明**，并同样推进 revision。
- 快照 schema 因此升到 **v2**；v1 快照仍可恢复（按"所有方向均未声明"迁移），
  未知版本仍被拒绝。

### 回合策略（可插拔）

回合顺序不再写死在核心里：战局只存一个**稳定字符串 id**，行为由 `TurnPolicyRegistry` 解析。

```squirrel
local ids = battle.policyIds();   // 已注册 id，按字典序
battle.start("initiative");       // 未注册的 id 会被拒绝，诊断里列出已注册集合
local active = battle.policyId(); // 当前战局实际采用的 id
```
- 内建三种：`"side_alternating"`（按阵营分组）、`"initiative"`（按 initiative 降序）
  与 `"charge_time_battle"`（CTB/ATB，见下）。
- 策略只回答"两个单位谁先动"，**并列时由调用方按 canonical subject 排序决胜**，
  因此任何策略在等键上自动确定，且不需要自己重述这条规则。
- 策略收到的是**值投影**（initiative、sideIndex、charge），不是 ECS handle——它无法解析单位、
  修改状态或依赖调用方没有提供的数据。
- C++ 侧可用 `TurnPolicyRegistry::add` 注册项目策略（`tactics/TurnPolicy.h`）；
  战局数据里只留 id 字符串，因此快照与命令日志不绑定具体实现。
- 该 id 进入快照，schema 因此升到 **v3**；v1/v2 的数值枚举会按冻结的映射迁移
  （`0 → side_alternating`，`1 → initiative`）。

### 充电时间轴（CTB / ATB）

纯排序策略里**每个单位每轮恰好行动一次**，所以"快"只体现在顺序上。要让 initiative
真正决定**行动频率**，策略需要状态：充电。这正是 `"charge_time_battle"` 做的事，而状态由
每个单位的 `TurnResources::charge` 拥有（不是策略对象持有——策略必须保持无状态，否则存档后
排期会变）：

```squirrel
battle.start("charge_time_battle");
local r = battle.unitResources(actorId).value;
// r.charge 是当前累计量；阈值是内建常量 100（kChargeTimeBattleThreshold）
```

规则（`ITurnPolicy::chargeModel` 声明，回合机执行）：

- 每个排期轮开始时，每个存活单位 `charge += initiativeGain × initiative`（内建模型
  `initiativeGain = 1`）。
- `charge ≥ threshold` 的单位才进入本轮的行动队列；队列按 charge 降序、再按 initiative 降序。
- 进入队列的单位在**本轮开始时**一次性扣除 `cost`（内建模型 `cost = threshold`），
  因此一个 2 倍 initiative 的单位行动频率就是 2 倍。
- **本轮无人达标时**：`advance` 报告该轮为 **`NoOp` 且 phase 不变**（仍是 `round_start`）。
  轮数与每轮资源刷新照常发生，调用方据此区分"时间过去了"与"有人行动了"，
  而不用为一个只有某种策略才会出现的状态新造 phase。
- 若某策略声明的充电规则**永远无法让任何单位达标**（例如 `initiativeGain = 0` 而
  `threshold > 0`，或全部单位 initiative 为 0），`start` 直接拒绝
  （`PreconditionViolation`）并让战局留在 `setup`；不会退化成无尽的空轮。

项目策略可以声明自己的模型（`ChargeModel{initiativeGain, cost, threshold}`），
回合机不需要任何改动；`{0, 0, 0}` 是默认值，语义就是"无充电模型：每个存活单位每轮都就绪"，
因此排序型策略的行为与加充电模型之前完全一致。

### 反应窗口

反击、机会攻击与限时打断共用同一个反应栈。窗口的候选由脚本以 JSON 提供，
但**排序与合法性判定仍由 C++ 决定**（`priority` 降序、`initiative` 降序、
`SubjectRef` canonical key 升序）：

```squirrel
local trigger = battle.eventAt(battle.eventCount() - 1);   // 取触发事件
local opened = battle.openReaction(trigger.value.sequence.tointeger(),
    "[{\"reactor\":\"<uuid>\",\"action\":\"tactics:counter\"," +
    "\"priority\":1,\"initiative\":20}]");
// opened.value == 反应栈深度

local accepted = battle.acceptReaction("<uuid>", "tactics:counter");
// accepted.value = { triggerSequence, reactor, action, remainingReactionPoints }

battle.declineReaction();   // 关闭栈顶窗口且不消耗资源
```

`openReaction` 的 `reactor` 与 `action` 为必填字符串；`priority` 与 `initiative`
缺省为 `0`。JSON 里任何一项不合法都会返回带 `candidatesJson[i]` 路径的诊断。
事件里的 `sequence` / `causationCommand` / `correlationCommand` 与 `roll` 的结果一样，
都是**十进制字符串**（64 位值不经浮点通道）。

`roll("namespace:stream")` 只允许在运行中的战局提交路径调用，返回十进制字符串形式的 64 位结果。
每个具名 stream 独立保存 state/rollIndex 并进入快照和命令日志；预览与查询不会推进随机流。

脚本可通过 `addEliminateObjective`、`addSurviveObjective` 和 `addOccupyObjectiveJson` 注册三个
内建 objective；占领目标的 JSON 格式是 `[[x,y,layer], ...]`。

## 当前 C++ API

通过 `eve::tactics::Tactics` 创建和持有战局实体：

```cpp
eve::tactics::Tactics tactics;
auto battle = tactics.newBattle(battleSubject, 42);
auto side = tactics.newSide(battle.value(), sideSubject);
tactics.addCell(battle.value(), {0, 0, 0});
tactics.addCell(battle.value(), {1, 0, 0});
auto unit = tactics.newUnit(battle.value(), side.value(), unitSubject,
                            unitDefinition, {0, 0, 0},
                            {.actionPoints = 1, .movePoints = 100,
                             .reactionPoints = 1, .initiative = 20});
```

开始战局后，每次 `advance` 只跨越一个可观察阶段。调用方必须提供单调递增的
`SimulationStep`，不使用墙上时钟：

```cpp
tactics.start(battle.value(), eve::tactics::kInitiativePolicyId);
tactics.advance(battle.value(), step1); // battle_start -> round_start
tactics.advance(battle.value(), step2); // round_start -> turn_start
tactics.advance(battle.value(), step3); // turn_start -> acting
auto moved = tactics.moveUnit(battle.value(), unitSubject, {1, 0, 0});
auto declared = tactics.useAbility(battle.value(), unitSubject, *eve::LogicalId::parse("tactics:strike"),
                                   {1, 0, 0});
```

移动预检和提交使用同一套 `PathQuery` 规则。共享 `action::ActionRuntime` 可通过
`MoveActionExecutor` 和 `makeMoveRequest` 接入 `tactics:move`。

`PathQuery::cellsInRange` 支持 Manhattan、Chebyshev 和 axial hex 三种逻辑距离。
内建 objective 包括 `EliminateSide`、`SurviveRounds` 和 `OccupyCells`。RPG 或游戏结算不应直接
修改棋盘；确认死亡后调用 `defeatUnit`，由 tactics 原子更新存活状态、占位、objective 和战局状态。

## 棋盘交互状态机（C++ 接口，无渲染依赖）

`tactics/Interaction.h` 解决的是"同一个规则被实现两遍"的问题：UI 通常自己维护选中/高亮状态，
于是"这个单位能不能走到那格"既有高亮版本、又有提交版本。这里的状态机只读一份**不可变投影**，
回答的是**意图**值，从不改战局：

```cpp
// C++：调用方先用自己的权威查询（reachable / cellsInRange / observe）拼出投影
eve::tactics::InteractionContext context;
context.status = BattleStatus::Running;
context.phase = BattlePhase::Acting;
context.activeUnit = controlled;          // 现在轮到谁
context.controlledUnit = controlled;      // 这个会话驱动谁
context.controlledCell = {0, 0, 0};
context.reachableCells = reachableCells;  // 由调用方算好，状态机不重算
context.unitsByCell = { {{2, 0, 0}, enemy} };
context.revision = observed.revision;

auto session = eve::tactics::InteractionSession::create(context);
// InteractionState: blocked / await_selection / unit_selected / targeting / resolving / ended
// InteractionIntentKind: none / select_unit / move_to / use_ability_on / cancel / confirm / end_turn
```

规则：

- **`create` 不会因为"还没轮到它"而失败**：会话照常存在，用会话状态 `blocked` 表达，
  因此 UI 可以跨回合持有一个会话，而不是每回合防御性重建。
- 点击**无法到达的格子**返回 `kind == none`（正常情况，不是错误）；**结构性非法**（战局未在
  `acting`、已结束、会话被阻塞、已有待提交意图）返回带诊断的失败。
- 意图带回 `actor`、`cell`、`action`、`targetUnit` 与**决策时所依据的 `expected` revision**，
  所以调用方能在提交前发现世界已经变化，而不是拿过期观察去提交。
- `armAbility` 允许传入**空的合法目标集**：UI 可以显示"这个技能现在没有合法目标"，
  每次点击都得到 `none`，而不必凭空造一个失败。
- 把格子解析成**格子上的单位**由状态机做（`unitsByCell`），因为"点到了哪一格 → 打谁"
  写错正是这类 bug 的高发区。
- 提交后把会话解析回 `await_selection`；提交进行中再次点击是 `Conflict`，
  不会把互相矛盾的意图排队。

## 表现意图与显式回退契约（C++ 接口，无渲染依赖）

`tactics/Presentation.h` 把战局事件投影成**数据**，而不是去改视图对象（TBSF 的
`MarkAsSelected/MarkAsAttacking/...`）。这样做的关键差别是**每条意图都说明自己何时失效、
以及怎么撤销**——"卡住的高亮"就是因为原框架没有任何东西负责撤销：

```cpp
eve::tactics::PresentationProjector projector;
eve::tactics::PresentationFrame frame;         // revision + 每个单位的"静止状态"
auto commands = projector.project(event, command, frame);
// commands[i] = { intent, revert }
// intent = {sequence, subject, other, state, from, to, path, revision, tick, expiresAtTick}
// state  = idle / friendly / selected / finished / targetable / attacking / defending / moving / destroyed
// revert = {restingState, trigger, sequence}
// trigger = never / on_expiry / on_next_turn / on_round_start
```

规则：

- `never` = **持久状态**（`destroyed`；跨回合的 `finished` 用 `on_round_start`），
  其撤销许可为 false；撤销一次持久状态会被拒绝并返回 `Unsupported`——不允许"撤销一次阵亡"
  把视图改成与战局不符。
- 瞬时状态必须带 `expiresAtTick`（0 只用于持久状态），`on_expiry` 的消费者在超过该 tick 后
  必须撤销或丢弃，因此表现层不会留下过期高亮。
- 几何信息（`from`/`to`/`path`）来自**事件指向的那条已被接受的命令**，不是从事件里猜的：
  事件流只带 `sequence` 与 `type`，命令日志才是位移与目标的权威。调用方拿不到命令时，
  意图照常产生但**不带几何**（而不是编一个目的地）。
- 与表现无关的事件（随机数、objective 结算）投影为**空列表**，这是正常答案。
- 全部是值：投影可以在快照/serve 之后逐字节重算，测试与渲染只是两个消费者。

## 视线与掩体策略（C++ 接口，无渲染依赖）

`tactics/LineOfSight.h` 把"能不能看见""这一格有多少掩体"做成**可注入策略 + 具名 capability**，
而不是写进技能结算里——后者正是参考框架的做法，结果是规则无法单测、无法复用、也无法换棋盘形状。

```cpp
auto sight = eve::tactics::gridLineOfSightPolicy();   // 内建：按棋盘自身拓扑直线追踪
auto cover = eve::tactics::gridCoverPolicy();         // 内建：目标周围/来向的阻挡
auto visible = sight->visible(board, from, to);       // Result<bool>
auto level = cover->cover(board, attacker, target);   // Result<CoverLevel>
```

规则：

- **阻挡读的是格子的 tag**（`kSightBlockerTag = "sight_blocker"`），不是 `passable`：
  "能不能走进去"和"能不能看穿"是两件事（矮墙、烟雾挡视线但不挡路），
  因此视线成为独立事实，且**不需要新的快照版本**。
- 追踪只算**两端点之间**的格：站在烟雾里仍能看出去，目标格上的阻挡保护目标但不隐藏它。
- 棋盘上**不存在的格按阻挡处理**——没铺格子的地方不是免费射界。
- 追踪从**canonical 格序**计算后按查询方向翻转，所以 `visible(a,b)` 与 `visible(b,a)` 由构造保证一致，
  而不是靠两份实现碰巧相同；测试对 5×5 棋盘**全部 625 个有序对**做了对称性扫描（§5.14 的空白）。
- 三种拓扑都支持（Square4/Square8 用 supercover 走法，HexAxial 用 cube 直线插值）。
- 跨层与不存在的格返回结构化拒绝（`Unsupported` / `NotFound`），而不是"看不见"。
- 掩体分三档：`full`（直线被挡，伤害被吸收）、`half`（目标旁边有阻挡但不在来向上）、
  `none`。"看不见"和"看得见但被吸收"是不同玩法，所以视线与掩体是**两个接口**，不是一个合并结果。
- `visibleCellsInRange(board, policy, origin, min, max, metric)` 把既有 `cellsInRange` 与视线组合起来
  ——正好是填 `InteractionContext::targetableCells` 需要的那个查询；它**不重新实现射程或视线**，
  两个规则各自只有一个所有者。
- 换棋盘（例如带自己投影的六边形大地图）时注册自己的 provider：

```cpp
static constexpr const char* capabilityName;   // "eve.tactics.ILineOfSightPolicy" / "..ICoverPolicy"
eve::cap::provide<eve::tactics::ILineOfSightPolicy>(myPolicy.get());
```

**provider 缺失时 `query` 返回空指针**，由调用方显式处理，不会有一个静默的默认实现替它回答。

## 快照与回放的脚本出口

战局可以整份存档、读回并回放——**跨脚本边界的都是 JSON 文本**，因为信封与命令日志都是自描述的
（schema、版本、实例、revision、tick、摘要都在文档里），读的一侧不需要额外的上下文：

```squirrel
local baselineRev = battle.revision();          // 回放日志必须锚定的 revision
local doc = battle.snapshotJson();              // 完整存档（含摘要），value 是 JSON 字符串
local log = battle.commandLogJson(baselineRev); // 该 revision 之后被接受的命令（同样是 JSON）
local algo = battle.snapshotAlgorithm();        // 当前生效的摘要算法 id，可随存档一起记录

// ...继续打...

battle.restoreJson(doc.value);                  // 校验摘要 → 解析 → 一次性提交，失败不改状态
battle.replayJson(log.value);                   // 用 restore 的同一个命令编解码器回放
```

规则：

- **`restoreJson` 先校验摘要在先、再验证完整候选状态、最后一次性提交**：文档损坏、版本未知、
  实例不符、哈希不符、单位缺失都不会留下半恢复的战局。
- **回放日志锚定在它的起点**：`commandLogJson(revision)` 产出的日志必须以该 revision 为
  `expectedRevision` 开始，否则 `replayJson` 直接拒绝——把别处接受的命令应用到当前状态上，
  是比失败更糟的结果。日志里的 `nextSequence` 也随命令一起保存，回放会校验它。
- **回放与恢复共用同一个命令编解码器**（命令的记录形状只有一个所有者），
  因此回放不可能与存档格式漂移；`restore` + `replay` 之后的快照与直接打出来的快照**逐字节相同**。
- `snapshotJson` / `commandLogJson` 的输出是**规范 JSON 文本**，可以直接写文件或过网；
  脚本不需要理解信封内部结构。

摘要算法的选择是**可注入、且不伪装**的：

- 引擎内建一个摘要实现，其 id 就叫 `fnv1a64x2-noncrypto`，并如实报告
  `NonCryptographic`——它用于**完整性/身份**校验（发现损坏或错配），**不是安全边界**。
- 需要密码学保证时，注册 `ISnapshotContentHasher` capability 即可；引擎宿主启动时只在
  **尚无 provider** 的情况下注册内建实现（已有 provider 不会被顶掉）。
- `snapshotAlgorithm` 让调用方**观察**当前用的是哪个算法并把它记在数据旁边，
  因此"用了弱摘要"是可见事实而不是隐含假设。

## 快照约定

`TacticsPersistence` 使用引擎统一的 `SnapshotEnvelope`，schema 为 `tactics:battle`、
当前版本为 **6**。快照保存稳定 `SubjectRef`，不保存 ECS handle。恢复只适用于身份集合相同的
目标战局：实现会先解析并验证完整候选状态，再一次性提交；哈希错误、未知版本、缺失单位或
非法占位均不会修改目标。payload 覆盖棋盘、单位资源/朝向、回合、seed、事件序列、反应栈、
objective 和已接受命令日志。

版本与迁移（每加一个字段就加一行，且门槛一律写成"该版本及其之后"）：

| 版本 | 新增 | 更早版本的迁移 |
| --- | --- | --- |
| v1 | 棋盘 + 单位 + 回合 + 事件/反应/命令/objective/随机 | —— |
| v2 | `board.edges` 有向边集合 | v1 按"所有方向均未声明"迁移 |
| v3 | `policy` 由数值枚举改为稳定 id 字符串 | v1/v2 按冻结映射迁移（`0 → side_alternating`，`1 → initiative`） |
| v4 | `use_ability` 命令种类 | v3 及更早**不得**携带该种类（`ParseError`） |
| v5 | 单位 `charge` 与回合 `schedule`（本轮行动队列） | v1–v4 按"充电为 0、名册即队列"迁移 |
| v6 | 命令记录的 `targetUnit` 与 `payload` | v1–v5 按"无目标单位、payload 为空"迁移 |

`charge` 只在充电型策略下非零；`schedule` 是**本轮的行动队列**（名册的子集，按行动顺序），
不能靠 restore 重算——产生它的充电已经扣掉了，所以它必须随快照保存。命令日志里的
`advance` 命令与 `NoOp` 轮一起进入回放基质，因此"空轮"也是可回放的确定性事实。
`useAbility` 写入的 `targetUnit` 与 `payload` 同样进入命令记录：`replay` 会把它们原样重新声明，
否则读回放日志的效果层会拿到与实时不同的声明。

`BattleReplay::commandsFrom` 提取指定 revision 之后的命令；`replay` 会逐条验证 command sequence、
expected revision 和 resulting revision。相同起始快照与命令序列应产生字节相同的最终 payload/hash。

哈希算法由宿主通过 `SnapshotHashProvider` 注入；模块不会把非加密散列伪装成安全摘要。

## 确定性规则

- 路径成本使用整数，所有相同成本选择以格坐标稳定排序。
- initiative 相同时以 `SubjectRef` canonical UUID 排序。
- 充电累计在 `int` 上**饱和**而非回绕（否则排期会不确定）；充电不会为负。
- 本轮无人就绪时 phase 不变、状态为 `NoOp`，轮数与每轮资源刷新照常推进。
- 反应候选按 priority、initiative、reactor、action 排序，窗口按 LIFO 处理。
- 事件带 command causation/correlation；嵌套反应沿根 command correlation 串联。
- 命令、快照和跨域事件只使用稳定身份，不跨帧保存裸指针。

可运行示例位于 `examples/tactics/`。
