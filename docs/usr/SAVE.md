# 跨模块存档用法

游戏存档不是单独的引擎「存档模块」，而是各玩法域用 **JSON 文本 + 严格校验 + 原子发布**
拼出来的。本页按当前源码说明推荐的拼法，细节仍以各模块手册为准。

## 原则

1. **每个可变事实只有一个权威所有者**（`GameState`、`Tracker`、`Bag`、战局等），存档只序列化权威状态。
2. **先校验完整候选，再一次发布**；任一 participant 失败则全部保持不变。
3. **内容定义（物品/任务/技能表）不进玩家存档**；读档前必须先加载同一 `contentVersion` 的定义。
4. **磁盘写入用 `fs.writeTextAtomic`**；`fs` 是启动脚本注入的 Filesystem 实例。读档可先 `validate` 再 `restore`，主档坏了再试备份。

## 三条竖切里怎么做

| 竖切 | 协调器 / API | 权威参与者 | 示例 |
|------|----------------|------------|------|
| RPG 产品环 | `eve.RPGSaveSession()` | `GameState` + `Tracker` + 主角或 `RPGParty` + `Bag` + `EquipmentSet` | [`examples/rpg-classic`](../../examples/rpg-classic/) |
| 仅背包 | `eve.InventorySaveSession()` | `Bag` + `EquipmentSet` | 见 [inventory](modules/inventory.md) |
| 战棋战局 | `battle.snapshotJson()` / `restoreJson` + 可选 `commandLogJson` / `replayJson` | tactics `Battle`（棋盘、回合、命令日志） | [`examples/tactics`](../../examples/tactics/) + [tactics 手册](modules/tactics.md) |

动作战斗竖切（`examples/combat-arena`）当前是**确定性仿真演示**，不以跨会话存档为验收目标；需要持久化时，把 HP/冷却接到 RPG 或自有权威状态后再用上面的会话拼装。

## RPG：推荐主路径

以下是已有 RPG 关卡的存档接线片段，不是独立 `main.nut`。先按
[rpg-classic](../../examples/rpg-classic/main.nut) 创建并加载 `gameState`、`tracker`、
`party`、`bag`、`equipment`；这些参与者必须比 session 活得久。定义先加载，随后绑定，
最后保存或恢复。绑定不创建角色，也不加载物品定义。

```squirrel
local session = eve.RPGSaveSession();
if (session.setContentVersion("mygame.content.v1") == 0)
    throw "invalid content version";
// 新游戏用队伍 schema v2：
if (session.bindParty(gameState, tracker, party, bag, equipment) == 0)
    throw "save participants are missing";
// 旧单角色兼容：session.bind(gameState, tracker, actor, bag, equipment);

local snap = session.snapshotJson(); // JSON string；失败返回空字符串
if (snap == "" || session.validateSnapshotJson(snap) == 0)
    throw "cannot snapshot this checkpoint";
if (fs.writeTextAtomic("slot1.json", snap) == 0)
    throw "cannot commit slot1.json";

// 读档：先加载同版本物品/任务/技能定义，再：
local json = fs.readText("slot1.json");
if (json == "" || session.validateSnapshotJson(json) == 0)
    throw "missing or invalid save";
if (session.restoreSnapshotJson(json) == 0)
    throw "restore rejected; participants unchanged";
// 单角色项目在恢复后调用 rpg.syncEquipModifiers(actor, equipment)。
// 队伍项目按其装备归属刷新投影，不把单角色 actor 偷换成 party。
```

这里 `bindParty`、`setContentVersion`、`validateSnapshotJson`、`restoreSnapshotJson` 和
`writeTextAtomic` 在脚本边界返回整数 `1/0`，不是 Result 表。不能读取 `.ok`。
`snapshotJson` 返回字符串。C++ 的同名方法使用 Result，不代表脚本返回形状相同。
绑定证据见 [RPG.cpp](../../src/modules/rpg/RPG.cpp) 和
[Filesystem.cpp](../../src/modules/filesystem/Filesystem.cpp)。

更多：contentVersion、兼容版本、ID rename / 任务追加迁移、单角色→队伍迁移，见
[RPG 模块手册 · RPGSaveSession](modules/rpg.md)。

## 战棋：快照 + 可选回放日志

