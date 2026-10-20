// ============================================================================
// RPG Atelier — one project that exercises character / combat / inventory /
// dialogue / map editing / scene editing together, so Agent 与人类能在同一
// 工程里验证「写 RPG + 改地图 + 摆场景」闭环。
//
// 模式（F1–F4）：
//   F1 Explore  方向地图、E 对话 / 开战、Q 喝药、C 看状态
//   F2 Battle   回合战斗：1 斩击 / 2 火球 / 3 治疗 / Esc 逃
//   F3 MapEdit  左键画地(gid=1) 右键画墙(gid=2) 中键擦除 Z 撤销 Y 重做
//   F4 Scene    scene_director 3D 舞台：1 摆箱 2 摆柱 3 重摆相机 R 重置
// ============================================================================

persist mode = "explore"
persist rpg = null
persist player = null
persist enemy = null
persist battle = null
persist inv = null
persist bag = null
persist eq = null
persist gold = 50
persist worldX = 2
persist worldY = 5
persist worldLayer = null
persist worldPath = null
persist slimeAlive = true
persist questAccepted = false
persist journal = []
persist hitFlash = 0.0
persist dlgReady = false
persist tileBuf = null
persist brush = null
persist hist = null
persist paintGid = 1
persist lastPaintX = -1
persist lastPaintY = -1
persist lastPaintGid = -1
persist scene_director = null
persist sd = null
persist uiBuilt = false
persist notice = "F1 探索 / F2 战斗 / F3 地图编辑 / F4 场景编辑"

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
        player.setBaseAttribute("hp", 100.0);
        player.setBaseAttribute("mp", 40.0);
        player.setBaseAttribute("attack", 18.0);
        player.setBaseAttribute("defense", 6.0);
        player.setCurrent("hp", 100.0);
        player.setCurrent("mp", 40.0);
        player.learnSkill("skill.slash");
        player.learnSkill("skill.fireball");
        player.learnSkill("skill.heal");
    }
}

function registerContent() {
    ensureRpg();
    local effects = readTextFile("data/effects.json");
    local skills = readTextFile("data/skills.json");
    if (effects != null) rpg.registerEffectsFromJson(effects);
    if (skills != null) rpg.registerSkillsFromJson(skills);
    // Settlement formulas are string expressions (not Squirrel closures).
    // Battle damage in this demo is resolved in script; these registrations
    // keep the data-driven surface honest for tools that read skill defs.
    rpg.registerSkillDamage("skill.slash", "hp", "a.attack - b.defense * 0.5", "", 0.05, 100);
    rpg.registerSkillDamage("skill.fireball", "hp", "a.attack * 1.8", "fire", 0.0, 100);
}

function ensureInventory() {
    if (inv == null) inv = eve.Inventory();
    local items = readTextFile("data/items.json");
    if (items != null) inv.registerItemsFromJson(items);
    if (bag == null) {
        bag = inv.newBag(12);
        bag.setId("atelier.player");
        bag.setKind("backpack");
        bag.setMaxWeight(40.0);
        bag.addRejectTag("quest");
        bag.addItem("potion.hp", 3);
        bag.addItem("sword.iron", 1);
    }
    if (eq == null) {
        eq = inv.newEquipmentSet();
        eq.setId("atelier.eq");
        eq.defineSlot("weapon");
        eq.addSlotAllowedTag("weapon", "weapon");
    }
    inv.clearGameplayControls();
    local published = inv.publishGameplay(
        "00000000-0000-7000-8000-0000000b0001",
        "00000000-0000-7000-8000-0000000b0002",
        bag, eq);
    if (!published.ok)
        logLine("inventory publish failed: " + published.message);
}

function ensureDialogue() {
    if (!has_module("dialogueFlow")) {
        dlgReady = false;
        return;
    }
    local src = readTextFile("data/atelier-dialogue.dnut");
    if (src == null) { dlgReady = false; return; }
    local loaded = dialogueFlow.loadDnutChecked(src, "data/atelier-dialogue.dnut");
    local linted = dialogueFlow.lintAllChecked();
    dlgReady = loaded.ok && linted.ok;
    if (!dlgReady)
        logLine("dialogue load failed");
    // publishGameplay lives on DialogueFlow (not Dialogue presentation).
    local pub = dialogueFlow.publishGameplay(
        "00000000-0000-7000-8000-0000000b0010",
        "00000000-0000-7000-8000-0000000b0002");
    if (!pub.ok)
        logLine("dialogue publish: " + pub.message);
}

