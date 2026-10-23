# rpg：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## RPGActor

- `addAttributeModifier(attribute: string, source: string, op: string, value: float, priority: int)` → `string`；[绑定实现 L613](../../../../src/modules/rpg/RPG.cpp#L613)。
- `canCastSkillReason(skillId: string)` → `string`；[绑定实现 L668](../../../../src/modules/rpg/RPG.cpp#L668)。
- `cancelCastSkill()` → `void`；[绑定实现 L672](../../../../src/modules/rpg/RPG.cpp#L672)。
- `getBaseAttribute(attribute: string)` → `float`；[绑定实现 L606](../../../../src/modules/rpg/RPG.cpp#L606)。
- `getBuffCount()` → `int`；[绑定实现 L653](../../../../src/modules/rpg/RPG.cpp#L653)。
- `getBuffEffectId(index: int)` → `string`；[绑定实现 L654](../../../../src/modules/rpg/RPG.cpp#L654)。
- `getBuffInstanceId(index: int)` → `int`；[绑定实现 L657](../../../../src/modules/rpg/RPG.cpp#L657)。
- `getBuffProp(instanceId: int, key: string, fallback: string)` → `string`；[绑定实现 L659](../../../../src/modules/rpg/RPG.cpp#L659)。
- `getBuffRemaining(index: int)` → `float`；[绑定实现 L656](../../../../src/modules/rpg/RPG.cpp#L656)。
- `getBuffSource(index: int)` → `string`；[绑定实现 L658](../../../../src/modules/rpg/RPG.cpp#L658)。
- `getBuffStacks(index: int)` → `int`；[绑定实现 L655](../../../../src/modules/rpg/RPG.cpp#L655)。
- `getCastProgress()` → `float`；[绑定实现 L675](../../../../src/modules/rpg/RPG.cpp#L675)。
- `getCastingSkillId()` → `string`；[绑定实现 L674](../../../../src/modules/rpg/RPG.cpp#L674)。
- `getCurrent(r: string)` → `float`；[绑定实现 L690](../../../../src/modules/rpg/RPG.cpp#L690)。
- `getMax(r: string)` → `float`；[绑定实现 L693](../../../../src/modules/rpg/RPG.cpp#L693)。
- `getSkillCooldown(skillId: string)` → `float`；[绑定实现 L665](../../../../src/modules/rpg/RPG.cpp#L665)。
- `getStatusCount()` → `int`；[绑定实现 L637](../../../../src/modules/rpg/RPG.cpp#L637)。
- `getStatusEffectId(index: int)` → `string`；[绑定实现 L638](../../../../src/modules/rpg/RPG.cpp#L638)。
- `getStatusInstanceId(index: int)` → `int`；[绑定实现 L641](../../../../src/modules/rpg/RPG.cpp#L641)。
- `getStatusProp(instanceId: int, key: string, fallback: string)` → `string`；[绑定实现 L643](../../../../src/modules/rpg/RPG.cpp#L643)。
- `getStatusRemaining(index: int)` → `float`；[绑定实现 L640](../../../../src/modules/rpg/RPG.cpp#L640)。
- `getStatusSource(index: int)` → `string`；[绑定实现 L642](../../../../src/modules/rpg/RPG.cpp#L642)。
- `getStatusStacks(index: int)` → `int`；[绑定实现 L639](../../../../src/modules/rpg/RPG.cpp#L639)。
- `getXp()` → `float`；[绑定实现 L679](../../../../src/modules/rpg/RPG.cpp#L679)。
- `getXpToNext()` → `float`；[绑定实现 L680](../../../../src/modules/rpg/RPG.cpp#L680)。
- `hasAttribute(attribute: string)` → `bool`；[绑定实现 L612](../../../../src/modules/rpg/RPG.cpp#L612)。
- `hasBuffTag(tag: string)` → `bool`；[绑定实现 L652](../../../../src/modules/rpg/RPG.cpp#L652)。
- `hasStatusTag(tag: string)` → `bool`；[绑定实现 L636](../../../../src/modules/rpg/RPG.cpp#L636)。
- `isCastingSkill()` → `bool`；[绑定实现 L673](../../../../src/modules/rpg/RPG.cpp#L673)。
- `knowsSkill(skillId: string)` → `bool`；[绑定实现 L663](../../../../src/modules/rpg/RPG.cpp#L663)。
- `modifyBaseAttribute(attribute: string, delta: float)` → `dynamic`；[绑定实现 L609](../../../../src/modules/rpg/RPG.cpp#L609)。
- `removeAllAttributeModifiersBySource(source: string)` → `int`；[绑定实现 L624](../../../../src/modules/rpg/RPG.cpp#L624)。
- `removeAttributeModifier(attribute: string, modifierId: string)` → `dynamic`；[绑定实现 L619](../../../../src/modules/rpg/RPG.cpp#L619)。
- `removeAttributeModifiersBySource(attribute: string, source: string)` → `int`；[绑定实现 L623](../../../../src/modules/rpg/RPG.cpp#L623)。
- `removeBuffByEffect(effectId: string)` → `int`；[绑定实现 L648](../../../../src/modules/rpg/RPG.cpp#L648)。
- `removeBuffBySource(source: string)` → `int`；[绑定实现 L649](../../../../src/modules/rpg/RPG.cpp#L649)。
- `removeBuffByTag(tag: string)` → `int`；[绑定实现 L650](../../../../src/modules/rpg/RPG.cpp#L650)。
- `removeStatusByEffect(effectId: string)` → `int`；[绑定实现 L632](../../../../src/modules/rpg/RPG.cpp#L632)。
- `removeStatusBySource(source: string)` → `int`；[绑定实现 L633](../../../../src/modules/rpg/RPG.cpp#L633)。
- `removeStatusByTag(tag: string)` → `int`；[绑定实现 L634](../../../../src/modules/rpg/RPG.cpp#L634)。
- `setSkillCooldown(skillId: string, seconds: float)` → `void`；[绑定实现 L666](../../../../src/modules/rpg/RPG.cpp#L666)。
- `setStatusProp(instanceId: int, key: string, value: string)` → `bool`；[绑定实现 L644](../../../../src/modules/rpg/RPG.cpp#L644)。
- `setXpToNext(value: float)` → `dynamic`；[绑定实现 L681](../../../../src/modules/rpg/RPG.cpp#L681)。

## RPGStoryEventSession

- `getEventId()` → `string`；[绑定实现 L1026](../../../../src/modules/rpg/RPG.cpp#L1026)。
- `getStepCount()` → `int`；[绑定实现 L1025](../../../../src/modules/rpg/RPG.cpp#L1025)。
- `getStepIndex()` → `int`；[绑定实现 L1024](../../../../src/modules/rpg/RPG.cpp#L1024)。

## SettlementContext

- `addTag(tag: string)` → `void`；[绑定实现 L770](../../../../src/modules/rpg/RPG.cpp#L770)。

## 接收者见绑定源码

- `clearAllItemStats()` → `void`；[绑定实现 L1442](../../../../src/modules/rpg/RPG.cpp#L1442)。
- `clearClassDefinitions()` → `void`；[绑定实现 L1453](../../../../src/modules/rpg/RPG.cpp#L1453)。
- `clearEffectDefinitions()` → `void`；[绑定实现 L1180](../../../../src/modules/rpg/RPG.cpp#L1180)。
- `clearItemStats(itemId: string)` → `void`；[绑定实现 L1441](../../../../src/modules/rpg/RPG.cpp#L1441)。
- `clearLootTables()` → `void`；[绑定实现 L1446](../../../../src/modules/rpg/RPG.cpp#L1446)。
- `clearQuestDefinitions()` → `void`；[绑定实现 L1421](../../../../src/modules/rpg/RPG.cpp#L1421)。
- `clearSettlementPipeline(pipeline: string)` → `void`；[绑定实现 L1493](../../../../src/modules/rpg/RPG.cpp#L1493)。
- `clearSkillDamage()` → `void`；[绑定实现 L1462](../../../../src/modules/rpg/RPG.cpp#L1462)。
- `clearSkillDefinitions()` → `void`；[绑定实现 L1183](../../../../src/modules/rpg/RPG.cpp#L1183)。
- `clearTraitDefinitions()` → `void`；[绑定实现 L1450](../../../../src/modules/rpg/RPG.cpp#L1450)。
- `getCastEventCaster(index: int)` → `RPGActor`；[绑定实现 L1483](../../../../src/modules/rpg/RPG.cpp#L1483)。
- `getCastEventCount()` → `int`；[绑定实现 L1482](../../../../src/modules/rpg/RPG.cpp#L1482)。
- `getCastEventSkillId(index: int)` → `string`；[绑定实现 L1485](../../../../src/modules/rpg/RPG.cpp#L1485)。
- `getCastEventTarget(index: int)` → `RPGActor`；[绑定实现 L1484](../../../../src/modules/rpg/RPG.cpp#L1484)。
- `getClassDefinitionCount()` → `int`；[绑定实现 L1454](../../../../src/modules/rpg/RPG.cpp#L1454)。
- `getEffectDefinitionCount()` → `int`；[绑定实现 L1181](../../../../src/modules/rpg/RPG.cpp#L1181)。
- `getItemStatCount()` → `int`；[绑定实现 L1443](../../../../src/modules/rpg/RPG.cpp#L1443)。
- `getLevelUpEventActor(index: int)` → `RPGActor`；[绑定实现 L1426](../../../../src/modules/rpg/RPG.cpp#L1426)。
- `getLevelUpEventCount()` → `int`；[绑定实现 L1425](../../../../src/modules/rpg/RPG.cpp#L1425)。
- `getLevelUpEventNewLevel(index: int)` → `int`；[绑定实现 L1428](../../../../src/modules/rpg/RPG.cpp#L1428)。
- `getLevelUpEventPreviousLevel(index: int)` → `int`；[绑定实现 L1427](../../../../src/modules/rpg/RPG.cpp#L1427)。
- `getLootTableCount()` → `int`；[绑定实现 L1447](../../../../src/modules/rpg/RPG.cpp#L1447)。
- `getName()` → `string`；[绑定实现 L1176](../../../../src/modules/rpg/RPG.cpp#L1176)。
- `getQuestDefinitionCount()` → `int`；[绑定实现 L1422](../../../../src/modules/rpg/RPG.cpp#L1422)。
- `getSettlementStageCount(pipeline: string)` → `int`；[绑定实现 L1488](../../../../src/modules/rpg/RPG.cpp#L1488)。
- `getSkillDefinitionCount()` → `int`；[绑定实现 L1184](../../../../src/modules/rpg/RPG.cpp#L1184)。
- `getStatusChangeEventAction(index: int)` → `string`；[绑定实现 L1479](../../../../src/modules/rpg/RPG.cpp#L1479)。
- `getStatusChangeEventActor(index: int)` → `RPGActor`；[绑定实现 L1475](../../../../src/modules/rpg/RPG.cpp#L1475)。
- `getStatusChangeEventCount()` → `int`；[绑定实现 L1474](../../../../src/modules/rpg/RPG.cpp#L1474)。
- `getStatusChangeEventEffectId(index: int)` → `string`；[绑定实现 L1477](../../../../src/modules/rpg/RPG.cpp#L1477)。
- `getStatusChangeEventInstanceId(index: int)` → `int`；[绑定实现 L1476](../../../../src/modules/rpg/RPG.cpp#L1476)。
- `getStatusChangeEventReason(index: int)` → `string`；[绑定实现 L1481](../../../../src/modules/rpg/RPG.cpp#L1481)。
- `getStatusChangeEventSource(index: int)` → `string`；[绑定实现 L1478](../../../../src/modules/rpg/RPG.cpp#L1478)。
- `getStatusChangeEventStacks(index: int)` → `int`；[绑定实现 L1480](../../../../src/modules/rpg/RPG.cpp#L1480)。
- `getTickEventActor(index: int)` → `RPGActor`；[绑定实现 L1469](../../../../src/modules/rpg/RPG.cpp#L1469)。
- `getTickEventCount()` → `int`；[绑定实现 L1468](../../../../src/modules/rpg/RPG.cpp#L1468)。
- `getTickEventEffectId(index: int)` → `string`；[绑定实现 L1471](../../../../src/modules/rpg/RPG.cpp#L1471)。
- `getTickEventInstanceId(index: int)` → `int`；[绑定实现 L1470](../../../../src/modules/rpg/RPG.cpp#L1470)。
- `getTickEventSource(index: int)` → `string`；[绑定实现 L1472](../../../../src/modules/rpg/RPG.cpp#L1472)。
- `getTickEventStacks(index: int)` → `int`；[绑定实现 L1473](../../../../src/modules/rpg/RPG.cpp#L1473)。
- `getTraitDefinitionCount()` → `int`；[绑定实现 L1451](../../../../src/modules/rpg/RPG.cpp#L1451)。
- `getVitalsEventAction(index: int)` → `string`；[绑定实现 L1432](../../../../src/modules/rpg/RPG.cpp#L1432)。
- `getVitalsEventActor(index: int)` → `RPGActor`；[绑定实现 L1430](../../../../src/modules/rpg/RPG.cpp#L1430)。
- `getVitalsEventAmount(index: int)` → `float`；[绑定实现 L1433](../../../../src/modules/rpg/RPG.cpp#L1433)。
- `getVitalsEventCount()` → `int`；[绑定实现 L1429](../../../../src/modules/rpg/RPG.cpp#L1429)。
- `getVitalsEventCurrent(index: int)` → `float`；[绑定实现 L1436](../../../../src/modules/rpg/RPG.cpp#L1436)。
- `getVitalsEventMax(index: int)` → `float`；[绑定实现 L1438](../../../../src/modules/rpg/RPG.cpp#L1438)。
- `getVitalsEventResource(index: int)` → `string`；[绑定实现 L1431](../../../../src/modules/rpg/RPG.cpp#L1431)。
- `getVitalsEventSource(index: int)` → `string`；[绑定实现 L1435](../../../../src/modules/rpg/RPG.cpp#L1435)。
- `globalGameState()` → `GameState`；[绑定实现 L1466](../../../../src/modules/rpg/RPG.cpp#L1466)。
- `hasQuestDefinition(id: string)` → `bool`；[绑定实现 L1423](../../../../src/modules/rpg/RPG.cpp#L1423)。
- `hasSettlementStage(pipeline: string, stage: string)` → `bool`；[绑定实现 L1489](../../../../src/modules/rpg/RPG.cpp#L1489)。
- `registerSkillsFromJson(json: string)` → `int`；[绑定实现 L1182](../../../../src/modules/rpg/RPG.cpp#L1182)。
- `removeSettlementStage(pipeline: string, stage: string)` → `bool`；[绑定实现 L1492](../../../../src/modules/rpg/RPG.cpp#L1492)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
