# 对话与剧情（Dialogue）

**脚本入口：** `eve.Dialogue()`

视觉小说式对话舞台：角色注册、打字机台词、选项分支、口型（lip-sync）、表情与
动作转发。**剧情继续用 Squirrel 写**（函数 / generator），不发明第二套 DSL。
文案可走 `i18n` 翻译表。

## 基本用法

```squirrel
dlg <- eve.Dialogue();
dlg.registerCharacter("alice", "爱丽丝");
dlg.bindAvatar("alice", aliceAv);       // Avatar 实例
dlg.show("alice", "left");              // 舞台槽位 left|center|right
dlg.say("alice", "你好，冒险者。");
dlg.update(dt);                         // 每帧驱动打字机
if (dlg.isWaitingAdvance()) dlg.advance();
```

## 目标导向指南

### 带选项的剧情分支

```squirrel
dlg.clearChoices();
dlg.addChoice("yes", "接受委托");
dlg.addChoice("no", "再想想");
dlg.presentChoices();
// 每帧：if (dlg.isWaitingChoice() && 按下1/2) dlg.selectChoice(index);
if (dlg.getSelectedChoiceId() == "yes") { /* 分支 A */ }
```

### 用 generator 写整段剧情

`dlg.say(...)` 后 `yield "wait"`，选项阶段 `yield "choice"`——驱动循环消费
generator 的 yield 值。完整可运行示例见
[`examples/dialogue`](../../../examples/dialogue/main.nut)（含 i18n + 口型 + Avatar）。

## API 快查

### `Dialogue`（模块）

- `getName()`：模块名（"Dialogue"）。
- 角色：`registerCharacter(id, displayName)`、`hasCharacter`、`getDisplayName`、
  `getCharacterCount`、`getCharacterId`、`bindAvatar(id, avatar)`、`getAvatar(id)`。
- 舞台：`show(id, slot)`、`hide`、`isShown`、`getSlot`、`setSlotX/getSlotX`、
  `setExpression`、`setMotion`、`syncStage()`。
- 台词：`say(speakerId, text)`、`narrate(text)`、`setTypeSpeed/getTypeSpeed`、
  `skipTyping`、`isTyping`、`isWaitingAdvance`、`isIdle`、`advance`、
  `getSpeakerId/getSpeakerName/getFullText/getVisibleText/getPhase`。
- 口型：`setLipSyncEnabled/isLipSyncEnabled`、`setLipSyncParameter/getLipSyncParameter`、
  `setLipSyncAmplitude/getLipSyncAmplitude/getLipSyncValue`。
- 选项：`clearChoices/addChoice(id, label)/presentChoices/isWaitingChoice`、
  `getChoiceCount/getChoiceId/getChoiceLabel/selectChoice/getSelectedChoiceId`。
- 变量：`setVar/getVarType/getVarInt/getVarFloat/getVarBool/getVarString/hasVar/clearVar/clearVars`。
- 条件：`registerCondition(name, fn)/unregisterCondition/evalCondition`。
- 台词池：`loadPoolsFromTable(table)`、`loadPoolsFromDnut(src)`、
  `loadPoolsFromDnutFile(path)`、`clearPools`、`getPoolCount/getPoolId/hasPool`、
  `getLastPoolsError`、`setRandomSeed/getRandomSeed`、`pickLine/playLine/playPool`、
  `getCurrentLineId/getCurrentLineMeta/getCurrentLineTags`。
- 驱动：`update(dt)`、`reset()`。

## 生命周期

- 角色注册幂等（重复注册覆盖显示名）；`bindAvatar` 接受 `avatar.newImageAvatar()`
  等任何 `AvatarInstance`。
- `update(dt)` 推进打字机/口型/舞台同步；选项选择前先 `isWaitingChoice()` 判空。
- `loadPoolsFromDnutFile` 解析 `.dnut`（剧本数据文件），错误看 `getLastPoolsError`。
