// ============================================================================
// EVEngine RPG 模块示例 —— 「经典回合制」
//
// 回合制战斗 + 职业/特征/等级/任务/GameState（数据驱动）。
// 战斗：1/2/3 行动，C 查看状态（占位），R 重开。
// ============================================================================

persist rpg = null
persist gs = null
persist player = null
persist enemy = null
persist battle = null
persist quest = null
persist wave = 1
persist gold = 0
persist kills = 0
persist state = "idle"
persist log = []
persist hitFlash = { player = 0.0, enemy = 0.0 }

const BASE_HP = 100.0;
const BASE_MP = 40.0;

inv <- null
bag <- null
equip <- null
statPoints <- 0
screen <- "battle"

shop <- [
    { id = "potion", name = "治疗药水", desc = "恢复 25 生命", price = 20 },
    { id = "iron_sword", name = "铁剑", desc = "攻击 +8", price = 120 },
    { id = "leather_armor", name = "皮甲", desc = "防御 +6", price = 90 }
]

function randf(a, b) { return a + (b - a) * (rand().tofloat() / RAND_MAX.tofloat()); }
function roundi(v) { return floor(v + 0.5).tointeger(); }
function logLine(text) {
    log.push(text);
    while (log.len() > 8) log.remove(0);
}

function readTextFile(path) {
    local handle = file(path, "r");
    if (handle == null) return null;
    local content = handle.read();
    handle.close();
    return content;
}

function registerContent() {
    local list = [
        ["effects", "registerEffectsFromJson"],
        ["skills", "registerSkillsFromJson"],
        ["traits", "registerTraitsFromJson"],
        ["classes", "registerClassesFromJson"],
        ["quests", "registerQuestsFromJson"]
    ];
    foreach (entry in list) {
        local json = readTextFile("data/" + entry[0] + ".json");
        local n = (json != null) ? rpg[entry[1]](json) : 0;
        print(format("rpg-classic: loaded %d from %s\n", n, "data/" + entry[0] + ".json"));
    }
    rpg.clearSkillDamage();
    rpg.registerSkillDamage("skill.strike", "hp", "a.attack - b.defense", "", 0.0, 100);
    rpg.registerSkillDamage("skill.fireball", "hp", "a.attack * 2", "fire", 0.05, 95);
    rpg.registerSkillDamage("skill.cleave", "hp", "a.attack * 1.6", "", 0.0, 100);
    rpg.registerSkillDamage("skill.self_heal", "hpHeal", "a.attack", "", 0.0, 100);
    rpg.registerSkillDamage("skill.enemy_claw", "hp", "a.attack - b.defense", "", 0.0, 100);

    local itemsJson = readTextFile("data/items.json");
    local itemsLoaded = (itemsJson != null) ? inv.registerItemsFromJson(itemsJson) : 0;
    print(format("rpg-classic: loaded %d items\n", itemsLoaded));
    rpg.registerItemStatsFromJson("iron_sword",
        @"[ {""attribute"":""attack"",""op"":""add"",""value"":8} ]");
    rpg.registerItemStatsFromJson("leather_armor",
        @"[ {""attribute"":""defense"",""op"":""add"",""value"":6} ]");
}

function setupInventory() {
    if (bag == null) bag = inv.newBag(12);
    else bag.clear();
    if (equip == null) equip = inv.newEquipmentSet();
    equip.setId("hero");
    equip.defineSlot("weapon");
    equip.defineSlot("armor");
    bag.addItem("potion", 1);
    bag.addItem("iron_sword", 1);
}

function equipItem(itemId, slot) {
    local slotIndex = bag.findItem(itemId);
    if (slotIndex < 0) return false;
    if (!equip.equipFromBag(slot, bag, slotIndex)) return false;
    rpg.syncEquipModifiers(player, equip);
    logLine("装备了 " + inv.getItemDisplayName(itemId));
    return true;
}

