# 战舰系统模块

**这篇解决什么**

- 需要**战舰 / 帆船 / 航母 / 星际战舰**这类**巨型载具**：尺度远大于 `vehicle`，内部舱段可重新排布并改变舰船能力。
- 入口：`eve.Warship()`。观察实体：`eve.view(eve.WarshipEntity)`。
- 不要用本模块替代 `vehicle` 的轮胎/座位/炮塔 FPS 面；也不要用 ArchSpace 冒充战舰玩法权威——ArchSpace 可后续做室内可视化投影。

Warship 拥有船体定义、舱段功能目录、布局权威与简易机动。挂点火力、海洋渲染、物理刚体留给与 `weapon` / `fluids` / `physics` 的组合。

完整示例见 `examples/warship`（表现层用仓库 `assets/models/type055` 做自定义
3D 视图；仿真仍只走本模块）。设计见 `docs/dev/通用战舰系统设计.md`。

## 快速用法

```squirrel
local warship = eve.Warship();

local fn = warship.registerFunctionsFromJson(@"[
  {""id"":""bridge"",""powerDraw"":2,""crewCapacity"":8},
  {""id"":""reactor"",""powerProvide"":12},
  {""id"":""engine"",""powerDraw"":4,""thrustBonus"":0.25},
  {""id"":""hangar"",""powerDraw"":3,""hangarCapacity"":6}
]");
if (!fn.ok) throw fn.status.summary;

local hulls = warship.registerHullsFromJson(@"[
  {""id"":""destroyer"",""category"":""warship"",""mobility"":""surface"",
   ""length"":140,""beam"":24,""maxSpeed"":48,""maxHealth"":900,
   ""grid"":{""cols"":4,""rows"":2,""decks"":1},
   ""defaultLayout"":[
     {""deck"":0,""row"":0,""col"":0,""function"":""bridge""},
     {""deck"":0,""row"":0,""col"":1,""function"":""reactor""},
     {""deck"":0,""row"":1,""col"":0,""function"":""engine""}
   ]}
]");
if (!hulls.ok) throw hulls.status.summary;

local ship = warship.newWarship("destroyer", 80.0, 200.0, 0.0, "blue");
warship.moveTo(ship, 700.0, 200.0);

// 改装机库：失败时布局不变
local place = warship.placeCompartment(ship, 0, 1, 1, "hangar");
if (!place.ok) print(place.status.summary + "\n");

warship.update(dt);
print("power=" + warship.getPowerNet(ship) + " hangar=" + warship.getHangarCapacity(ship) + "\n");
```

## 绑定与行为说明

### 目录与生成

| API | 说明 |
|---|---|
| `registerHullsFromJson` / `registerFunctionsFromJson` | 注册船体或舱段功能；返回 Result（`ok` + `value` 计数）。 |
| `clearHullDefinitions` / `clearFunctionDefinitions` | 清空目录（测试/演示）。 |
| `hasHullDefinition` / `hasFunctionDefinition` | 查询是否已注册。 |
| `getHullDefinitionCategory` / `getHullDefinitionMobility` | 查询模板元数据。 |
| `newWarship(defId,x,y,heading,faction)` | 生成舰体并应用 `defaultLayout`；未知模板返回 null。 |

### 舱段重组

| API | 说明 |
|---|---|
| `placeCompartment(ship,deck,row,col,functionId)` | 放置/替换一格功能。 |
| `clearCompartment` / `swapCompartments` | 清空或交换两格。 |
| `rearrangeFromJson(ship,jsonArray)` | 原子替换整船布局；失败不改写。 |
| `getCompartmentFunction` / `getGridCols|Rows|Decks` | 查询格子与网格尺寸。 |
| `getPowerNet` / `getThrustMul` / `getHangarCapacity` / `getWeaponBayCount` / `getCrewCapacity` / `getSailPower` / `isUnderpowered` | 读取布局投影能力。 |

### 机动与状态

| API | 说明 |
|---|---|
| `moveTo` / `stop` / `setInput` / `update(dt)` | 命令移动、停车、直接舵令与步进。 |
| `getX/Y/Heading/Speed` / `getLength/Beam` | 位置与船体尺度。 |
| `getHealth` / `applyDamage` / `isDestroyed` | 生命值。 |
| `getEventCount` / `getEventType` / `clearEvents` | 轮询 `arrived` / `layout_changed` / `damaged` / `destroyed` / `underpowered`。 |

## 生命周期与所有权

- Warship 拥有舰体 ECS 状态与舱段布局权威；不拥有武器、物理世界或 ArchSpace 文档。
- 推荐固定步长调用 `update(dt)`。
- 消费完事件再 `clearEvents()`；不要在 `eve.view` 遍历中创建或销毁舰体。

## 船体 category / mobility 约定

| category | 典型用途 |
|---|---|
| `warship` | 水面作战舰 |
| `sailship` | 帆船 / 风帆时代舰 |
| `carrier` | 航母 / 载机平台 |
| `starship` | 星际舰艇 |

| mobility | 行为 |
|---|---|
| `surface` | 默认水面机动 |
| `sail` | 受 `sailPower` 加成 |
| `carrier` | 更慢、转向更钝 |
| `space` | 更高加减速 |