```squirrel
// 已创建并载入定义的 battle；不要在回放期间继续接收玩家命令。
local baseline = battle.revision();
local doc = battle.snapshotJson();                 // Result；value 为 JSON 字符串
if (!doc.ok) throw "battle snapshot failed";
// 在这里正常执行后续命令，之后导出从 baseline 开始的日志。
local log = battle.commandLogJson(baseline);       // 该 revision 之后的命令
if (!log.ok) throw "battle command log failed";

// 读回权威战局：
local restored = battle.restoreJson(doc.value);
if (!restored.ok) throw "battle restore failed";
// 从快照点续打命令（日志必须锚定 baseline）：
local replayed = battle.replayJson(log.value);
if (!replayed.ok) throw "battle replay failed";
```

战棋的恢复方法名是 `restoreJson`，不是 RPG 的 `restoreSnapshotJson`。
快照与日志必须来自同一基线、同一内容定义；日志并不是可重复追加的增量补丁。
快照恢复与日志回放是两个独立调用，整个组合不因此自动成为一个跨域事务。
接口及 Result 投影见 [Tactics.cpp](../../src/modules/tactics/Tactics.cpp)，失败、校验和
回放用例见 [tactics_snapshot_script.cpp](../../test/tactics_snapshot_script.cpp)。

HP / 技能冷却若在脚本 ECS 或 RPG 侧，**不要**指望 tactics 快照自动带上——应另存一份
（例如再挂 `RPGSaveSession`，或自有 JSON），在 `restore` 之后按同一顺序恢复。组合「棋盘 + RPG」
的单事务编排是 [0.7 路线图 Theme A/E](../dev/2026-10-08-release-roadmap-0.6-0.7.md) 的目标，
0.6 要求调用方显式编排。

## 落盘与槽位

```squirrel
// 在已初始化的游戏内，jsonText 是已通过业务校验的完整文档。
if (fs.writeTextAtomic("save-slot1.json", jsonText) == 0)
    throw "save write failed";
local text = fs.readText("save-slot1.json");
if (text == "") throw "save absent or unreadable";
```

按项目的文件系统初始化流程，用 `setIdentity` / `setupWriteDirectory` 设置独立写目录并
检查它们的返回值（见 [game-tutorial](../../examples/game-tutorial/main.nut) 的初始化），先确认 `fs.getSaveDirectory()`；
不要保存到只读的游戏包。原子替换保证的是单文件提交，不保证多个文件一起提交。
Web 运行时的 `writeTextAtomic` 当前明确失败，不能把缓存写入当作同等级的玩家存档保证。

`rpg-classic` 用三个槽位文件 + `.backup.json` 轮换，并在标题页只展示通过
`validateSnapshotJson` 的槽；可作产品模板。

备份流程是：校验旧主档 → 原子写备份并检查成功 → 原子写新主档并检查成功。
坏主档不得覆盖好备份。读取流程是：校验主档 → 校验备份 → 恢复选定候选；两个都坏则
保留当前游戏并向 UI 报告失败。仅通过校验不能保证稍后恢复成功，仍必须检查恢复返回值。

### 改格式与验证

把游戏 schema/version 与 `contentVersion` 分开管理：前者描述保存数据的形状，后者描述
参与恢复的物品、任务等定义版本。ID 改名和新增任务通过 session 的显式迁移入口注册，
不要替换 JSON 文本中的所有同名字符串。不能恢复的未来版本应拒绝，而不是部分读入。

验证至少覆盖：正常冷启动读档、主档截断后使用备份、两个文件都坏、版本不兼容、角色或
物品定义缺失、写盘失败、参与者拒绝恢复时旧状态保持不变。先检查权威状态，随后重建
HUD、装备修饰和渲染投影；“标题页显示有存档”不能证明实际恢复成功。

## 不要做的事

- 把调试用的引擎 `Snapshot` / DevTools 快照当成玩家存档格式。
- 在 `restore` 成功前改写权威容器「试探读档」。
- 存档里塞可重建投影（装备属性修饰、渲染缓存、瞬时事件队列）。
- 假设 WebGPU / 软件 Vulkan 与桌面 HDR 显示器行为一致（见 [graphics 已知边界](modules/graphics.md#06-已知边界)）。

## 相关链接

- [RPG](modules/rpg.md) · [Inventory](modules/inventory.md) · [Tactics](modules/tactics.md) · [Filesystem](modules/filesystem.md)
- [0.6 收口清单](../dev/2026-10-08-0.6-closeout-checklist.md)
