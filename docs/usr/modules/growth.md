# 生长模拟模块

**脚本入口：** `eve.Growth()` / `eve.Organism`

Growth 是农场动植物的确定性仿真：物种定义、季节日历、需求（水/食/肥力）、阶段生长、疾病、繁殖与收获回执。模块**不**绑定 hex 地块，也**不**直接改库存——收获返回 `{itemId, count}` 数组，由游戏脚本写入 `inventory`。

时间必须由调用方注入：`advanceDays` 或 `stepSeconds`（按 `secondsPerSimDay` 换成仿真日）。不要用墙钟推进。

## 注册物种

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
    ""harvestYield"": [{""itemId"": ""item.wheat"", ""minCount"": 2, ""maxCount"": 2}],
    ""diseases"": [{""diseaseId"": ""blight"", ""chancePerDay"": 0.2, ""durationDays"": 2, ""growthMul"": 0.5}]
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

## 种植、浇水、收获

```squirrel
local crop = growth.plant("crop.wheat");
crop.waterBy(1000.0);
crop.fertilize(1000.0);
growth.advanceDays(3.0);
if (crop.isMature()) {
    local yield = crop.harvest();
    if (yield.ok) {
        foreach (line in yield.value) {
            print(line.itemId + " x" + line.count + "\n");
        }
    }
}
```

## 畜牧与繁殖

```squirrel
local a = growth.spawnAnimal("animal.cow");
local b = growth.spawnAnimal("animal.cow");
// 保持喂养并推进至成熟后：
growth.tryBreed(a, b);
growth.advanceDays(3.0); // 妊娠结束时产生 offspring 事件与新 Organism
```

## 季节

`setDaysPerSeason(n)` 后，`dayIndex` / `seasonName` / `year` 随 `advanceDays` 前进。季节倍率来自物种 `seasonMul`（春夏秋冬）。这与 `daynight` 表现时钟无关。

## 事件

`clearEvents` / `eventCount` / `eventKind` / `eventSpeciesId` / `eventDetail` 观察阶段变化、产出、疾病、繁殖等。
