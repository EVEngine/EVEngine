// Maze tower-defense vertical slice: building + path-open probe + WaveDirector +
// sensing first-target + economy. Simulation uses a fixed 30 Hz step.

persist mapMod = null
persist layer = null
persist pathfinder = null
persist building = null
persist world = null
persist ghost = null
persist sensingMod = null
persist sense = null
persist economy = null
persist td = null
persist director = null
persist defenseBase = null
persist font = null

persist cell = 40.0
persist gridW = 18
persist gridH = 14
persist originX = 40.0
persist originY = 80.0
persist spawnX = 0
persist spawnY = 6
persist goalX = 17
persist goalY = 6
persist gold = 0
persist palette = ["tower.sentry", "tower.cannon"]
persist paletteIndex = 0
persist creeps = []
persist towers = []
persist shots = []
persist statusText = "Place towers, keep the lane open, then Space to start."
persist tick = 0
persist accumulator = 0.0
persist prevKeys = {}
persist prevMouseLeft = false
persist nextCreepId = 1
persist scheduleStarted = false

const GID_FLOOR = 1
const GID_BLOCKED = 2
const PLAYER_ID = 1
const STEP = 0.0333333

function min(a, b) {
    return a < b ? a : b;
}

function sqrt(v) {
    if (v <= 0.0) return 0.0;
    local x = v;
    for (local i = 0; i < 8; ++i) x = 0.5 * (x + v / x);
    return x;
}

function requireOk(result, label) {
    if (!result.ok) throw label + ": " + result.status.summary;
    return result.value;
}

function keyPressed(name) {
    local down = keyboard.isDown(name);
    local was = (name in prevKeys) ? prevKeys[name] : false;
    prevKeys[name] <- down;
    return down && !was;
}

function mousePressed() {
    local down = mouse.isDown(1);
    local pressed = down && !prevMouseLeft;
    prevMouseLeft = down;
    return pressed;
}

function cellToWorld(cx, cy) {
    return { x = originX + (cx + 0.5) * cell, y = originY + (cy + 0.5) * cell };
}

function worldToCell(wx, wy) {
    return {
        x = ((wx - originX) / cell).tointeger(),
        y = ((wy - originY) / cell).tointeger()
    };
}

