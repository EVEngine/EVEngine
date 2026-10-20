// ============================================================================
// Mini RPG Workshop — bench product (EVEngine vs Godot).
//
// One cohesive project covering the shared brief checklist:
//   Play: character stats, turn combat (2+ actions), inventory consumable,
//         NPC dialogue with a choice branch.
//   Edit: map paint/erase (walkability feeds play), scene prop place/move.
//
// Modes (F1–F4):
//   F1 Explore  WASD move, E talk/encounter, Q drink potion, C status, R reset
//   F2 Battle   1 Strike / 2 Arc Bolt / 3 Mend / Esc flee
//   F3 MapEdit  LMB floor(gid=1) RMB wall(gid=2) MMB erase; Z/Y undo/redo
//   F4 Scene    arrows move 2D prop marker; 1 box / 2 pillar / 3 camera / R reset
//               (3D via scene_director when available; 2D marker always works)
// ============================================================================

persist mode = "explore"
persist rpg = null
persist player = null
persist enemy = null
persist battle = null
persist inv = null
persist bag = null
persist eq = null
persist gold = 40
persist worldX = 2
persist worldY = 5
persist worldLayer = null
persist worldPath = null
persist goblinAlive = true
persist questAccepted = false
persist journal = []
persist hitFlash = 0.0
persist dlgReady = false
persist tileBuf = null
persist brush = null
persist hist = null
persist lastPaintX = -1
persist lastPaintY = -1
persist lastPaintGid = -1
persist scene_director = null
persist sd = null
persist propX = 7
persist propY = 4
persist uiBuilt = false
persist notice = "F1 Explore / F2 Battle / F3 MapEdit / F4 Scene"

const MAP_W = 16;
const MAP_H = 10;
const PLAYER_MAX_HP = 100.0;
const PLAYER_MAX_MP = 40.0;
const GOBLIN_MAX_HP = 48.0;

function logLine(t) {
    journal.push(t);
    while (journal.len() > 7) journal.remove(0);
}

function readTextFile(path) {
    try {
        local h = file(path, "r");
        if (h == null) return null;
        local c = h.read();
        h.close();
        return c;
    } catch (e) {
        return null;
    }
}

function ensureRpg() {
    if (rpg == null) rpg = eve.RPG();
    if (player == null) {
        player = rpg.newActor();
        player.setBaseAttribute("hp", PLAYER_MAX_HP);
        player.setBaseAttribute("mp", PLAYER_MAX_MP);
        player.setBaseAttribute("attack", 16.0);
        player.setBaseAttribute("defense", 5.0);
        player.setCurrent("hp", PLAYER_MAX_HP);
        player.setCurrent("mp", PLAYER_MAX_MP);
        player.learnSkill("skill.strike");
        player.learnSkill("skill.bolt");
        player.learnSkill("skill.mend");
    }
}

function registerContent() {
    ensureRpg();
    local effects = readTextFile("data/effects.json");
    local skills = readTextFile("data/skills.json");
    if (effects != null) rpg.registerEffectsFromJson(effects);
    if (skills != null) rpg.registerSkillsFromJson(skills);
    rpg.registerSkillDamage("skill.strike", "hp", "a.attack - b.defense * 0.5", "", 0.05, 100);
    rpg.registerSkillDamage("skill.bolt", "hp", "a.attack * 1.7", "arc", 0.0, 100);
}

function ensureInventory() {
    if (inv == null) inv = eve.Inventory();
    local items = readTextFile("data/items.json");
    if (items != null) inv.registerItemsFromJson(items);
    if (bag == null) {
        bag = inv.newBag(12);
        bag.setId("bench.player");
        bag.setKind("backpack");
        bag.setMaxWeight(40.0);
        bag.addRejectTag("quest");
        bag.addItem("potion.hp", 3);
        bag.addItem("sword.iron", 1);
    }
    if (eq == null) {
        eq = inv.newEquipmentSet();
        eq.setId("bench.eq");
        eq.defineSlot("weapon");
        eq.addSlotAllowedTag("weapon", "weapon");
    }
    inv.clearGameplayControls();
    local published = inv.publishGameplay(
        "00000000-0000-7000-8000-0000000c0001",
        "00000000-0000-7000-8000-0000000c0002",
        bag, eq);
    if (!published.ok)
        logLine("inventory publish failed: " + published.message);
}

