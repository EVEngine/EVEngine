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

// 每回合资源的读取侧（含此前只写不可读的 acted）
local r = battle.unitResources(actorId).value;
// r = {actionPoints, movePoints, reactionPoints, roundActionPoints, roundMovePoints,
//      roundReactionPoints, initiative, alive, acted}
```

`reachable` / `cellsInRange` 的返回值是**该次查询所观察到的棋盘版本的投影**，
不代表预留；状态改变后必须重算。`preview*` 成功也不代表占用或资源已被预留。

### 行动经济与技能声明

`useAbility(actor, action, x, y, layer)` 只做**声明**：它扣一点行动力、发出 `action.declared`
事件、并把 `use_ability` 命令写入命令日志。**伤害与效果结算不属于 tactics**——由 RPG 或游戏
适配层接收事件后处理，因此脚本无法借这个调用夹带伤害：

```squirrel
local declared = battle.useAbility(actorId, "tactics:strike", 1, 0, 0);
// declared.value = null；失败时 {ok=false, code, diagnostics}

// 行动力就是可用次数：耗尽后被拒绝（PreconditionViolation），且不扣不记
local resources = battle.unitResources(actorId).value;   // actionPoints 已减 1
```

拒绝条件与诊断码（拒绝**不改变任何状态**）：

| 条件 | DiagnosticCode |
| --- | --- |
| 不在 `running` / `acting`，或没有 active unit | `PreconditionViolation` |
| `action` 不是合法 `LogicalId` | `InvalidArgument` |
| `actor` 不拥有当前回合 | `PreconditionViolation` |
| `actionPoints` 已为 0 | `PreconditionViolation` |
| 目标层与行动者所在层不同，或该格不存在 | `InvalidArgument` |

`use_ability` 命令因此进入快照 schema **v4**；v1–v3 快照仍可恢复，但 v3 及更早的快照
**不得**携带 `use_ability`（会被判为 `ParseError`），否则旧版本读者会读到无法解释的命令种类。

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

- 内建两种：`"side_alternating"`（按阵营分组）与 `"initiative"`（按 initiative 降序）。
- 策略只回答"两个单位谁先动"，**并列时由调用方按 canonical subject 排序决胜**，
  因此任何策略在等键上自动确定，且不需要自己重述这条规则。
- 策略收到的是**值投影**（initiative、sideIndex），不是 ECS handle——它无法解析单位、
  修改状态或依赖调用方没有提供的数据。
- C++ 侧可用 `TurnPolicyRegistry::add` 注册项目策略（`tactics/TurnPolicy.h`）；
  战局数据里只留 id 字符串，因此快照与命令日志不绑定具体实现。
- 该 id 进入快照，schema 因此升到 **v3**；v1/v2 的数值枚举会按冻结的映射迁移
  （`0 → side_alternating`，`1 → initiative`）。

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

## 快照约定

`TacticsPersistence` 使用引擎统一的 `SnapshotEnvelope`，schema 为 `tactics:battle`、
当前版本为 4。快照保存稳定 `SubjectRef`，不保存 ECS handle。恢复只适用于身份集合相同的
目标战局：实现会先解析并验证完整候选状态，再一次性提交；哈希错误、未知版本、缺失单位或
非法占位均不会修改目标。payload 覆盖棋盘、单位资源/朝向、回合、seed、事件序列、反应栈、
objective 和已接受命令日志。

`BattleReplay::commandsFrom` 提取指定 revision 之后的命令；`replay` 会逐条验证 command sequence、
expected revision 和 resulting revision。相同起始快照与命令序列应产生字节相同的最终 payload/hash。

哈希算法由宿主通过 `SnapshotHashProvider` 注入；模块不会把非加密散列伪装成安全摘要。

## 确定性规则

- 路径成本使用整数，所有相同成本选择以格坐标稳定排序。
- initiative 相同时以 `SubjectRef` canonical UUID 排序。
- 反应候选按 priority、initiative、reactor、action 排序，窗口按 LIFO 处理。
- 事件带 command causation/correlation；嵌套反应沿根命令 correlation 串联。
- 命令、快照和跨域事件只使用稳定身份，不跨帧保存裸指针。

可运行示例位于 `examples/tactics/`。
