# RPG 示例 —— 经典回合制 · 完整 RPG 界面

用 `eve.RPG()` + `eve.Inventory()` + `map` 搭的 RPG Maker 风格纵向切片，演示数据驱动
主模型与完整外围界面：**标题与三存档槽 / 地图探索 / 对象交互 / 遭遇战 / 背包 /
商店 / 属性加点 / 装备安装 / 状态界面**。与之互补的实时动作 demo 见 [`examples/rpg`](../rpg/)。

## 运行

```sh
make run/win32-debug GAME=examples/rpg-classic
# 或对应平台： run/linux-debug / run/macosx-debug
```

## 玩法

**标题与存档槽**
- 启动后从三个独立槽位选择“继续”或“新游戏”
- 每个槽位独立维护原子主档和上一份有效备份，并在标题页区分空、可继续、可恢复备份和损坏状态
- 删除槽位需要先请求、再确认；删除顺序先备份后主档，中断时优先保留当前主档
- 新游戏首次保存前，`F9` 不会意外用槽位里的旧进度覆盖当前冒险
- 旧版 `rpg-classic-save.json` 单槽存档会非破坏性迁移到槽位 1，原文件仍保留

**暂停与设置**
- 探索、整备、对话和战斗中按 `Esc` 暂停，再按一次继续
- 暂停菜单可在安全检查点保存；战斗或对话中会明确拒绝保存
- 返回标题需要二次确认，未首次保存的新游戏会显示额外警告
- 主音量按 10% 步进调整，并独立保存到版本化的 `rpg-classic-settings.txt`，不污染角色存档
- 全队倒下后可选择在村庄安全点醒来：全队恢复 50% 生命/魔力、扣除 10% 金币并自动保存；恢复事务或地图切换失败会回滚失败现场

**探索**
- 方向键或 `W/A/S/D` 在村庄郊外和雾林移动，墙体与障碍不可通行
- `E` 与面前的商人、任务 NPC、战利品容器或史莱姆交互；踏入史莱姆格也会进入战斗
- `E` 使用地图中的 portal 对象在村庄与雾林双向切换；目标地图、格子和朝向来自显式对象属性
- 与村庄长者对话选择接受任务；清理完成后返回长者处选择领取报酬
- 在雾林接受巡守支线、找回东北侧物资箱并返回领奖；NPC 与物资箱使用与主线相同的数据契约
- 三处史莱姆和战利品容器均为一次性世界事件，状态随存档恢复
- `C` 打开角色状态，`Q` 使用治疗药水
- `F5` 在探索/整备安全检查点保存，`F9` 从主档或备份读取

**遭遇战**
- `1` 攻击（`a.attack - b.defense`，可暴击）
- `2` 火球（`a.attack * 2`，火元素，耗 10 MP）
- `3` 治疗
- `Tab` 在存活敌人之间切换目标；雾林终战是双敌人编队
- 胜利后清除地图上的对应遭遇并返回探索；战斗中禁止保存
- `R` 倒下后重开

**商人整备界面**
- **属性加点**：每级获得 3 点，分配攻击 / 防御 / 生命上限 / 魔力上限
- **商店**：用金币购买 治疗药水 / 铁剑 / 皮甲（`data/items.json` 定义物品）
- **装备安装**：把背包里的铁剑 / 皮甲装备到武器 / 护甲槽位，属性加成落到角色

## 数据文件（全部数据驱动，改 JSON 即热重载）

