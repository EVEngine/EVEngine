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

### `DialogueUX`（默认呈现辅助）

构建包含 `dialogue` 时，根表同时提供 `dialogueUX`。它不强制使用某一种 UI，
但为官方 `DefaultDialogueUI` 和自定义 UI 保存一致的呈现状态：

- 历史：`record`、`clearHistory`、`getHistoryCount`、
  `getHistoryLineId/getHistorySpeaker/getHistoryText`。
- 富文本：`plainText` 去除 `[pause]`、`[speed]`、`[color]`、`[shake]` 控制标签；
  `getTextActionCount` 返回控制动作数。
- 自动播放：`setAutoMode/isAutoMode`、`setAutoDelay/getAutoDelay`、
  `updateAuto(dt, voicePlaying)`、`resetAutoTimer`。
- 跳过：`setSkipMode("off"|"read"|"all")`、`getSkipMode`、
  `markRead/isRead/shouldSkip`。

`make_default_dialogue_ui(dialogue, dialogueUX, ui)` 创建可直接挂载的默认 UI；
调用返回对象的 `mount()`、每帧 `update(dt)` 和 `render()` 即可。项目可以完全替换
这个脚本视图，而继续复用 `DialogueUX` 的历史、已读和自动推进状态。

### `DialogueVoice`（语音与音频口型）

根表 `dialogueVoice` 管理按 `lineId + locale` 定位的语音。项目先用
`sound.newSoundDataFromFile` 与 `audio.newSource` 创建 Source，再调用
`bindSource(lineId, locale, source)`；无音频设备的测试也可用
`registerClip(lineId, locale, duration, envelopeCsv)` 注册时长和振幅轨。

- 资产：`registerClip`、`bindSource`、`hasClip`、`clear`。
- 播放：`play(lineId, locale)`、`stop`、`update(dt)`、`isPlaying`、
  `getTime/getDuration/getCurrentLineId`。
- 自动推进：`shouldAutoAdvance()` 在语音达到结尾后返回 true。
- 口型：`getAmplitude()` 读取预计算振幅包络，采样率由
  `setEnvelopeRate/getEnvelopeRate` 控制。可把返回值传给 Avatar 参数。

locale 精确匹配失败时回退到空 locale 的默认语音。Source 的生命周期仍由游戏持有，
切换台词或销毁 `DialogueVoice` 时会停止当前 Source。

### `DialogueFlow`（参数化流程与内容工具）

`dialogueFlow` 将 `.dnut` 中的 `conversation` 块编译为带稳定 node ID 的资产，并用
可保存的显式 Runner 执行。台词池与 conversation 块可以共存于同一文件。

```dnut
conversation common.greeting version=1 entry=decide params=speaker,listener,location
node decide branch
when score(speaker, listener, location) >= threshold(speaker.personality) -> friendly
else -> formal
node friendly line speaker=speaker pool=greeting.friendly i18n=dialogue.greeting.friendly next=end
node formal line speaker=speaker text="Good day, {listener.name}." next=end
node end end
endconversation
```

- 内容：`loadFromDnut/loadFromDnutFile`、`importYarn/importTwee`、`clear`、
  `getConversationCount()`、`getConversationId(index)`、`hasConversation`、`getLastError`。
- 外部格式：Yarn Spinner 节点和 Twine Twee 3 passages 会转换为相同的稳定节点模型；
  角色前缀、参数占位符、双向 Twine 链接、Yarn shortcut options、`jump/stop/wait/call/set`
  命令以及 `#line:/#voice:` 标签都会保留，并进入统一的引用校验流程。Twee 的
  `StoryTitle/StoryData` 元数据 passages 会自动忽略。
- 校验：`getDiagnosticCount`、`getDiagnosticSeverity/getDiagnosticMessage`；编译检查
  重复或缺失 ID、无效引用，并报告不可达节点。
- 工具链：相同 path、相同内容的 `loadFromDnut` 会命中内存增量缓存，
  `getLastLoadChanged()` 可判断是否重编译；`removeSource(path)` 卸载该来源产生的资产；
  `lintAll()` 批量检查全库并验证跨文件 call；`renameConversation/renameNode` 自动改写引用，
  同时提升资产版本以显式拒绝不兼容的旧执行游标。
