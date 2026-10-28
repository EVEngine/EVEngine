# EVEngine ARPG 框架设计

状态：核心闭环已实现

## 设计来源与取舍

框架吸收了几类经典 ARPG 的共同结构，而不是复制某一款游戏的数值：

- Diablo 类循环：击杀 → 掉落 → 装备 → 强化角色 → 挑战更强敌人。
- Path of Exile 类数据模型：属性名、效果、技能、标签和内容 id 都是数据，不用固定枚举。
- MMORPG/Gameplay Ability System 类能力模型：属性、持续效果、技能消耗/冷却/施法、结算事件相互分离。
- Utility AI：AI 只根据黑板输入给动作评分；感知、寻路和动画仍由 scene/map/animation 模块负责。
- 任务目标观察器：战斗、拾取、对话等系统只发 `(topic, target, amount)`，任务系统不反向依赖它们。

核心原则是“定义与运行时分离、系统间用事件连接、随机过程可设种子、存档只保存运行时”。这样既能做
小型纯 Squirrel 游戏，也能由 C++ 插件替换公式或增加更复杂的规则。

## 模块图

```text
输入 / Scene / Map
        │ 黑板输入
        ▼
   Utility AI ──────► SkillSystem
                          │ cast event
                          ▼
Status/Effect ─tick─► CombatSystem ─kill─► Quest Tracker
       │                  │                    │ rewards
       ▼                  ▼                    ▼
AttributeSystem      ProgressionSystem   LootSystem ─► Inventory
       ▲                                      │
       └──── EquipmentSet attr.* 同步 ◄────────┘

StateCodec: Actor + Progression + Skills + Statuses + Quest Tracker
```

## 已实现的核心

### 角色、属性、效果与技能

`RPGActor` 是 ECS 实体，组件包括 Attributes、Statuses、Skills、Progression。属性修改器支持加法、
百分比、独立乘区、覆盖和钳制；Effect/Skill/Quest/Loot/AI 定义均可从 JSON 注册。

### 标准战斗

`CombatSystem::dealDamage` 提供可直接使用的标准公式：

```text
raw       = power + attack
armored   = raw * armorConstant / (armorConstant + max(0, armor))
resisted  = armored * (1 - clamp(resist.<type>, -1, maxResistance))
critical  = resisted * max(1, critMultiplier)  // 命中 critChance 时
```

默认属性键为 `health/attack/armor/critChance/critMultiplier/resist.<type>`，可通过
`CombatSystem::rules()` 更换。伤害、治疗、暴击和击杀均形成 `CombatEvent`，`RPG::update` 后由脚本轮询。
随机种子可显式设置，便于回放与测试。

### 成长

每个 Actor 独立保存等级、当前经验、最大等级和未分配点数。升级需求为
`ceil(baseXp * growth^(level-1))`，一次经验可连续升级，并产生 LevelUpEvent。

### 任务

任务状态机为：

```text
locked → inactive → active → ready → completed
                   └───────────────→ failed
```

支持前置链、自动/手动开始、自动完成/领奖完成、多个计数目标、失败/放弃/重置、奖励描述和事件快照。
前置环会在注册时拒绝。`notify("kill", "wolf", 1)` 这类通知可同时推进任意数量的活跃任务。

### 战利品与装备

LootTable 支持权重、独立掉率、数量范围、多 roll、同表唯一结果和 luck 修正。结果对象仅包含物品 id 与
数量，因此 RPG 模块不反向依赖 Inventory；脚本可决定放入背包、落到世界或邮寄。

物品定义 `extra` 中的 `attr.<属性名>` 会由 `EquipmentSet::synchronizeAttributes(actor)` 转换为
可精确移除的属性修改器。例如 `{"attr.attack":"12","attr.critChance":"0.05"}`。

### Utility AI

决策集由动作和 considerations 组成。每项把黑板输入归一化后经过 `linear/inverse/quadratic/sqrt/threshold`
曲线，再乘入动作效用。这个边界有意不负责目标搜索、碰撞和移动：这些属于 scene/map/physics。

### 存档

`StateCodec` 输出版本化 JSON。角色快照包含基础属性、成长、已学技能/冷却和持续状态；任务快照包含状态和
目标进度。静态定义由内容包先注册，再恢复运行时快照。目标指针和正在读条的临时对象不会跨存档保存。

## Squirrel 入口

所有能力均挂在 `eve.RPG()`：

- 内容：`registerEffectsFromJson`、`registerSkillsFromJson`、`registerQuestsFromJson`、
  `registerLootTablesFromJson`、`registerAIDecisionSetsFromJson`
- 工厂：`newActor`、`newTracker`、`newUtilityBrain`
- 战斗：`dealDamage`、`heal`、combat/level 事件查询
- 掉落：`rollLoot`
- 存档：`serializeActor/deserializeActor`、`serializeTracker/deserializeTracker`

原有 Attribute/Status/Skill/Settlement API 保持兼容。

## 游戏帧编排建议

1. 更新感知数据并让 UtilityBrain 选择动作。
2. 玩家或 AI 调用 `beginCastSkill`。
3. 调用 `rpg.update(dt)`。
4. 将 cast/tick 事件转成 `dealDamage/heal`。
5. 再次 `rpg.update(0)` 拉取 combat/level 事件（或下一帧统一处理）。
6. 击杀时发任务通知、获得经验并 roll 掉落。
7. 装备变化后调用 `synchronizeAttributes`。
8. 在安全点序列化 Actor 与 Tracker。

## 扩展边界

- 更复杂的伤害乘区、格挡、穿透：注册 SettlementPipeline 阶段，或替换 CombatSystem 调用。
- 行为树/GOAP：可以与 UtilityBrain 并存，最终仍输出技能或移动意图。
- 随机词缀与锻造：在 ItemStack.props 中保存实例词缀，再以 modifier source 同步。
- 联机权威：服务器设置随机种子并发送 CombatEvent/LootRoll 结果，客户端只表现。
