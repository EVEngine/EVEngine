# RPG 系统模块

**脚本入口：** `eve.RPG()`

组合属性、效果、状态（Buff）、技能、施法与伤害结算，并提供**目标追踪器（任务/成就/教程）**、
**等级/经验成长**、**当前生命等资源（受伤/治疗/死亡）**、**装备加成与掉落**、**特征（Traits）**、
**回合制战斗**、**职业与升级学技能**、以及**全局开关/变量（GameState）**。

ECS 组件 `RPGActor::Statuses` 保存运行时 buff/debuff 实例；可用 `applyBuff` /
`hasBuff` / `removeBuff*` 等别名，与 `applyEffect` / `hasEffect` / `removeStatus*` 等价。

## 基本用法

```squirrel
local rpg = eve.RPG();
local actor = rpg.newActor();
actor.setBaseAttribute("hp", 100);
actor.setBaseAttribute("attack", 12);
print(actor.getFinalAttribute("hp") + "\n");

// 注册效果后施加 buff（JSON 可带 extra 自定义字段）
rpg.registerEffectsFromJson(@"[
  {""id"":""buff.power"",""durationPolicy"":""duration"",""duration"":5.0,
   ""stackPolicy"":""refresh"",
   ""modifiers"":[{""attribute"":""attack"",""op"":""add"",""value"":5.0}],
   ""tags"":[""buff""],""extra"":{""icon"":""ui/power.png""}}
]");
local id = actor.applyBuff("buff.power", "potion");
actor.setBuffProp(id, "uiTint", "#ffaa00");
```

## 对象关系与调用时机

`RPG` 保存 effect/skill 定义和结算 pipeline；`RPGActor` 保存属性、状态、已学技能、冷却、施法状态、
**等级/经验**与**当前资源**。JSON 是定义，Actor 是运行时实例，事件队列负责把结算连接到表现。

每帧 `rpg.update(dt)` 后可轮询：

- tick 事件（DOT/HOT）：`getTickEvent*`
- 状态变更（apply/refresh/stack/remove/expire/reject）：`getStatusChangeEvent*`
- 施法结算：`getCastEvent*`
- 升级：`getLevelUpEvent*`
- 当前资源（受伤/治疗/死亡）：`getVitalsEvent*`

C++ 扩展点：`StatusSystem::registerApplyCondition` / `registerStackPolicy` /
`registerLifecycleHook`（免疫、自定义叠层、UI 钩子）。

## 目标导向指南

### 建立角色属性

`newActor()` 后用 `setBaseAttribute` 建立 hp、attack、defense 等基础值；永久或临时加成通过 modifier 添加，最终战斗数值读取 `getFinalAttribute()`。

### 注册并释放技能

从 JSON 注册 effect 和 skill 定义，让 Actor 学习技能；释放前用 `canCastSkill()` 和原因接口检查资源、冷却与条件。每帧 `rpg.update(dt)`，随后消费 cast/tick 事件并执行表现或伤害。

### 追踪任务与成就

```squirrel
local rpg = eve.RPG();
rpg.registerQuestsFromJson(questJson);
local log = rpg.newTracker();     // 玩家任务
local ach = rpg.newTracker();     // 成就（定义里 startPolicy=auto）
log.activate("quest.hunt");
log.notify("kill", "slime", 1);
ach.notify("kill", "slime", 1);   // 两份 Tracker 互不影响
log.pollEvents();                 // 处理完 notify 后必须 poll 才能读 getEvent*
```

### 成长与战斗落账

```squirrel
actor.gainXp(120.0);                       // 升到下一级时产生 getLevelUpEvent*
actor.setBaseAttribute("hp", 100);
actor.setCurrent("hp", 100);
actor.takeDamage("hp", 25, "slime");       // current 夹到 [0, max=hp 最终属性]，归零发 death
if (actor.isDead("hp")) actor.revive("hp");// 复活回满
// 伤害数值可先走结算流水线：SettlementContext.set("amount", dmg) 后调用 takeDamage 落账
```

### 装备加成与掉落