function ensureDialogue() {
    if (!has_module("dialogueFlow")) {
        dlgReady = false;
        return;
    }
    local src = readTextFile("data/workshop-dialogue.dnut");
    if (src == null) { dlgReady = false; return; }
    local loaded = dialogueFlow.loadDnutChecked(src, "data/workshop-dialogue.dnut");
    local linted = dialogueFlow.lintAllChecked();
    dlgReady = loaded.ok && linted.ok;
    if (!dlgReady)
        logLine("dialogue load failed");
    local pub = dialogueFlow.publishGameplay(
        "00000000-0000-7000-8000-0000000c0010",
        "00000000-0000-7000-8000-0000000c0002");
    if (!pub.ok)
        logLine("dialogue publish: " + pub.message);
}

function ensureWorld() {
    if (worldLayer != null) return;
    if (!has_module("map") || map == null) map = eve.Map();
    local contract = readTextFile("data/world-object-contract.json");
    if (contract == null) throw "missing world-object-contract.json";
    local admitted = map.loadFromFileWithObjectContract("data/workshop-map.json", contract);
    if (!admitted.ok)
        throw "map rejected: " + admitted.status.summary;
    worldLayer = map.getLayer(0);
    if (worldLayer == null) throw "map has no ground layer";
    worldPath = map.newPathfinder(worldLayer);
    worldPath.blockGid(2);
    worldPath.syncFromLayer();
}

function ensureMapEditor() {
    if (!has_module("levelEditor") || levelEditor == null) {
        logLine("levelEditor missing — MapEdit uses direct tile writes");
        return;
    }
    if (tileBuf == null) {
        tileBuf = levelEditor.newTileBuffer(MAP_W, MAP_H);
        brush = levelEditor.newBrush();
        hist = levelEditor.newHistory();
        brush.setTool("paint");
        brush.setTile(1);
        brush.setSize(1);
        brush.setShape("square");
        for (local y = 0; y < MAP_H; y += 1)
            for (local x = 0; x < MAP_W; x += 1)
                tileBuf.setGid(x, y, worldLayer.getTile(x, y));
    }
}

function syncBufferToLayer() {
    if (tileBuf == null || worldLayer == null) return;
    for (local y = 0; y < MAP_H; y += 1)
        for (local x = 0; x < MAP_W; x += 1)
            worldLayer.setTile(x, y, tileBuf.getGid(x, y));
    if (worldPath != null) worldPath.syncFromLayer();
}

function paintAt(tx, ty, gid) {
    if (brush == null || tileBuf == null || hist == null) {
        if (worldLayer == null) return;
        worldLayer.setTile(tx, ty, gid);
        if (worldPath != null) worldPath.syncFromLayer();
        logLine("paint @(" + tx + "," + ty + ") gid=" + gid);
        return;
    }
    brush.setTile(gid);
    hist.beginGroup("paint");
    local n = brush.paintAt(tileBuf, tx, ty);
    for (local i = 0; i < brush.getChangeCount(); i += 1)
        hist.recordTile(brush.getChangeX(i), brush.getChangeY(i),
                        brush.getChangeOldGid(i), brush.getChangeNewGid(i));
    hist.endGroup();
    syncBufferToLayer();
    logLine("paint " + n + " @(" + tx + "," + ty + ") gid=" + gid);
}

function installSceneDirector() {
    if ("sceneDirectorScript" in eve && eve.sceneDirectorScript != "") {
        try {
            local fn = compilestring(eve.sceneDirectorScript);
            if (fn != null) fn();
        } catch (e) {
            logLine("scene_director install: " + e);
        }
    }
    if (scene_director != null) sd = scene_director;
}

function ensureScene() {
    installSceneDirector();
    if (sd == null) return;
    sd_require(sd.reset());
    sd_require(sd.spawn({
        id = "floor", kind = "ground",
        pos = [0.0, 0.0, 0.0], scale = [24.0, 1.0, 24.0],
        tint = [0.32, 0.40, 0.36], roughness = 0.9
    }));
    // Mirror the 2D prop as a 3D crate so scene edit is visible in render3D.
    local px = (propX - 8).tofloat() * 1.2;
    local pz = (propY - 5).tofloat() * 1.2;
    sd_require(sd.spawn({
        id = "marker_crate", kind = "box",
        pos = [px, 0.5, pz], scale = [1.0, 1.0, 1.0],
        tint = [0.75, 0.55, 0.28]
    }));
    sd_require(sd.set_lighting({ timeOfDay = "day", atmosphere = "clear", intensity = 1.0 }));
    sd_require(sd.camera({ eye = [0.0, 12.0, 18.0], target = [0.0, 1.0, 0.0], fov = 55.0 }));
}