function ensureWorld() {
    if (worldLayer != null) return;
    if (!has_module("map") || map == null) map = eve.Map();
    local contract = readTextFile("data/world-object-contract.json");
    if (contract == null) throw "missing world-object-contract.json";
    local admitted = map.loadFromFileWithObjectContract("data/atelier-map.json", contract);
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
        logLine("levelEditor module missing — MapEdit degraded");
        return;
    }
    if (tileBuf == null) {
        local w = 16;
        local h = 10;
        // TileLayer width/height accessors vary by build; keep atelier map size.
        tileBuf = levelEditor.newTileBuffer(w, h);
        brush = levelEditor.newBrush();
        hist = levelEditor.newHistory();
        brush.setTool("paint");
        brush.setTile(1);
        brush.setSize(1);
        brush.setShape("square");
        // Seed buffer from the live layer so paint starts from truth.
        for (local y = 0; y < h; y += 1)
            for (local x = 0; x < w; x += 1)
                tileBuf.setGid(x, y, worldLayer.getTile(x, y));
    }
}

function syncBufferToLayer() {
    if (tileBuf == null || worldLayer == null) return;
    local w = tileBuf.getWidth();
    local h = tileBuf.getHeight();
    for (local y = 0; y < h; y += 1)
        for (local x = 0; x < w; x += 1)
            worldLayer.setTile(x, y, tileBuf.getGid(x, y));
    if (worldPath != null) worldPath.syncFromLayer();
}

function paintAt(tx, ty, gid) {
    if (brush == null || tileBuf == null || hist == null) {
        // Fallback without levelEditor: mutate layer directly.
        if (worldLayer == null) return;
        worldLayer.setTile(tx, ty, gid);
        if (worldPath != null) worldPath.syncFromLayer();
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
        tint = [0.35, 0.42, 0.38], roughness = 0.9
    }));
    sd_require(sd.set_lighting({ timeOfDay = "day", atmosphere = "clear", intensity = 1.0 }));
    sd_require(sd.camera({ eye = [0.0, 12.0, 18.0], target = [0.0, 1.0, 0.0], fov = 55.0 }));
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
        logLine("选择:" + opts);
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
        if (questAccepted == false && dialogueFlow.getActiveConversationId() == "")
            { /* keep */ }
        mode = "explore";
        return;
    }
    presentDialogue();
}

function chooseDialogue(index) {
    if (!dialogueFlow.isActive() || dialogueFlow.getNodeKind() != "choice") return;
    if (index < 0 || index >= dialogueFlow.getRouteCount()) return;
    local route = dialogueFlow.getRouteId(index);
    local sel = dialogueFlow.select(route);
    if (route == "accept") {
        questAccepted = true;
        logLine("任务已接受");
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
            local conv = map.getObjectProperty(i, "conversation", "elder.offer");
            startDialogue(conv);
            return;
        }
        if (typ == "encounter" && slimeAlive) {
            startDialogue("slime.hint");
            return;
        }
    }
    logLine("附近没有可交互对象");
}

function beginBattle() {
    ensureRpg();
    enemy = rpg.newActor();
    enemy.setBaseAttribute("hp", 45.0);
    enemy.setBaseAttribute("mp", 0.0);
    enemy.setBaseAttribute("attack", 10.0);
    enemy.setBaseAttribute("defense", 2.0);
    enemy.setCurrent("hp", 45.0);
    battle = rpg.newBattle();
    battle.addActor(player, 0);
    battle.addActor(enemy, 1);
    mode = "battle";
    logLine("遭遇史莱姆！1斩击 2火球 3治疗 Esc逃");
}

function playerBattleAction(skillId) {
    if (battle == null || battle.isFinished()) return;
    if (skillId == "skill.fireball" && player.getCurrent("mp") < 10.0) {
        logLine("MP 不足");
        return;
    }
    if (skillId == "skill.heal") {
        player.heal("hp", 28.0);
        player.setCurrent("mp", player.getCurrent("mp") - 8.0);
        logLine("治疗 +" + 28);
    } else {
        local dmg = 0.0;
        if (skillId == "skill.slash")
            dmg = player.getFinalAttribute("attack") - enemy.getFinalAttribute("defense") * 0.5;
        else
            dmg = player.getFinalAttribute("attack") * 1.8;
        if (dmg < 1.0) dmg = 1.0;
        if (skillId == "skill.fireball")
            player.setCurrent("mp", player.getCurrent("mp") - 10.0);
        enemy.takeDamage("hp", dmg, "player");
        hitFlash = 0.25;
        logLine(skillId + " 造成 " + floor(dmg + 0.5).tointeger() + " 伤害");
    }
    if (enemy.isDead() || enemy.getCurrent("hp") <= 0.0) {
        slimeAlive = false;
        gold += 12;
        bag.addItem("potion.hp", 1);
        logLine("胜利！+12 金，+1 药水");
        battle = null;
        enemy = null;
        mode = "explore";
        return;
    }
    // Enemy counter
    local edmg = enemy.getFinalAttribute("attack") - player.getFinalAttribute("defense") * 0.4;
    if (edmg < 1.0) edmg = 1.0;
    player.takeDamage("hp", edmg, "slime");
    logLine("史莱姆反击 " + floor(edmg + 0.5).tointeger());
    if (player.isDead() || player.getCurrent("hp") <= 0.0) {
        logLine("战败……按 R 重置");
        battle = null;
        mode = "explore";
        player.setCurrent("hp", 1.0);
    }
}

