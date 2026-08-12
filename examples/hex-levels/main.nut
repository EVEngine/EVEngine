// ============================================================================
// EVEngine 六边形关卡测试套件 —— Hex Levels
//
// 用程序化生成的六边形地牢，串联验证引擎能力：
//   1 程序化 hex tilemap + A* 寻路
//   2 2D 动态视野（FOV / 战争迷雾）
//   3 2D 动态点光源（火把跟随 + 环境光）
//   4 空间哈希拾取碰撞 + Inventory
//   5 粒子（火把 / 拾取爆发）
//   6 Flow Field 群体寻路可视化
//   7 格子代价绕路
//   8 多观察者 + 感知检测
//   9 FoW 遮罩强度 / 算法切换
//
// 关卡可独立切换；数字键 1–9 选关，0 为综合通关模式。
// 操作：WASD / 方向键移动  E 拾取  R 重生成  N 下一关  T 切换 FOV 算法
// 运行： make run/<platform>-debug GAME=examples/hex-levels
// ============================================================================

if (!("layer" in getroottable())) layer <- null;
if (!("pf" in getroottable())) pf <- null;
if (!("fov" in getroottable())) fov <- null;
if (!("revealerId" in getroottable())) revealerId <- -1;
if (!("path" in getroottable())) path <- null;
if (!("hash" in getroottable())) hash <- null;
if (!("inv" in getroottable())) inv <- null;
if (!("bag" in getroottable())) bag <- null;
if (!("torchLight" in getroottable())) torchLight <- null;
if (!("cam" in getroottable())) cam <- null;
if (!("torchFx" in getroottable())) torchFx <- null;
if (!("sparkFx" in getroottable())) sparkFx <- null;
if (!("level" in getroottable())) level <- 1;
if (!("seed" in getroottable())) seed <- 42;
if (!("algo" in getroottable())) algo <- "dungeon.bsp";
if (!("playerTx" in getroottable())) playerTx <- 0;
if (!("playerTy" in getroottable())) playerTy <- 0;
if (!("exitTx" in getroottable())) exitTx <- 0;
if (!("exitTy" in getroottable())) exitTy <- 0;
if (!("spawnTx" in getroottable())) spawnTx <- 0;
if (!("spawnTy" in getroottable())) spawnTy <- 0;
if (!("moveCd" in getroottable())) moveCd <- 0.0;
if (!("status" in getroottable())) status <- "";
if (!("logLines" in getroottable())) logLines <- [];
if (!("prevKeys" in getroottable())) prevKeys <- {};
if (!("loot" in getroottable())) loot <- [];
if (!("uiBuilt" in getroottable())) uiBuilt <- false;
if (!("pathLen" in getroottable())) pathLen <- 0;
if (!("visibleCount" in getroottable())) visibleCount <- 0;
if (!("exploredCount" in getroottable())) exploredCount <- 0;
if (!("flowPaths" in getroottable())) flowPaths <- [];
if (!("fovAlgo" in getroottable())) fovAlgo <- "shadowcast";
if (!("torchRevealerId" in getroottable())) torchRevealerId <- -1;
if (!("mudCells" in getroottable())) mudCells <- [];
if (!("fixturesLoaded" in getroottable())) fixturesLoaded <- false;
if (!("activeLootTable" in getroottable())) activeLootTable <- "starter";
if (!("lastCatalogLevel" in getroottable())) lastCatalogLevel <- -1;
if (!("levelCfg" in getroottable())) levelCfg <- null;
if (!("featureOn" in getroottable())) featureOn <- {};
if (!("fovRadius" in getroottable())) fovRadius <- 6;
if (!("heroPerception" in getroottable())) heroPerception <- 0.0;
if (!("perceptionScale" in getroottable())) perceptionScale <- 0.0;
if (!("torchFovRadius" in getroottable())) torchFovRadius <- 3;
if (!("cellCostValue" in getroottable())) cellCostValue <- 8.0;
if (!("cellCostStrip" in getroottable())) cellCostStrip <- 4;
if (!("swarmStarts" in getroottable())) swarmStarts <- 4;