function unequipSlot(slot) {
    if (equip.isSlotEmpty(slot)) return;
    local itemId = equip.getSlotItemId(slot);
    if (equip.unequipToBag(slot, bag)) {
        rpg.syncEquipModifiers(player, equip);
        logLine("卸下了 " + inv.getItemDisplayName(itemId));
    }
}

function usePotion() {
    local s = bag.findItem("potion");
    if (s < 0) { logLine("背包里没有治疗药水"); return; }
    bag.removeAt(s, 1);
    player.heal("hp", 25.0);
    logLine("使用治疗药水，恢复 25 生命");
}

function buyItem(itemId, price) {
    if (gold < price) { logLine("金币不足"); return; }
    if (!bag.canAddItem(itemId, 1)) { logLine("背包已满"); return; }
    gold -= price;
    gs.setVariable("gold", gold.tofloat());
    bag.addItem(itemId, 1);
    logLine("购买了 " + inv.getItemDisplayName(itemId));
}

function allocate(attr, delta) {
    if (statPoints <= 0) { logLine("没有可用属性点"); return; }
    statPoints -= 1;
    player.modifyBaseAttribute(attr, delta);
    logLine("分配 1 点 → " + attr);
}

function bagSummary() {
    local text = "";
    for (local i = 0; i < bag.getSlotCount(); i += 1) {
        if (bag.isSlotEmpty(i)) continue;
        local id = bag.getSlotItemId(i);
        local qty = bag.getSlotQuantity(i);
        local line = inv.getItemDisplayName(id) + " x" + qty;
        text = (text == "") ? line : (text + "\n" + line);
    }
    return (text == "") ? "（空）" : text;
}

function setScreen(s) {
    screen = s;
    ui.select("adjust"); ui.setHostVisible(s == "adjust");
    ui.select("status"); ui.setHostVisible(s == "status");
    if (s == "adjust") refreshAdjustUI();
    if (s == "status") refreshStatusUI();
}

function refreshStatusUI() {
    ui.select("status");
    local s = "战士 Lv." + player.getLevel() + "\n";
    s += "攻击 " + roundi(player.getFinalAttribute("attack")) + "\n";
    s += "防御 " + roundi(player.getFinalAttribute("defense")) + "\n";
    s += "生命 " + roundi(player.getCurrent("hp")) + "/" + roundi(player.getMax("hp")) + "\n";
    s += "魔力 " + roundi(player.getCurrent("mp")) + "/" + roundi(player.getMax("mp")) + "\n";
    s += "可用属性点 " + statPoints + "\n";
    ui.setText("st_stats", s);
    ui.setText("st_eq", "武器：" + (equip.isSlotEmpty("weapon") ? "无" : inv.getItemDisplayName(equip.getSlotItemId("weapon"))) +
               "\n护甲：" + (equip.isSlotEmpty("armor") ? "无" : inv.getItemDisplayName(equip.getSlotItemId("armor"))));
    ui.setText("st_bag", bagSummary());
}

function refreshAdjustUI() {
    ui.select("adjust");
    ui.setText("adj_points", "可用点数 " + statPoints);
    ui.setText("adj_gold", "金币 " + gold);
    for (local i = 0; i < 3; i += 1) {
        ui.setText("shop" + i, shop[i].name + "  " + shop[i].desc + "  [" + shop[i].price + " 金币]");
    }
    ui.setText("eq_weapon", "武器：" + (equip.isSlotEmpty("weapon") ? "无" : inv.getItemDisplayName(equip.getSlotItemId("weapon"))));
    ui.setText("eq_armor", "护甲：" + (equip.isSlotEmpty("armor") ? "无" : inv.getItemDisplayName(equip.getSlotItemId("armor"))));
    ui.setText("adj_bag", "背包：\n" + bagSummary());
}