- 热重载：`reloadFromDnut(source, path)` 在临时工作区编译并执行全库跨文件引用校验；
  编译、引用校验或活动游标迁移任一失败时，会保留旧资产和旧执行位置。成功时则结合
  `registerMigration` 恢复当前对话和全部调用栈。普通 `loadFromDnut` 仍允许按任意顺序初次
  装入互相引用的文件，全部装入后用 `lintAll()` 做一次完整校验。
- 本地化：`exportLocalizationCsv()` 返回带 conversation/node 稳定 ID、i18n key、
  speaker、源文和 voice key 的 RFC4180 CSV。
- 回导与配音：`importLocalizationCsv(csv, defaultLocale)` 接受 `i18n_key/locale/translation`
  以及可选的 `voice/status/duration` 列；`setLocale/getLocale` 控制当前语言，读取当前
  `getText/getVoice` 时按精确 locale、语言代码、默认 locale 依次回退。
  `exportMissingLocalizationCsv(locale)` 生成待翻译清单，
  `exportVoiceRecordingCsv(locale)` 生成并可再次回导的录音清单，包含角色、源文、译文、
  voice key、录制状态和时长；当前行可用 `getVoiceStatus/getVoiceDuration` 读取回导结果，
  将时长直接交给 `dialogueVoice.registerVoice` 即可驱动语音结束自动推进。
- 执行：`start(id, bindings)`、`advance`、`select(routeId)`、`isActive/isBlocked`、
  `getActiveConversationId/getNodeId/getNodeKind`。
- 当前节点：`getSpeaker/getText/getPool/getI18nKey/getVoice`、
  `getRouteCount/getRouteId`。
- 复杂条件：`setExpressionEvaluator(fn)` 注册纯计算函数，fn 接收
  `{ expression, bindings, locals }` 并返回 bool 或结构化值；
  `clearExpressionEvaluator` 解除注册。表达式文本不会被编译器限制成简单比较式。
- 脚本存档：`captureStateJson()` 返回可直接交给项目存档系统的 JSON；
  `restoreStateJson(json)` 恢复当前节点、参数、局部值和完整的子对话调用栈。
  资产升级后用 `registerMigration(oldAssetId, oldVersion, currentAssetId, nodeMap)`
  显式登记迁移，其中 `nodeMap` 是逗号分隔的 `old:new` 对；未改名的稳定节点无需列出。
  迁移会事务式覆盖当前帧和全部调用帧，任何缺失规则或目标节点都会拒绝恢复；
  `clearMigrations()` 清除规则。
- 参数化文本：本地化回退完成后，`getText()` 会以 locals 优先、bindings 次之的顺序
  渲染 `{speaker.name}` 等路径；支持 `{path??fallback}`，以及 `|upper`、`|lower`、
  `|capitalize` 修饰。找不到且没有 fallback 的占位符会原样保留，便于内容 QA 发现缺参。
- 人物语气：`addToneRule(expression, prefix, suffix, find, replacement)` 添加有序规则；
  expression 复用 `setExpressionEvaluator`，因此可以读取人物性格、关系、疲劳或剧情状态，
  对渲染后的文本加前后缀或做词语替换。多个命中规则依次叠加，
  `clearToneRules()` 可在场景或角色配置切换时清空。

Runner 在 line、choice、wait 和异步 command 边界暂停；C++ API 的
`captureState/restoreState` 保存资产版本、node ID、bindings、locals 和子对话调用栈。

## 生命周期

- 角色注册幂等（重复注册覆盖显示名）；`bindAvatar` 接受 `avatar.newImageAvatar()`
  等任何 `AvatarInstance`。
- `update(dt)` 推进打字机/口型/舞台同步；选项选择前先 `isWaitingChoice()` 判空。
- `loadPoolsFromDnutFile` 解析 `.dnut`（剧本数据文件），错误看 `getLastPoolsError`。