function moveProp(dx, dy) {
    local nx = propX + dx;
    local ny = propY + dy;
    if (nx < 1 || ny < 1 || nx >= MAP_W - 1 || ny >= MAP_H - 1) {
        logLine("prop out of bounds");
        return;
    }
    propX = nx;
    propY = ny;
    logLine("prop -> (" + propX + "," + propY + ")");
    // Keep 3D stage in sync when director is live.
    if (sd != null) {
        local px = (propX - 8).tofloat() * 1.2;
        local pz = (propY - 5).tofloat() * 1.2;
        try {
            sd_require(sd.spawn({
                id = "marker_crate", kind = "box",
                pos = [px, 0.5, pz], scale = [1.0, 1.0, 1.0],
                tint = [0.75, 0.55, 0.28]
            }));
        } catch (e) { /* spawn overwrite may throw on some builds */ }
    }
}

function startDialogue(id) {
    if (!dlgReady || !has_module("dialogueFlow")) {
        logLine("dialogue unavailable");
        return;
    }
    local started = dialogueFlow.startChecked(id, {});
    if (!started.ok) {
        logLine("start " + id + " failed: " + started.status.summary);
        return;
    }
    mode = "dialogue";
    presentDialogue();
}

function presentDialogue() {
    if (!dialogueFlow.isActive()) {
        mode = "explore";
        return;
    }
    local kind = dialogueFlow.getNodeKind();
    if (kind == "line") {
        local speaker = dialogueFlow.getSpeaker();
        local text = dialogueFlow.getText();
        if (has_module("dialogue")) dialogue.say(speaker, text);
        logLine(speaker + ": " + text);
    } else if (kind == "choice") {
        local opts = "";
        for (local i = 0; i < dialogueFlow.getRouteCount(); i += 1)
            opts += " [" + (i + 1) + "]" + dialogueFlow.getRouteId(i);
        logLine("Choice:" + opts);
    } else if (kind == "end") {
        mode = "explore";
    }
}

function advanceDialogue() {
    if (!dialogueFlow.isActive()) { mode = "explore"; return; }
    local kind = dialogueFlow.getNodeKind();
    if (kind == "choice") return;
    local adv = dialogueFlow.advanceChecked();
    if (!adv.ok || !dialogueFlow.isActive()) {
        mode = "explore";
        return;
    }
    presentDialogue();
}

function chooseDialogue(index) {
    if (!dialogueFlow.isActive() || dialogueFlow.getNodeKind() != "choice") return;
    if (index < 0 || index >= dialogueFlow.getRouteCount()) return;
    local route = dialogueFlow.getRouteId(index);
    dialogueFlow.select(route);
    if (route == "accept") {
        questAccepted = true;
        logLine("Quest accepted");
    }
    presentDialogue();
}

function tryInteract() {
    for (local i = 0; i < map.getObjectCount(); i += 1) {
        local ox = map.getObjectX(i).tointeger();
        local oy = map.getObjectY(i).tointeger();
        local dx = abs(ox - worldX);
        local dy = abs(oy - worldY);
        if (dx + dy > 1) continue;
        local typ = map.getObjectType(i);
        if (typ == "npc") {
            local conv = map.getObjectProperty(i, "conversation", "keeper.offer");
            startDialogue(conv);
            return;
        }
        if (typ == "encounter" && goblinAlive) {
            startDialogue("goblin.hint");
            return;
        }
    }
    // Prop adjacency note (scene-edited marker).
    local pdx = abs(propX - worldX);
    local pdy = abs(propY - worldY);
    if (pdx + pdy <= 1) {
        logLine("Crate marker at (" + propX + "," + propY + ")");
        return;
    }
    logLine("Nothing to interact with");
}

function beginBattle() {
    ensureRpg();
    enemy = rpg.newActor();
    enemy.setBaseAttribute("hp", GOBLIN_MAX_HP);
    enemy.setBaseAttribute("mp", 0.0);
    enemy.setBaseAttribute("attack", 11.0);
    enemy.setBaseAttribute("defense", 2.0);
    enemy.setCurrent("hp", GOBLIN_MAX_HP);
    battle = rpg.newBattle();
    battle.addActor(player, 0);
    battle.addActor(enemy, 1);
    mode = "battle";
    logLine("Goblin fight! 1 Strike  2 Bolt  3 Mend  Esc flee");
}