function drinkPotion() {
    if (bag.countItem("potion.hp") <= 0) { logLine("没有药水"); return; }
    local removed = bag.removeItem("potion.hp", 1);
    if (removed <= 0) { logLine("喝药失败"); return; }
    player.heal("hp", 35.0);
    logLine("喝下治疗药水");
}

function movePlayer(dx, dy) {
    local nx = worldX + dx;
    local ny = worldY + dy;
    if (worldPath == null || !worldPath.isWalkable(nx, ny)) {
        logLine("无法通行");
        return;
    }
    worldX = nx;
    worldY = ny;
    // Step onto encounter tile
    for (local i = 0; i < map.getObjectCount(); i += 1) {
        if (map.getObjectType(i) == "encounter" && slimeAlive &&
            map.getObjectX(i).tointeger() == worldX &&
            map.getObjectY(i).tointeger() == worldY) {
            beginBattle();
            return;
        }
    }
}

function exportMcpTools() {
    if (!("mcp" in eve)) return;
    eve.mcp.tool("atelier_status", {
        description = "RPG Atelier 运行态：模式、坐标、任务、背包、史莱姆",
        inputSchema = { type = "object", properties = {} },
        handler = function(args) {
            return {
                mode = mode,
                x = worldX, y = worldY,
                questAccepted = questAccepted,
                slimeAlive = slimeAlive,
                gold = gold,
                hp = player != null ? player.getCurrent("hp") : 0,
                mp = player != null ? player.getCurrent("mp") : 0,
                potions = bag != null ? bag.countItem("potion.hp") : 0,
                mapEditor = tileBuf != null,
                sceneDirector = sd != null
            };
        }
    });
    eve.mcp.tool("atelier_set_mode", {
        description = "切换 Atelier 模式 explore|battle|map_edit|scene",
        inputSchema = {
            type = "object",
            properties = { mode = { type = "string" } },
            required = ["mode"]
        },
        handler = function(args) {
            local m = ("mode" in args) ? args.mode : "explore";
            // Only flip mode here. Heavy ensure*/director install runs on the
            // next eve_update so MCP tools/call cannot stall the JSON-RPC poll.
            if (m == "map_edit") mode = "map_edit";
            else if (m == "scene") mode = "scene";
            else if (m == "battle") beginBattle();
            else mode = "explore";
            return { ok = true, mode = mode };
        }
    });
}

function eve_init() {
    gfx.setBackgroundColor(0.08, 0.10, 0.14, 1.0);
    registerContent();
    ensureInventory();
    ensureDialogue();
    ensureWorld();
    ensureMapEditor();
    exportMcpTools();
    logLine("RPG Atelier 就绪");
}

