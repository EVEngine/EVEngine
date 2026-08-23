// Crowd demo: flow-field marching + Boids flocking.
// 1 = flow, 2 = seek, 3 = boids, left mouse = new target, space = pause.
// Soft hot-reload guards: keep C++-side crowd state across script reloads.

if (!("crowd" in getroottable()))
    crowd <- null;
if (!("mode" in getroottable()))
    mode <- "flow";
if (!("goalX" in getroottable()))
    goalX <- 0.0;
if (!("goalY" in getroottable()))
    goalY <- 0.0;
if (!("paused" in getroottable()))
    paused <- false;
if (!("mouseWasDown" in getroottable()))
    mouseWasDown <- false;

const AGENT_COUNT = 2000;
const GRID_W = 80;      // 80 * 12 = 960 px
const GRID_H = 52;      // 52 * 12 = 624 px
const CELL = 12.0;

function hueToRgb(h) {
    local i = (h * 6.0).tointeger() % 6;
    local f = h * 6.0 - i.tofloat();
    local r = 0.0, g = 0.0, b = 0.0;
    switch (i) {
        case 0: r = 1.0; g = f; break;
        case 1: r = 1.0 - f; g = 1.0; break;
        case 2: g = 1.0; b = f; break;
        case 3: g = 1.0 - f; b = 1.0; break;
        case 4: r = f; b = 1.0; break;
        default: r = 1.0; b = 1.0 - f; break;
    }
    return { r = r, g = g, b = b };
}

function retarget(gx, gy) {
    goalX = gx;
    goalY = gy;
    if (mode == "flow") {
        local cx = ((gx - 0.0) / CELL).tointeger();
        local cy = ((gy - 0.0) / CELL).tointeger();
        crowd.buildFlowField(cx, cy);
    }
}

function applyMode() {
    local n = crowd.getAgentCount();
    for (local i = 0; i < n; i += 1) {
        crowd.setAgentAction(i, mode);
        if (mode == "seek" || mode == "boids")
            crowd.setAgentTarget(i, goalX, goalY);
        else
            crowd.clearAgentTarget(i);
    }
}

eve_init = function() {
    gfx.setBackgroundColor(0.06, 0.07, 0.10, 1.0);
    if (crowd == null)
        crowd = eve.Crowd();

    if (crowd.getAgentCount() == 0) {
        crowd.resizeField(GRID_W, GRID_H, CELL, 0.0, 0.0);
        // 两堵带缺口的墙。
        for (local y = 0; y < GRID_H; y += 1) {
            if (y < 16 || y > 30) crowd.setBlocked(28, y, true);
            if (y < 20 || y > 36) crowd.setBlocked(52, y, true);
        }
        crowd.setDefaultSpeed(95.0);
        crowd.setDefaultRadius(3.5);
        crowd.setDefaultTurnRate(5.5);
        crowd.setSeparationRadius(20.0);
        crowd.setPerceptionRadius(48.0);
        crowd.setSeparationWeight(1.0);
        crowd.setAlignmentWeight(0.04);
        crowd.setCohesionWeight(0.02);
        crowd.setWanderWeight(0.15);
        crowd.setGoalWeight(0.8);

        retarget(GRID_W * CELL - 30.0, GRID_H * CELL * 0.5);

        for (local i = 0; i < AGENT_COUNT; i += 1) {
            local x = 24.0 + (i % 44) * 6.0;
            local y = 40.0 + ((i / 44) % 52) * 6.0;
            crowd.addAgent(x, y, 0.0, 3.5);
        }
        applyMode();
    }
};

eve_reload <- function() {
    // 热重载后保持当前行动模式。
};

eve_update = function(dt) {
    if (key_just_pressed("1")) {
        mode = "flow";
        retarget(goalX, goalY);
        applyMode();
    } else if (key_just_pressed("2")) {
        mode = "seek";
        applyMode();
    } else if (key_just_pressed("3")) {
        mode = "boids";
        applyMode();
    } else if (key_just_pressed("space")) {
        paused = !paused;
    }

    // 鼠标左键重新指定目标（按下沿）。
    local down = mouse.isDown(1);
    if (down && !mouseWasDown) {
        local mx = mouse.getX().tofloat();
        local my = mouse.getY().tofloat();
        mode = "flow";
        retarget(mx, my);
    }
    mouseWasDown = down;

    if (paused) return;

    crowd.step(dt);
};

eve_render = function() {
    gfx.clear();

    // 障碍物（流场阻挡格）。
    for (local y = 0; y < GRID_H; y += 1) {
        for (local x = 0; x < GRID_W; x += 1) {
            if (!crowd.isReachable(x, y)) {
                gfx.drawSolidRect(x * CELL, y * CELL, CELL, CELL, 0.18, 0.20, 0.26, 1.0);
            }
        }
    }

    // 目标点。
    gfx.drawSolidRect(goalX - 6.0, goalY - 6.0, 12.0, 12.0, 0.95, 0.35, 0.25, 1.0);

    // 单位：方块颜色随朝向变化，白色小点为头部。
    local n = crowd.getAgentCount();
    local xs = array(n);
    local ys = array(n);
    local hs = array(n);
    crowd.getPositions(xs, ys);
    crowd.getHeadings(hs);
    for (local i = 0; i < n; i += 1) {
        local h = (hs[i] / 6.2831853 + 0.5) % 1.0;
        local c = hueToRgb(h);
        local r = 3.0;
        gfx.drawSolidRect(xs[i] - r, ys[i] - r, r * 2.0, r * 2.0, c.r, c.g, c.b, 0.85);
        gfx.drawSolidRect(xs[i] + cos(hs[i]) * 5.0 - 1.0,
                          ys[i] + sin(hs[i]) * 5.0 - 1.0,
                          2.0, 2.0, 1.0, 1.0, 1.0, 0.75);
    }
};
