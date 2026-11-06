# 动作 RPG 宿主接线

**这篇解决什么：** 用现有 `action` / `combat` / `settlement` / `rpg` / `inventory`
搭单机动作 RPG 时，宿主必须遵守的身份、时序、失败语义与存档边界。  
**首选入口：** `eve.RPGActionEncounter()`（双参与者闭环）或原生
`eve::rpg::ActionEncounter` / `RPGActionDamageSink`。  
**不要：** 新建 CombatActor / ARPG 总管模块；不要用第二次 `rpg.update(0)`「刷新一切」；
不要双写 HP（CombatState 与 Vitals 并行提交）。

可玩参考：[`examples/arpg-loop`](../../../examples/arpg-loop/)。  
交付阶段见 [`docs/dev/superpowers/plans/2026-11-06-动作RPG交付计划.md`](../../dev/superpowers/plans/2026-11-06-动作RPG交付计划.md)。

## Host Checklist

1. 每个生命有不可复用的 `SubjectRef`（UUID）；ECS Handle 带 generation。
2. 每目标至多一个 accepting `IActionDamageSink`，且属于当前遭遇。
3. 固定仿真步（如 60 Hz）拥有判定；渲染帧率不参与伤害。
4. 每步顺序固定：意图 → ability/timeline → hit windows → settlement → 奖励/事件 poll → 表现。
5. **每个 runtime 每个 tick 只 `advance` / `step` 一次**（`AbilityRuntime`、`CombatLoopRuntime`）。
6. 死亡 receipt 认领失败可重试；成功后身份消费不可逆。
7. 掉落缓存与击杀奖励分事务；满包不吞缓存。
8. 换装后 `syncEquipModifiers`；生命上限下降有明确夹取规则。
9. 存档前：无敌人、无 `hasPendingReward`、玩家存活；读档后重建映射并清理瞬时战斗态。
   写档顺序：`snapshotJson` → `validateSnapshotJson` → `writeTextAtomic`；读档先 validate 再 restore。
   `contentVersion` 固定为 `arpg-loop-v1`（未来版本默认拒绝，见 [SAVE](../SAVE.md)）。
10. 观察面只读；Agent 验证不走 `eve_eval` 改权威状态。

## `eve.RPGActionEncounter` 用法

```squirrel
local duel = eve.RPGActionEncounter();
local bound = duel.bind(hero, enemy, heroLifeUuid, enemyLifeUuid);
if (!bound.ok) throw bound.status.summary;

local hit = duel.hit(true, 25.0, tick);   // playerAttacks, amount, injected tick
if (!hit.ok) { /* 见失败矩阵；HP 未变 */ }
else {
    local applied = hit.value;
    if (duel.hasPendingReward()) {
        local claimed = duel.claim(gameState, tracker, "encounter.wolf");
        if (!claimed.ok) { /* 保留 pending，稍后重试 */ }
    }
}

if (restartOrLoad) duel.close();   // 放弃瞬时遭遇；读档后重新 bind
```

| 方法 | 说明 |
| --- | --- |
| `bind` | 绑定两个 `RPGActor` 与各自生命 UUID；有 pending 时 Conflict |
| `hit` | 经 action `combat:damage` → RPG Vitals；返回实际伤害 |
| `claim` | 把 pending 击杀交给 `ActionKillReward`；失败保留 receipt |
| `close` | 立即撤销 provider；丢弃 pending（重启/读档） |
| `hasPendingReward` | 有待发奖时禁止安全点存档 |
| `isClosed` / `isBound` / `lastTick` | 宿主观测；不改变权威状态 |

权威：脚本可拥有位置与攻击窗口；**HP 只在 RPG Vitals**；奖励去重在 `GameState`；物品在 `inventory`。

### 消耗与表现边界（竖切）

- **`examples/arpg-loop`**：攻击/重击/闪避的冷却与准入由脚本固定步拥有，**不**走 Ability
  `CostSpec` 资源扣费。需要正式能量/法力事务时，改用 `action` Ability + resource provider
  （见 `examples/combat-arena` 的说明：示例可自管 energy，取消不自动退费）。