function makePlayer() {
    local p = rpg.newActor();
    p.setBaseAttribute("attack", 18.0);
    p.setBaseAttribute("defense", 6.0);
    p.setBaseAttribute("hp", BASE_HP);
    p.setBaseAttribute("mp", BASE_MP);
    p.setCurrent("hp", BASE_HP);
    p.setCurrent("mp", BASE_MP);
    p.setXpToNext(50.0);
    p.setClass("class.warrior");
    return p;
}

function enemyMaxHp(w) { return 30.0 + w * 12.0; }

function makeEnemy(w) {
    local e = rpg.newActor();
    local maxHp = enemyMaxHp(w);
    e.setBaseAttribute("attack", 10.0 + w * 2.0);
    e.setBaseAttribute("defense", 2.0 + w * 0.8);
    e.setBaseAttribute("hp", maxHp);
    e.setCurrent("hp", maxHp);
    e.learnSkill("skill.enemy_claw");
    return e;
}

function runCombat(playerSkill, target) {
    if (battle == null) battle = rpg.newBattle();
    battle.setAction(player, playerSkill, target);
    battle.autoEnemyActions();
    battle.startRound();
    while (battle.executeNextAction() && !battle.isFinished()) {}
    battle.pollEvents();
    for (local i = 0; i < battle.getEventCount(); i += 1) {
        local act = battle.getEventAction(i);
        local amount = roundi(battle.getEventAmount(i));
        if (act == "damage")
            logLine((battle.getEventCaster(i) == player ? "玩家" : "敌人") +
                    " 造成 " + amount + " 伤害" + (battle.getEventCrit(i) ? "（暴击！）" : ""));
        else if (act == "heal")
            logLine("玩家 恢复了 " + amount + " 生命");
        else if (act == "miss")
            logLine((battle.getEventCaster(i) == player ? "玩家" : "敌人") + " 的攻击落空了");
    }
}

function onVictory() {
    local before = player.getLevel();
    local leveled = player.gainXp(30.0 + wave * 10.0);
    local learned = player.checkLevelSkills();
    if (player.getLevel() > before) {
        statPoints += (player.getLevel() - before) * 3;
        logLine("玩家升级到 " + player.getLevel() + " 级！获得属性点");
    }
    if (learned > 0) logLine("学到新技能！");

    kills += 1;
    quest.notify("kill", "slime", 1);
    if (quest.getState("quest.slayer") == "ready") {
        if (quest.claim("quest.slayer")) {
            gold += 50;
            gs.setVariable("gold", gold.tofloat());
            logLine("任务「清剿史莱姆」完成，获得 50 金币");
        }
    } else {
        gold += 10;
        gs.setVariable("gold", gold.tofloat());
    }

    wave += 1;
    if (enemy != null) enemy.release();
    enemy = makeEnemy(wave);
    battle = rpg.newBattle();
    battle.addActor(player, 0);
    battle.addActor(enemy, 1);
    battle.setPlayerSide(0);
}

function tryPlayerSkill(skillId) {
    if (skillId == "skill.fireball" && player.getCurrent("mp") < 10.0) {
        logLine("MP 不足，无法施放火球");
        return;
    }
    if (skillId == "skill.fireball")
        player.takeDamage("mp", 10.0, "skill");

    runCombat(skillId, enemy);
    if (battle.isVictory()) {
        logLine("击败敌人！");
        hitFlash.enemy = 0.2;
        local beaten = wave;           // 记住刚击败的波数（onVictory 里会 +1）
        onVictory();
        if ((beaten % 3) == 0) setScreen("adjust");  // 第 3、6、9… 波后进入调整
    } else if (battle.isDefeat()) {
        logLine("玩家倒下了……");
        state = "gameover";
    }
}