| 文件 | 内容 |
|------|------|
| `data/classes.json` | 职业「战士」：职业基础特征 + 按等级学技能 |
| `data/traits.json` | 特征：力量（攻击 +20% / 暴击 +10%）、火抗（火伤 -50%） |
| `data/skills.json` | 技能定义 |
| `data/items.json` | 物品（背包/商店用）：药水、铁剑、皮甲 |
| `data/quests.json` | 主线「清剿史莱姆」与雾林寻回物资支线，目标及奖励均由定义驱动 |
| `data/encounters.json` | 三处遭遇的有序敌人编队、成员属性/技能、整场经验、金币、任务通知与升级点数 |
| `data/effects.json` | 效果模板 |
| `data/village.json` | 村庄地图、阻挡格、出生点、商人、宝箱、两处遭遇与森林入口 |
| `data/forest.json` | 第二张探索地图、返回村庄入口、森林宝箱与第三处史莱姆遭遇 |
| `data/world-object-contract.json` | 地图对象类型与属性契约；加载失败不会替换当前场景 |
| `data/village-dialogue.dnut` | 长者与巡守各自的领取、提醒、交付和完成后对话，使用稳定节点与选择 ID |
| `data/story-events.json` | 版本化剧情事件序列；雾林首次抵达包含对话、等待和由稳定本地化 key 引用的提示步骤 |
| `data/localization.json` | `eve.i18n.bundle` v1 中英双语包；对话、菜单、HUD、玩家日志、任务、物品、技能、地图、商店与遭遇展示均由稳定 key 投影 |

伤害公式（`"a.attack * 2"`）、暴击、元素、命中在 `main.nut` 用 `registerSkillDamage`
注册，装备属性加成用 `registerItemStatsFromJson` + `syncEquipModifiers`。

## 演示了什么

| 系统 | 用到的点 |
|------|----------|
| 职业 | `setClass` 施加职业特征并学到 1 级技能；升级后 `checkLevelSkills` 补学 |
| 特征 | `paramRate` 落属性、`exParam` 暴击、`elementRate` 火抗 |
| 回合制战斗 | `Battle`：`setActionChecked` 校验行动与目标，支持多敌人行动顺序、命中/暴击/元素和胜败 `isVictory`/`getWinnerSide` |
| 成长 | `gainXp` 升级、`getLevel`/`getXp`/`getXpToNext` |
| 任务 | `replaceQuestsFromJson` 严格验证并原子热替换目录，`newTracker` + `activate`/`notify`/`claim` 驱动进度；HUD 自动投影所有主线和已接支线 |
| 全局状态 | `GameState` 变量记录金币 |
| 背包/商店 | `inventory` 模块：`newBag`/`addItem`/`findItem`/`removeAt`/`canAddItem` |
| 装备 | `newEquipmentSet`/`equipFromBag`/`unequipToBag` + `syncEquipModifiers` |
| 属性加点 | 每级点数，`modifyBaseAttribute` 分配 |
| 战斗胜利事务 | `rpg.settleBattleVictory` 一次提交经验、升级技能、金币、击杀数、任务进度和遭遇消费；重复结算不产生部分奖励 |
| 遭遇目录 | 地图只引用稳定 `encounterId`；`replaceEncountersFromJson` 严格原子发布有序敌方编队与整场奖励，创建和结算共用同一事实源 |
| 商品目录与交易 | `shop.json` 经 `replaceShopOffersFromJson` 严格原子发布；`buyShopOffer` / `sellShopOffer` 按权威价格提交金币与背包，失败不留下半笔交易 |
| 状态界面 | `ui` 多窗口 + `setHostVisible` 切换 |
| 探索地图 | `map.loadFromFileWithObjectContract` + `Pathfinder`，对象契约先准入；`collectWorldLoot` 原子结算通用 loot，portal 属性显式驱动跨地图切换 |
| 对话任务链 | 通用 `quest_npc` 属性绑定 Quest 与对话前缀，按 `Tracker` 状态选择领取、提醒、交付或完成后会话 |
| 奖励事务 | `rpg.claimQuestRewards` 先验证完整物品批次，再原子提交任务、金币与背包；库存监听器只观察最终状态 |
| 队友策略 | `battle-tactics.json` 按序定义游侠策略：任一队友生命低于 40% 时治疗最低生命队友，否则攻击最低生命敌人；目录严格验证并参与内容包回滚 |
| 剧情事件编排 | `story-events.json` 严格发布 UI 中立步骤；`RPGStoryEventSession` 将游标和完成事实写入 `GameState`，目录热替换不影响活动事件，事件中 F5 保存后会重放尚未确认步骤 |
| 本地化内容契约 | `replaceBundleFromJson` 原子校验所有语言的键覆盖、单复数类型和占位符；提交整包前逐语言检查对话节点、剧情提示、玩家日志及产品展示 key，失败恢复上一已接受内容包；标题页和暂停页可即时切换中英文 |
| 世界交互事务 | `rpg.collectWorldLoot` 将一次性世界对象、任务通知、金币和物品作为一个提交单元，失败不留下半开宝箱 |
| 产品启动闭环 | 标题页检查三个存档槽；继续游戏只读取通过完整事务预检的主档或备份 |
| 会话与设置 | `Esc` 暂停、确认返回标题、确认删除槽位；主音量和语言使用独立 v2 偏好文件，兼容读取 v1 音量设置；语言切换验证、写入、UI 重建作为一个可回滚操作 |
| 版本化存档 | `RPGSaveSession` schema v2 聚合 `GameState`、`Tracker`、主角＋游侠全队安全检查点、背包与装备，再通过 `filesystem.writeTextAtomic` / `readText` 落盘；全部权威参与者先统一校验、再原子恢复 |

