# rts：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## 接收者见绑定源码

- `addBuildingAmmoSupply(subjectText: string, rounds: float)` → `Table<string, dynamic>`；[绑定实现 L4791](../../../../src/modules/rts/RTS.cpp#L4791)。
- `addMatchParticipant(matchText: string, factionText: string, team: int)` → `Table<string, dynamic>`；[绑定实现 L3712](../../../../src/modules/rts/RTS.cpp#L3712)。
- `addUnitAmmoSupply(subjectText: string, rounds: float)` → `Table<string, dynamic>`；[绑定实现 L4780](../../../../src/modules/rts/RTS.cpp#L4780)。
- `addUnitReserveAmmo(subjectText: string, rounds: int)` → `Table<string, dynamic>`；[绑定实现 L4769](../../../../src/modules/rts/RTS.cpp#L4769)。
- `applyStatusEffect(sourceText: string, targetText: string, effect: string, durationOverride: float)` → `Table<string, dynamic>`；[绑定实现 L4822](../../../../src/modules/rts/RTS.cpp#L4822)。
- `assignBuilder(workerText: string, buildingText: string)` → `Table<string, dynamic>`；[绑定实现 L4754](../../../../src/modules/rts/RTS.cpp#L4754)。
- `assignWorker(workerText: string, nodeText: string, dropoffText: string)` → `Table<string, dynamic>`；[绑定实现 L4696](../../../../src/modules/rts/RTS.cpp#L4696)。
- `attackGroundUnits(subjectTexts: string, x: float, y: float, append: bool)` → `Table<string, dynamic>`；[绑定实现 L4647](../../../../src/modules/rts/RTS.cpp#L4647)。
- `attackMoveUnits(subjectTexts: Array<dynamic>, x: float, y: float, append: bool, spacing: float)` → `Table<string, dynamic>`；[绑定实现 L93](../../../../src/modules/rts/RTSFormationBindings.cpp#L93)。
- `attackUnits(subjectTexts: string, targetText: string, append: bool)` → `Table<string, dynamic>`；[绑定实现 L4627](../../../../src/modules/rts/RTS.cpp#L4627)。
- `boardTransportUnits(subjectTexts: string, transportText: string, append: bool)` → `Table<string, dynamic>`；[绑定实现 L4944](../../../../src/modules/rts/RTS.cpp#L4944)。
- `buildingCount()` → `dynamic`；[绑定实现 L3655](../../../../src/modules/rts/RTS.cpp#L3655)。
- `cancelFireSupport(requesterText: string)` → `Table<string, dynamic>`；[绑定实现 L4859](../../../../src/modules/rts/RTS.cpp#L4859)。
- `cancelScriptAbility(casterText: string)` → `Table<string, dynamic>`；[绑定实现 L4083](../../../../src/modules/rts/RTS.cpp#L4083)。
- `cancelScriptConstruction(buildingText: string)` → `Table<string, dynamic>`；[绑定实现 L4027](../../../../src/modules/rts/RTS.cpp#L4027)。
- `cancelScriptProduction(producerText: string, queueIndex: int)` → `Table<string, dynamic>`；[绑定实现 L4050](../../../../src/modules/rts/RTS.cpp#L4050)。
- `captureScriptCheckpoint(name: string)` → `Table<string, dynamic>`；[绑定实现 L3838](../../../../src/modules/rts/RTS.cpp#L3838)。
- `captureUnits(subjectTexts: string, buildingText: string, append: bool)` → `Table<string, dynamic>`；[绑定实现 L4904](../../../../src/modules/rts/RTS.cpp#L4904)。
- `castScriptAbility(casterText: string, ability: string, targetText: string, x: float, y: float)` → `Table<string, dynamic>`；[绑定实现 L4065](../../../../src/modules/rts/RTS.cpp#L4065)。
- `clearBuildingRally(producerText: string)` → `Table<string, dynamic>`；[绑定实现 L4123](../../../../src/modules/rts/RTS.cpp#L4123)。
- `configureAutoConstruction(factionText: string, enabled: bool, maxBuildersPerSite: int, reserveWorkers: int)` → `Table<string, dynamic>`；[绑定实现 L4722](../../../../src/modules/rts/RTS.cpp#L4722)。
- `configureAutoRepair(factionText: string, enabled: bool, maxRepairersPerBuilding: int, reserveWorkers: int)` → `Table<string, dynamic>`；[绑定实现 L4738](../../../../src/modules/rts/RTS.cpp#L4738)。
- `configureMatch(matchText: string, ruleText: string, archetype: string, targetValue: float)` → `Table<string, dynamic>`；[绑定实现 L3688](../../../../src/modules/rts/RTS.cpp#L3688)。
- `configureScriptAI(factionText: string, workerText: string, armyText: string, targetBuildingText: string, desiredWorkers: int, attackThreshold: int, thinkInterval: float, formationSpacing: float, enabled: bool)` → `Table<string, dynamic>`；[绑定实现 L3926](../../../../src/modules/rts/RTS.cpp#L3926)。
- `configureSettlementRulesJson(json: string)` → `Table<string, dynamic>`；[绑定实现 L3662](../../../../src/modules/rts/RTS.cpp#L3662)。
- `escortUnits(subjectTexts: string, targetText: string, guardRadius: float, spacing: float)` → `Table<string, dynamic>`；[绑定实现 L4668](../../../../src/modules/rts/RTS.cpp#L4668)。
- `evacuateBuilding(buildingText: string, x: float, y: float)` → `Table<string, dynamic>`；[绑定实现 L4975](../../../../src/modules/rts/RTS.cpp#L4975)。
- `exportScriptCommandLog()` → `Table<string, dynamic>`；[绑定实现 L4230](../../../../src/modules/rts/RTS.cpp#L4230)。
- `factionCount()` → `dynamic`；[绑定实现 L3658](../../../../src/modules/rts/RTS.cpp#L3658)。
- `garrisonUnits(subjectTexts: string, buildingText: string, append: bool)` → `Table<string, dynamic>`；[绑定实现 L4924](../../../../src/modules/rts/RTS.cpp#L4924)。
- `getName()` → `string`；[绑定实现 L3650](../../../../src/modules/rts/RTS.cpp#L3650)。
- `heal(sourceText: string, targetText: string, amount: float)` → `Table<string, dynamic>`；[绑定实现 L4812](../../../../src/modules/rts/RTS.cpp#L4812)。
- `holdUnits(subjectTexts: string)` → `Table<string, dynamic>`；[绑定实现 L5003](../../../../src/modules/rts/RTS.cpp#L5003)。
- `importScriptCommandLog(text: string, clearExisting: bool)` → `Table<string, dynamic>`；[绑定实现 L4234](../../../../src/modules/rts/RTS.cpp#L4234)。
- `inspectFrameEvents()` → `Table<string, dynamic>`；[绑定实现 L3826](../../../../src/modules/rts/RTS.cpp#L3826)。
- `inspectMatch(matchText: string)` → `Table<string, dynamic>`；[绑定实现 L3751](../../../../src/modules/rts/RTS.cpp#L3751)。
- `linkBuildingRally(producerText: string, sourceText: string)` → `Table<string, dynamic>`；[绑定实现 L4108](../../../../src/modules/rts/RTS.cpp#L4108)。
- `matchCount()` → `dynamic`；[绑定实现 L3659](../../../../src/modules/rts/RTS.cpp#L3659)。
- `moveFormationUnits(texts: Array<dynamic>, x: float, y: float, append: bool, kind: string, spacing: float, columns: int, rotation: float)` → `Table<string, dynamic>`；[绑定实现 L108](../../../../src/modules/rts/RTSFormationBindings.cpp#L108)。
- `moveGroupUnits(texts: Array<dynamic>, x: float, y: float, kind: string, spacing: float, columns: int, rotation: float, leadDistance: float)` → `Table<string, dynamic>`；[绑定实现 L45](../../../../src/modules/rts/RTSFormationBindings.cpp#L45)。
- `moveUnits(subjectTexts: Array<dynamic>, x: float, y: float, append: bool, spacing: float)` → `Table<string, dynamic>`；[绑定实现 L79](../../../../src/modules/rts/RTSFormationBindings.cpp#L79)。
- `newMatch(subjectText: string)` → `Table<string, dynamic>`；[绑定实现 L3682](../../../../src/modules/rts/RTS.cpp#L3682)。
- `patrolUnits(subjectTexts: string, x: float, y: float, spacing: float)` → `Table<string, dynamic>`；[绑定实现 L4870](../../../../src/modules/rts/RTS.cpp#L4870)。
- `playerCount()` → `dynamic`；[绑定实现 L3657](../../../../src/modules/rts/RTS.cpp#L3657)。
- `queueScriptAbilityCommand(tick: int, casterText: string, ability: string, targetText: string, x: float, y: float)` → `Table<string, dynamic>`；[绑定实现 L4324](../../../../src/modules/rts/RTS.cpp#L4324)。
- `queueScriptAppendAttackMoveCommand(tick: int, subjects: string, x: float, y: float, spacing: float)` → `Table<string, dynamic>`；[绑定实现 L4589](../../../../src/modules/rts/RTS.cpp#L4589)。
- `queueScriptAppendMoveCommand(tick: int, subjects: string, x: float, y: float, spacing: float)` → `Table<string, dynamic>`；[绑定实现 L4579](../../../../src/modules/rts/RTS.cpp#L4579)。
- `queueScriptAttack(tick: int, subjectTexts: string, targetText: string)` → `Table<string, dynamic>`；[绑定实现 L4469](../../../../src/modules/rts/RTS.cpp#L4469)。
- `queueScriptAttackGroundCommand(tick: int, subjects: string, x: float, y: float, append: bool)` → `Table<string, dynamic>`；[绑定实现 L4574](../../../../src/modules/rts/RTS.cpp#L4574)。
- `queueScriptAttackMove(tick: int, subjectTexts: string, x: float, y: float)` → `Table<string, dynamic>`；[绑定实现 L4451](../../../../src/modules/rts/RTS.cpp#L4451)。
- `queueScriptBoardTransportCommand(tick: int, subjects: string, target: string)` → `Table<string, dynamic>`；[绑定实现 L4621](../../../../src/modules/rts/RTS.cpp#L4621)。
- `queueScriptCancelAbilityCommand(tick: int, subjectTexts: string)` → `Table<string, dynamic>`；[绑定实现 L4363](../../../../src/modules/rts/RTS.cpp#L4363)。
- `queueScriptCancelConstructionCommand(tick: int, buildingText: string)` → `Table<string, dynamic>`；[绑定实现 L4425](../../../../src/modules/rts/RTS.cpp#L4425)。
- `queueScriptCancelFireSupportCommand(tick: int, requesterText: string)` → `Table<string, dynamic>`；[绑定实现 L4397](../../../../src/modules/rts/RTS.cpp#L4397)。
- `queueScriptCancelProductionCommand(tick: int, producerText: string, queueIndex: int)` → `Table<string, dynamic>`；[绑定实现 L4348](../../../../src/modules/rts/RTS.cpp#L4348)。
- `queueScriptCaptureCommand(tick: int, subjects: string, target: string)` → `Table<string, dynamic>`；[绑定实现 L4611](../../../../src/modules/rts/RTS.cpp#L4611)。
- `queueScriptConstructionCommand(tick: int, factionText: string, builderText: string, buildingSubjectText: string, definitionText: string, x: float, y: float)` → `Table<string, dynamic>`；[绑定实现 L4237](../../../../src/modules/rts/RTS.cpp#L4237)。
- `queueScriptEscortCommand(tick: int, subjectTexts: string, targetText: string, guardRadius: float, spacing: float)` → `Table<string, dynamic>`；[绑定实现 L4514](../../../../src/modules/rts/RTS.cpp#L4514)。
- `queueScriptFireSupportCommand(tick: int, requesterText: string, x: float, y: float, radius: float, shotsPerResponder: int, maxResponders: int)` → `Table<string, dynamic>`；[绑定实现 L4377](../../../../src/modules/rts/RTS.cpp#L4377)。
- `queueScriptFormationMove(tick: int, texts: Array<dynamic>, x: float, y: float, append: bool, kind: string, spacing: float, columns: int, rotation: float)` → `Table<string, dynamic>`；[绑定实现 L123](../../../../src/modules/rts/RTSFormationBindings.cpp#L123)。
- `queueScriptGarrisonCommand(tick: int, subjects: string, target: string)` → `Table<string, dynamic>`；[绑定实现 L4616](../../../../src/modules/rts/RTS.cpp#L4616)。
- `queueScriptGroupMove(tick: int, texts: Array<dynamic>, x: float, y: float, kind: string, spacing: float, columns: int, rotation: float, leadDistance: float)` → `Table<string, dynamic>`；[绑定实现 L59](../../../../src/modules/rts/RTSFormationBindings.cpp#L59)。
- `queueScriptHoldCommand(tick: int, subjects: string)` → `Table<string, dynamic>`；[绑定实现 L4565](../../../../src/modules/rts/RTS.cpp#L4565)。
- `queueScriptMove(tick: int, subjectTexts: string, x: float, y: float)` → `Table<string, dynamic>`；[绑定实现 L4433](../../../../src/modules/rts/RTS.cpp#L4433)。
- `queueScriptPatrolCommand(tick: int, subjects: string, x: float, y: float)` → `Table<string, dynamic>`；[绑定实现 L4569](../../../../src/modules/rts/RTS.cpp#L4569)。
- `queueScriptProductionCommand(tick: int, producerText: string, unitSubjectText: string, definitionText: string, priority: int)` → `Table<string, dynamic>`；[绑定实现 L4263](../../../../src/modules/rts/RTS.cpp#L4263)。
- `queueScriptReinforcement(producerText: string, unitSubjectText: string, definitionText: string, priority: int)` → `Table<string, dynamic>`；[绑定实现 L3969](../../../../src/modules/rts/RTS.cpp#L3969)。
- `queueScriptReinforcementCommand(tick: int, producerText: string, unitSubjectText: string, definitionText: string, priority: int)` → `Table<string, dynamic>`；[绑定实现 L4285](../../../../src/modules/rts/RTS.cpp#L4285)。
- `queueScriptRepairCommand(tick: int, subjects: string, target: string)` → `Table<string, dynamic>`；[绑定实现 L4606](../../../../src/modules/rts/RTS.cpp#L4606)。
- `queueScriptResearch(producerText: string, upgrade: string, priority: int)` → `Table<string, dynamic>`；[绑定实现 L4034](../../../../src/modules/rts/RTS.cpp#L4034)。
- `queueScriptResearchCommand(tick: int, producerText: string, upgrade: string, priority: int)` → `Table<string, dynamic>`；[绑定实现 L4307](../../../../src/modules/rts/RTS.cpp#L4307)。
- `queueScriptSellBuildingCommand(tick: int, buildingText: string)` → `Table<string, dynamic>`；[绑定实现 L4429](../../../../src/modules/rts/RTS.cpp#L4429)。
- `queueScriptStopCommand(tick: int, subjects: string)` → `Table<string, dynamic>`；[绑定实现 L4561](../../../../src/modules/rts/RTS.cpp#L4561)。
- `queueScriptSuppressAreaCommand(tick: int, subjectTexts: string, startX: float, startY: float, endX: float, endY: float, width: float, shotsPerUnit: int)` → `Table<string, dynamic>`；[绑定实现 L4494](../../../../src/modules/rts/RTS.cpp#L4494)。
- `queueScriptUnit(producerText: string, unitSubjectText: string, definitionText: string, priority: int)` → `Table<string, dynamic>`；[绑定实现 L3948](../../../../src/modules/rts/RTS.cpp#L3948)。
- `removeScriptCheckpoint(name: string)` → `Table<string, dynamic>`；[绑定实现 L3844](../../../../src/modules/rts/RTS.cpp#L3844)。
- `removeSubject(subjectText: string)` → `Table<string, dynamic>`；[绑定实现 L3671](../../../../src/modules/rts/RTS.cpp#L3671)。
- `repairUnits(subjectTexts: string, buildingText: string, append: bool)` → `Table<string, dynamic>`；[绑定实现 L4884](../../../../src/modules/rts/RTS.cpp#L4884)。
- `requestFireSupport(requesterText: string, x: float, y: float, radius: float, shotsPerResponder: int, maxResponders: int)` → `Table<string, dynamic>`；[绑定实现 L4838](../../../../src/modules/rts/RTS.cpp#L4838)。
- `resourceNodeCount()` → `dynamic`；[绑定实现 L3656](../../../../src/modules/rts/RTS.cpp#L3656)。
- `restoreScriptCheckpoint(name: string)` → `Table<string, dynamic>`；[绑定实现 L3841](../../../../src/modules/rts/RTS.cpp#L3841)。
- `scriptContact(factionText: string, targetText: string)` → `Table<string, dynamic>`；[绑定实现 L3888](../../../../src/modules/rts/RTS.cpp#L3888)。
- `scriptResource(factionText: string, resource: string)` → `Table<string, dynamic>`；[绑定实现 L3914](../../../../src/modules/rts/RTS.cpp#L3914)。
- `scriptTerrainElevation(x: int, y: int)` → `Table<string, dynamic>`；[绑定实现 L3862](../../../../src/modules/rts/RTS.cpp#L3862)。
- `scriptTick()` → `dynamic`；[绑定实现 L3660](../../../../src/modules/rts/RTS.cpp#L3660)。
- `sellScriptBuilding(buildingText: string)` → `Table<string, dynamic>`；[绑定实现 L4031](../../../../src/modules/rts/RTS.cpp#L4031)。
- `setBuildingRally(producerText: string, x: float, y: float, attackMove: bool, groupedReinforcements: bool)` → `Table<string, dynamic>`；[绑定实现 L4093](../../../../src/modules/rts/RTS.cpp#L4093)。
- `setReinforcementAutoCancel(producerText: string, seconds: float)` → `Table<string, dynamic>`；[绑定实现 L4190](../../../../src/modules/rts/RTS.cpp#L4190)。
- `setReinforcementFallback(producerText: string, preferred: string, fallback: string)` → `Table<string, dynamic>`；[绑定实现 L4178](../../../../src/modules/rts/RTS.cpp#L4178)。
- `setReinforcementLimit(producerText: string, maximum: int)` → `Table<string, dynamic>`；[绑定实现 L4133](../../../../src/modules/rts/RTS.cpp#L4133)。
- `setReinforcementTransport(producerText: string, transportText: string, minimumLoad: int)` → `Table<string, dynamic>`；[绑定实现 L4201](../../../../src/modules/rts/RTS.cpp#L4201)。
- `setReinforcementTypeLimit(producerText: string, unitType: string, maximum: int)` → `Table<string, dynamic>`；[绑定实现 L4148](../../../../src/modules/rts/RTS.cpp#L4148)。
- `setReinforcementTypePriority(producerText: string, unitType: string, priority: int)` → `Table<string, dynamic>`；[绑定实现 L4166](../../../../src/modules/rts/RTS.cpp#L4166)。
- `setScriptNavigationBlocked(x: int, y: int, blocked: bool)` → `Table<string, dynamic>`；[绑定实现 L3853](../../../../src/modules/rts/RTS.cpp#L3853)。
- `setScriptNavigationCost(x: int, y: int, cost: float)` → `Table<string, dynamic>`；[绑定实现 L3856](../../../../src/modules/rts/RTS.cpp#L3856)。
- `setScriptTerrainElevation(x: int, y: int, elevation: float)` → `Table<string, dynamic>`；[绑定实现 L3859](../../../../src/modules/rts/RTS.cpp#L3859)。
- `setUnitAutoResupply(subjectText: string, enabled: bool)` → `Table<string, dynamic>`；[绑定实现 L4802](../../../../src/modules/rts/RTS.cpp#L4802)。
- `setUnitCloaked(unitText: string, cloaked: bool)` → `Table<string, dynamic>`；[绑定实现 L4986](../../../../src/modules/rts/RTS.cpp#L4986)。
- `setUnitMovementPriority(subjectTexts: string, priority: int)` → `Table<string, dynamic>`；[绑定实现 L4689](../../../../src/modules/rts/RTS.cpp#L4689)。
- `setUnitStance(subjectTexts: string, stanceText: string, leashRange: float)` → `Table<string, dynamic>`；[绑定实现 L4679](../../../../src/modules/rts/RTS.cpp#L4679)。
- `setWorkerAutoAssignment(subjectTexts: string, enabled: bool)` → `Table<string, dynamic>`；[绑定实现 L4715](../../../../src/modules/rts/RTS.cpp#L4715)。
- `startMatch(matchText: string)` → `Table<string, dynamic>`；[绑定实现 L3727](../../../../src/modules/rts/RTS.cpp#L3727)。
- `startScriptConstruction(factionText: string, buildingSubjectText: string, definitionText: string, x: float, y: float, builderText: string)` → `Table<string, dynamic>`；[绑定实现 L3992](../../../../src/modules/rts/RTS.cpp#L3992)。
- `stopUnits(subjectTexts: string)` → `Table<string, dynamic>`；[绑定实现 L4996](../../../../src/modules/rts/RTS.cpp#L4996)。
- `suppressAreaUnits(subjectTexts: string, startX: float, startY: float, endX: float, endY: float, width: float, shotsPerUnit: int)` → `Table<string, dynamic>`；[绑定实现 L4658](../../../../src/modules/rts/RTS.cpp#L4658)。
- `surrenderMatch(matchText: string, factionText: string)` → `Table<string, dynamic>`；[绑定实现 L3737](../../../../src/modules/rts/RTS.cpp#L3737)。
- `unitCount()` → `dynamic`；[绑定实现 L3654](../../../../src/modules/rts/RTS.cpp#L3654)。
- `unloadTransport(transportText: string, x: float, y: float)` → `Table<string, dynamic>`；[绑定实现 L4964](../../../../src/modules/rts/RTS.cpp#L4964)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
