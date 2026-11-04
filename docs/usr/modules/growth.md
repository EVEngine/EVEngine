# 生长模拟模块

**脚本入口：** `eve.Growth()` / `eve.Organism`

Growth 是**数据驱动**的生命体仿真：物种 JSON 描述阶段、命名需求、产出、收获与繁殖策略。默认示例可以是地球农场（小麦 / 牛），同一套管线也能描述外星晶体裂变、黏菌出芽等完全不同的生育体系。

模块**不**绑定 hex 地块，也**不**直接改库存——收获返回 `{itemId, count}` 数组，由游戏脚本写入 `inventory`。

时间必须由调用方注入：`advanceDays` 或 `stepSeconds`（按 `secondsPerSimDay` 换成仿真日）。不要用墙钟推进。

## 配置原则

| 维度 | 配置位置 | 说明 |
| --- | --- | --- |
| 生命体标签 | `kind` | 任意字符串（`plant` / `animal` / `xeno.crystal`…）；**不**再硬编码分支 |
| 需求 | `needs[]` | 命名计量（`water` / `radiation` / `ether`…），含 drain / 下限 / 是否致死 |
| 阶段 | `stages[]` | 任意阶段 id + `durationDays` |
| 季节倍率 | `seasonMul` 或 `seasonMulById` | 对齐日历季节顺序或季节 id |
| 周期产出 | `produceIntervalDays` + `produceYield` | 任意生命体（奶、晶尘、孢子粉…） |
| 收获 | `harvestYield` | 成熟后一次性收获（任意生命体） |
| 繁殖 | `reproduction.mode` | `none` / `pair_gestation` / `self_bud` / `fission` / `spore_burst` |
| 日历 | `setSeasonLayoutFromJson` | 自定义季节名与天数（外星年周期） |

旧的 `waterDrainPerDay` / `canBreed` / `gestationDays` 仍可解析，并归一进 `needs` / `reproduction`。

## 注册物种（地球农场）

```squirrel
local growth = eve.Growth();
growth.resetWorld();
local reg = growth.registerSpeciesFromJson(@"[
  {
    ""id"": ""crop.wheat"",
    ""kind"": ""plant"",
    ""stages"": [
      {""id"": ""seed"", ""durationDays"": 1},
      {""id"": ""sprout"", ""durationDays"": 1},
      {""id"": ""ripe"", ""durationDays"": 1}
    ],
    ""waterDrainPerDay"": 100,
    ""fertilityDrainPerDay"": 10,
    ""seasonMul"": [1.0, 1.2, 0.8, 0.2],
    ""harvestYield"": [{""itemId"": ""item.wheat"", ""minCount"": 2, ""maxCount"": 2}]
  },
  {
    ""id"": ""animal.cow"",
    ""kind"": ""animal"",
    ""stages"": [
      {""id"": ""calf"", ""durationDays"": 2},
      {""id"": ""adult"", ""durationDays"": 1}
    ],
    ""waterDrainPerDay"": 50,
    ""foodDrainPerDay"": 50,
    ""produceIntervalDays"": 2,
    ""produceYield"": [{""itemId"": ""item.milk"", ""minCount"": 1, ""maxCount"": 1}],
    ""canBreed"": true,
    ""gestationDays"": 3
  }
]");
if (!reg.ok) throw reg.status.summary;
```

## 注册物种（外星：命名需求 + 裂变）

```squirrel
growth.registerSpeciesFromJson(@"[{
  ""id"": ""xeno.void_crystal"",
  ""kind"": ""xeno.crystal"",
  ""stages"": [
    {""id"": ""shard"", ""durationDays"": 1},
    {""id"": ""lattice"", ""durationDays"": 1}
  ],
  ""needs"": [
    {""id"": ""radiation"", ""drainPerDay"": 40, ""minPermille"": 100, ""lethalWhenEmpty"": true},
    {""id"": ""ether"", ""drainPerDay"": 20, ""minPermille"": 50}
  ],
  ""seasonMulById"": {""bloom"": 1.5, ""void"": 0.25},
  ""produceIntervalDays"": 2,
  ""produceYield"": [{""itemId"": ""item.crystal_dust"", ""minCount"": 1, ""maxCount"": 1}],
  ""harvestYield"": [{""itemId"": ""item.crystal_core"", ""minCount"": 1, ""maxCount"": 1}],
  ""reproduction"": {
    ""mode"": ""fission"",
    ""intervalDays"": 2,
    ""parentResetsStagesOnBirth"": true
  }
}]");
growth.setSeasonLayoutFromJson(@"[
  {""id"": ""bloom"", ""days"": 10},
  {""id"": ""void"", ""days"": 10}
]");
local crystal = growth.spawn("xeno.void_crystal");
crystal.refillNeed("radiation", 1000.0);
crystal.refillNeed("ether", 1000.0);
```

## 生成与照料

```squirrel
local crop = growth.plant("crop.wheat");       // kind == plant
local cow  = growth.spawnAnimal("animal.cow"); // kind == animal
local any  = growth.spawn("xeno.slime");       // 任意 kind
crop.waterBy(1000.0);
any.refillNeed("moisture", 1000.0);
print(any.needPermille("moisture"));
```

## 繁殖

- **pair_gestation**（哺乳类式）：`tryBreed(a, b)` → 妊娠 → `offspring` 事件
- **self_bud / fission / spore_burst**：成熟后按 `intervalDays` 自动繁殖，或 `tryReproduce(org)` 强制一次

```squirrel
growth.tryBreed(cowA, cowB);
growth.tryReproduce(crystal); // fission / self_bud / spore_burst
```

## 季节

默认春夏秋冬。`setDaysPerSeason(n)` 仍可用；外星年用 `setSeasonLayoutFromJson`。`seasonName()` 返回当前季节 **id**。

## 事件

`clearEvents` / `eventCount` / `eventKind` / `eventSpeciesId` / `eventDetail` 观察阶段变化、产出、疾病、繁殖等。