存档通过 `writeTextAtomic` 原子替换到引擎配置的用户写目录，三个槽位分别使用
`rpg-classic-slot1.json` 至 `rpg-classic-slot3.json`。当前契约为 `rpg-classic.content.v9`：
`v8` 与 `v7` 通过 `allowCompatibleContentVersion` 保持当前队伍 schema，`allowSingleActorPartyMigration` 为
`v6`、`v5` 和 `v4` 明确登记旧主角到队伍第 0 位的迁移，
新游侠采用当前内容基线；`v5` 的旧字符串开关继续迁移到 `RPGWorldState`；
`addQuestAdditionMigration` 从 `v4` 显式补入新的雾林支线，同时完整保留既有任务进度；
旧的无场景存档恢复到村庄，未命名空间化的世界事件会迁移到对应村庄或森林事件；完整性摘要用于发现
意外损坏，不用于防作弊。当前示例采用安全检查点语义：
探索位置、朝向和已消费世界事件由 `GameState` 的权威快照保存，`RPGWorldState` 只提供类型化对象生命周期适配；不保存一场战斗中尚未结算的临时行动队列，
读取后返回探索安全点。背包格位、物品实例、
耐久/自定义属性与装备槽由 inventory 模块自己的版本化 codec 负责，示例不再手工统计物品数量。
保存前只有通过 `validateSnapshotJson` 的旧主档才会轮换到当前槽位的 `.backup.json`；读取时主档损坏、
截断或不兼容会自动尝试备份。游戏自己的波次、金币等业务约束在发布后再次检查，失败时用读取前快照回滚。

任务内容启动时必须通过严格目录验证；热重载中的非法策略、重复 ID、坏目标、缺失前置或依赖环会被拒绝，
上一份已验证目录继续服务当前会话，避免部分新任务污染运行时。对话 `.dnut` 同样先完整编译并执行
`lintAll()`，失败时不切换到新会话定义。启动准入还会遍历村庄和雾林的对象层：每个 `quest_npc`
必须引用真实任务及 `offer/active/turnin/completed` 四种完整会话，每个任务门控 loot 必须引用真实任务，
每个 portal 必须指向已登记地图。组合引用失败时游戏不会进入探索态。

任务、遭遇、商店、队友策略、剧情事件 JSON 与对话 `.dnut` 作为一个叙事内容包发布。热重载先分别执行严格解析和 lint，再执行地图跨引用
准入；只有全部成功才更新已验收源码快照。任何后续步骤失败都会用上一次已验收的任务和对话源码执行
补偿恢复，因此运行时不会停留在“新任务 + 旧对话”或“旧任务 + 新对话”的混合版本。

`rpg.classic.playthroughCompletesBothQuestsAndRestoresCheckpoint` 提供无窗口产品闭环回归：它读取本示例的
真实技能、遭遇、任务、物品、剧情事件和对话内容，先验证雾林事件在对话后保存、恢复并继续等待/提示步骤，
再经过任务接取、三场目录驱动战斗、跨地图世界对象消费和主支线交付，最后用 `RPGSaveSession`
验证任务、金币、背包、角色、地图、事件完成事实和永久世界状态可整体恢复。
