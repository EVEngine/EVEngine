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
local loaded = rpg.replaceQuestsFromJson(questJson);
if (!loaded.ok) throw loaded.status.summary;
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

// 公式注册时严格校验；成功返回 1，失败返回 0 且保留同 id 的旧定义

// 全局开关/变量（RPG Maker 的 $gameSwitches / $gameVariables）
local gs = rpg.newGameState();
gs.switchOn("door");
gs.setVariable("gold", 100);

// 稳定的跨地图对象生命周期；权威数据仍写入 gs，因此自动进入 RPGSaveSession
local world = rpg.newWorldState(gs);
local consumed = world.consumeObject("village", "old_chest");
if (!consumed.ok) print(consumed.status.summary + "\n");
if (world.isObjectConsumed("village", "old_chest")) { /* 不再生成宝箱 */ }
```

伤害公式支持数字、`a.<属性>`、`b.<属性>`、括号、`+ - * /` 和一元正负号。
缺失括号、尾随文本、非法 token 与非有限数值会拒绝注册；C++
`evaluateFormulaChecked()` 在运行时除零返回结构化失败，兼容求值入口投影为 `0`。

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
- `getLevelUpEventPreviousLevel()`、`getLootTableCount()`、`getMax()`、`getName()`、`getQuestDefinitionCount()`、`hasQuestDefinition()`、`getSettlementStageCount()`、`getSkillCooldown()`、`getSkillDefinitionCount()`
- `getStatusCount()`、`getStatusEffectId()`、`getStatusInstanceId()`、`getStatusRemaining()`、`getStatusStacks()`、`getTickEventActor()`、`getTickEventCount()`、`getTickEventEffectId()`
- `getTickEventInstanceId()`、`getTickEventSource()`、`getTickEventStacks()`、`getVitalsEventAction()`、`getVitalsEventActor()`、`getVitalsEventAmount()`、`getVitalsEventCount()`、`getVitalsEventCurrent()`
- `getVitalsEventMax()`、`getVitalsEventResource()`、`getVitalsEventSource()`、`getXp()`、`getXpToNext()`、`has()`、`hasAttribute()`、`hasEffect()`
- `hasSettlementStage()`、`hasTag()`、`heal()`、`isCastingSkill()`、`isDead()`、`knowsSkill()`、`learnSkill()`、`modifyBaseAttribute()`
- `newActor()`、`newBattle()`、`newGameState()`、`newWorldState()`、`newSettlementContext()`、`newTracker()`、`registerClassesFromJson()`、`registerEffectsFromJson()`、`globalGameState()`
- `removeAllAttributeModifiersBySource()`、`removeAttributeModifier()`、`removeAttributeModifiersBySource()`、`removeSettlementStage()`、`removeStatus()`、`removeStatusByEffect()`、`removeStatusBySource()`、`removeStatusByTag()`
- `release()`（销毁 ECS actor）、`revive()`、`rollLoot()`、`runSettlement()`、`set()`、`setBaseAttribute()`、`setCurrent()`、`setSettlementStageEnabled()`、`setSettlementStagePriority()`
- `setSkillCooldown()`、`setXpToNext()`、`syncEquipModifiers()`、`takeDamage()`、`registerItemStatsFromJson()`、`registerLootTablesFromJson()`、`registerQuestsFromJson()`、`replaceQuestsFromJson()`、`registerSkillDamage()`、`registerSkillsFromJson()`、`registerTraitsFromJson()`、`clearTraitDefinitions()`、`getTraitDefinitionCount()`、`getClassDefinitionCount()`、`update()`
- `claimQuestRewards(tracker, gameState, bag, questId)` 原子领取任务声明的全部 `item` / `attribute` 奖励；返回结构化 Result。
- `collectWorldLoot(...)` 原子提交一次持久化 loot 交互的世界消费、任务通知、属性与物品效果。
- `settleBattleVictory(...)` 原子结算战斗胜利带来的经验、升级技能、任务进度、世界遭遇消费和 GameState 奖励。
- `replaceShopOffersFromJson(json)` 严格验证并原子替换商品目录；`buyShopOffer(...)` / `sellShopOffer(...)` 按权威 offer ID 原子交易。
- 商品目录查询：`getShopOfferCount()`、`hasShopOffer()`、`getShopOfferId()`、`getShopOfferItemId()`、`getShopOfferName()`、`getShopOfferDescription()`、`getShopOfferBuyPrice()`、`getShopOfferSellPrice()`；`clearShopOffers()` 仅用于卸载或初次发布失败回滚。
- `replaceEncountersFromJson(json)` 严格验证并原子替换遭遇目录；`newEncounterMemberActor(id, index)` 按有序编队创建敌人，`settleEncounterVictory(...)` 按整场定义原子结算。`newEncounterActor(id)` 是创建首成员的单敌人兼容入口。
- 遭遇目录查询：`hasEncounter()`、`getEncounterDisplayName()`、`getEncounterMemberCount()`、`getEncounterMemberDisplayName()`、`getEncounterMemberMaxHp()`；`getEncounterMaxHp()` 是首成员兼容查询，`clearEncounters()` 仅用于卸载或初次发布失败回滚。

新内容管线应使用 `replaceQuestsFromJson(json)`。它严格验证策略、稳定 ID、目标、奖励、依赖存在性和依赖环，
成功时一次替换完整任务目录；失败返回通用结构化 Result，且旧目录保持不变。旧的
`registerQuestsFromJson` 仅作为宽松兼容入口保留，不适合产品热重载。

产品任务交付应调用 `claimQuestRewards()`，而不是先 `bag.addItem()` 再 `tracker.claim()`。该入口先验证
任务状态、全部奖励类型、数值和背包完整批次，再提交 GameState、Tracker 与 Bag；失败不改变任一权威
所有者，也不发布库存事件。成功时库存 hook 在完整提交后执行，因此回调读取到的任务、属性和背包状态一致。
旧 `Tracker.claim()` 仍是无奖励状态机兼容入口，适用于成就、教程或由调用方明确自行结算的追踪器。

持久化宝箱、采集点和任务物资应调用 `collectWorldLoot()`。它拒绝重复消费、未激活的任务门控、
不完整通知和无法容纳的完整物品批次；成功时按同一事务更新 `GameState` 世界事实与属性、`Tracker`
目标进度和 `Bag`。失败时四类状态均保持原样，库存监听器只在完整提交后收到事件。

战斗结束应调用 `settleBattleVictory()`，不要在脚本中依次增加经验、同步职业技能、推进任务、发放
金币并消费遭遇对象。该入口先准备并验证完整成长结果，再提交 `GameState`、`Tracker`、世界事实与
`RPGActor`；冲突或重复遭遇不会留下部分奖励。升级事件采用提交后轮询，观察者只能看到完整终态。

产品商店应先用 `replaceShopOffersFromJson()` 发布完整商品目录，再调用 `buyShopOffer()` /
`sellShopOffer()`；客户端只提交 offer ID 和数量，物品与买卖价格从已验证目录读取。目录拒绝未知字段、
未知物品、重复 ID、负价格和高于买价的卖价，失败时旧目录保持不变。交易会检查完整余额、背包容量
或持有数量，并检测准备后的背包陈旧冲突；失败时恢复 GameState，库存监听器只在货币与物品都到达
最终状态后收到事件。货币变量仍由 GameState 单独权威持有。

产品遭遇应先用 `replaceEncountersFromJson()` 发布完整目录。每个定义必须引用已注册技能和任务，并完整声明
有序 `members` 编队、各成员属性/技能/显示名，以及整场成长与金币奖励、任务通知、击杀计数和升级点数。
成员 ID 在一场遭遇内必须唯一，单场最多 32 名敌人。未知字段、重复 ID、悬空引用或非法数值
会拒绝整个新目录，旧目录继续服务。地图只保存稳定 `encounterId`；创建敌人与胜利结算都从同一个已验证定义
投影，脚本不能另传奖励金额；多成员全部被击败后仍只结算一次，从而避免地图、战斗表现和结算三份事实漂移。`RPGActor` 仍由 RPG ECS 世界拥有，
`GameState`、`Tracker` 和角色成长各自保持唯一权威所有者。

**Tracker 方法：** `activate()`、`abandon()`、`canActivate()`、`canActivateReason()`、`claim()`、`fail()`、`getCount()`、`getEventAction()`、`getEventAmount()`、`getEventCount()`、`getEventEntryId()`、`getEventObjectiveId()`、`getEventReason()`、`getEventTarget()`、`getEventTopic()`、`getExtra()`、`getId()`、`getObjectiveCount()`、`getObjectiveCountRequired()`、`getObjectiveCurrent()`、`getObjectiveId()`、`getRewardAmount()`、`getRewardCount()`、`getRewardId()`、`getRewardType()`、`getState()`、`hasTag()`、`isObjectiveDone()`、`notify()`、`pollEvents()`、`reset()`、`syncAuto()`

**特征（RPGActor）：** `applyTrait()`、`removeTrait()`、`removeTraitsBySource()`、`removeTraitsByTrait()`、`hasTrait()`、`getTraitCount()`、`getTraitIdAt()`、`getTraitInstanceIdAt()`、`getTraitSourceAt()`、`getParamRate()`、`getElementRate()`、`getStateRate()`、`isStateResist()`、`getExParam()`、`getAttackSpeed()`、`getAttackTimesAdd()`

**职业（RPGActor）：** `setClass()`、`getClassId()`、`hasClass()`、`checkLevelSkills()`、`getClassLearnCount()`、`getClassLearnSkillIdAt()`、`getClassLearnLevelAt()`

**Battle：** `addActor()`、`autoEnemyActions()`、`executeNextAction()`、`getActor()`、`getActorCount()`、`getEventAction()`、`getEventAmount()`、`getEventCaster()`、`getEventCount()`、`getEventCrit()`、`getEventSkillId()`、`getEventTarget()`、`getPlayerSide()`、`getSide()`、`getTurn()`、`getWinnerSide()`、`isActorAlive()`、`isDefeat()`、`isFinished()`、`isVictory()`、`pollEvents()`、`setAction()`、`setPlayerSide()`、`startRound()`

产品战斗输入应使用 `setActionChecked()`：它只在回合开始前接受存活参战者的一次行动，并校验技能已学、
显式目标属于本场战斗且阵营符合 `self` / `enemySingle` 规则。失败返回结构化 Result 且不会污染待行动队列。
`setAction()` 是保留旧调用形状的兼容门面。

**GameState：** `addVariable()`、`clear()`、`getSelfVariable()`、`getVariable()`、`hasSelfVariable()`、`isSwitchOn()`、`restoreSnapshotJson()`、`setSelfVariable()`、`setSwitch()`、`setVariable()`、`snapshotJson()`、`switchOff()`、`switchOn()`

**RPGWorldState：** `consumeObject()`、`isObjectConsumed()`、`resetObject()`。这是借用 `GameState` 的
类型化适配器，不是第二份状态；map/object ID 必须非空、至多 256 字节且不含控制字符。consume/reset
返回结构化 Result，并以 `Applied`/`NoOp` 区分首次变化和幂等重复。`GameState` 必须比适配器活得更久。

**成长读档：** `RPGActor.restoreProgression()` 会校验等级、当前经验与升级阈值，并在失败时保持原成长状态不变；它不会伪造升级事件。

`GameState.snapshotJson()` 生成 schema 为 `eve.rpg.game-state`、版本为 1 的确定性 JSON；
`restoreSnapshotJson()` 会先校验完整候选再原子替换，未知版本或字段类型错误不会清空当前状态。
版本 1 忽略未知字段，为后续兼容扩展保留空间。

`Tracker.snapshotJson()` / `restoreSnapshotJson()` 保存任务与目标进度，不保存瞬时事件队列。
恢复时会与当前任务注册表逐项核对任务 id、目标 id、顺序和进度边界；内容版本不匹配会明确失败，
不会悄悄丢弃旧任务进度。

`RPGActor.checkpointJson()` / `restoreCheckpointJson()` 提供安全检查点语义：持久化基础属性、
等级经验、当前资源、已学技能、职业和特征身份；活动效果、施法、冷却与运行时事件属于战斗瞬态。
捕获时存在活动效果或施法会明确失败，恢复目标必须是相同职业、特征和属性布局的干净角色原型。

`rpg.newParty()` 创建 non-owning 的有序 `RPGParty`。`addMember(memberId, actor)` / `removeMember(memberId)`
以稳定成员 ID 管理关系，`clear()` 只清关系、不销毁 ECS 拥有的 Actor；重复 ID、重复 Actor 和非法 ID
都会返回结构化失败。`count()`、`contains(memberId)`、`getMemberId(index)` 提供有序 roster 查询，
`getMemberActor(index)` / `findMemberActor(memberId)` 返回即时借用引用，结构变化后不得继续保留。
`hasStaleMembers()` 使用 generation-qualified handle 检测已销毁 Actor；`addToBattle(battle, side)` 会先解析
完整 roster，只有全队有效时才一次性加入 Battle，避免半支队伍进入战斗。
`recoverAtCheckpoint(healthResource, healthRatio, secondaryResource, secondaryRatio)` 用于安全点恢复：它先验证
全队引用、比例和每名成员的资源上限，再统一复活并恢复可选的次要资源；任一成员无效时全队保持原状。

`Battle.setActionByPolicyChecked(actor, skillId, policy)` 在 canonical 行动校验前确定性选择目标。policy 支持
`auto`、`self`、`lowestHealthAlly`、`lowestHealthEnemy`；最低生命比例相同时保持参战者加入顺序。
技能的 `targetType` 支持 `self`、`allySingle` 和敌方目标，策略与技能目标域不一致会返回结构化失败。

队友和敌方策略可由 `replaceBattleTacticsFromJson(json)` 严格整批发布。每个策略是有序 rules：规则引用
`skillId`、`targetPolicy`，并可用 `conditionResource`＋`belowRatio` 判断目标资源比例；最后一条必须是无条件兜底。
未知字段、未知技能、非法比例、目标域冲突或无兜底会保留上一份目录。运行时调用
`queueBattleTactic(battle, actor, tacticsId)` 选择首个匹配规则并通过 Battle 的 checked API 排队；
`clearBattleTactics()` 用于显式卸载目录。

剧情事件使用 `replaceStoryEventsFromJson(json)` 严格发布 schema 为 `eve.rpg.story-events`、版本为 1
的内容文档。每个事件包含稳定 ID、`repeatable` 策略和 1–128 个有序展示步骤；步骤类型为
`dialogue`、`message`、`wait`、`move`、`camera`，统一携带 `reference`、`actorId`、`x/y` 和
`duration` 字段，并按类型严格校验字段组合。未知 schema/version、未知字段、重复 ID、非有限坐标或
非法时长会拒绝整批替换并保留旧目录。`getStoryEventCount()` / `hasStoryEvent(id)` 查询已提交内容，
`clearStoryEvents()` 用于内容包补偿或卸载。

`rpg.newStoryEventSession()` 创建 caller-owned `RPGStoryEventSession`。`begin(eventId, gameState)` 从
`GameState` 的 `story.event:<id>` 独立变量恢复游标；非重复事件完成后再次开始会返回结构化冲突。
当前步骤通过 `getStepKind()`、`getReference()`、`getActorId()`、`getX/Y/Duration()` 作为 owning
投影交给 UI、Dialogue、Map 或 Camera adapter 执行，展示成功后才调用 `advance(gameState)` 确认。
`advance()` 还会对持久游标执行 compare-and-set 校验，两个旧 session 或错误存档 owner 不能重复确认同一步。
最后一步确认会先把完成事实写入 `GameState`，再令 session 结束。活动 session 拷贝不可变定义，目录热替换
不会造成悬垂引用；GameState 是游标和完成状态的唯一权威，session 不保留其指针，也不调用脚本回调。
因此游戏可在步骤确认边界保存，并在读档后用新 session 精确重放尚未确认的展示步骤。

**RPGStoryEventSession 方法：** `advance()`、`begin()`、`getActorId()`、`getDuration()`、
`getEventId()`、`getReference()`、`getStepCount()`、`getStepIndex()`、`getStepKind()`、`getX()`、
`getY()`、`isActive()`、`isFinished()`。

`eve.RPGSaveSession()` 是 non-owning 的存档协调器。调用
`bind(gameState, tracker, actor, bag, equipment)` 后，
会生成兼容的单角色 schema v1；新游戏应创建 `RPGParty` 并调用
`bindParty(gameState, tracker, party, bag, equipment)`，生成按稳定成员 ID 和顺序保存全队 Actor 的 schema v2。
还必须用 `setContentVersion(id)` 配置当前游戏数据契约，`getContentVersion()` 可读取已配置值。对于只增加内容、
且当前 codec 能完整读取的旧数据版本，可逐个调用 `allowCompatibleContentVersion(oldId)`；它不是任意数据转换器，
旧存档仍需通过当前任务、角色、物品和装备定义的全部验证。ID 确实发生变化时，使用
`addIdRenameMigration(fromVersion, domain, oldId, newId)` 注册直接到当前版本的纯转换；脚本 domain 支持
`item`、`quest`、`questObjective`、`skill`、`class`、`trait`、`traitSource` 和 `equipmentSlot`。
当新版本只是在 Tracker 注册表中追加任务时，使用
`addQuestAdditionMigration(fromVersion, questId)` 显式声明新增任务。迁移会按当前注册表顺序重建 owning
Tracker payload，只为声明的任务写入 canonical 初始状态；旧任务进度保持原值，未声明的缺失任务和已删除任务仍被拒绝。
任务定义必须在注册迁移前已加载；该规则可与同一来源版本的 ID rename 组合。
从旧单角色存档升级到队伍存档时，必须为每个来源内容版本显式调用
`allowSingleActorPartyMigration(fromVersion)`：旧 Actor 只恢复到队伍第 0 位，其余成员保留当前内容预配置的基线，
没有登记的 schema v1 不会被队伍会话猜测性读取。
同一来源版本不能同时声明为“直接兼容”和“需要转换”，重复或冲突规则会明确失败。转换只修改通过完整性校验后的
owning payload 副本，随后仍执行全部 participant codec；磁盘原文和实时状态在 commit 前保持不变。
`snapshotJson()` 会生成公共严格 envelope 中的
`rpg:save-session` 单角色版本 1 或队伍版本 2 文档，并以 schema 固定的 128 位非密码摘要检测意外损坏；该摘要不承担防作弊或来源认证。
`validateSnapshotJson(json)` 会执行与恢复完全相同的 envelope、内容版本和 participant 准备流程，但不发布任何状态，
适合选择主存档或备份存档。`restoreSnapshotJson()` 会先验证 envelope 字段、内容摘要、精确或显式兼容的内容版本和
全部 participant，再同时发布 GameState、Tracker、玩家 Actor/全队 Actor、Bag 与 EquipmentSet，任一 participant
失败则所有权威状态都保持不变。装备属性修饰是可重建投影，不重复写入库存格式；恢复后调用
`syncEquipModifiers(actor, equipment)` 从权威装备状态重建。
内容版本是游戏维护的 opaque 标识，与存档 schema 版本分离；定义、任务或物品发生不兼容变更时必须
更新它并提供逐版本显式转换迁移，不能用兼容注册绕过验证。当前契约支持直接 N-1→N，不猜测缺失步骤、
不接受未知新版本且不支持降级；恢复旧版本后，下一次保存会自然写成当前版本。

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/rpg/`](../../../src/modules/rpg/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `rpg`。