```squirrel
rpg.registerItemStatsFromJson("sword", @"[
  {""attribute"":""attack"",""op"":""add"",""value"":15}
]");
rpg.syncEquipModifiers(actor, equipmentSet); // 按槽位 source="equip:<slot>" 施加/撤销属性加成
rpg.registerLootTablesFromJson(@"[
  {""id"":""goblin"",""entries"":[{""itemId"":""gold"",""chance"":1.0,""minQty"":5,""maxQty"":5}]}
]");
local bag = inv.newBag(20);                 // 或任何 inventory::Bag
local drops = rpg.rollLoot("goblin", bag, 42);
```

### 特征 / 职业 / 回合制战斗 / 全局状态

```squirrel
// 特征：参数倍率落到属性，元素/状态耐性、暴击等供战斗查询
rpg.registerTraitsFromJson(@"[
  {""id"":""mighty"",""traits"":[{""kind"":""paramRate"",""target"":""attack"",""value"":1.5}]},
  {""id"":""fire_resist"",""traits"":[{""kind"":""elementRate"",""target"":""fire"",""value"":0.5}]}
]");
actor.applyTrait("mighty", "class");
actor.getElementRate("fire");             // 1 无修正
actor.getExParam("critRate");

// 职业 + 升级学技能
rpg.registerClassesFromJson(@"[
  {""id"":""warrior"",""traits"":[""mighty""],
   ""learnSkills"":[{""skillId"":""power_strike"",""level"":2}]}
]");
actor.setClass("warrior");
actor.gainXp(100); actor.checkLevelSkills();   // 升级后补学已解锁技能

// 回合制战斗：伤害公式 DSL "a.atk * 4 - b.def * 2"；阵营任意、伤害资源任意
rpg.registerSkillDamage("fireball", "hp", "a.attack * 2", "fire", 0.0, 100);
rpg.registerSkillDamage("mana_drain", "mana", "a.attack", "", 0.0, 100); // 打到 mana
local battle = rpg.newBattle();
battle.addActor(hero, 0); battle.addActor(slime, 1);   // 阵营是任意整数 id
battle.setPlayerSide(0);                                // 玩家侧（默认 0）
battle.setAction(hero, "fireball", slime);
battle.autoEnemyActions();
battle.startRound();
while (battle.executeNextAction() && !battle.isFinished()) {}
if (battle.isVictory()) { /* 胜利 */ }
battle.getWinnerSide();   // 多派系时判定获胜阵营

// 全局开关/变量（RPG Maker 的 $gameSwitches / $gameVariables）
local gs = rpg.newGameState();
gs.switchOn("door");
gs.setVariable("gold", 100);
```

## 常见问题