function registerContent() {
    building.registerBuildingsFromJson(@"[
      {""id"":""tower.sentry"",""displayName"":""Sentry"",""category"":""tower"",
       ""footprintW"":1,""footprintH"":1,""tags"":[""tower""],""requireTerrain"":[1],
       ""cost"":{""gold"":40},
       ""extra"":{""range"":""140"",""damage"":""8"",""cooldown"":""0.55"",""sellRatio"":""0.6""}},
      {""id"":""tower.cannon"",""displayName"":""Cannon"",""category"":""tower"",
       ""footprintW"":1,""footprintH"":1,""tags"":[""tower""],""requireTerrain"":[1],
       ""cost"":{""gold"":70},
       ""extra"":{""range"":""170"",""damage"":""18"",""cooldown"":""0.95"",""sellRatio"":""0.55""}}
    ]");
    requireOk(director.loadScheduleJson(@"[
      {""id"":""w1"",""prepSeconds"":2.0,""rewardGold"":30,
       ""entries"":[{""archetypeId"":""creep.basic"",""count"":6,""intervalSeconds"":0.7}]},
      {""id"":""w2"",""prepSeconds"":3.0,""rewardGold"":50,
       ""entries"":[
         {""archetypeId"":""creep.basic"",""count"":5,""intervalSeconds"":0.55},
         {""archetypeId"":""creep.tank"",""count"":2,""intervalSeconds"":1.1,""delaySeconds"":0.4}
       ]},
      {""id"":""w3"",""prepSeconds"":3.5,""rewardGold"":80,
       ""entries"":[{""archetypeId"":""creep.tank"",""count"":4,""intervalSeconds"":0.9}]}
    ]"), "schedule");
}

function archetypeStats(id) {
    if (id == "creep.tank") return { hp = 60.0, speed = 55.0, reward = 12 };
    return { hp = 28.0, speed = 78.0, reward = 6 };
}

function towerExtra(id, key, fallback) {
    // Building definition extras are stringly typed; parse at use sites.
    local text = building.getBuildingExtra(id, key);
    if (text == null || text == "") return fallback;
    return text.tofloat();
}

function rebuildNavFromBuildings() {
    for (local y = 0; y < gridH; ++y)
        for (local x = 0; x < gridW; ++x)
            pathfinder.setBlocked(x, y, false);
    pathfinder.setBlocked(spawnX, spawnY, false);
    pathfinder.setBlocked(goalX, goalY, false);
    for (local y = 0; y < gridH; ++y) {
        for (local x = 0; x < gridW; ++x) {
            local occ = world.getOccupant(x, y);
            if (occ != 0) pathfinder.setBlocked(x, y, true);
        }
    }
}

function canAfford(defId) {
    local cost = building.getBuildingCost(defId, "gold");
    return economy.get(PLAYER_ID, "gold") >= cost;
}

function tryPlace(cx, cy) {
    local defId = palette[paletteIndex];
    if (cx <= 0 || cy <= 0 || cx >= gridW - 1 || cy >= gridH - 1) {
        statusText = "Cannot build on the border.";
        return;
    }
    if ((cx == spawnX && cy == spawnY) || (cx == goalX && cy == goalY)) {
        statusText = "Spawn and goal cells are reserved.";
        return;
    }
    if (!canAfford(defId)) {
        statusText = "Not enough gold.";
        return;
    }
    local open = pathfinder.keepsPathOpenFootprint(spawnX, spawnY, goalX, goalY, cx, cy, 1, 1);
    if (!open.ok || !open.value) {
        statusText = "That tower would seal the path.";
        return;
    }
    ghost.setBuildingId(defId);
    local pos = cellToWorld(cx, cy);
    ghost.setFromWorld(world, pos.x, pos.y);
    if (!ghost.validate(world)) {
        statusText = "Cannot place: " + ghost.getReason();
        return;
    }
    local cost = building.getBuildingCost(defId, "gold");
    if (!economy.debit(PLAYER_ID, "gold", cost)) {
        statusText = "Not enough gold.";
        return;
    }
    local id = world.placeGhost(ghost);
    towers.append({
        id = id,
        defId = defId,
        x = pos.x,
        y = pos.y,
        range = towerExtra(defId, "range", 140.0),
        damage = towerExtra(defId, "damage", 8.0),
        cooldown = towerExtra(defId, "cooldown", 0.6),
        cool = 0.0
    });
    rebuildNavFromBuildings();
    gold = economy.get(PLAYER_ID, "gold");
    statusText = "Placed " + defId;
}

function spawnCreep(archetypeId) {
    local stats = archetypeStats(archetypeId);
    local id = "creep-" + nextCreepId;
    nextCreepId += 1;
    local pos = cellToWorld(spawnX, spawnY);
    local path = pathfinder.findPath(spawnX, spawnY, goalX, goalY);
    local nodes = [];
    if (path != null) {
        for (local i = 0; i < path.getLength(); ++i)
            nodes.append({ x = path.getX(i), y = path.getY(i) });
    }
    creeps.append({
        id = id,
        archetype = archetypeId,
        x = pos.x,
        y = pos.y,
        hp = stats.hp,
        maxHp = stats.hp,
        speed = stats.speed,
        reward = stats.reward,
        path = nodes,
        pathIndex = 0,
        progress = 0.0
    });
    requireOk(sense.upsert(id, pos.x, pos.y, "enemy", "creep", ""), "sensing upsert");
    requireOk(sense.setMetrics(id, 0.0, stats.hp), "sensing metrics");
}

function updateCreeps(dt) {
    for (local i = creeps.len() - 1; i >= 0; --i) {
        local c = creeps[i];
        if (c.hp <= 0) {
            requireOk(sense.remove(c.id), "sensing remove");
            economy.credit(PLAYER_ID, "gold", c.reward);
            creeps.remove(i);
            continue;
        }
        if (c.pathIndex >= c.path.len()) {
            requireOk(defenseBase.leak(1), "leak");
            requireOk(sense.remove(c.id), "sensing remove");
            creeps.remove(i);
            statusText = "Leak! Lives " + defenseBase.lives();
            continue;
        }
        local node = c.path[c.pathIndex];
        local target = cellToWorld(node.x, node.y);
        local dx = target.x - c.x;
        local dy = target.y - c.y;
        local dist = sqrt(dx * dx + dy * dy);
        local step = c.speed * dt;
        if (dist <= step || dist < 0.001) {
            c.x = target.x;
            c.y = target.y;
            c.pathIndex += 1;
        } else {
            c.x += dx / dist * step;
            c.y += dy / dist * step;
        }
        local total = c.path.len().tofloat();
        c.progress = total > 0.0 ? c.pathIndex.tofloat() / total : 1.0;
        requireOk(sense.upsert(c.id, c.x, c.y, "enemy", "creep", ""), "sensing upsert");
        requireOk(sense.setMetrics(c.id, c.progress, c.hp), "sensing metrics");
    }
    gold = economy.get(PLAYER_ID, "gold");
}

function updateTowers(dt) {
    foreach (t in towers) {
        t.cool -= dt;
        if (t.cool > 0.0) continue;
        local n = requireOk(sense.circleSorted(t.x, t.y, t.range, "creep", "", "", "", "", 1, "first"), "target");
        if (n <= 0) continue;
        local target = sense.resultAt(0);
        if (target == null) continue;
        local tid = target.getId();
        local creep = null;
        foreach (c in creeps) if (c.id == tid) { creep = c; break; }
        if (creep == null) continue;
        creep.hp -= t.damage;
        shots.append({ x0 = t.x, y0 = t.y, x1 = creep.x, y1 = creep.y, life = 0.12 });
        t.cool = t.cooldown;
        requireOk(sense.setMetrics(creep.id, creep.progress, creep.hp), "sensing metrics");
    }
    for (local i = shots.len() - 1; i >= 0; --i) {
        shots[i].life -= dt;
        if (shots[i].life <= 0.0) shots.remove(i);
    }
}

function consumeDirector(dt) {
    if (!scheduleStarted) return;
    tick += 1;
    local step = requireOk(director.advance(tick, dt), "wave advance");
    for (local i = 0; i < step.spawnCount; ++i)
        spawnCreep(step.spawns[i].archetypeId);
    for (local i = 0; i < step.eventCount; ++i) {
        local ev = step.events[i];
        if (ev.kind == "waveCompleted") {
            economy.credit(PLAYER_ID, "gold", ev.rewardGold);
            statusText = "Wave clear +" + ev.rewardGold + "g";
        } else if (ev.kind == "scheduleCompleted") {
            statusText = "All waves cleared.";
        } else if (ev.kind == "waveStarted") {
            statusText = "Wave " + (ev.waveIndex + 1) + " incoming";
        }
    }
    gold = economy.get(PLAYER_ID, "gold");
}

function resetLevel() {
    creeps.clear();
    towers.clear();
    shots.clear();
    tick = 0;
    scheduleStarted = false;
    nextCreepId = 1;
    if (sense != null) {
        // Recreate sensing world to drop stale subjects.
        local created = sensingMod.newWorld();
        sense = requireOk(created, "sensing world");
    }
    world = building.newWorld(gridW, gridH, cell);
    world.setId("td-lane");
    world.fillTerrain(1);
    ghost = building.newGhost();
    rebuildNavFromBuildings();
    requireOk(defenseBase.reset(20), "base reset");
    requireOk(director.reset(), "director reset");
    economy.debit(PLAYER_ID, "gold", economy.get(PLAYER_ID, "gold"));
    economy.credit(PLAYER_ID, "gold", 120);
    gold = economy.get(PLAYER_ID, "gold");
    statusText = "Place towers, keep the lane open, then Space to start.";
}

function simStep(dt) {
    if (keyPressed("1")) paletteIndex = 0;
    if (keyPressed("2")) paletteIndex = 1;
    if (keyPressed("r")) resetLevel();
    if (keyPressed("space") && !scheduleStarted) {
        requireOk(director.start(), "director start");
        scheduleStarted = true;
        statusText = "Waves started.";
    }
    if (mousePressed()) {
        local c = worldToCell(mouse.getX().tofloat(), mouse.getY().tofloat());
        if (c.x >= 0 && c.y >= 0 && c.x < gridW && c.y < gridH)
            tryPlace(c.x, c.y);
    }
    consumeDirector(dt);
    updateCreeps(dt);
    updateTowers(dt);
    if (defenseBase.isDefeated()) statusText = "Defeat - press R to retry.";
}

eve_init <- function() {
    gfx.setBackgroundColor(0.07, 0.09, 0.12, 1.0);
    font = gfx.newFont(eve.Font().newFontDataFromFile("assets/fonts/DejaVuSans-Bold.ttf", 20),
        " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/.-:+");
    gfx.setFont(font);

    mapMod = eve.Map();
    layer = mapMod.newLayer(gridW, gridH, cell, cell);
    layer.fill(GID_FLOOR);
    pathfinder = mapMod.newPathfinder(layer);
    pathfinder.setTopology("ortho4");

    building = eve.Building();
    sensingMod = eve.Sensing();
    sense = requireOk(sensingMod.newWorld(), "sensing world");
    economy = eve.Economy();
    if (!economy.registerResourceType("gold", "currency", 9999, "finite"))
        throw "gold register failed";
    td = eve.TowerDefense();
    director = td.newDirector();
    defenseBase = td.newBase(20);
    registerContent();
    resetLevel();
    print("TOWER DEFENSE ready\n");
};

eve_update <- function(dt) {
    accumulator += min(dt, 0.1);
    while (accumulator >= STEP) {
        simStep(STEP);
        accumulator -= STEP;
    }
};

eve_render <- function() {
    gfx.clear();
    for (local y = 0; y < gridH; ++y) {
        for (local x = 0; x < gridW; ++x) {
            local shade = ((x + y) % 2 == 0) ? 0.16 : 0.13;
            gfx.drawSolidRect(originX + x * cell, originY + y * cell, cell - 1.0, cell - 1.0,
                shade, shade + 0.02, shade + 0.04, 1.0);
        }
    }
    local spawn = cellToWorld(spawnX, spawnY);
    local goal = cellToWorld(goalX, goalY);
    gfx.drawSolidRect(spawn.x - 14.0, spawn.y - 14.0, 28.0, 28.0, 0.2, 0.7, 0.35, 1.0);
    gfx.drawSolidRect(goal.x - 14.0, goal.y - 14.0, 28.0, 28.0, 0.85, 0.3, 0.25, 1.0);

    foreach (t in towers) {
        local color = t.defId == "tower.cannon" ? [0.75, 0.45, 0.2] : [0.35, 0.55, 0.85];
        gfx.drawSolidRect(t.x - 14.0, t.y - 14.0, 28.0, 28.0, color[0], color[1], color[2], 1.0);
    }
    foreach (c in creeps) {
        local r = c.archetype == "creep.tank" ? 16.0 : 11.0;
        gfx.drawSolidRect(c.x - r, c.y - r, r * 2.0, r * 2.0, 0.55, 0.2, 0.65, 1.0);
        local ratio = c.maxHp > 0.0 ? c.hp / c.maxHp : 0.0;
        gfx.drawSolidRect(c.x - 12.0, c.y - r - 8.0, 24.0 * ratio, 3.0, 0.9, 0.3, 0.25, 1.0);
    }
    foreach (s in shots)
        gfx.drawSolidRect(s.x0, s.y0, 3.0, 3.0, 1.0, 0.85, 0.3, 1.0);

    gfx.print("EVENGINE / TOWER DEFENSE", 36.0, 24.0, 0.7, 0.85, 0.9, 1.0, 0.7);
    gfx.print("Gold " + gold + "   Lives " + defenseBase.lives() + "   Wave " + (director.waveIndex() + 1) +
        " / " + director.waveCount() + "   [" + director.phaseName() + "]", 36.0, 48.0, 0.95, 0.9, 0.75, 1.0, 0.7);
    gfx.print("Selected: " + palette[paletteIndex] + "   " + statusText, 36.0, 740.0, 0.75, 0.8, 0.85, 1.0, 0.65);
    gfx.print("1/2 select   LMB place   Space start   R reset", 36.0, 766.0, 0.55, 0.65, 0.7, 1.0, 0.55);
};