TILE_W <- 48.0;
TILE_H <- 28.0;
HEX_SIDE <- 14.0;
MAP_W <- 36;
MAP_H <- 26;
WALL_GID <- 1;
FLOOR_GID <- 2;
DOOR_GID <- 3;
PICK_R <- 18.0;

LEVEL_NAMES <- {
    [0] = "综合通关",
    [1] = "程序化寻路",
    [2] = "动态视野",
    [3] = "动态光照",
    [4] = "拾取碰撞",
    [5] = "粒子系统",
    [6] = "Flow Field 群体",
    [7] = "格子代价绕路",
    [8] = "多观察者感知",
    [9] = "FoW 遮罩算法"
};

function pushLog(text) {
    logLines.push(text);
    while (logLines.len() > 6)
        logLines.remove(0);
}

function readTextFile(path) {
    local handle = file(path, "r");
    local content = "";
    local n = handle.len();
    for (local i = 0; i < n; i++)
        content += handle.readn('b').tochar();
    handle.close();
    return content;
}

function ensureFixtures() {
    if (fixturesLoaded) return;
    try {
        dofile("data/fixtures.nut");
        fixturesLoaded = true;
    } catch (e) {
        pushLog("fixtures.nut 加载失败: " + e);
        fixturesLoaded = false;
    }
}

function featureEnabled(name, fallback = false) {
    if (name in featureOn) return featureOn[name];
    return fallback;
}

function applyLevelBootConfig() {
    featureOn = {};
    levelCfg = null;
    fovRadius = 6;
    heroPerception = 0.0;
    perceptionScale = 0.0;
    torchFovRadius = 3;
    cellCostValue = 8.0;
    cellCostStrip = 4;
    swarmStarts = 4;
    activeLootTable = "starter";

    if (!fixturesLoaded || !(level in LEVEL_CATALOG)) return;
    local meta = LEVEL_CATALOG[level];
    levelCfg = meta;
    if (lastCatalogLevel != level) {
        seed = meta.seed;
        algo = meta.algo;
        lastCatalogLevel = level;
    }
    MAP_W = meta.w;
    MAP_H = meta.h;
    activeLootTable = meta.loot;

    if ("enable" in meta) {
        foreach (k, v in meta.enable)
            featureOn[k] <- v;
    }
    if ("fov" in meta) {
        local f = meta.fov;
        if ("algorithm" in f) fovAlgo = f.algorithm;
        if ("radius" in f) fovRadius = f.radius;
        if ("heroRadius" in f) fovRadius = f.heroRadius;
        if ("heroPerception" in f) heroPerception = f.heroPerception;
        if ("torchRadius" in f) torchFovRadius = f.torchRadius;
        if ("perceptionScale" in f) perceptionScale = f.perceptionScale;
        if (("enabled" in f) && !f.enabled)
            featureOn.fov <- false;
    }
    if ("light" in meta && torchLight != null) {
        local L = meta.light;
        if ("radius" in L) torchLight.setRadius(L.radius);
        if ("color" in L && L.color.len() >= 4)
            torchLight.setColor(L.color[0], L.color[1], L.color[2], L.color[3]);
        if (("type" in L) && L.type == "point")
            torchLight.setType("point");
    }
    if ("cellCost" in meta) {
        cellCostValue = meta.cellCost.cost;
        cellCostStrip = meta.cellCost.stripWidth;
        featureOn.cellcost <- true;
    }
    if ("swarmStarts" in meta) {
        swarmStarts = meta.swarmStarts;
        featureOn.flow <- true;
    }
}

function applyProcgenParams(p) {
    if (levelCfg != null && ("params" in levelCfg)) {
        local pr = levelCfg.params;
        if ("loops" in pr) p.setInt("loops", pr.loops);
        if ("fill" in pr) p.setFloat("fill", pr.fill);
        if ("floorPct" in pr) p.setFloat("floorPct", pr.floorPct);
        if ("preset" in pr) p.setString("preset", pr.preset);
        if ("maxAttempts" in pr) p.setInt("maxAttempts", pr.maxAttempts);
        return;
    }
    if (algo == "cave.cellular") {
        p.setInt("loops", 4);
        p.setFloat("fill", 0.45);
    } else if (algo == "cave.drunkard") {
        p.setFloat("floorPct", 0.42);
    } else if (algo == "wfc.simple") {
        p.setString("preset", "dungeon");
        p.setInt("maxAttempts", 64);
    }
}

