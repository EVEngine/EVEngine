# 跨模块存档用法（0.6）

游戏存档不是单独的引擎「存档模块」，而是各玩法域用 **JSON 文本 + 严格校验 + 原子发布**
拼出来的。本页说明 0.6 推荐的拼法，细节仍以各模块手册为准。

## 原则

1. **每个可变事实只有一个权威所有者**（`GameState`、`Tracker`、`Bag`、战局等），存档只序列化权威状态。
2. **先校验完整候选，再一次发布**；任一 participant 失败则全部保持不变。
3. **内容定义（物品/任务/技能表）不进玩家存档**；读档前必须先加载同一 `contentVersion` 的定义。
4. **磁盘写入用 `filesystem.writeTextAtomic`**；读档可先 `validate` 再 `restore`，主档坏了再试备份。

## 三条竖切里怎么做

| 竖切 | 协调器 / API | 权威参与者 | 示例 |
|------|----------------|------------|------|
| RPG 产品环 | `eve.RPGSaveSession()` | `GameState` + `Tracker` + 主角或 `RPGParty` + `Bag` + `EquipmentSet` | [`examples/rpg-classic`](../../examples/rpg-classic/) |
| 仅背包 | `eve.InventorySaveSession()` | `Bag` + `EquipmentSet` | 见 [inventory](modules/inventory.md) |
| 战棋战局 | `battle.snapshotJson()` / `restoreSnapshotJson` + 可选 `commandLogJson` / `replayJson` | tactics `Battle`（棋盘、回合、命令日志） | [`examples/tactics`](../../examples/tactics/) + [tactics 手册](modules/tactics.md) |

动作战斗竖切（`examples/combat-arena`）当前是**确定性仿真演示**，不以跨会话存档为验收目标；需要持久化时，把 HP/冷却接到 RPG 或自有权威状态后再用上面的会话拼装。

## RPG：推荐主路径

```squirrel
local session = eve.RPGSaveSession();
session.setContentVersion("mygame.content.v1");
// 新游戏用队伍 schema v2：
session.bindParty(gameState, tracker, party, bag, equipment);
// 旧单角色兼容：session.bind(gameState, tracker, actor, bag, equipment);

local check = session.validateSnapshotJson(json); // 不改状态
if (!check.ok) { /* 试备份或拒绝 */ }

local snap = session.snapshotJson();              // 严格 envelope + 摘要
filesystem.writeTextAtomic("slot1.json", snap);

// 读档：先加载同版本物品/任务/技能定义，再：
if (session.restoreSnapshotJson(json) == 0) { /* 失败，状态未改 */ }
rpg.syncEquipModifiers(actor, equipment);         // 装备修饰是可重建投影
```

更多：contentVersion、兼容版本、ID rename / 任务追加迁移、单角色→队伍迁移，见
[RPG 模块手册 · RPGSaveSession](modules/rpg.md)。

## 战棋：快照 + 可选回放日志

```squirrel
local baseline = battle.revision();
local doc = battle.snapshotJson();                 // Result；value 为 JSON 字符串
local log = battle.commandLogJson(baseline);       // 该 revision 之后的命令

// 读回权威战局：
battle.restoreSnapshotJson(doc.value);
// 从快照点续打命令（日志必须锚定 baseline）：
battle.replayJson(log.value);
```

HP / 技能冷却若在脚本 ECS 或 RPG 侧，**不要**指望 tactics 快照自动带上——应另存一份
（例如再挂 `RPGSaveSession`，或自有 JSON），在 `restore` 之后按同一顺序恢复。组合「棋盘 + RPG」
的单事务编排是 [0.7 路线图 Theme A/E](../dev/2026-10-08-release-roadmap-0.6-0.7.md) 的目标，
0.6 要求调用方显式编排。

## 落盘与槽位

```squirrel
filesystem.writeTextAtomic("save-slot1.json", jsonText);
local text = filesystem.readText("save-slot1.json");
```

`rpg-classic` 用三个槽位文件 + `.backup.json` 轮换，并在标题页只展示通过
`validateSnapshotJson` 的槽；可作产品模板。

## 不要做的事

- 把调试用的引擎 `Snapshot` / DevTools 快照当成玩家存档格式。
- 在 `restore` 成功前改写权威容器「试探读档」。
- 存档里塞可重建投影（装备属性修饰、渲染缓存、瞬时事件队列）。
- 假设 WebGPU / 软件 Vulkan 与桌面 HDR 显示器行为一致（见 [graphics 已知边界](modules/graphics.md#06-已知边界)）。

## 相关链接

- [RPG](modules/rpg.md) · [Inventory](modules/inventory.md) · [Tactics](modules/tactics.md) · [Filesystem](modules/filesystem.md)
- [0.6 收口清单](../dev/2026-10-08-0.6-closeout-checklist.md)
