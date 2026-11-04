// ============================================================================
// EVEngine Growth 模块示例 —— 作物成熟收获 + 畜牧产出/繁殖
//
// 运行： make run/<platform>-debug GAME=examples/growth
// ============================================================================

persist growth = null
persist crop = null
persist cowA = null
persist cowB = null
persist bootstrapped = false
persist logLines = []
persist harvested = false
persist breedTried = false

function pushLog(text) {
    logLines.push(text);
    while (logLines.len() > 10)
        logLines.remove(0);
    print("[growth] " + text + "\n");
}

function ensureSetup() {
    if (bootstrapped) return;
    growth = eve.Growth();
    growth.resetWorld();
    growth.setDaysPerSeason(28);
    growth.setSecondsPerSimDay(2.0);
    growth.setRngSeed(42);

    local reg = growth.registerSpeciesFromJson(@"[
      {
        ""id"": ""crop.wheat"",
        ""kind"": ""plant"",
        ""stages"": [
          {""id"": ""seed"", ""durationDays"": 1},
          {""id"": ""sprout"", ""durationDays"": 1},
          {""id"": ""ripe"", ""durationDays"": 1}
        ],
        ""waterDrainPerDay"": 80,
        ""fertilityDrainPerDay"": 20,
        ""minWaterPermille"": 100,
        ""minFertilityPermille"": 50,
        ""seasonMul"": [1.0, 1.1, 0.9, 0.35],
        ""harvestYield"": [{""itemId"": ""item.wheat"", ""minCount"": 3, ""maxCount"": 3}]
      },
      {
        ""id"": ""animal.cow"",
        ""kind"": ""animal"",
        ""stages"": [
          {""id"": ""calf"", ""durationDays"": 2},
          {""id"": ""adult"", ""durationDays"": 1}
        ],
        ""waterDrainPerDay"": 40,
        ""foodDrainPerDay"": 40,
        ""minWaterPermille"": 100,
        ""minFoodPermille"": 100,
        ""produceIntervalDays"": 2,
        ""produceYield"": [{""itemId"": ""item.milk"", ""minCount"": 1, ""maxCount"": 1}],
        ""canBreed"": true,
        ""gestationDays"": 3
      }
    ]");
    if (!reg.ok) throw reg.status.summary;

    crop = growth.plant("crop.wheat");
    cowA = growth.spawnAnimal("animal.cow");
    cowB = growth.spawnAnimal("animal.cow");
    if (!crop || !cowA || !cowB) throw "failed to create organisms";
    crop.waterBy(1000.0);
    crop.fertilize(1000.0);
    cowA.waterBy(1000.0); cowA.feed(1000.0);
    cowB.waterBy(1000.0); cowB.feed(1000.0);
    pushLog("planted wheat + two cows");
    bootstrapped = true;
}

function care() {
    if (crop && !crop.isDead()) {
        crop.waterBy(200.0);
        crop.fertilize(50.0);
    }
    if (cowA && !cowA.isDead()) { cowA.waterBy(120.0); cowA.feed(120.0); }
    if (cowB && !cowB.isDead()) { cowB.waterBy(120.0); cowB.feed(120.0); }
}

eve_update = function(dt) {
    ensureSetup();
    care();
    local step = growth.stepSeconds(dt);
    if (!step.ok) throw step.status.summary;

    if (crop && crop.isMature() && !harvested) {
        local harvestResult = crop.harvest();
        if (harvestResult.ok && harvestResult.value.len() > 0) {
            local line = harvestResult.value[0];
            pushLog("harvest " + line.itemId + " x" + line.count);
            harvested = true;
        }
    }

    if (!breedTried && cowA && cowB && cowA.isMature() && cowB.isMature()) {
        local bred = growth.tryBreed(cowA, cowB);
        if (bred.ok) {
            pushLog("breeding started");
            breedTried = true;
        }
    }

    local n = growth.eventCount();
    if (n > 0) {
        for (local i = 0; i < n; ++i) {
            local detail = growth.eventDetail(i);
            pushLog(growth.eventKind(i) + " " + growth.eventSpeciesId(i) +
                    (detail.len() > 0 ? (" " + detail) : ""));
        }
        growth.clearEvents();
    }
};

eve_render = function() {
    gfx.clear();
};