function keyPressed(name) {
    local down = keyboard.isDown(name);
    local key = "k_" + name;
    local was = (key in prevKeys) ? prevKeys[key] : false;
    prevKeys[key] <- down;
    return down && !was;
}

function keyDown(name) {
    return keyboard.isDown(name);
}

function configureHex(layerRef) {
    // TileLayer orientation via Tiled-compatible JSON globals.
    layerRef.applyConfig(@"{" +
        "\"orientation\":\"hexagonal\"," +
        "\"staggeraxis\":\"y\"," +
        "\"staggerindex\":\"odd\"," +
        "\"hexsidelength\":" + HEX_SIDE +
        "}");
}

function bindPalette() {
    procgen.setPaletteGid("hex_levels", "empty", 0);
    procgen.setPaletteGid("hex_levels", "wall", WALL_GID);
    procgen.setPaletteGid("hex_levels", "floor", FLOOR_GID);
    procgen.setPaletteGid("hex_levels", "corridor", FLOOR_GID);
    procgen.setPaletteGid("hex_levels", "door", DOOR_GID);
}

function ensureEntities() {
    ensureFixtures();
    if (inv == null) inv = eve.Inventory();
    if (bag == null) {
        local itemsJson = null;
        try {
            itemsJson = readTextFile("data/items.json");
        } catch (e) {
            itemsJson = null;
            pushLog("items.json 读取失败，使用内嵌定义");
        }
        if (itemsJson != null && itemsJson.len() > 2)
            inv.registerItemsFromJson(itemsJson);
        else
            inv.registerItemsFromJson(@"[
              {""id"":""hex.potion"",""displayName"":""六角药水"",""maxStack"":10,""weight"":0.2,""tags"":[""loot"",""potion""]},
              {""id"":""hex.key"",""displayName"":""黄铜钥匙"",""maxStack"":1,""weight"":0.1,""tags"":[""loot"",""key""]},
              {""id"":""hex.gem"",""displayName"":""地牢宝石"",""maxStack"":5,""weight"":0.05,""tags"":[""loot"",""gem""]}
            ]");
        bag = inv.newBag(24);
        bag.setId("hex.player");
        bag.setMaxWeight(80.0);
    }
    if (hash == null) hash = spatial.newSpatialHash2D(40.0);
    if (cam == null) {
        // Script class on eve table; createCamera() marks it active.
        cam = eve.Camera2D();
        cam.setZoom(1.0);
        cam.setAmbient(0.12, 0.12, 0.16);
    }
    if (torchLight == null) {
        torchLight = eve.Light2D();
        torchLight.setType("point");
        torchLight.setColor(1.0, 0.78, 0.45, 2.0);
        torchLight.setRadius(160.0);
        torchLight.setEnabled(false);
    }
    if (torchFx == null) {
        try {
            torchFx = particles.newEmitterFromFile("data/particles/torch_fire.json");
        } catch (e) {
            torchFx = null;
        }
        if (torchFx == null) {
            torchFx = particles.newEmitter(192);
            torchFx.applyPreset("fire");
            torchFx.setParticleSize(6.0, 6.0);
        }
    }
    if (sparkFx == null) {
        try {
            sparkFx = particles.newEmitterFromFile("data/particles/pickup_burst.json");
        } catch (e) {
            sparkFx = null;
        }
        if (sparkFx == null) {
            sparkFx = particles.newEmitter(96);
            sparkFx.applyPreset("spark");
            sparkFx.setEmitterLife(0.3);
        }
    }
}

function clearLoot() {
    if (hash != null) hash.clear();
    loot = [];
}

