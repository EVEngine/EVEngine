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
persist hudFont = null
persist statusLines = []

function pushLog(text) {
    logLines.push(text);
    while (logLines.len() > 10)
        logLines.remove(0);
    print("[growth] " + text + "\n");
}

function ensureSetup() {
    if (bootstrapped) return;
    if (!hudFont) {
        local fontData = eve.Font().newFontDataFromFile("assets/fonts/DejaVuSans-Bold.ttf", 22);
        hudFont = gfx.newFont(fontData,
            " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789=+-!./:[]()%_x");
    }
    gfx.setBackgroundColor(0.10, 0.14, 0.12, 1.0);

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
    // Quiet maintenance: do not spam lifecycle events into the HUD log.
    if (crop && !crop.isDead()) {
        crop.waterBy(200.0);
        crop.fertilize(50.0);
    }
    if (cowA && !cowA.isDead()) { cowA.waterBy(120.0); cowA.feed(120.0); }
    if (cowB && !cowB.isDead()) { cowB.waterBy(120.0); cowB.feed(120.0); }
}

function meterText(v) {
    // Script bindings project meters as float (not double userdata).
    return "" + floor(v + 0.5);
}

function refreshStatus() {
    statusLines = [];
    statusLines.push("GROWTH DEMO  day=" + growth.dayIndex() +
                     "  season=" + growth.seasonName() +
                     "  year=" + growth.year());
    if (crop) {
        statusLines.push("wheat  stage=" + crop.stageId() +
                         "  mature=" + crop.isMature() +
                         "  harvested=" + crop.isHarvested() +
                         "  water=" + meterText(crop.waterPermille()));
    }
    if (cowA) {
        statusLines.push("cowA   mature=" + cowA.isMature() +
                         "  pregnant=" + cowA.isPregnant() +
                         "  food=" + meterText(cowA.foodPermille()));
    }
    if (cowB) {
        statusLines.push("cowB   mature=" + cowB.isMature() +
                         "  pregnant=" + cowB.isPregnant() +
                         "  food=" + meterText(cowB.foodPermille()));
    }
    statusLines.push("events (latest):");
    local start = logLines.len() > 6 ? logLines.len() - 6 : 0;
    for (local i = start; i < logLines.len(); ++i)
        statusLines.push("  " + logLines[i]);
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
            local kind = growth.eventKind(i);
            if (kind == "watered" || kind == "fed" || kind == "fertilized")
                continue;
            local detail = growth.eventDetail(i);
            pushLog(kind + " " + growth.eventSpeciesId(i) +
                    (detail.len() > 0 ? (" " + detail) : ""));
        }
        growth.clearEvents();
    }
    refreshStatus();
};

function drawHudLine(text, x, y, r, g, b) {
    gfx.drawText(hudFont, text, x, y, r, g, b, 1.0, 0.85);
}

eve_render = function() {
    gfx.clear();
    // Panel + accent strip (bright enough to verify Lavapipe readback).
    gfx.drawSolidRect(24.0, 24.0, 912.0, 592.0, 0.08, 0.16, 0.12, 1.0);
    gfx.drawSolidRect(24.0, 24.0, 912.0, 56.0, 0.18, 0.42, 0.28, 1.0);
    gfx.drawSolidRect(24.0, 80.0, 8.0, 536.0, 0.45, 0.78, 0.40, 1.0);

    if (!hudFont || statusLines.len() == 0) return;
    local y = 38.0;
    for (local i = 0; i < statusLines.len(); ++i) {
        local r = i == 0 ? 0.92 : 0.86;
        local g = i == 0 ? 0.98 : 0.94;
        local b = i == 0 ? 0.82 : 0.78;
        drawHudLine(statusLines[i], 48.0, y, r, g, b);
        y += i == 0 ? 42.0 : 28.0;
    }
};