function playerBattleAction(skillId) {
    if (battle == null || battle.isFinished()) return;
    if (skillId == "skill.bolt" && player.getCurrent("mp") < 10.0) {
        logLine("Not enough MP");
        return;
    }
    if (skillId == "skill.mend") {
        if (player.getCurrent("mp") < 8.0) { logLine("Not enough MP"); return; }
        player.heal("hp", 28.0);
        player.setCurrent("mp", player.getCurrent("mp") - 8.0);
        logLine("Mend +28 HP");
    } else {
        local dmg = 0.0;
        if (skillId == "skill.strike")
            dmg = player.getFinalAttribute("attack") - enemy.getFinalAttribute("defense") * 0.5;
        else
            dmg = player.getFinalAttribute("attack") * 1.7;
        if (dmg < 1.0) dmg = 1.0;
        if (skillId == "skill.bolt")
            player.setCurrent("mp", player.getCurrent("mp") - 10.0);
        enemy.takeDamage("hp", dmg, "player");
        hitFlash = 0.25;
        logLine(skillId + " hits for " + floor(dmg + 0.5).tointeger());
    }
    if (enemy.isDead() || enemy.getCurrent("hp") <= 0.0) {
        goblinAlive = false;
        gold += 15;
        bag.addItem("potion.hp", 1);
        logLine("Victory! +15 gold, +1 potion");
        battle = null;
        enemy = null;
        mode = "explore";
        return;
    }
    local edmg = enemy.getFinalAttribute("attack") - player.getFinalAttribute("defense") * 0.4;
    if (edmg < 1.0) edmg = 1.0;
    player.takeDamage("hp", edmg, "goblin");
    logLine("Goblin hits for " + floor(edmg + 0.5).tointeger());
    if (player.isDead() || player.getCurrent("hp") <= 0.0) {
        logLine("Defeated… press R to reset");
        battle = null;
        mode = "explore";
        player.setCurrent("hp", 1.0);
    }
}

function drinkPotion() {
    if (bag.countItem("potion.hp") <= 0) { logLine("No potions"); return; }
    local removed = bag.removeItem("potion.hp", 1);
    if (removed <= 0) { logLine("Drink failed"); return; }
    player.heal("hp", 35.0);
    logLine("Drank Herb Potion (+35 HP)");
}

function movePlayer(dx, dy) {
    local nx = worldX + dx;
    local ny = worldY + dy;
    if (worldPath == null || !worldPath.isWalkable(nx, ny)) {
        logLine("Blocked");
        return;
    }
    // Prop occupies a cell in play — honest scene edit → play coupling.
    if (nx == propX && ny == propY) {
        logLine("Crate blocks the way (move it in F4 Scene)");
        return;
    }
    worldX = nx;
    worldY = ny;
    for (local i = 0; i < map.getObjectCount(); i += 1) {
        if (map.getObjectType(i) == "encounter" && goblinAlive &&
            map.getObjectX(i).tointeger() == worldX &&
            map.getObjectY(i).tointeger() == worldY) {
            beginBattle();
            return;
        }
    }
}

function eve_init() {
    gfx.setBackgroundColor(0.07, 0.09, 0.13, 1.0);
    registerContent();
    ensureInventory();
    ensureDialogue();
    ensureWorld();
    ensureMapEditor();
    logLine("Mini RPG Workshop ready");
    print("RPG_BENCH_COMPARE_READY\n");
}