function placeLootNear(tx, ty, itemId, lootId) {
    if (pf == null || !pf.isWalkable(tx, ty)) return false;
    if (tx == playerTx && ty == playerTy) return false;
    // tileToWorld* already includes layer origin.
    local wx = layer.tileToWorldX(tx, ty) + TILE_W * 0.5;
    local wy = layer.tileToWorldY(tx, ty) + TILE_H * 0.5;
    local half = 9.0;
    if (!hash.insert(lootId, wx - half, wy - half, wx + half, wy + half))
        return false;
    loot.push({
        id = lootId,
        tx = tx,
        ty = ty,
        itemId = itemId,
        wx = wx,
        wy = wy,
        taken = false
    });
    return true;
}

function rebuildPath() {
    pathLen = 0;
    path = null;
    flowPaths = [];
    if (pf == null) return;
    path = pf.findPath(playerTx, playerTy, exitTx, exitTy);
    if (path != null)
        pathLen = path.getLength();

    if (featureEnabled("flow", level == 0 || level == 6)) {
        local field = pf.buildFlowField(exitTx, exitTy);
        if (field != null) {
            local starts = [
                [playerTx, playerTy],
                [spawnTx + 1, spawnTy],
                [spawnTx, spawnTy + 1],
                [spawnTx - 1, spawnTy]
            ];
            local n = 0;
            foreach (s in starts) {
                if (n >= swarmStarts) break;
                if (!pf.isWalkable(s[0], s[1])) continue;
                if (!field.isReachable(s[0], s[1])) continue;
                local fp = pf.followFlow(field, s[0], s[1]);
                if (fp != null && fp.getLength() > 0) {
                    flowPaths.push(fp);
                    n += 1;
                }
            }
        }
    }
}

function refreshFov() {
    visibleCount = 0;
    exploredCount = 0;
    if (fov == null || !featureEnabled("fov", level != 1)) return;
    if (revealerId >= 0)
        fov.setRevealerPosition(revealerId, playerTx, playerTy);
    fov.compute();
    local w = layer.getMapWidth();
    local h = layer.getMapHeight();
    local y = 0;
    while (y < h) {
        local x = 0;
        while (x < w) {
            if (fov.isVisible(x, y)) visibleCount += 1;
            if (fov.isExplored(x, y)) exploredCount += 1;
            x += 1;
        }
        y += 1;
    }
}

function playerWorldCenter(out) {
    out.x <- layer.tileToWorldX(playerTx, playerTy) + TILE_W * 0.5;
    out.y <- layer.tileToWorldY(playerTx, playerTy) + TILE_H * 0.55;
}

function syncHeroVisuals() {
    local pos = {};
    playerWorldCenter(pos);
    if (cam != null)
        cam.setPosition(pos.x, pos.y);

    local lightOn = featureEnabled("light", level == 0 || level >= 3);
    if (torchLight != null) {
        torchLight.setPosition(pos.x + 4.0, pos.y - 2.0);
        torchLight.setEnabled(lightOn);
        if (lightOn)
            cam.setAmbient(0.04, 0.04, 0.06);
        else
            cam.setAmbient(0.16, 0.16, 0.20);
    }
    local fxOn = featureEnabled("particles", level == 0 || level >= 5);
    if (torchFx != null) {
        torchFx.setPosition(pos.x + 4.0, pos.y - 6.0);
        if (fxOn) {
            if (!torchFx.isActive()) torchFx.start();
        } else {
            if (!torchFx.isStopped()) torchFx.stop();
        }
    }
}

