// ============================================================================
// EVEngine Growth 模块示例 —— 作物成熟收获 + 畜牧产出/繁殖（带农场素材）
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
persist tex = null
persist floaters = []
persist showCalf = false
persist milkCount = 0
persist wheatCount = 0

function pushLog(text) {
    logLines.push(text);
    while (logLines.len() > 10)
        logLines.remove(0);
    print("[growth] " + text + "\n");
}

function loadTextures() {
    if (tex != null) return;
    tex = {};
    local files = {
        bg = "assets/bg/farm.png",
        plot = "assets/sprites/plot.png",
        barn = "assets/sprites/barn.png",
        fence = "assets/sprites/fence.png",
        wheat_seed = "assets/sprites/wheat_seed.png",
        wheat_sprout = "assets/sprites/wheat_sprout.png",
        wheat_ripe = "assets/sprites/wheat_ripe.png",
        wheat_stubble = "assets/sprites/wheat_stubble.png",
        cow_calf = "assets/sprites/cow_calf.png",
        cow_adult = "assets/sprites/cow_adult.png",
        cow_pregnant = "assets/sprites/cow_pregnant.png",
        item_wheat = "assets/sprites/item_wheat.png",
        item_milk = "assets/sprites/item_milk.png",
        sign = "assets/sprites/sign.png"
    };
    foreach (name, path in files)
        tex[name] <- gfx.newTextureFromFile(path);
}

function spawnFloater(kind, x, y) {
    local key = kind == "milk" ? "item_milk" : "item_wheat";
    floaters.push({
        key = key,
        x = x,
        y = y,
        life = 1.6,
        vy = -42.0
    });
}

function ensureSetup() {
    if (bootstrapped) return;
    loadTextures();
    if (!hudFont) {
        local fontData = eve.Font().newFontDataFromFile("assets/fonts/DejaVuSans-Bold.ttf", 20);
        hudFont = gfx.newFont(fontData,
            " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789=+-!./:[]()%_x");
    }
    gfx.setBackgroundColor(0.35, 0.55, 0.72, 1.0);

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

function meterText(v) {
    return "" + floor(v + 0.5);
}

function refreshStatus() {
    statusLines = [];
    statusLines.push("GROWTH DEMO");
    statusLines.push("day=" + growth.dayIndex() +
                     "  " + growth.seasonName() +
                     "  y" + growth.year());
    if (crop) {
        statusLines.push("wheat  " + crop.stageId() +
                         (crop.isHarvested() ? "  [cut]" : (crop.isMature() ? "  [ripe]" : "")));
        statusLines.push("  water " + meterText(crop.waterPermille()));
    }
    if (cowA) {
        statusLines.push("cowA  " + (cowA.isMature() ? "adult" : "calf") +
                         (cowA.isPregnant() ? "  preg" : ""));
        statusLines.push("  food " + meterText(cowA.foodPermille()));
    }
    if (cowB) {
        statusLines.push("cowB  " + (cowB.isMature() ? "adult" : "calf") +
                         (cowB.isPregnant() ? "  preg" : ""));
        statusLines.push("  food " + meterText(cowB.foodPermille()));
    }
    statusLines.push("bag  wheat x" + wheatCount + "  milk x" + milkCount);
    statusLines.push("events:");
    local start = logLines.len() > 5 ? logLines.len() - 5 : 0;
    for (local i = start; i < logLines.len(); ++i)
        statusLines.push(" " + logLines[i]);
}

function updateFloaters(dt) {
    local next = [];
    foreach (f in floaters) {
        f.life -= dt;
        f.y += f.vy * dt;
        f.vy += 18.0 * dt;
        if (f.life > 0.0) next.push(f);
    }
    floaters = next;
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
            wheatCount += line.count;
            spawnFloater("wheat", 250.0, 360.0);
            spawnFloater("wheat", 280.0, 340.0);
            spawnFloater("wheat", 220.0, 350.0);
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
            if (kind == "produced" && detail.find("milk") != null) {
                milkCount += 1;
                spawnFloater("milk", 620.0, 420.0);
            }
            if (kind == "offspring")
                showCalf = true;
        }
        growth.clearEvents();
    }
    updateFloaters(dt);
    refreshStatus();
};

function drawHudLine(text, x, y, r, g, b) {
    gfx.drawText(hudFont, text, x, y, r, g, b, 1.0, 0.9);
}

function blit(key, x, y, w, h) {
    gfx.drawTexturedRect(tex[key], x, y, w, h, 1.0, 1.0, 1.0, 1.0);
}

function wheatSpriteKey() {
    if (!crop) return "wheat_seed";
    if (crop.isHarvested()) return "wheat_stubble";
    local stage = crop.stageId();
    if (stage == "ripe") return "wheat_ripe";
    if (stage == "sprout") return "wheat_sprout";
    return "wheat_seed";
}

function cowSpriteKey(org) {
    if (!org) return "cow_calf";
    if (!org.isMature()) return "cow_calf";
    if (org.isPregnant()) return "cow_pregnant";
    return "cow_adult";
}

eve_render = function() {
    gfx.clear();
    if (tex == null) return;

    blit("bg", 0.0, 0.0, 960.0, 640.0);
    blit("barn", 40.0, 250.0, 192.0, 168.0);
    blit("fence", 220.0, 390.0, 120.0, 72.0);
    blit("fence", 330.0, 390.0, 120.0, 72.0);
    blit("fence", 440.0, 390.0, 120.0, 72.0);

    // Crop plot + stage sprite
    blit("plot", 180.0, 430.0, 192.0, 128.0);
    local wk = wheatSpriteKey();
    local wh = wk == "wheat_ripe" ? 144.0 : 120.0;
    blit(wk, 212.0, 430.0 - (wh - 96.0), 96.0, wh);
    blit("sign", 150.0, 520.0, 90.0, 63.0);

    // Pasture cows
    blit(cowSpriteKey(cowA), 560.0, 430.0, 144.0, 120.0);
    blit(cowSpriteKey(cowB), 720.0, 450.0, 144.0, 120.0);
    if (showCalf)
        blit("cow_calf", 640.0, 500.0, 108.0, 90.0);

    // Produce floaters
    foreach (f in floaters) {
        local a = f.life < 0.4 ? (f.life / 0.4) : 1.0;
        gfx.drawTexturedRect(tex[f.key], f.x, f.y, 48.0, 48.0, 1.0, 1.0, 1.0, a);
    }

    // Inventory chips
    blit("item_wheat", 24.0, 560.0, 48.0, 48.0);
    blit("item_milk", 24.0, 500.0, 48.0, 48.0);

    // HUD panel
    gfx.drawSolidRect(668.0, 16.0, 276.0, 360.0, 0.08, 0.12, 0.10, 0.82);
    gfx.drawSolidRect(668.0, 16.0, 276.0, 40.0, 0.16, 0.36, 0.24, 0.95);
    if (!hudFont || statusLines.len() == 0) return;
    local y = 28.0;
    for (local i = 0; i < statusLines.len(); ++i) {
        local r = i == 0 ? 0.95 : 0.90;
        local g = i == 0 ? 0.98 : 0.94;
        local b = i == 0 ? 0.80 : 0.82;
        drawHudLine(statusLines[i], 684.0, y, r, g, b);
        y += i == 0 ? 36.0 : 24.0;
    }
};
