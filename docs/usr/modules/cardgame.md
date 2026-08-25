# 卡牌游戏工具模块

**脚本入口：** `eve.Card()`

面向卡牌游戏（炉石 / MTGA / 杀戮尖塔风格）的通用 UI 工具集：扇形手牌布局、抽牌/洗牌、悬浮放大、拖拽到落牌区、敌方手牌背面与偷看、费用不足置灰，以及 UiCard 同款的可实时调节布局参数。参考 [ycarowr/UiCard](https://github.com/ycarowr/UiCard) 的功能面实现，以 C++ 模块提供（布局/交互/渲染均在本模块内完成）。

## 基本用法

```squirrel
local card = eve.Card();

// 注册卡牌类型定义
card.registerCardsFromJson(@"[
  {""id"":""flame"",""name"":""烈焰元素"",""kind"":""creature"",""cost"":2,""attack"":3,""health"":2,""tint"":[0.85,0.35,0.20]},
  {""id"":""fireball"",""name"":""火球术"",""kind"":""spell"",""cost"":3,""tint"":[0.95,0.55,0.25]}
]");

// 布局配置（UiCard 的 Configs）
local cfg = card.newConfig();
cfg.setHandX(config.width * 0.5);
cfg.setHandY(config.height - 80.0);
card.setConfig(cfg);

// 牌库 + 手牌 + 落牌区
local deck = card.newDeck();
deck.push(card.newCard("flame"));
deck.shuffle();

local ph = card.newHand(cfg);
ph.setOwner("player");

local eh = card.newHand(card.newConfig());
eh.setOwner("enemy");
eh.setFaceDown(true);        // 背面朝上
eh.setInteractive(false);    // 不可拖拽

card.newZone("play", "出牌区", config.width * 0.5 - 240, config.height * 0.32, 480, 170);
card.drawCard("player");     // 从牌库抽一张到手牌

// 每帧
card.update(dt, mouse.getX(), mouse.getY(), mouse.isDown(1) && !ui.wantCaptureMouse());
card.render(gfx);            // 画落牌区 + 所有手牌
card.renderDeck(gfx);        // 画牌库堆 + 剩余张数
```

## 对象关系与调用时机

`eve.Card()` 是单例模块，持有全部状态：卡牌类型定义表（`registerCardsFromJson`）、一份牌库（`newDeck`）、若干手牌（`newHand`）、若干落牌区（`newZone`）、若干布局配置（`newConfig`）。`new*` 返回的脚本对象是**非拥有句柄**——底层 C++ 对象由模块持有，脚本持有引用即可跨帧使用（不要手动释放）。

`update` 逐手牌推进布局与拖拽状态机，并把交互写入事件队列：`getEventType(i)` 为 `"click"`、`"drop"` 或 `"dropRejected"`，`getEventCardId(i)` / `getEventZone(i)` / `getEventHand(i)` / `getEventReason(i)` 给出对应字段；处理完用 `clearEvents()` 清空。拖拽命中落牌区后卡牌**仍留在手牌数组**，由脚本调用 `hand.removeCard(card)` 决定是否离手；不处理则自动归位。

自定义 2D 或 3D 表现可调用 `setBuiltInVisuals(false)`，然后每帧在 `update` 后调用 `capturePresentation()`。`getPresentation(i)` 返回只读布局快照，含实例/定义 ID、位置、尺寸、角度、缩放、透明度与交互状态。快照对象只在下一次 `capturePresentation()` 前有效，不要跨帧保存；用 `getInstanceId()` 关联自己的 Sprite/Renderable。

Card 不内置固定的卡牌编辑器。项目可用 `setCardDefinition` / `removeCardDefinition`
增删定义，用按 ID 排序的 `getCardDefinitionId` 枚举定义，再根据自己的 schema 动态组合
Inspector。编辑器和游戏由此直接消费同一份 Card 运行时数据；
[`examples/composable-editor/gameplay_components.nut`](../../../examples/composable-editor/gameplay_components.nut)
展示了用通用 `Schema`、`Definitions` 和 ECS 组合项目专属卡牌工具的做法。

把逻辑手牌映射到 3D 桌面时，用 `newPlaneMapper()` 创建映射器，`setLogicalRect()` 设置 Card 画布，`setPlane()` 设置世界 origin、全宽 U 向量和全高 V 向量。把 `Camera3D.screenToRay()` 的射线交给 `mapRay()`，随后将 `getLogicalX/Y()` 传给 `card.update()`；用 `mapLayout(snapshot.getX(), snapshot.getY())` 取得对应世界坐标。

## 目标导向指南

### 做一个炉石式手牌与出牌流程

建玩家手牌（`setInteractive(true)`），按 `card.getCost() > mana` 逐帧 `setDisabled(true)` 置灰；在事件循环里命中 `"drop"` 且 `getEventZone == "play"` 时检查费用、`hand.removeCard` 并扣法力，`"discard"` 时弃掉，`"hand"` 时不做任何事。

### 敌方手牌 / 偷看

`hand.setFaceDown(true)` 整手渲染为牌背；`hand.setPeek(true)` 时翻面显示正面。`setInteractive(false)` 防止拖走敌方牌。

### 实时调节布局（UiCard Configs 面板）

把 `LayoutConfig` 的 getter/setter 接到 ImGui 面板：`ui.slider` + `ui.consumeChange` + `ui.getValue`，每帧用 `ui.setValue` 同步。可调：`spacing`、`arcHeight`、`rotationAngle`、`hoverRotation`、`hoverScale`、`hoverLift`、`hoverSpeed`、`motionSpeed`、`disabledAlpha`、`handY`。

## 常见问题

- 卡牌文字：C++ 模块会使用 Graphics 当前字体绘制文字；未设置字体时静默跳过文字，只画色块/宝石。字体资源由宿主渲染初始化流程管理。
- `mouse.isDown(1)` 是左键；面板悬浮时先查 `ui.wantCaptureMouse()`，避免把 UI 操作误当卡牌拖拽。
- 布局为“中心锚定”的抛物线扇形（两端上翘），`rotationAngle` 控制两端最大旋转角；`hoverRotation` 控制悬浮时是否转正。
- `Zone.acceptKinds` 通过 `addAcceptKind` 设置后会参与实际 Drop 判定，不匹配的卡牌不会产生 drop 事件。
- `newCard(defId)` 返回的实例 id 形如 `defId#N`（保证唯一）；卡牌定义用 `getCardDefinition*` 系列读取。

## API 快查

- `eve.Card()`：`registerCardsFromJson`、`clearCardDefinitions`、`getCardDefinitionCount`、`getCardDefinitionId`、`hasCardDefinition`、`setCardDefinition`、`removeCardDefinition`、`getCardDefinitionName/Kind/Cost/Attack/Health/TintR/TintG/TintB`
- `eve.Card()` 工厂：`newConfig()`、`newCard(defId)`、`newDeck()`、`newZone(id, label, x, y, w, h)`、`newHand(cfg)`
- `eve.Card()` 空间适配：`newPlaneMapper()`；`CardPlaneMapper.setLogicalRect/setPlane/mapRay/mapLayout/getLogicalX/getLogicalY/getWorldX/getWorldY/getWorldZ`
- `eve.Card()` 状态：`setConfig`/`getConfig`、`handCount`/`getHand`/`findHand`、`zoneCount`/`getZone`、`getDeck`、`drawCard(handOwner)`
- `eve.Card()` 每帧：`update(dt, mx, my, down)`、`render(gfx)`、`renderDeck(gfx)`、`set/getBuiltInVisuals`
- `eve.Card()` 表现快照：`capturePresentation`、`getPresentationCount`、`getPresentation(index)`
- `eve.Card()` 事件：`clearEvents`、`getEventCount`、`getEventType`、`getEventHand`、`getEventZone`、`getEventCardId`、`getEventReason`
- `eve.Card()` 目标选择：`beginTargeting`、`updateTargeting`、`cancelTargeting`、`renderTargeting`、`isTargeting`、`isTargetValid`、`getTargetSource`、`getTargetId`
- `LayoutConfig`：`get/setCardW`、`get/setCardH`、`get/setSpacing`、`get/setHandX`、`get/setHandY`、`get/setArcHeight`、`get/setRotationAngle`、`get/setHoverRotation`、`get/setHoverScale`、`get/setHoverLift`、`get/setHoverSpeed`、`get/setMotionSpeed`、`get/setDisabledAlpha`、`get/setShowZones`、`get/setDeckX`、`get/setDeckY`、`get/setDragThreshold`
- `CardData`：`getId/setId`（兼容接口）、`getInstanceId/getDefinitionId`、`getName/setName`、`getKind/setKind`、`getCost/setCost`、`getAttack/setAttack`、`getHealth/setHealth`、`isFaceUp/setFaceUp`、`isDisabled/setDisabled`、`getState/setState`、`getTintR/G/B`、`setTint(r, g, b)`、`setArt/getArt`、`getX/getY/getW/getH/getAngle/getScale/getAlpha`、`isHovered/isDragging`、`hit(px, py)`、`describe()`
- `CardPresentationSnapshot`：`getInstanceId/getDefinitionId/getState`、`getX/getY/getW/getH/getAngle/getScale/getAlpha`、`isHovered/isDragging/isDisabled/isFaceUp`
- `Deck`：`push`、`draw`、`peek`、`count`、`isEmpty`、`clear`、`shuffle`、`getCard`
- `Zone`：`getId/setId`、`getLabel/setLabel`、`getX/getY/getW/getH`、`setRect`、`getColorR/G/B`、`setColor(r, g, b)`、`getAlpha/setAlpha`、`isEnabled/setEnabled`、`addAcceptKind`、`clearAcceptKinds`、`accepts`、`contains`、`render`
- `Hand`：`getOwner/setOwner`、`getConfig/setConfig`、`isFaceDown/setFaceDown`、`isPeek/setPeek`、`isInteractive/setInteractive`、`addCard`、`removeCard`、`clear`、`count`、`getCard`、`findCard`、`pickCard`、`render`

## 使用要点

- `eve.Card()`、`LayoutConfig`、`Hand`、`Deck`、`Zone`、`CardData` 实例都保存在全局或实体状态中，`eve_init` 里只建一次，不要每帧重建。
- 一个 `Hand` 使用一份独立的 `LayoutConfig`（如敌方手牌新建一份并调 `setHandY`），不要共享后改字段。
- `hand.addCard` 会把牌从 0 缩放平滑生长到槽位，形成“出牌”动画；拖拽未落入任何落牌区时自动归位。

**源码：** [`src/modules/card/`](../../../src/modules/card/)  
**示例：** [`examples/cardgame/`](../../../examples/cardgame/)  
**运行：** `make run/<platform>-debug GAME=examples/cardgame`