function regenerate() {
    ensureEntities();
    bindPalette();
    clearLoot();
    applyLevelBootConfig();

    if (layer == null) {
        layer = map.newLayer(MAP_W, MAP_H, TILE_W, TILE_H);
        layer.setOrigin(48.0, 80.0);
        layer.setLayer(0);
        layer.setVisible(true);
    } else {
        layer.resize(MAP_W, MAP_H);
    }
    configureHex(layer);
    layer.setCamera(cam);

    local p = procgen.newParams();
    p.setSeed(seed);
    p.setSize(MAP_W, MAP_H);
    applyProcgenParams(p);

    local out = procgen.newOutput();
    out.setTarget("tilelayer");
    out.setLayer(layer);
    out.setPalette("hex_levels");
    if (!procgen.generateTo(algo, p, out)) {
        status = "生成失败: " + procgen.lastError();
        pushLog(status);
        return;
    }

    local grid = procgen.generate(algo, p);
    spawnTx = -1;
    spawnTy = -1;
    exitTx = -1;
    exitTy = -1;
    if (grid != null) {
        local i = 0;
        while (i < grid.getObjectCount()) {
            local t = grid.getObjectType(i);
            local ox = grid.getObjectX(i).tointeger();
            local oy = grid.getObjectY(i).tointeger();
            if (t == "spawn") { spawnTx = ox; spawnTy = oy; }
            if (t == "stairs") { exitTx = ox; exitTy = oy; }
            i += 1;
        }
    }
    if (spawnTx < 0) {
        local y = 0;
        while (y < MAP_H && spawnTx < 0) {
            local x = 0;
            while (x < MAP_W && spawnTx < 0) {
                local gid = layer.getTile(x, y);
                if (gid == FLOOR_GID || gid == DOOR_GID) {
                    spawnTx = x; spawnTy = y;
                }
                x += 1;
            }
            y += 1;
        }
    }
    if (exitTx < 0) {
        exitTx = spawnTx;
        exitTy = spawnTy;
        local best = -1;
        local y = 0;
        while (y < MAP_H) {
            local x = 0;
            while (x < MAP_W) {
                local gid = layer.getTile(x, y);
                if (gid == FLOOR_GID || gid == DOOR_GID) {
                    local dx = x - spawnTx;
                    local dy = y - spawnTy;
                    local dist = dx * dx + dy * dy;
                    if (dist > best) {
                        best = dist;
                        exitTx = x;
                        exitTy = y;
                    }
                }
                x += 1;
            }
            y += 1;
        }
    }

    playerTx = spawnTx;
    playerTy = spawnTy;

    pf = map.newPathfinder(layer);
    pf.blockGid(WALL_GID);
    pf.setBlockEmpty(true);
    pf.setTopology("auto");

    mudCells = [];
    if (featureEnabled("cellcost", level == 0 || level == 7)) {
        local half = cellCostStrip / 2;
        if (half < 1) half = 1;
        local y = spawnTy - 1;
        while (y <= spawnTy + 1) {
            local x = spawnTx + 2;
            while (x <= spawnTx + 1 + cellCostStrip) {
                if (pf.isWalkable(x, y)) {
                    pf.setCellCost(x, y, cellCostValue);
                    mudCells.push({ tx = x, ty = y });
                }
                x += 1;
            }
            y += 1;
        }
    }

    fov = map.newFov(layer);
    fov.blockOpaqueGid(WALL_GID);
    fov.setBlockEmpty(false);
    fov.setTopology("auto");
    fov.setAlgorithm(fovAlgo);
    torchRevealerId = -1;
    if (featureEnabled("fov", level != 1)) {
        local radius = fovRadius;
        if (radius < 1) radius = 1;
        if (featureEnabled("perception", level == 0 || level == 8)) {
            fov.setPerceptionRadiusScale(perceptionScale);
            revealerId = fov.addRevealer(playerTx, playerTy, radius);
            fov.setRevealerPerception(revealerId, heroPerception);
            torchRevealerId = fov.addRevealer(exitTx, exitTy, torchFovRadius);
        } else {
            revealerId = fov.addRevealer(playerTx, playerTy, radius);
        }
    } else {
        revealerId = -1;
    }

    rebuildPath();
    local lootId = 1;
    local tableName = activeLootTable;
    if (fixturesLoaded && (tableName in LOOT_TABLES)) {
        local entries = LOOT_TABLES[tableName];
        foreach (e in entries) {
            local tx = spawnTx + e.ox;
            local ty = spawnTy + e.oy;
            if (("pathMid" in e) && e.pathMid && path != null && path.getLength() > 4) {
                local mid = path.getLength() / 2;
                tx = path.getX(mid);
                ty = path.getY(mid);
            }
            placeLootNear(tx, ty, e.itemId, lootId);
            lootId += 1;
        }
    } else {
        placeLootNear(spawnTx + 1, spawnTy, "hex.potion", lootId); lootId += 1;
        placeLootNear(spawnTx, spawnTy + 1, "hex.key", lootId); lootId += 1;
        placeLootNear(spawnTx - 1, spawnTy, "hex.potion", lootId); lootId += 1;
        if (path != null && path.getLength() > 4) {
            local mid = path.getLength() / 2;
            placeLootNear(path.getX(mid), path.getY(mid), "hex.gem", lootId);
        }
    }

    if (bag != null) bag.clear();

    refreshFov();
    syncHeroVisuals();

    local lname = (levelCfg != null && ("name" in levelCfg)) ? levelCfg.name :
                  ((level in LEVEL_NAMES) ? LEVEL_NAMES[level] : ("L" + level));
    status = lname + " | " + algo + " seed=" + seed + " path=" + pathLen +
             " topo=" + pf.getTopology() + " fov=" + fovAlgo + " r=" + fovRadius +
             " loot=" + activeLootTable;
    pushLog("进入关卡: " + lname + " [" + ((levelCfg != null) ? levelCfg.key : ("L" + level)) + "]");
}