- 只注册 Skill 未注册其引用的 Effect。
- 忘记每帧 `rpg.update(dt)`，导致冷却、DOT 和施法不推进。
- 直接修改最终属性：基础值和 modifier 应分层管理。
- 追踪器读完事件前先 `pollEvents()`；两个 Tracker 要各自 `notify`；`completePolicy=claim` 时满进度是 `ready`，要 `claim`。
- 掉落物品需先在 `inventory` 模块注册物品定义，否则 `bag.addItem` 拒绝加入。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `addAttributeModifier()`、`addTag()`、`applyEffect()`、`beginCastSkill()`、`canCastSkill()`、`canCastSkillReason()`、`cancelCastSkill()`、`clearEffectDefinitions()`
- `clearAllItemStats()`、`clearClassDefinitions()`、`clearItemStats()`、`clearLootTables()`、`clearQuestDefinitions()`、`clearSettlementPipeline()`、`clearSkillDamage()`、`clearSkillDefinitions()`、`forgetSkill()`、`gainXp()`
- `get()`、`getBaseAttribute()`、`getCastEventCaster()`、`getCastEventCount()`、`getCastEventSkillId()`、`getCastEventTarget()`、`getCastProgress()`、`getCastingSkillId()`
- `getCurrent()`、`getEffectDefinitionCount()`、`getFinalAttribute()`、`getItemStatCount()`、`getLevel()`、`getLevelUpEventActor()`、`getLevelUpEventCount()`、`getLevelUpEventNewLevel()`
- `getLevelUpEventPreviousLevel()`、`getLootTableCount()`、`getMax()`、`getName()`、`getQuestDefinitionCount()`、`getSettlementStageCount()`、`getSkillCooldown()`、`getSkillDefinitionCount()`
- `getStatusCount()`、`getStatusEffectId()`、`getStatusInstanceId()`、`getStatusRemaining()`、`getStatusStacks()`、`getTickEventActor()`、`getTickEventCount()`、`getTickEventEffectId()`
- `getTickEventInstanceId()`、`getTickEventSource()`、`getTickEventStacks()`、`getVitalsEventAction()`、`getVitalsEventActor()`、`getVitalsEventAmount()`、`getVitalsEventCount()`、`getVitalsEventCurrent()`
- `getVitalsEventMax()`、`getVitalsEventResource()`、`getVitalsEventSource()`、`getXp()`、`getXpToNext()`、`has()`、`hasAttribute()`、`hasEffect()`
- `hasSettlementStage()`、`hasTag()`、`heal()`、`isCastingSkill()`、`isDead()`、`knowsSkill()`、`learnSkill()`、`modifyBaseAttribute()`
- `newActor()`、`newBattle()`、`newGameState()`、`newSettlementContext()`、`newTracker()`、`registerClassesFromJson()`、`registerEffectsFromJson()`、`globalGameState()`
- `removeAllAttributeModifiersBySource()`、`removeAttributeModifier()`、`removeAttributeModifiersBySource()`、`removeSettlementStage()`、`removeStatus()`、`removeStatusByEffect()`、`removeStatusBySource()`、`removeStatusByTag()`
- `revive()`、`rollLoot()`、`runSettlement()`、`set()`、`setBaseAttribute()`、`setCurrent()`、`setSettlementStageEnabled()`、`setSettlementStagePriority()`
- `setSkillCooldown()`、`setXpToNext()`、`syncEquipModifiers()`、`takeDamage()`、`registerItemStatsFromJson()`、`registerLootTablesFromJson()`、`registerQuestsFromJson()`、`registerSkillDamage()`、`registerSkillsFromJson()`、`registerTraitsFromJson()`、`clearTraitDefinitions()`、`getTraitDefinitionCount()`、`getClassDefinitionCount()`、`update()`

**Tracker 方法：** `activate()`、`abandon()`、`canActivate()`、`canActivateReason()`、`claim()`、`fail()`、`getCount()`、`getEventAction()`、`getEventAmount()`、`getEventCount()`、`getEventEntryId()`、`getEventObjectiveId()`、`getEventReason()`、`getEventTarget()`、`getEventTopic()`、`getExtra()`、`getId()`、`getObjectiveCount()`、`getObjectiveCountRequired()`、`getObjectiveCurrent()`、`getObjectiveId()`、`getRewardAmount()`、`getRewardCount()`、`getRewardId()`、`getRewardType()`、`getState()`、`hasTag()`、`isObjectiveDone()`、`notify()`、`pollEvents()`、`reset()`、`syncAuto()`

**特征（RPGActor）：** `applyTrait()`、`removeTrait()`、`removeTraitsBySource()`、`removeTraitsByTrait()`、`hasTrait()`、`getTraitCount()`、`getTraitIdAt()`、`getTraitInstanceIdAt()`、`getTraitSourceAt()`、`getParamRate()`、`getElementRate()`、`getStateRate()`、`isStateResist()`、`getExParam()`、`getAttackSpeed()`、`getAttackTimesAdd()`

**职业（RPGActor）：** `setClass()`、`getClassId()`、`hasClass()`、`checkLevelSkills()`、`getClassLearnCount()`、`getClassLearnSkillIdAt()`、`getClassLearnLevelAt()`

**Battle：** `addActor()`、`autoEnemyActions()`、`executeNextAction()`、`getActor()`、`getActorCount()`、`getEventAction()`、`getEventAmount()`、`getEventCaster()`、`getEventCount()`、`getEventCrit()`、`getEventSkillId()`、`getEventTarget()`、`getPlayerSide()`、`getSide()`、`getTurn()`、`getWinnerSide()`、`isActorAlive()`、`isDefeat()`、`isFinished()`、`isVictory()`、`pollEvents()`、`setAction()`、`setPlayerSide()`、`startRound()`

**GameState：** `addVariable()`、`clear()`、`getSelfVariable()`、`getVariable()`、`hasSelfVariable()`、`isSwitchOn()`、`setSelfVariable()`、`setSwitch()`、`setVariable()`、`switchOff()`、`switchOn()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/rpg/`](../../../src/modules/rpg/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `rpg`。