function eve_update(dt) {
    if (hitFlash > 0.0) hitFlash -= dt;
    if (has_module("map") && map != null) map.update(dt);
    if (has_module("dialogue") && dialogue != null) dialogue.update(dt);
    if (mode == "scene" && sd != null) sd_require(sd.refresh());

    if (key_just_pressed("F1")) { mode = "explore"; notice = "Explore mode"; }
    if (key_just_pressed("F2")) { beginBattle(); notice = "Battle mode"; }
    if (key_just_pressed("F3")) {
        mode = "map_edit";
        notice = "MapEdit: LMB floor  RMB wall  MMB erase  Z/Y undo/redo";
    }
    if (key_just_pressed("F4")) {
        mode = "scene";
        notice = "Scene: arrows move prop  1 box  2 pillar  3 camera  R reset";
    }

    if (mode == "map_edit") ensureMapEditor();
    if (mode == "scene" && sd == null) ensureScene();

    if (mode == "explore") {
        if (key_just_pressed("W") || key_just_pressed("Up")) movePlayer(0, -1);
        if (key_just_pressed("S") || key_just_pressed("Down")) movePlayer(0, 1);
        if (key_just_pressed("A") || key_just_pressed("Left")) movePlayer(-1, 0);
        if (key_just_pressed("D") || key_just_pressed("Right")) movePlayer(1, 0);
        if (key_just_pressed("E")) tryInteract();
        if (key_just_pressed("Q")) drinkPotion();
        if (key_just_pressed("C"))
            logLine("HP " + player.getCurrent("hp") + "/" + PLAYER_MAX_HP +
                    " MP " + player.getCurrent("mp") + "/" + PLAYER_MAX_MP +
                    " ATK " + player.getFinalAttribute("attack") +
                    " DEF " + player.getFinalAttribute("defense") +
                    " gold " + gold +
                    " potions " + bag.countItem("potion.hp"));
        if (key_just_pressed("R")) {
            goblinAlive = true;
            questAccepted = false;
            player.setCurrent("hp", PLAYER_MAX_HP);
            player.setCurrent("mp", PLAYER_MAX_MP);
            worldX = 2; worldY = 5;
            propX = 7; propY = 4;
            logLine("Adventure reset");
        }
    } else if (mode == "battle") {
        if (key_just_pressed("1")) playerBattleAction("skill.strike");
        if (key_just_pressed("2")) playerBattleAction("skill.bolt");
        if (key_just_pressed("3")) playerBattleAction("skill.mend");
        if (key_just_pressed("Escape")) { mode = "explore"; battle = null; logLine("Fled"); }
    } else if (mode == "dialogue") {
        if (key_just_pressed("1")) chooseDialogue(0);
        if (key_just_pressed("2")) chooseDialogue(1);
        if (key_just_pressed("Space") || key_just_pressed("E")) advanceDialogue();
        if (key_just_pressed("Escape")) { dialogueFlow.stop(); mode = "explore"; }
        if (!dialogueFlow.isActive() && goblinAlive && key_just_pressed("1"))
            beginBattle();
    } else if (mode == "map_edit") {
        if (!(mouse.isDown(1) || mouse.isDown(3) || mouse.isDown(2))) {
            lastPaintX = -1; lastPaintY = -1; lastPaintGid = -1;
        } else if (worldLayer != null) {
            local mx = mouse.getX();
            local my = mouse.getY();
            local tx = worldLayer.worldToTileX(mx.tofloat(), my.tofloat());
            local ty = worldLayer.worldToTileY(mx.tofloat(), my.tofloat());
            if (tx >= 0 && ty >= 0 && tx < MAP_W && ty < MAP_H) {
                local gid = mouse.isDown(1) ? 1 : (mouse.isDown(3) ? 2 : 0);
                if (tx != lastPaintX || ty != lastPaintY || gid != lastPaintGid) {
                    paintAt(tx, ty, gid);
                    lastPaintX = tx; lastPaintY = ty; lastPaintGid = gid;
                }
            }
        }
        if (key_just_pressed("Z") && hist != null && hist.canUndo()) {
            hist.undo();
            hist.applyLastToBuffer(tileBuf);
            syncBufferToLayer();
            logLine("undo");
        }
        if (key_just_pressed("Y") && hist != null && hist.canRedo()) {
            hist.redo();
            hist.applyLastToBuffer(tileBuf);
            syncBufferToLayer();
            logLine("redo");
        }
    } else if (mode == "scene") {
        if (key_just_pressed("W") || key_just_pressed("Up")) moveProp(0, -1);
        if (key_just_pressed("S") || key_just_pressed("Down")) moveProp(0, 1);
        if (key_just_pressed("A") || key_just_pressed("Left")) moveProp(-1, 0);
        if (key_just_pressed("D") || key_just_pressed("Right")) moveProp(1, 0);
        if (sd == null) {
            if (key_just_pressed("1") || key_just_pressed("2") || key_just_pressed("3"))
                logLine("scene_director unavailable — 2D prop marker still editable");
            if (key_just_pressed("R")) {
                propX = 7; propY = 4;
                logLine("prop reset");
            }
        } else {
            if (key_just_pressed("1"))
                sd_require(sd.spawn({
                    id = "extra_box", kind = "box",
                    pos = [2.0, 0.5, 0.0], scale = [1, 1, 1], tint = [0.7, 0.5, 0.3]
                }));
            if (key_just_pressed("2"))
                sd_require(sd.spawn({
                    id = "pillar", kind = "cylinder",
                    pos = [-2.0, 1.0, 1.0], scale = [0.6, 2.0, 0.6], tint = [0.55, 0.55, 0.6]
                }));
            if (key_just_pressed("3"))
                sd_require(sd.camera({
                    eye = [6.0, 8.0, 10.0], target = [0.0, 1.0, 0.0], fov = 50.0
                }));
            if (key_just_pressed("R")) {
                propX = 7; propY = 4;
                ensureScene();
                logLine("scene reset");
            }
        }
    }
}