function tryMove(dx, dy) {
    local nx = playerTx + dx;
    local ny = playerTy + dy;
    if (pf == null || !pf.isWalkable(nx, ny)) return false;
    playerTx = nx;
    playerTy = ny;
    rebuildPath();
    if (level == 0 || featureEnabled("fov", level >= 2)) refreshFov();
    syncHeroVisuals();
    if (playerTx == exitTx && playerTy == exitTy) {
        pushLog("到达出口！按 R 换种子，或 N 下一关。");
        status = "通关! " + status;
    }
    return true;
}

function tryPickup() {
    if (!featureEnabled("pickup", level == 0 || level >= 4)) {
        pushLog("本关未启用拾取（切到启用 pickup 的关卡）。");
        return;
    }
    local pos = {};
    playerWorldCenter(pos);
    local n = hash.queryCircle(pos.x, pos.y, PICK_R);
    if (n <= 0) {
        pushLog("附近没有可拾取物。");
        return;
    }
    local i = 0;
    while (i < n) {
        local hit = hash.getResultId(i);
        local li = 0;
        while (li < loot.len()) {
            local L = loot[li];
            if (!L.taken && L.id == hit) {
                local added = bag.addItem(L.itemId, 1);
                if (added > 0) {
                    L.taken = true;
                    hash.remove(L.id);
                    pushLog("拾取 " + L.itemId);
                    if (featureEnabled("particles", level == 0 || level >= 5)) {
                        sparkFx.setPosition(L.wx, L.wy);
                        sparkFx.reset();
                        sparkFx.setEmitterLifetime(0.25);
                        sparkFx.start();
                        sparkFx.emit(20);
                    }
                } else {
                    pushLog("背包放不下。");
                }
            }
            li += 1;
        }
        i += 1;
    }
}

function buildUi() {
    ui.beginBuild();
    ui.beginWindow("HexLevels", "root");
    ui.text("六边形关卡测试", "title");
    ui.text("", "status");
    ui.text("", "pos");
    ui.text("", "stats");
    ui.text("", "bag");
    ui.separator("sep");
    ui.text("[1-9]关卡 [0]综合 [N]下一关 [R]重生成", "help1");
    ui.text("[WASD]移动 [E]拾取 [T]FOV算法 [F/G/H]生成", "help2");
    ui.text("", "log");
    ui.end();
    ui.mountBuildAs("hud");
    ui.select("hud");
    ui.setHostOverlay(true);
    ui.setHostPos(12.0, 12.0, 0.0, 0.0);
    uiBuilt = true;
}

function refreshHud() {
    if (!uiBuilt) return;
    ui.select("hud");
    ui.setText("status", status);
    ui.setText("pos", "玩家 (" + playerTx + "," + playerTy + ")  出口 (" +
               exitTx + "," + exitTy + ")");
    ui.setText("stats", "可见 " + visibleCount + "  已探索 " + exploredCount +
               "  路径长 " + pathLen + "  掉落 " + loot.len());
    local bagText = "背包: 药水x" + bag.countItem("hex.potion") +
                    " 钥匙x" + bag.countItem("hex.key") +
                    " 宝石x" + bag.countItem("hex.gem") +
                    " 币x" + bag.countItem("hex.coin");
    ui.setText("bag", bagText);
    local log = "";
    foreach (line in logLines) {
        if (log.len() > 0) log += "\n";
        log += line;
    }
    ui.setText("log", log);
}

