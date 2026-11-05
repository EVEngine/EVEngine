// Game-owned host: mirrors poses into SensingWorld, runs per-enemy cone queries,
// writes vision memories into NpcAiWorld, and keeps aggro outside sensing.
function evRequire(result, operation) {
    if (result == null || !result.ok) {
        local summary = result != null && ("status" in result) ? result.status.summary : "null";
        throw "enemy-vision " + operation + " failed: " + summary;
    }
    return result.value;
}

function evClamp(v, lo, hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

function evLen(x, y) {
    return sqrt(x * x + y * y);
}

function evNorm(x, y) {
    local n = evLen(x, y);
    if (n < 0.0001) return {x = 1.0, y = 0.0};
    return {x = x / n, y = y / n};
}

function evConeQueryJson(enemy) {
    // Custom halfAngle/range per enemy — do not rebuild distance loops by hand.
    return "{" +
        "\"schema\":\"eve.sensing.query\",\"version\":1," +
        "\"origin\":{\"space\":\"world2d\",\"x\":" + enemy.x + ",\"y\":" + enemy.y + "}," +
        "\"subjectId\":\"" + enemy.id + "\"," +
        "\"shape\":{\"kind\":\"cone\",\"x\":" + enemy.x + ",\"y\":" + enemy.y +
        ",\"dirX\":" + enemy.dirX + ",\"dirY\":" + enemy.dirY +
        ",\"halfAngle\":" + enemy.halfAngle + ",\"range\":" + enemy.range + "}," +
        "\"requiredTags\":[\"player\"]," +
        "\"maxCount\":1,\"sort\":\"distance\",\"countPolicy\":\"truncate\"" +
        "}";
}

class EnemyVisionSimulation {
    sensingMod = null;
    sensing = null;
    npcMod = null;
    ai = null;
    player = null;
    enemies = null;
    aggro = null;
    tick = 0;
    time = 0.0;
    events = null;
    visionHz = 10;
    visionAccum = 0.0;

    constructor() {
        sensingMod = eve.Sensing();
        npcMod = eve.NpcAi();
        sensing = evRequire(sensingMod.newWorld(), "create SensingWorld");
        ai = evRequire(npcMod.newWorld(256, 8), "create NpcAiWorld");
        evRequire(sensing.setSpatialIndexEnabled(true, 64.0), "enable sensing spatial index");
        evRequire(ai.registerBehavior(enemyVisionBehavior()), "register enemy.vision behavior");

        player = {
            id = "player"
            x = 220.0
            y = 350.0
            vx = 0.0
            vy = 0.0
            radius = 14.0
        };
        enemies = [];
        aggro = {};
        events = [];
        addEnemy("guard.east", 720.0, 220.0, -1.0, 0.0, 0.55, 220.0, 1.6);
        addEnemy("guard.south", 540.0, 520.0, 0.0, -1.0, 0.70, 180.0, 2.1);
        addEnemy("guard.west", 880.0, 400.0, -1.0, 0.2, 0.40, 260.0, 1.2);
        note("three guards online — move into a cone to raise vision memory");
    }

    function addEnemy(id, x, y, dirX, dirY, halfAngle, range, patrolSpeed) {
        local facing = evNorm(dirX, dirY);
        local agent = evRequire(ai.createAgent("enemy.vision"), "create agent " + id);
        enemies.append({
            id = id
            x = x.tofloat()
            y = y.tofloat()
            homeX = x.tofloat()
            homeY = y.tofloat()
            dirX = facing.x
            dirY = facing.y
            halfAngle = halfAngle.tofloat()
            range = range.tofloat()
            patrolSpeed = patrolSpeed.tofloat()
            chaseSpeed = 110.0
            agent = agent
            state = "patrol"
            seesPlayer = false
            patrolPhase = (enemies.len() * 1.7).tofloat()
            flash = 0.0
        });
    }

    function close() {
        foreach (enemy in enemies) {
            if (enemy.agent != null) {
                evRequire(ai.destroyAgent(enemy.agent), "destroy " + enemy.id);
                enemy.agent = null;
            }
        }
        enemies.clear();
        aggro.clear();
    }

    function note(message) {
        events.append({time = time, text = message});
        if (events.len() > 6) events.remove(0);
    }

    function mirrorFacts() {
        // SensingWorld is a mirror of authoritative poses owned by this host.
        evRequire(sensing.upsert(player.id, player.x, player.y, "player", "player,unit", ""),
                  "upsert player");
        foreach (enemy in enemies) {
            evRequire(sensing.upsert(enemy.id, enemy.x, enemy.y, "enemy", "enemy,unit", "player"),
                      "upsert " + enemy.id);
        }
    }

    function refreshVision() {
        mirrorFacts();
        foreach (enemy in enemies) {
            local found = sensing.query(evConeQueryJson(enemy));
            evRequire(found, "cone query " + enemy.id);
            local ranked = found.value.ranked;
            local hit = ranked != null && ranked.len() > 0;
            local previous = enemy.seesPlayer;
            enemy.seesPlayer = hit;
            if (hit) {
                local subject = ranked[0].subject;
                if (subject == null || subject == "") subject = player.id;
                evRequire(ai.setBlackboardString(enemy.agent, "target", subject),
                          "blackboard target " + enemy.id);
                // Short TTL: stop refreshing and the next AI tick emits perception.forgotten.vision.
                // Explicit forget() erases memory but does not queue that wake-up signal.
                evRequire(ai.remember(enemy.agent, subject, "vision", 0.95, tick, 2, "{}"),
                          "remember vision " + enemy.id);
                // Aggro / primary stay game-owned — sensing never picks a threat table.
                aggro[enemy.id] <- subject;
                if (!previous) {
                    enemy.flash = 0.35;
                    note(enemy.id + " saw " + subject);
                }
            }
            // Missed cone: do not refresh remember; expiry drives patrol transition.
        }
        local report = evRequire(ai.tick(tick, 1.0 / visionHz, enemies.len(), 4), "tick NpcAi");
        foreach (enemy in enemies) {
            local snap = evRequire(ai.snapshot(enemy.agent), "snapshot " + enemy.id);
            local nextState = snap.activeState;
            if (enemy.state == "chase" && nextState == "patrol") {
                if (enemy.id in aggro) delete aggro[enemy.id];
                note(enemy.id + " lost sight");
            }
            enemy.state = nextState;
        }
        return report;
    }

    function moveToward(unit, tx, ty, speed, dt) {
        local dx = tx - unit.x;
        local dy = ty - unit.y;
        local n = evLen(dx, dy);
        if (n < 1.0) return;
        local step = speed * dt;
        if (step > n) step = n;
        unit.x += dx / n * step;
        unit.y += dy / n * step;
    }

    function faceToward(enemy, tx, ty) {
        local facing = evNorm(tx - enemy.x, ty - enemy.y);
        enemy.dirX = facing.x;
        enemy.dirY = facing.y;
    }

    function verifyPipeline() {
        // Deterministic smoke: place the player inside guard.east's cone, then outside.
        local guard = enemies[0];
        player.x = guard.x + guard.dirX * (guard.range * 0.45);
        player.y = guard.y + guard.dirY * (guard.range * 0.45);
        for (local i = 0; i < 4; i += 1) {
            tick += 1;
            refreshVision();
        }
        if (guard.state != "chase") throw "verify: expected chase after cone hit, got " + guard.state;
        if (!(guard.id in aggro)) throw "verify: aggro missing after vision";
        player.x = 40.0;
        player.y = 40.0;
        for (local i = 0; i < 6; i += 1) {
            tick += 1;
            refreshVision();
        }
        if (guard.state != "patrol") throw "verify: expected patrol after leave, got " + guard.state;
        if (guard.id in aggro) throw "verify: aggro should clear on forgotten vision";
        note("pipeline verify passed");
        return true;
    }

    function step(dt, input) {
        tick += 1;
        time += dt;
        visionAccum += dt;

        local speed = 180.0;
        player.vx = input.x * speed;
        player.vy = input.y * speed;
        player.x = evClamp(player.x + player.vx * dt, 40.0, 1060.0);
        player.y = evClamp(player.y + player.vy * dt, 40.0, 660.0);

        // Vision queries are rate-limited; movement still runs at simulation Hz.
        while (visionAccum >= 1.0 / visionHz) {
            visionAccum -= 1.0 / visionHz;
            refreshVision();
        }

        foreach (enemy in enemies) {
            enemy.flash = evClamp(enemy.flash - dt, 0.0, 1.0);
            if (enemy.state == "chase" && (enemy.id in aggro)) {
                faceToward(enemy, player.x, player.y);
                moveToward(enemy, player.x, player.y, enemy.chaseSpeed, dt);
            } else {
                enemy.patrolPhase += dt;
                local ox = cos(enemy.patrolPhase) * 36.0;
                local oy = sin(enemy.patrolPhase * 0.7) * 22.0;
                local tx = enemy.homeX + ox;
                local ty = enemy.homeY + oy;
                faceToward(enemy, tx, ty);
                moveToward(enemy, tx, ty, enemy.patrolSpeed * 18.0, dt);
            }
            enemy.x = evClamp(enemy.x, 40.0, 1060.0);
            enemy.y = evClamp(enemy.y, 40.0, 660.0);
        }
    }
}
