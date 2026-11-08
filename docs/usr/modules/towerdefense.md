# 塔防组合 — `towerdefense`

**脚本入口：** `eve.TowerDefense()`，槽位 `towerdefense`。

## 这篇解决什么

- **问题**：塔防需要确定性波次出怪、基地漏怪扣命，并与 `map` 寻路、`building` 放置、`sensing` 选敌、`economy` 账本组合。
- **入口**：`newDirector()` / `WaveDirector.loadScheduleJson` + `advance(tick, dt)`；基地用 `newBase(lives)`。
- **不要**：在本模块里复制伤害、寻路或建筑占用；路径是否被塔封死用 `Pathfinder.keepsPathOpen*`；选敌用 `SensingWorld.circleSorted` 的 `first|last|strongest|weakest|closest`。
- **相关**：[`map`](map.md) 寻路、[`building`](building.md) 放置、[`sensing`](sensing.md) 选敌、[`economy`](economy.md) 金币。

## 波次导演

```squirrel
local td = eve.TowerDefense();
local director = td.newDirector();
local loaded = director.loadScheduleJson(@"[
  {""id"":""w1"",""prepSeconds"":3.0,""rewardGold"":25,
   ""entries"":[{""archetypeId"":""creep.basic"",""count"":5,""intervalSeconds"":0.6}]}
]");
if (!loaded.ok) throw loaded.status.summary;
if (!director.start().ok) throw "cannot start";

persist tick = 0
eve_update <- function(dt) {
    tick += 1;
    local step = director.advance(tick, dt);
    if (!step.ok) throw step.status.summary;
    local frame = step.value;
    for (local i = 0; i < frame.spawnCount; ++i) {
        local spawn = frame.spawns[i];
        // 游戏负责在 spawn 点创建单位：spawn.archetypeId
    }
};
```

`advance` 要求 **严格递增的仿真 tick** 与非负 `dt`。生命周期事件：`waveStarted` / `waveCompleted`（含 `rewardGold`）/ `scheduleCompleted` / `failed`。`idle` / `completed` / `failed` 上再次 `advance` 返回空结果（仍 `ok`），便于游戏循环继续轮询。

## 基地生命

```squirrel
local base = td.newBase(20);
local left = base.leak(1);
if (!left.ok) throw left.status.summary;
if (base.isDefeated()) print("defeat\n");
```

## 放置时保持路径

建塔前探测 spawn→goal 是否仍可达（不永久改网格）：

```squirrel
local open = pathfinder.keepsPathOpenFootprint(spawnX, spawnY, goalX, goalY, cellX, cellY, 1, 1);
if (!open.ok || !open.value) { /* 拒绝放置 */ }
```

## 炮塔选敌策略

对敌人调用 `sensing.setMetrics(id, pathProgress, strength)`，再用：

```squirrel
world.circleSorted(tx, ty, range, "creep", "", "", "", "", 1, "first");
```

策略名：`closest` / `furthest` / `first` / `last` / `strongest` / `weakest`。