function nextLevel() {
    level = (level + 1) % 10;
    regenerate();
}

eve_init = function() {
    gfx.setBackgroundColor(0.05, 0.06, 0.09, 1.0);
    if (map == null) map = eve.Map();
    if (procgen == null) procgen = eve.Procgen();
    if (particles == null) particles = eve.Particles();
    if (spatial == null) spatial = eve.Spatial();
    ensureEntities();
    if (!uiBuilt) buildUi();
    regenerate();
};

eve_update = function(dt) {
    if (moveCd > 0.0) moveCd -= dt;

    if (keyPressed("1")) { level = 1; regenerate(); }
    if (keyPressed("2")) { level = 2; regenerate(); }
    if (keyPressed("3")) { level = 3; regenerate(); }
    if (keyPressed("4")) { level = 4; regenerate(); }
    if (keyPressed("5")) { level = 5; regenerate(); }
    if (keyPressed("6")) { level = 6; regenerate(); }
    if (keyPressed("7")) { level = 7; regenerate(); }
    if (keyPressed("8")) { level = 8; regenerate(); }
    if (keyPressed("9")) { level = 9; regenerate(); }
    if (keyPressed("0")) { level = 0; regenerate(); }
    if (keyPressed("n") || keyPressed("N")) nextLevel();
    if (keyPressed("r") || keyPressed("R")) {
        seed += 1;
        regenerate();
    }
    if (keyPressed("t") || keyPressed("T")) {
        if (fovAlgo == "shadowcast") fovAlgo = "raycast";
        else if (fovAlgo == "raycast") fovAlgo = "permissive";
        else if (fovAlgo == "permissive") fovAlgo = "rectangle";
        else fovAlgo = "shadowcast";
        if (fov != null) {
            fov.setAlgorithm(fovAlgo);
            fov.markDirty();
            refreshFov();
        }
        status = "FOV 算法 → " + fovAlgo;
        pushLog(status);
    }
    if (keyPressed("f") || keyPressed("F")) { algo = "dungeon.bsp"; regenerate(); }
    if (keyPressed("g") || keyPressed("G")) { algo = "cave.cellular"; regenerate(); }
    if (keyPressed("h") || keyPressed("H")) { algo = "wfc.simple"; regenerate(); }
    if (keyPressed("e") || keyPressed("E")) tryPickup();

    if (moveCd <= 0.0) {
        local moved = false;
        if (keyDown("w") || keyDown("W") || keyDown("Up")) moved = tryMove(0, -1);
        else if (keyDown("s") || keyDown("S") || keyDown("Down")) moved = tryMove(0, 1);
        else if (keyDown("a") || keyDown("A") || keyDown("Left")) moved = tryMove(-1, 0);
        else if (keyDown("d") || keyDown("D") || keyDown("Right")) moved = tryMove(1, 0);
        if (moved) moveCd = 0.12;
    }

    if (featureEnabled("particles", level == 0 || level >= 5))
        particles.update(dt);
    map.update(dt);
    refreshHud();
};

