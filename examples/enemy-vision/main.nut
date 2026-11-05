dofile("behavior.nut");
dofile("simulation.nut");

local simulation = null;
local accumulator = 0.0;
local statusLine = "booting";
local hudReady = false;

function evDrawCone(enemy, alert) {
    local steps = 10;
    local r = alert ? 0.95 : 0.35;
    local g = alert ? 0.35 : 0.55;
    local b = alert ? 0.20 : 0.75;
    local a = alert ? 0.55 : 0.28;
    for (local i = 0; i <= steps; i += 1) {
        local t = -enemy.halfAngle + (enemy.halfAngle * 2.0) * (i.tofloat() / steps.tofloat());
        local ca = cos(t);
        local sa = sin(t);
        local dx = enemy.dirX * ca - enemy.dirY * sa;
        local dy = enemy.dirX * sa + enemy.dirY * ca;
        local ex = enemy.x + dx * enemy.range;
        local ey = enemy.y + dy * enemy.range;
        for (local s = 1; s <= 8; s += 1) {
            local u = s.tofloat() / 8.0;
            gfx.drawSolidRect(enemy.x + (ex - enemy.x) * u - 2.0,
                              enemy.y + (ey - enemy.y) * u - 2.0,
                              4.0, 4.0, r, g, b, a);
        }
    }
}

function evDrawUnit(x, y, radius, cr, cg, cb) {
    gfx.drawSolidRect(x - radius, y - radius, radius * 2.0, radius * 2.0, cr, cg, cb, 1.0);
}

eve_init <- function() {
    gfx.setBackgroundColor(0.06, 0.08, 0.11, 1.0);
    local font = gfx.newFont(
        eve.Font().newFontDataFromFile("assets/DejaVuSans-Bold.ttf", 22),
        " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/.-:+|_");
    gfx.setFont(font);
    hudReady = true;
    simulation = EnemyVisionSimulation();
    simulation.verifyPipeline();
    simulation.player.x = 220.0;
    simulation.player.y = 350.0;
    statusLine = "guards=" + simulation.enemies.len() + " verify=pass";
    print("ENEMY_VISION_READY sensing+npc_ai visionHz=10 enemies=" +
          simulation.enemies.len() + " verify=pass\n");
};

eve_update <- function(dt) {
    if (simulation == null) return;
    local input = {x = 0.0, y = 0.0};
    if (keyboard.isDown("a") || keyboard.isDown("left")) input.x -= 1.0;
    if (keyboard.isDown("d") || keyboard.isDown("right")) input.x += 1.0;
    if (keyboard.isDown("w") || keyboard.isDown("up")) input.y -= 1.0;
    if (keyboard.isDown("s") || keyboard.isDown("down")) input.y += 1.0;
    accumulator += dt;
    local steps = 0;
    while (accumulator >= 1.0 / 60.0 && steps < 12) {
        simulation.step(1.0 / 60.0, input);
        accumulator -= 1.0 / 60.0;
        steps += 1;
    }
    local chasing = 0;
    foreach (enemy in simulation.enemies) if (enemy.state == "chase") chasing += 1;
    statusLine = "tick=" + simulation.tick + " chasing=" + chasing +
                 " aggro=" + simulation.aggro.len();
};

eve_render <- function() {
    gfx.clear();
    if (simulation == null) return;

    foreach (enemy in simulation.enemies) {
        local alert = enemy.state == "chase";
        evDrawCone(enemy, alert);
        local er = alert ? 0.90 : 0.55;
        local eg = alert ? 0.25 + enemy.flash * 0.4 : 0.55;
        local eb = alert ? 0.20 : 0.70;
        evDrawUnit(enemy.x, enemy.y, 16.0, er, eg, eb);
        gfx.drawSolidRect(enemy.x + enemy.dirX * 18.0 - 3.0,
                          enemy.y + enemy.dirY * 18.0 - 3.0,
                          6.0, 6.0, 0.95, 0.95, 0.70, 1.0);
    }

    evDrawUnit(simulation.player.x, simulation.player.y, simulation.player.radius,
               0.25, 0.85, 0.55);

    if (hudReady) {
        gfx.print("ENEMY VISION", 28.0, 24.0, 0.92, 0.95, 1.0, 1.0, 1.1);
        gfx.print("WASD move into a cone | sensing QueryCone + npc_ai remember(vision)",
                  28.0, 56.0, 0.55, 0.70, 0.82, 1.0, 0.7);
        gfx.print(statusLine, 28.0, 86.0, 0.75, 0.82, 0.55, 1.0, 0.8);
        local y = 640.0;
        for (local i = simulation.events.len() - 1; i >= 0; i -= 1) {
            gfx.print(simulation.events[i].text, 28.0, y, 0.70, 0.78, 0.88, 1.0, 0.65);
            y -= 22.0;
        }
        foreach (enemy in simulation.enemies) {
            gfx.print(enemy.id + " / " + enemy.state, enemy.x - 40.0, enemy.y - 34.0,
                      0.85, 0.88, 0.92, 1.0, 0.6);
        }
    }
};