function eve_update(dt) {
    if (hitFlash > 0.0) hitFlash -= dt;
    if (has_module("map") && map != null) map.update(dt);
    if (has_module("dialogue") && dialogue != null) dialogue.update(dt);
    if (mode == "scene" && sd != null) sd_require(sd.refresh());

    if (key_just_pressed("F1")) { mode = "explore"; notice = "探索模式"; }
    if (key_just_pressed("F2")) { beginBattle(); notice = "战斗模式"; }
    if (key_just_pressed("F3")) { mode = "map_edit"; notice = "地图编辑：左键地 右键墙 中键擦 Z/Y 撤销重做"; }
    if (key_just_pressed("F4")) { mode = "scene"; notice = "场景编辑：1箱 2柱 3相机 R重置"; }

    // Deferred heavy setup (also covers MCP atelier_set_mode).
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
            logLine("HP " + player.getCurrent("hp") + " MP " + player.getCurrent("mp") +
                    " AT " + gold + " 药 " + bag.countItem("potion.hp"));
        if (key_just_pressed("R")) {
            slimeAlive = true;
            questAccepted = false;
            player.setCurrent("hp", 100.0);
            player.setCurrent("mp", 40.0);
            worldX = 2; worldY = 5;
            logLine("已重置冒险");
        }
    } else if (mode == "battle") {
        if (key_just_pressed("1")) playerBattleAction("skill.slash");
        if (key_just_pressed("2")) playerBattleAction("skill.fireball");
        if (key_just_pressed("3")) playerBattleAction("skill.heal");
        if (key_just_pressed("Escape")) { mode = "explore"; battle = null; logLine("逃离战斗"); }
    } else if (mode == "dialogue") {
        if (key_just_pressed("1")) chooseDialogue(0);
        if (key_just_pressed("2")) chooseDialogue(1);
        if (key_just_pressed("Space") || key_just_pressed("E")) advanceDialogue();
        if (key_just_pressed("Escape")) { dialogueFlow.stop(); mode = "explore"; }
        // After slime hint ends, offer fight key
        if (!dialogueFlow.isActive() && slimeAlive && key_just_pressed("1"))
            beginBattle();
    } else if (mode == "map_edit") {
        if (!(mouse.isDown(1) || mouse.isDown(3) || mouse.isDown(2))) {
            lastPaintX = -1; lastPaintY = -1; lastPaintGid = -1;
        } else if (worldLayer != null) {
            local mx = mouse.getX();
            local my = mouse.getY();
            local tx = worldLayer.worldToTileX(mx.tofloat(), my.tofloat());
            local ty = worldLayer.worldToTileY(mx.tofloat(), my.tofloat());
            if (tx >= 0 && ty >= 0 && tx < 16 && ty < 10) {
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
        if (sd == null) {
            if (key_just_pressed("1") || key_just_pressed("R"))
                logLine("scene_director 不可用");
        } else {
            if (key_just_pressed("1"))
                sd_require(sd.spawn({ id = "crate", kind = "box", pos = [2.0, 0.5, 0.0], scale = [1,1,1], tint = [0.7,0.5,0.3] }));
            if (key_just_pressed("2"))
                sd_require(sd.spawn({ id = "pillar", kind = "cylinder", pos = [-2.0, 1.0, 1.0], scale = [0.6,2.0,0.6], tint = [0.55,0.55,0.6] }));
            if (key_just_pressed("3"))
                sd_require(sd.camera({ eye = [6.0, 8.0, 10.0], target = [0.0, 1.0, 0.0], fov = 50.0 }));
            if (key_just_pressed("R")) {
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
    if (mode == "scene") {
        gfx.render3D();
        if (sd != null) sd.mark_rendered();
    } else {
        if (has_module("map") && map != null) map.render(gfx);
        // Player marker
        if (worldLayer != null) {
            local px = worldLayer.tileToWorldX(worldX, worldY);
            local py = worldLayer.tileToWorldY(worldX, worldY);
            gfx.drawSolidRect(px + 4.0, py + 4.0, 24.0, 24.0, 0.25, 0.75, 1.0, 1.0);
            for (local i = 0; i < map.getObjectCount(); i += 1) {
                local ox = worldLayer.tileToWorldX(map.getObjectX(i).tointeger(), map.getObjectY(i).tointeger());
                local oy = worldLayer.tileToWorldY(map.getObjectX(i).tointeger(), map.getObjectY(i).tointeger());
                local typ = map.getObjectType(i);
                if (typ == "npc")
                    gfx.drawSolidRect(ox + 6.0, oy + 6.0, 20.0, 20.0, 0.9, 0.8, 0.2, 1.0);
                else if (typ == "encounter" && slimeAlive)
                    gfx.drawSolidRect(ox + 6.0, oy + 6.0, 20.0, 20.0, 0.3, 0.85, 0.35, 1.0);
            }
        }
        if (mode == "battle" && player != null && enemy != null) {
            drawBar(80.0, 520.0, 200.0, 14.0, player.getCurrent("hp") / 100.0, 0.2, 0.8, 0.3);
            drawBar(80.0, 540.0, 200.0, 10.0, player.getCurrent("mp") / 40.0, 0.2, 0.45, 0.9);
            local emax = 45.0;
            local er = enemy.getCurrent("hp") / emax;
            local flash = hitFlash > 0.0 ? 1.0 : 0.35;
            drawBar(700.0, 520.0, 200.0, 14.0, er, flash, 0.25, 0.25);
        }
    }

    if (has_module("ui") && ui != null) {
        if (!uiBuilt) {
            ui.beginBuild();
            ui.beginWindow("AtelierHud", "root");
            ui.text("模式", "hud-mode");
            ui.text("状态", "hud-pos");
            ui.text("日志", "hud-journal");
            ui.end();
            ui.mountBuildAs("hud");
            ui.select("hud");
            ui.setHostOverlay(true);
            ui.setHostPos(14.0, 14.0, 0.0, 0.0);
            uiBuilt = true;
        }
        ui.setText("hud-mode", "模式 " + mode + "  |  " + notice);
        ui.setText("hud-pos", "格 (" + worldX + "," + worldY + ") 金 " + gold +
                              " 任务 " + (questAccepted ? "已接" : "未接") +
                              " 史莱姆 " + (slimeAlive ? "在" : "灭"));
        local lines = "";
        for (local i = 0; i < journal.len(); i += 1)
            lines += journal[i] + "\n";
        ui.setText("hud-journal", lines);
        ui.beginFrameAndRender();
    }
}