function buildUI() {
    local pad = 14.0;
    ui.beginBuild();
    ui.beginWindow("Player", "root");
    ui.text("玩家", "title");
    ui.progress(1.0, "hpbar", "");
    ui.text("HP 0/0", "hptext");
    ui.progress(1.0, "mpbar", "");
    ui.text("MP 0/0", "mptext");
    ui.text("等级 Lv.1", "leveltext");
    ui.text("", "pstatus");
    ui.separator("sep1");
    ui.text("[1] 攻击", "s1");
    ui.text("[2] 火球 (MP10)", "s2");
    ui.text("[3] 治疗", "s3");
    ui.end();
    ui.mountBuildAs("player");
    ui.select("player"); ui.setHostOverlay(true); ui.setHostPos(pad, pad, 0.0, 0.0);

    ui.beginBuild();
    ui.beginWindow("Enemy", "root");
    ui.text("敌人 - 第 1 波", "title");
    ui.progress(1.0, "ehpbar", "");
    ui.text("HP 0/0", "ehptext");
    ui.text("", "estatus");
    ui.end();
    ui.mountBuildAs("enemy");
    ui.select("enemy"); ui.setHostOverlay(true); ui.setHostPos(config.width - pad, pad, 1.0, 0.0);

    ui.beginBuild();
    ui.beginWindow("Quest", "root");
    ui.text("任务：清剿史莱姆 0/3", "qtext");
    ui.text("金币 0", "goldtext");
    ui.text("", "log");
    ui.end();
    ui.mountBuildAs("quest");
    ui.select("quest"); ui.setHostOverlay(true); ui.setHostPos(pad, config.height - pad, 0.0, 1.0);

    ui.beginBuild();
    ui.beginWindow("Game Over", "root");
    ui.text("玩家倒下", "msg");
    ui.text("", "final");
    ui.button("重新开始 (R)", "restart");
    ui.end();
    ui.mountBuildAs("gameover");
    ui.select("gameover"); ui.setHostVisible(false);
    ui.setHostPos(config.width * 0.5, config.height * 0.45, 0.5, 0.5);

    ui.beginBuild();
    ui.beginWindow("角色状态", "root");
    ui.text("角色状态", "st_title");
    ui.text("", "st_stats");
    ui.separator("st_s1");
    ui.text("装备", "st_eq_title");
    ui.text("", "st_eq");
    ui.separator("st_s2");
    ui.text("背包", "st_bag_title");
    ui.text("", "st_bag");
    ui.separator("st_s3");
    ui.button("关闭 (C)", "st_close");
    ui.end();
    ui.mountBuildAs("status");
    ui.select("status"); ui.setHostOverlay(true);
    ui.setHostPos(config.width * 0.5, config.height * 0.5, 0.5, 0.5);
    ui.setHostVisible(false);

    ui.beginBuild();
    ui.beginWindow("调整 - 整备", "root");
    ui.text("属性加点", "adj_h1");
    ui.text("可用点数 0", "adj_points");
    ui.button("攻击 +1", "stat_atk");
    ui.button("防御 +1", "stat_def");
    ui.button("生命上限 +10", "stat_hp");
    ui.button("魔力上限 +10", "stat_mp");
    ui.separator("adj_s1");
    ui.text("商店", "adj_h2");
    ui.text("金币 0", "adj_gold");
    ui.text("", "shop0"); ui.button("购买", "shop0_buy");
    ui.text("", "shop1"); ui.button("购买", "shop1_buy");
    ui.text("", "shop2"); ui.button("购买", "shop2_buy");
    ui.separator("adj_s2");
    ui.text("装备 / 背包", "adj_h3");
    ui.text("武器：无", "eq_weapon");
    ui.text("护甲：无", "eq_armor");
    ui.text("背包：", "adj_bag");
    ui.button("装备铁剑", "eq_sword");
    ui.button("装备皮甲", "eq_armor_btn");
    ui.button("卸下武器", "uneq_weapon");
    ui.button("卸下护甲", "uneq_armor");
    ui.separator("adj_s3");
    ui.button("返回战斗", "adj_back");
    ui.end();
    ui.mountBuildAs("adjust");
    ui.select("adjust"); ui.setHostOverlay(true);
    ui.setHostPos(config.width * 0.5, config.height * 0.5, 0.5, 0.5);
    ui.setHostVisible(false);
}