eve_render = function() {
    gfx.clear();
    map.render(gfx);

    // Mud / high-cost cells (level 7).
    if (level == 0 || level == 7) {
        foreach (m in mudCells) {
            local wx = layer.tileToWorldX(m.tx, m.ty);
            local wy = layer.tileToWorldY(m.tx, m.ty);
            gfx.drawSolidRect(wx + TILE_W * 0.15, wy + TILE_H * 0.2,
                              TILE_W * 0.7, TILE_H * 0.6, 0.45, 0.32, 0.12, 0.55);
        }
    }

    // A* path breadcrumbs.
    if ((level == 0 || level == 1 || level == 7) && path != null) {
        local i = 0;
        while (i < path.getLength()) {
            local tx = path.getX(i);
            local ty = path.getY(i);
            local wx = layer.tileToWorldX(tx, ty);
            local wy = layer.tileToWorldY(tx, ty);
            gfx.drawSolidRect(wx + TILE_W * 0.35, wy + TILE_H * 0.35,
                              TILE_W * 0.3, TILE_H * 0.3, 0.2, 0.7, 0.9, 0.55);
            i += 1;
        }
    }

    // Flow-field swarm paths (level 6).
    if ((level == 0 || level == 6) && flowPaths.len() > 0) {
        foreach (fp in flowPaths) {
            local i = 0;
            while (i < fp.getLength()) {
                local wx = layer.tileToWorldX(fp.getX(i), fp.getY(i));
                local wy = layer.tileToWorldY(fp.getX(i), fp.getY(i));
                gfx.drawSolidRect(wx + TILE_W * 0.4, wy + TILE_H * 0.4,
                                  TILE_W * 0.2, TILE_H * 0.2, 0.95, 0.55, 0.15, 0.7);
                i += 1;
            }
        }
    }

    // FoW overlay — level 9 uses mask byte intensity.
    if ((level == 0 || level >= 2) && fov != null) {
        local y = 0;
        while (y < layer.getMapHeight()) {
            local x = 0;
            while (x < layer.getMapWidth()) {
                if (layer.getTile(x, y) == 0) { x += 1; continue; }
                local vis = fov.isVisible(x, y);
                if (!vis) {
                    local wx = layer.tileToWorldX(x, y);
                    local wy = layer.tileToWorldY(x, y);
                    if (level == 9) {
                        local mb = fov.getMaskByte(x, y).tofloat();
                        local a = 1.0 - (mb / 255.0);
                        if (a < 0.2) a = 0.2;
                        gfx.drawSolidRect(wx, wy, TILE_W, TILE_H, 0.0, 0.0, 0.05, a);
                    } else if (!fov.isExplored(x, y)) {
                        gfx.drawSolidRect(wx, wy, TILE_W, TILE_H, 0.0, 0.0, 0.0, 0.82);
                    } else {
                        gfx.drawSolidRect(wx, wy, TILE_W, TILE_H, 0.0, 0.0, 0.0, 0.45);
                    }
                }
                x += 1;
            }
            y += 1;
        }
    }

    // Loot markers.
    if (level == 0 || level >= 4) {
        foreach (L in loot) {
            if (L.taken) continue;
            if ((level == 0 || level >= 2) && fov != null && !fov.isVisible(L.tx, L.ty))
                continue;
            gfx.drawSolidRect(L.wx - 5.0, L.wy - 5.0, 10.0, 10.0, 0.95, 0.35, 0.85, 1.0);
        }
    }

    // Exit marker.
    local ex = layer.tileToWorldX(exitTx, exitTy);
    local ey = layer.tileToWorldY(exitTx, exitTy);
    if (level == 1 || level >= 3 || level == 0 ||
        (fov != null && fov.isExplored(exitTx, exitTy)))
        gfx.drawSolidRect(ex + TILE_W * 0.25, ey + TILE_H * 0.25,
                          TILE_W * 0.5, TILE_H * 0.5, 0.25, 0.9, 0.4, 0.85);

    // Hero + torch glow.
    local pos = {};
    playerWorldCenter(pos);
    if (level == 0 || level == 3 || level >= 8) {
        gfx.drawSolidRect(pos.x - 40.0, pos.y - 40.0, 80.0, 80.0, 1.0, 0.7, 0.35, 0.12);
        gfx.drawSolidRect(pos.x - 22.0, pos.y - 22.0, 44.0, 44.0, 1.0, 0.85, 0.45, 0.18);
    }
    // Static exit torch for multi-revealer level.
    if ((level == 0 || level == 8) && torchRevealerId >= 0) {
        local txw = layer.tileToWorldX(exitTx, exitTy) + TILE_W * 0.5;
        local tyw = layer.tileToWorldY(exitTx, exitTy) + TILE_H * 0.5;
        gfx.drawSolidRect(txw - 16.0, tyw - 16.0, 32.0, 32.0, 0.4, 0.7, 1.0, 0.22);
    }
    gfx.drawSolidRect(pos.x - 9.0, pos.y - 9.0, 18.0, 18.0, 0.95, 0.85, 0.35, 1.0);

    if (level == 0 || level == 5 || level >= 8)
        particles.render(gfx);

    ui.beginFrameAndRender();
};
