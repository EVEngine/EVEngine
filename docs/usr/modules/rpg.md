# RPG 系统模块

**脚本入口：** `eve.RPG()`

组合属性、效果、状态、技能、施法与伤害结算。

## 基本用法

```squirrel
local rpg = eve.RPG();
local actor = rpg.newActor();
actor.setBaseAttribute("hp", 100);
actor.setBaseAttribute("attack", 12);
print(actor.getFinalAttribute("hp") + "\n");
```

## 对象关系与调用时机

`RPG` 保存 effect/skill 定义和结算 pipeline；`RPGActor` 保存属性、状态、已学技能、冷却和施法状态。JSON 是定义，Actor 是运行时实例，事件队列负责把结算连接到表现。

## 目标导向指南

### 建立角色属性

`newActor()` 后用 `setBaseAttribute` 建立 hp、attack、defense 等基础值；永久或临时加成通过 modifier 添加，最终战斗数值读取 `getFinalAttribute()`。

### 注册并释放技能

从 JSON 注册 effect 和 skill 定义，让 Actor 学习技能；释放前用 `canCastSkill()` 和原因接口检查资源、冷却与条件。每帧 `rpg.update(dt)`，随后消费 cast/tick 事件并执行表现或伤害。

## 常见问题

- 只注册 Skill 未注册其引用的 Effect。
- 忘记每帧 `rpg.update(dt)`，导致冷却、DOT 和施法不推进。
- 直接修改最终属性：基础值和 modifier 应分层管理。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `addAttributeModifier()`、`addTag()`、`applyEffect()`、`beginCastSkill()`、`canCastSkill()`、`canCastSkillReason()`、`cancelCastSkill()`、`clearEffectDefinitions()`
- `clearSettlementPipeline()`、`clearSkillDefinitions()`、`forgetSkill()`、`get()`、`getBaseAttribute()`、`getCastEventCaster()`、`getCastEventCount()`、`getCastEventSkillId()`
- `getCastEventTarget()`、`getCastProgress()`、`getCastingSkillId()`、`getEffectDefinitionCount()`、`getFinalAttribute()`、`getName()`、`getSettlementStageCount()`、`getSkillCooldown()`
- `getSkillDefinitionCount()`、`getStatusCount()`、`getStatusEffectId()`、`getStatusInstanceId()`、`getStatusRemaining()`、`getStatusStacks()`、`getTickEventActor()`、`getTickEventCount()`
- `getTickEventEffectId()`、`getTickEventInstanceId()`、`getTickEventSource()`、`getTickEventStacks()`、`has()`、`hasAttribute()`、`hasEffect()`、`hasSettlementStage()`
- `hasTag()`、`isCastingSkill()`、`knowsSkill()`、`learnSkill()`、`modifyBaseAttribute()`、`newActor()`、`newSettlementContext()`、`registerEffectsFromJson()`
- `registerSkillsFromJson()`、`removeAllAttributeModifiersBySource()`、`removeAttributeModifier()`、`removeAttributeModifiersBySource()`、`removeSettlementStage()`、`removeStatus()`、`removeStatusByEffect()`、`removeStatusBySource()`
- `removeStatusByTag()`、`runSettlement()`、`set()`、`setBaseAttribute()`、`setSettlementStageEnabled()`、`setSettlementStagePriority()`、`setSkillCooldown()`、`update()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/rpg/`](../../../src/modules/rpg/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `rpg`。
