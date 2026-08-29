// ============================================================================
// EVEngine Inventory 模块示例 —— 背包拾取 / 整理 / 装备
//
// 演示 eve.Inventory()：
//   物品定义  —— JSON 注册药水、矿石、武器
//   Bag       —— 主背包 + 任务栏（acceptTags）
//   操作      —— 拾取、拆分、转移、丢弃
//   Equipment —— 武器槽穿脱
//
// 按键：1 拾取战利品  2 喝药  3 装备/卸下武器  4 矿石进仓库  R 重置
// 运行： make run/<platform>-debug GAME=examples/inventory
// ============================================================================

persist inv = null
persist bag = null
persist questBag = null
persist stash = null
persist eq = null
persist logLines = []
persist prevKeys = {}
persist lootLeft = 0
persist uiBuilt = false

function pushLog(text) {
    logLines.push(text);
    while (logLines.len() > 2)
        logLines.remove(0);
}

function registerItems() {
    inv.registerItemsFromJson(@"[
      {""id"":""potion.hp"",""displayName"":""治疗药水"",""maxStack"":10,""weight"":0.3,""tags"":[""consumable"",""potion""]},
      {""id"":""ore.iron"",""displayName"":""铁矿"",""maxStack"":50,""weight"":1.0,""tags"":[""material""]},
      {""id"":""sword.iron"",""displayName"":""铁剑"",""maxStack"":1,""weight"":3.5,
       ""equipSlot"":""weapon"",""tags"":[""weapon"",""melee""]},
      {""id"":""quest.letter"",""displayName"":""密信"",""maxStack"":1,""weight"":0.1,""tags"":[""quest""]}
    ]");
}

function resetRun() {
    if (bag) bag.destroy();
    if (questBag) questBag.destroy();
    if (stash) stash.destroy();
    bag = inv.newBag(12);
    bag.setId("player");
    bag.setKind("backpack");
    bag.setMaxWeight(30.0);
    bag.addRejectTag("quest");

    questBag = inv.newBag(4);
    questBag.setId("quest");
    questBag.setKind("quest");
    questBag.addAcceptTag("quest");

    stash = inv.newBag(20);
    stash.setId("stash");
    stash.setKind("chest");
    stash.setMaxWeight(100.0);

    if (!eq) {
        eq = inv.newEquipmentSet();
        eq.setId("eq");
        eq.defineSlot("weapon");
        eq.addSlotAllowedTag("weapon", "weapon");
    } else if (!eq.isSlotEmpty("weapon")) {
        eq.clearSlot("weapon");
    }

    bag.addItem("potion.hp", 3);
    lootLeft = 5;
    logLines = [];
    pushLog("冒险开始：背包里有 3 瓶药水。");
}

function keyPressed(name) {
    return key_just_pressed(name);
}

function refreshHud() {
    if (!uiBuilt) return;
    local weapon = eq.isSlotEmpty("weapon") ? "-" : eq.getSlotItemId("weapon");
    ui.setText("stats", "背包 " + bag.getUsedSlotCount() + "/" + bag.getSlotCount() +
        "  负重 " + bag.getUsedWeight() + "/" + bag.getMaxWeight() +
        "  任务密信 " + questBag.countItem("quest.letter") +
        "  仓库铁矿 " + stash.countItem("ore.iron") +
        "  武器 " + weapon);
    ui.setText("help", "1 拾取  2 喝药  3 装备  4 存矿  R 重置");
    local logText = "";
    foreach (line in logLines)
        logText += line + "\n";
    ui.setText("log", logText);
}

eve_init = function() {
    gfx.setBackgroundColor(0.10, 0.12, 0.16, 1.0);
    if (inv == null) {
        inv = eve.Inventory();
        registerItems();
        resetRun();
    } else {
        registerItems();
    }

    if (!uiBuilt) {
        ui.beginBuild();
        ui.beginWindow("InventoryDemo", "root");
        ui.text("背包系统示例", "title");
        ui.text("", "stats");
        ui.text("", "help");
        ui.text("", "log");
        ui.end();
        ui.mountBuildAs("hud");
        ui.select("hud");
        ui.setHostOverlay(true);
        ui.setHostPos(14.0, 14.0, 0.0, 0.0);
        uiBuilt = true;
    }
    refreshHud();
};

eve_reload <- function() {
    registerItems();
};

eve_update = function(dt) {
    if (keyPressed("r") || keyPressed("R")) {
        resetRun();
        refreshHud();
        return;
    }

    if (keyPressed("1")) {
        if (lootLeft <= 0) {
            pushLog("地上已经没有战利品了。");
        } else {
            local addedSword = bag.addItem("sword.iron", 1);
            local addedOre = bag.addItem("ore.iron", 8);
            local addedQuest = questBag.addItem("quest.letter", 1);
            lootLeft -= 1;
            pushLog("拾取：剑x" + addedSword + " 矿x" + addedOre +
                    " 密信x" + addedQuest + "（剩余堆 " + lootLeft + "）");
        }
    }

    if (keyPressed("2")) {
        if (bag.countItem("potion.hp") <= 0)
            pushLog("没有药水了。");
        else {
            bag.removeItem("potion.hp", 1);
            pushLog("喝下一瓶治疗药水。");
        }
    }

    if (keyPressed("3")) {
        if (!eq.isSlotEmpty("weapon")) {
            if (eq.unequipToBag("weapon", bag))
                pushLog("卸下武器。");
            else
                pushLog("背包满了，卸不下。");
        } else {
            local slot = bag.findItem("sword.iron");
            if (slot < 0)
                pushLog("背包里没有铁剑。");
            else if (eq.equipFromBag("weapon", bag, slot))
                pushLog("装备铁剑。");
            else
                pushLog("无法装备。");
        }
    }

    if (keyPressed("4")) {
        local n = inv.transferItem(bag, stash, "ore.iron", 20);
        if (n > 0)
            pushLog("存入仓库铁矿 x" + n);
        else
            pushLog("没有可存的铁矿，或仓库满了。");
    }

    refreshHud();
};

eve_render = function() {
    gfx.clear();

    local cols = 4;
    local cell = 48.0;
    local ox = 40.0;
    local oy = 220.0;
    for (local i = 0; i < bag.getSlotCount(); i += 1) {
        local x = ox + (i % cols) * (cell + 8.0);
        local y = oy + (i / cols).tointeger() * (cell + 8.0);
        if (bag.isSlotEmpty(i))
            gfx.drawSolidRect(x, y, cell, cell, 0.20, 0.22, 0.28, 1.0);
        else
            gfx.drawSolidRect(x, y, cell, cell, 0.35, 0.55, 0.40, 1.0);
    }

    if (eq.isSlotEmpty("weapon"))
        gfx.drawSolidRect(320.0, 220.0, 64.0, 64.0, 0.25, 0.22, 0.22, 1.0);
    else
        gfx.drawSolidRect(320.0, 220.0, 64.0, 64.0, 0.70, 0.30, 0.28, 1.0);

    local used = stash.getUsedSlotCount().tofloat();
    local maxs = stash.getSlotCount().tofloat();

    gfx.drawSolidRect(420.0, 220.0, 200.0, 24.0, 0.18, 0.20, 0.26, 1.0);
    if (maxs > 0.0)
        gfx.drawSolidRect(420.0, 220.0, 200.0 * used / maxs, 24.0, 0.30, 0.45, 0.70, 1.0);

    ui.beginFrameAndRender();
};