function drawBar(x, y, w, h, ratio, r, g, b) {
    gfx.drawSolidRect(x, y, w, h, 0.15, 0.15, 0.18, 1.0);
    local rw = w * ratio;
    if (rw < 0.0) rw = 0.0;
    if (rw > w) rw = w;
    gfx.drawSolidRect(x, y, rw, h, r, g, b, 1.0);
}

function eve_render() {
    gfx.clear();
    if (mode == "scene" && sd != null) {
        gfx.render3D();
        sd.mark_rendered();
    } else {
        if (has_module("map") && map != null) map.render(gfx);
        if (worldLayer != null) {
            local cx = worldLayer.tileToWorldX(propX, propY);
            local cy = worldLayer.tileToWorldY(propX, propY);
            gfx.drawSolidRect(cx + 2.0, cy + 2.0, 28.0, 28.0, 0.72, 0.48, 0.22, 1.0);

            local px = worldLayer.tileToWorldX(worldX, worldY);
            local py = worldLayer.tileToWorldY(worldX, worldY);
            gfx.drawSolidRect(px + 4.0, py + 4.0, 24.0, 24.0, 0.25, 0.75, 1.0, 1.0);
            for (local i = 0; i < map.getObjectCount(); i += 1) {
                local ox = worldLayer.tileToWorldX(map.getObjectX(i).tointeger(),
                                                   map.getObjectY(i).tointeger());
                local oy = worldLayer.tileToWorldY(map.getObjectX(i).tointeger(),
                                                   map.getObjectY(i).tointeger());
                local typ = map.getObjectType(i);
                if (typ == "npc")
                    gfx.drawSolidRect(ox + 6.0, oy + 6.0, 20.0, 20.0, 0.9, 0.8, 0.2, 1.0);
                else if (typ == "encounter" && goblinAlive)
                    gfx.drawSolidRect(ox + 6.0, oy + 6.0, 20.0, 20.0, 0.85, 0.35, 0.3, 1.0);
            }
        }
        if (mode == "battle" && player != null && enemy != null) {
            drawBar(80.0, 520.0, 200.0, 14.0, player.getCurrent("hp") / PLAYER_MAX_HP, 0.2, 0.8, 0.3);
            drawBar(80.0, 540.0, 200.0, 10.0, player.getCurrent("mp") / PLAYER_MAX_MP, 0.2, 0.45, 0.9);
            local er = enemy.getCurrent("hp") / GOBLIN_MAX_HP;
            local flash = hitFlash > 0.0 ? 1.0 : 0.35;
            drawBar(700.0, 520.0, 200.0, 14.0, er, flash, 0.25, 0.25);
        }
    }

    if (has_module("ui") && ui != null) {
        if (!uiBuilt) {
            ui.beginBuild();
            ui.beginWindow("WorkshopHud", "root");
            ui.text("mode", "hud-mode");
            ui.text("status", "hud-pos");
            ui.text("log", "hud-journal");
            ui.end();
            ui.mountBuildAs("hud");
            ui.select("hud");
            ui.setHostOverlay(true);
            ui.setHostPos(14.0, 14.0, 0.0, 0.0);
            uiBuilt = true;
        }
        ui.setText("hud-mode", "mode " + mode + "  |  " + notice);
        ui.setText("hud-pos", "cell (" + worldX + "," + worldY + ") gold " + gold +
                              " quest " + (questAccepted ? "yes" : "no") +
                              " goblin " + (goblinAlive ? "alive" : "dead") +
                              " prop (" + propX + "," + propY + ")");
        local lines = "";
        for (local i = 0; i < journal.len(); i += 1)
            lines += journal[i] + "\n";
        ui.setText("hud-journal", lines);
        ui.beginFrameAndRender();
    }
}
