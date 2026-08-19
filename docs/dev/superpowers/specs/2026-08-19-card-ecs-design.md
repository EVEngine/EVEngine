# 卡牌模块对齐引擎 ECS

日期：2026-08-19
状态：已实现（`feat/card-ecs`，`card.*` 7 项测试通过）

## 背景

`eve::card::Card` 用 `unique_ptr` 向量持有 `CardData` / `Hand` / `Deck` / `Zone`，是给 Squirrel 非拥有句柄用的所有权池。引擎其余模块（`Renderable2D`、`RPGActor`、`TileLayer`）已经是 `ecs::Entity`，可走 `ecs::View` 与 `eve.view`。卡牌实例应对齐这一模型。

## 目标

1. `CardData`、`Hand`、`Deck`、`Zone` 成为 `ecs::Entity`；生命周期由 ECS 表持有。
2. C++ 可 `ecs::View<CardData, CardData::Identity>()` 等查询。
3. 脚本 `eve.view(eve.CardData)` 通过 `registerCppEntityView` 扫到工厂创建的实例。
4. `examples/cardgame/main.nut` 与现有脚本 API 不变。

## 非目标

- 不把 `CardDefinition`、`LayoutConfig`、`CardEvent` 做成实体。
- 不把 `Hand::update` 拆成独立 System。
- 不让脚本类 `extends eve.Entity`。
- 一期不提供脚本 `destroy()`；从手牌/牌库移除不等于销毁实体。

## 实体与组件

C++ 类名保持 `CardData`（脚本类名已是 `CardData`，无需 `CardInstance` 别名）。

| 实体 | 组件 |
|---|---|
| `CardData` | `Identity`（id/name/kind）、`Stats`（cost/attack/health）、`Visual`（faceUp/disabled/tint/texture）、`Layout`（xywh/angle/scale/alpha）、`State`（phase/hovered/dragging） |
| `Hand` | `Meta`（owner/config/faceDown/peek/interactive）、`Membership`（有序 `vector<CardData*>`）、`Drag` |
| `Deck` | `Membership`（有序栈，顶在末尾） |
| `Zone` | `Rect`、`Filter`（acceptKinds） |

Hand/Deck 的成员列表必须有序：扇形下标与抽牌栈顶不能靠无序 `View`。

## 所有权

- 工厂 `newCard` / `newHand` / `newDeck` / `newZone` 走 `ecs::CreateEntity`，模块用 `ecs::EntityHandle` 记住创建顺序（供 `getHand(i)` 等）。
- `defs_`、`configs_`、`events_` 仍留在模块上。
- 模块析构时 `DestroyEntity` 自己工厂创建的实体。
- `removeCard` / `Deck::draw` 只改 Membership，不销毁实体。

## 每帧路径

`Card::update` / `render` / `renderDeck` 行为不变，遍历 live handle 而非 `unique_ptr`。布局/悬浮/拖拽仍在 `Hand::update`。

## 错误处理

与现在一致：未知 defId / 空牌库返回 `nullptr`；越界查询返回 `nullptr` 或空串。

## 测试

`test/card.cpp`：JSON 建牌、Hand/Deck 顺序、`View` 计数、remove 后仍在 View、模块析构后 View 回落、脚本 `eve.view(eve.CardData)`。
