// Game-owned host: mirrors poses into SensingWorld, runs per-enemy cone queries,
// then applies World2D LOS against authored obstacle AABBs (no World2D LOS
// capability ships yet). Vision memories stay in NpcAiWorld; aggro stays here.
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

// Segment vs AABB (Liang-Barsky). Returns hit distance along the segment, or -1.
function evRaycastObstacles(x0, y0, x1, y1, obstacles) {
    local dx = x1 - x0;
    local dy = y1 - y0;
    local bestT = 2.0;
    foreach (wall in obstacles) {
        local minX = wall.x;
        local minY = wall.y;
        local maxX = wall.x + wall.w;
        local maxY = wall.y + wall.h;
        local t0 = 0.0;
        local t1 = 1.0;
        local parallel = false;
        local edges = [
            [-dx, x0 - minX],
            [dx, maxX - x0],
            [-dy, y0 - minY],
            [dy, maxY - y0]
        ];
        foreach (e in edges) {
            local p = e[0];
            local q = e[1];
            if (p == 0.0) {
                if (q < 0.0) {
                    parallel = true;
                    break;
                }
            } else {
                local r = q / p;
                if (p < 0.0) {
                    if (r > t1) {
                        parallel = true;
                        break;
                    }
                    if (r > t0) t0 = r;
                } else {
                    if (r < t0) {
                        parallel = true;
                        break;
                    }
                    if (r < t1) t1 = r;
                }
            }
        }
        if (parallel) continue;
        if (t0 <= t1 && t0 >= 0.0 && t0 <= 1.0 && t0 < bestT) bestT = t0;
    }
    if (bestT <= 1.0) {
        return bestT * evLen(dx, dy);
    }
    return -1.0;
}

function evHasLineOfSight(x0, y0, x1, y1, obstacles) {
    return evRaycastObstacles(x0, y0, x1, y1, obstacles) < 0.0;
}

function evResolveCircleAabb(px, py, radius, wall) {
    local nearestX = evClamp(px, wall.x, wall.x + wall.w);
    local nearestY = evClamp(py, wall.y, wall.y + wall.h);
    local dx = px - nearestX;
    local dy = py - nearestY;
    local dist = evLen(dx, dy);
    if (dist >= radius || dist < 0.0001) return {x = px, y = py};
    local push = (radius - dist) / dist;
    return {x = px + dx * push, y = py + dy * push};
}

class EnemyVisionSimulation {
    sensingMod = null;
    sensing = null;
    npcMod = null;
    ai = null;
    player = null;
    enemies = null;
    obstacles = null;
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
        // Visible cover: cones still select candidates; host LOS rejects blocked rays.
        // wall.mid sits inside guard.east's range so both clear and occluded probes fit the cone.
        obstacles = [
            {id = "wall.mid", x = 560.0, y = 140.0, w = 36.0, h = 240.0},
            {id = "wall.low", x = 360.0, y = 400.0, w = 260.0, h = 32.0},
            {id = "cover.crate", x = 700.0, y = 320.0, w = 70.0, h = 70.0}
        ];
        addEnemy("guard.east", 820.0, 240.0, -1.0, 0.0, 0.55, 400.0, 1.6);
        addEnemy("guard.south", 520.0, 580.0, 0.0, -1.0, 0.70, 260.0, 2.1);
        addEnemy("guard.west", 960.0, 450.0, -1.0, 0.15, 0.40, 280.0, 1.2);
        note("walls block LOS — stand in a cone behind cover to stay hidden");
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
            radius = 16.0
            agent = agent
            state = "patrol"
            seesPlayer = false
            blocked = false
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

    function separateFromWalls(unit) {
        local pos = {x = unit.x, y = unit.y};
        foreach (wall in obstacles) {
            pos = evResolveCircleAabb(pos.x, pos.y, unit.radius, wall);
        }
        unit.x = pos.x;
        unit.y = pos.y;
    }

    function refreshVision() {
        mirrorFacts();
        foreach (enemy in enemies) {
            local found = sensing.query(evConeQueryJson(enemy));
            evRequire(found, "cone query " + enemy.id);
            local ranked = found.value.ranked;
            local inCone = ranked != null && ranked.len() > 0;
            local clear = false;
            local subject = player.id;
            if (inCone) {
                subject = ranked[0].subject;
                if (subject == null || subject == "") subject = player.id;
                clear = evHasLineOfSight(enemy.x, enemy.y, player.x, player.y, obstacles);
            }
            local hit = inCone && clear;
            enemy.blocked = inCone && !clear;
            local previous = enemy.seesPlayer;
            enemy.seesPlayer = hit;
            if (hit) {
                evRequire(ai.setBlackboardString(enemy.agent, "target", subject),
                          "blackboard target " + enemy.id);
                // Short TTL: stop refreshing and the next AI tick emits perception.forgotten.vision.
                evRequire(ai.remember(enemy.agent, subject, "vision", 0.95, tick, 2, "{}"),
                          "remember vision " + enemy.id);
                aggro[enemy.id] <- subject;
                if (!previous) {
                    enemy.flash = 0.35;
                    note(enemy.id + " saw " + subject);
                }
            } else if (enemy.blocked && !previous) {
                // Still in the geometric cone, but a wall owns the ray.
                note(enemy.id + " cone blocked by cover");
            }
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
        separateFromWalls(unit);
    }

    function faceToward(enemy, tx, ty) {
        local facing = evNorm(tx - enemy.x, ty - enemy.y);
        enemy.dirX = facing.x;
        enemy.dirY = facing.y;
    }

    function verifyPipeline() {
        local guard = enemies[0];
        // Clear LOS: between guard.east (x=820) and wall.mid (x=560..596).
        player.x = 680.0;
        player.y = 240.0;
        for (local i = 0; i < 4; i += 1) {
            tick += 1;
            refreshVision();
        }
        if (guard.state != "chase") throw "verify: expected chase with clear LOS, got " + guard.state;
        if (!(guard.id in aggro)) throw "verify: aggro missing after clear vision";

        // Same facing cone, west of wall.mid — still in range (820-440=380 < 400), ray occluded.
        player.x = 440.0;
        player.y = 240.0;
        for (local i = 0; i < 6; i += 1) {
            tick += 1;
            refreshVision();
        }
        if (guard.state != "patrol") throw "verify: expected patrol behind cover, got " + guard.state;
        if (guard.id in aggro) throw "verify: aggro should clear when LOS blocked";
        if (!guard.blocked) throw "verify: expected blocked=true while player stays in cone behind wall";

        note("pipeline verify passed (LOS + cover)");
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
        separateFromWalls(player);

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
                local ox = cos(enemy.patrolPhase) * 28.0;
                local oy = sin(enemy.patrolPhase * 0.7) * 18.0;
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