- **表现**：伤害数字、hitstop、闪光只读 settlement / encounter 结果；失败不回滚已提交 HP。
  参考 `examples/impact-arena` 的 DamageNumbers 接线，不要在 HUD 再存一份血量。

## 失败语义矩阵

宿主应对 `Result` 的 `status`，失败时权威状态不变。

| 操作 | 条件 | 典型 `DiagnosticCode` | 状态影响 |
| --- | --- | --- | --- |
| `bind` | 非法/相同 UUID、相同 actor | `InvalidArgument` | 不变 |
| `bind` | 仍有 pending 奖励 | `Conflict` | 不变 |
| `hit` | 遭遇已 `close` | `PreconditionViolation` | 不变 |
| `hit` | tick 倒退、非正伤害、非有限 | `InvalidArgument` | 不变 |
| `hit` | 有 pending 未 claim | `Conflict` | 不变 |
| `hit` | 参与者已销毁 / generation 失效 | `StaleHandle` | 不变 |
| `hit` | 任一方 HP≤0 | `PreconditionViolation` | 不变 |
| `hit` | 目标有 ≠1 个 owning sink | `Conflict` | 不变（含重叠遭遇） |
| `claim` | 无 pending | `NoOp` 成功 | 不变 |
| `claim` | 模板/任务前置失败 | 结构化失败 | **保留** pending |
| `claim` | 重复生命身份 | `NoOp` 成功 | 去重，清 pending |
| Ability `step` / CombatLoop `advance` | 同一 tick 再次推进 | `Conflict` | 冷却/位移/命中不二次结算 |

多遭遇可同时存在，只要**目标集合不相交**。误绑同一目标时，任一方 `hit` 在提交前失败。

## 命中去重范围

| 范围 | 规则 |
| --- | --- |
| `RPGActionEncounter::hit` | 同一 tick **允许多次**命中（多段技）；tick 不得倒退 |
| Action notify `combat:damage` | 不按 `executionId` 去重；同 execution 多段合法 |
| 单次挥击扫掠 | 由宿主 / `MeleeHitRuntime` 在 hitbox 侧决定是否对同一目标重复结算 |
| 持续区域 | 宿主自管 period；每次 admitted hit 仍走唯一 sink |

不要用「禁止同 tick 第二次 `hit`」代替多段去重；那会误伤合法多段技。

## 与其它模块的分工

| 模块 | 宿主用法 |
| --- | --- |
| [action](action.md) | Ability / timeline；每 tick 一次 `advanceAbilities` |
| [combat](combat.md) | 可选 `CombatLoopRuntime`；每 tick 一次 `advance` |
| [settlement](settlement.md) | 伤害提交；表现只读 `applied` |
| [rpg](rpg.md) | Vitals、任务、Encounter、存档 |
| [inventory](inventory.md) | 掉落与装备 |
| [SAVE](../SAVE.md) | `RPGSaveSession` 安全点 |

## 验证

- 原生：`test/integration/rpg-action/`（扣血、击杀奖励、遭遇冲突、失败矩阵、满包未领缓存存档、死亡宿主门闩）
- 存档失败注入：`rpg.saveSession.roundTripAndCrossParticipantRollback`（损坏档）、
  `rpg.saveSession.arpgLoopContentVersionRejectsFutureAndMigratesCompatible`（版本过新 / 兼容迁移）
- 双推进：`action.ability.rejectsDuplicateStepOnSameTick`、`combatLoop.rejectsDuplicateAdvanceOnSameTick`
- 示例：`MIN_RUN_SECONDS=2 bash scripts/smoke_examples.sh arpg-loop`
- 一键 Agent：`make check/arpg-loop`（或 `bash scripts/verify_arpg_loop.sh`；无显示时用 `xvfb-run -a`）
- 手动 Agent（先起 MCP 示例）：`python3 examples/arpg-loop/agent_check.py <port> <evidence-dir>`（禁止 `eve_eval` 改 HP）
- 竖切覆盖：camp/arena 切换、scout 支线 claim、商店失败/成功、三杀成长、营地存档去重

发版 checklist 建议勾选：上述原生用例绿 + `arpg-loop` smoke PASS + `make check/arpg-loop`。
