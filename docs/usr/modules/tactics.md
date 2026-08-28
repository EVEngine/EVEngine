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

可用的战局操作包括 `setTopology/addCell/addSide/addUnit/start/advance/move/face/wait/defeatUnit`，查询包括
`status/phase/activeUnit/unitCell/revision/eventCount/eventAt`。所有可能失败的写操作都返回统一
`{ok, code, status, diagnostics, value}` 结构。

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
tactics.start(battle.value(), eve::tactics::TurnPolicyKind::Initiative);
tactics.advance(battle.value(), step1); // battle_start -> round_start
tactics.advance(battle.value(), step2); // round_start -> turn_start
tactics.advance(battle.value(), step3); // turn_start -> acting
auto moved = tactics.moveUnit(battle.value(), unitSubject, {1, 0, 0});
```

移动预检和提交使用同一套 `PathQuery` 规则。共享 `action::ActionRuntime` 可通过
`MoveActionExecutor` 和 `makeMoveRequest` 接入 `tactics:move`。

`PathQuery::cellsInRange` 支持 Manhattan、Chebyshev 和 axial hex 三种逻辑距离。
内建 objective 包括 `EliminateSide`、`SurviveRounds` 和 `OccupyCells`。RPG 或游戏结算不应直接
修改棋盘；确认死亡后调用 `defeatUnit`，由 tactics 原子更新存活状态、占位、objective 和战局状态。

## 快照约定

`TacticsPersistence` 使用引擎统一的 `SnapshotEnvelope`，schema 为 `tactics:battle`、
当前版本为 1。快照保存稳定 `SubjectRef`，不保存 ECS handle。恢复只适用于身份集合相同的
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