function refreshHud() {
    ui.select("player");
    ui.setValue("hpbar", (player.getCurrent("hp") / player.getMax("hp")).tofloat());
    ui.setText("hptext", "HP " + roundi(player.getCurrent("hp")) + "/" + roundi(player.getMax("hp")));
    ui.setValue("mpbar", (player.getCurrent("mp") / player.getMax("mp")).tofloat());
    ui.setText("mptext", "MP " + roundi(player.getCurrent("mp")) + "/" + roundi(player.getMax("mp")));
    ui.setText("leveltext", "战士 Lv." + player.getLevel() + "  经验 " +
               roundi(player.getXp()) + "/" + roundi(player.getXpToNext()));

    local pn = player.getTraitCount();
    local stat = "";
    for (local i = 0; i < pn; i += 1) {
        local id = player.getTraitIdAt(i);
        if (id == "trait.mighty") stat = (stat == "") ? "力量" : (stat + " + 力量");
        if (id == "trait.fire_guard") stat = (stat == "") ? "火抗" : (stat + " + 火抗");
    }
    ui.setText("pstatus", "特征：" + (stat == "" ? "无" : stat));

    ui.select("enemy");
    if (enemy != null) {
        ui.setText("title", "敌人 - 第 " + wave + " 波");
        ui.setValue("ehpbar", (enemy.getCurrent("hp") / enemyMaxHp(wave)).tofloat());
        ui.setText("ehptext", "HP " + roundi(enemy.getCurrent("hp")) + "/" + roundi(enemyMaxHp(wave)));
        ui.setText("estatus", "攻击 " + roundi(enemy.getFinalAttribute("attack")));
    }

    ui.select("quest");
    local qcount = (quest.getState("quest.slayer") == "completed") ? 3 : kills;
    local qstate = (quest.getState("quest.slayer") == "ready") ? "（可领奖！）" : "";
    ui.setText("qtext", "任务：清剿史莱姆 " + qcount + "/3" + qstate);
    ui.setText("goldtext", "金币 " + gold + "   等级 " + player.getLevel());
    local logText = "";
    for (local i = 0; i < log.len(); i += 1) logText += log[i] + "\n";
    ui.setText("log", logText);
}

function showGameOver(show) {
    ui.select("gameover");
    ui.setHostVisible(show);
    if (show) { ui.setHostModal(true); ui.setText("final", "击败 " + wave + " 波，等级 " + player.getLevel() + "，金币 " + gold); }
    else ui.setHostModal(false);
}

function startNewGame() {
    if (player != null) player.release();
    if (enemy != null) enemy.release();
    wave = 1; gold = 0; kills = 0;
    log = [];
    hitFlash.player = 0.0; hitFlash.enemy = 0.0;
    state = "idle";
    if (gs == null) gs = rpg.newGameState();
    gs.clear(); gs.switchOn("new_game"); gs.setVariable("gold", 0.0);
    player = makePlayer();
    enemy = makeEnemy(wave);
    setupInventory();
    battle = rpg.newBattle();
    battle.addActor(player, 0);
    battle.addActor(enemy, 1);
    battle.setPlayerSide(0);
    quest = rpg.newTracker();
    quest.activate("quest.slayer");
    showGameOver(false);
    setupInventory();
    setScreen("battle");
    logLine("第 1 波开始：按 1/2/3 行动，C 查看状态");
}

eve_init = function() {
    gfx.setBackgroundColor(0.06, 0.05, 0.09, 1.0);
    if (rpg == null) rpg = eve.RPG();
    if (inv == null) inv = eve.Inventory();
    registerContent();
    buildUI();
    if (player == null) startNewGame();
    else refreshHud();
};

eve_reload <- function() { registerContent(); };

eve_update = function(dt) {
    local id = ui.consumeClick();
    while (id != "") {
        if (id == "gameover/restart") startNewGame();
        else if (id == "status/st_close") setScreen("battle");
        else if (id == "adjust/adj_back") setScreen("battle");
        else if (id == "adjust/stat_atk") { allocate("attack", 1.0); refreshAdjustUI(); }
        else if (id == "adjust/stat_def") { allocate("defense", 1.0); refreshAdjustUI(); }
        else if (id == "adjust/stat_hp") { allocate("hp", 10.0); refreshAdjustUI(); }
        else if (id == "adjust/stat_mp") { allocate("mp", 10.0); refreshAdjustUI(); }
        else if (id == "adjust/shop0_buy") { buyItem(shop[0].id, shop[0].price); refreshAdjustUI(); }
        else if (id == "adjust/shop1_buy") { buyItem(shop[1].id, shop[1].price); refreshAdjustUI(); }
        else if (id == "adjust/shop2_buy") { buyItem(shop[2].id, shop[2].price); refreshAdjustUI(); }
        else if (id == "adjust/eq_sword") { equipItem("iron_sword", "weapon"); refreshAdjustUI(); }
        else if (id == "adjust/eq_armor_btn") { equipItem("leather_armor", "armor"); refreshAdjustUI(); }
        else if (id == "adjust/uneq_weapon") { unequipSlot("weapon"); refreshAdjustUI(); }
        else if (id == "adjust/uneq_armor") { unequipSlot("armor"); refreshAdjustUI(); }
        id = ui.consumeClick();
    }
    if (hitFlash.player > 0.0) hitFlash.player -= dt;
    if (hitFlash.enemy > 0.0) hitFlash.enemy -= dt;
    if (screen == "status") {
        if (key_just_pressed("C")) setScreen("battle");
    } else if (screen == "adjust") {
        // 调整界面用按钮操作
    } else if (state == "gameover") {
        if (key_just_pressed("R")) startNewGame();
    } else if (key_just_pressed("1")) {
        tryPlayerSkill("skill.strike");
    } else if (key_just_pressed("2")) {
        tryPlayerSkill("skill.fireball");
    } else if (key_just_pressed("3")) {
        tryPlayerSkill("skill.self_heal");
    } else if (key_just_pressed("C")) {
        refreshStatusUI(); setScreen("status");
    } else if (key_just_pressed("Q")) {
        usePotion();
    }
    refreshHud();
};

eve_render = function() {
    gfx.clear();
    gfx.drawSolidRect(0.0, config.height - 60.0, config.width * 1.0, 60.0, 0.12, 0.1, 0.14, 1.0);
    local px = config.width * 0.28; local py = config.height - 150.0;
    local pf = hitFlash.player > 0.0 ? 0.6 : 0.0;
    gfx.drawSolidRect(px - 30.0, py, 60.0, 90.0, 0.25 + pf, 0.45 + pf, 0.8 + pf, 1.0);
    gfx.drawSolidRect(px - 16.0, py - 34.0, 32.0, 32.0, 0.35 + pf, 0.55 + pf, 0.85 + pf, 1.0);
    local ex = config.width * 0.72; local ey = config.height - 160.0;
    local ef = hitFlash.enemy > 0.0 ? 0.6 : 0.0;
    local esize = 70.0 + (wave.tofloat() * 3.0); if (esize > 130.0) esize = 130.0;
    gfx.drawSolidRect(ex - esize * 0.5, ey, esize, esize, 0.75 + ef, 0.25 + ef, 0.2 + ef, 1.0);
    gfx.drawSolidRect(ex - esize * 0.22, ey + esize * 0.25, esize * 0.14, esize * 0.14, 1.0, 1.0, 0.6, 1.0);
    gfx.drawSolidRect(ex + esize * 0.08, ey + esize * 0.25, esize * 0.14, esize * 0.14, 1.0, 1.0, 0.6, 1.0);
    ui.beginFrameAndRender();
};

eve_quit = function() {
};