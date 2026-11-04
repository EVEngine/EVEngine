function rect(x, y, w, h, r, g, b, a = 1.0) {
    gfx.drawSolidRect(viewX + x * viewScale, viewY + y * viewScale, w * viewScale,
        h * viewScale, r, g, b, a);
}
function label(text, x, y, scale = 0.6, r = 0.88, g = 0.93, b = 0.98, alpha = 1.0) {
    gfx.drawText(hudFont, text, viewX + x * viewScale, viewY + y * viewScale,
        r, g, b, alpha, scale * viewScale);
}
function disc(x, y, radius, r, g, b, alpha = 1.0) {
    for (local i = -radius; i < radius; i += 3.0) {
        local half = sqrt(arenaMax(0.0, radius * radius - (i + 1.5) * (i + 1.5)));
        if (half > 0.0) rect(x - half, y + i, half * 2.0, 3.0, r, g, b, alpha);
    }
}
function ring(x, y, radius, r, g, b, alpha = 1.0, thickness = 3.0) {
    for (local i = 0; i < 64; ++i) {
        local a = i * 6.283185 / 64.0;
        rect(x + cos(a) * radius - thickness * 0.5, y + sin(a) * radius - thickness * 0.5,
            thickness, thickness, r, g, b, alpha);
    }
}
function slash(x, y, dx, dy, radius, r, g, b, alpha) {
    local angle = atan2(dy, dx);
    for (local i = -15; i <= 15; ++i) {
        local a = angle + i * 0.065;
        disc(x + cos(a) * radius, y + sin(a) * radius, 3.8, r, g, b, alpha);
    }
}
function renderActor(actor) {
    local p = actor.view.position;
    if (actor.view.health <= 0.0) {
        ring(p.x, p.z, 14.0, 0.32, 0.2, 0.28, 0.55, 2.0);
        return;
    }
    local hero = actor.hero;
    local r = hero ? 0.2 : 0.92, g = hero ? 0.9 : 0.28, b = hero ? 0.94 : 0.42;
    local dodge = actor.action != null && actor.action.name == "dodge" && actor.action.phase == "active";
    if (dodge) {
        for (local i = 1; i < 5; ++i)
            disc(p.x - actor.action.x * i * 16.0, p.z - actor.action.y * i * 16.0,
                19.0 - i * 2.0, r, g, b, 0.16 - i * 0.025);
    }
    disc(p.x + 4.0, p.z + 8.0, 24.0, 0.0, 0.0, 0.0, 0.35);
    disc(p.x, p.z, 25.0, r, g, b, 0.1);
    ring(p.x, p.z, 22.0, r, g, b, 0.6, 2.0);
    disc(p.x, p.z, 16.0, r, g, b, 1.0);
    disc(p.x - 2.0, p.z - 3.0, 10.0, 0.04, 0.09, 0.14, 0.9);
    disc(p.x + actor.faceX * 15.0, p.z + actor.faceY * 15.0, 5.0, 0.9, 1.0, 1.0, 0.9);
    if (actor.flash > 0.0) disc(p.x, p.z, 17.0, 1.0, 1.0, 1.0, actor.flash * 3.0);
    if (!hero) {
        rect(p.x - 22.0, p.z - 34.0, 44.0, 4.0, 0.13, 0.16, 0.22);
        rect(p.x - 22.0, p.z - 34.0, 44.0 * actor.view.health / actor.view.maxHealth,
            4.0, r, g, b);
    }
    if (actor.action != null && actor.action.name != "dodge") {
        local action = actor.action;
        local radius = hero ? (action.name == "heavy" ? 115.0 : 78.0) : 73.0;
        if (action.phase == "windup") {
            slash(p.x, p.z, action.x, action.y, radius, r, g, b, 0.24);
            ring(p.x, p.z, 28.0 + action.elapsed * 22.0, r, g, b, 0.5, 2.0);
        } else if (action.phase == "active") {
            slash(p.x, p.z, action.x, action.y, radius, hero ? 1.0 : r,
                hero ? 0.86 : g, hero ? 0.4 : b, 0.95);
            slash(p.x, p.z, action.x, action.y, radius - 8.0, r, g, b, 0.35);
        }
    }
}
function renderArena() {
    gfx.clear();
    rect(0.0, 0.0, 1280.0, 720.0, 0.025, 0.035, 0.06);
    rect(42.0, 151.0, 1196.0, 491.0, 0.055, 0.075, 0.11);
    for (local x = 60.0; x < 1230.0; x += 40.0) rect(x, 165.0, 1.0, 460.0, 0.08, 0.105, 0.14);
    for (local y = 166.0; y < 630.0; y += 40.0) rect(56.0, y, 1170.0, 1.0, 0.08, 0.105, 0.14);
    ring(640.0, 390.0, 185.0, 0.15, 0.24, 0.3, 0.5, 2.0);
    ring(640.0, 390.0, 90.0, 0.15, 0.24, 0.3, 0.4, 2.0);
    rect(42.0, 151.0, 1196.0, 2.0, 0.15, 0.3, 0.36);
    rect(42.0, 640.0, 1196.0, 2.0, 0.15, 0.3, 0.36);
    label("03 / LIVE COMBAT", 60.0, 168.0, 0.38, 0.32, 0.49, 0.58);
    label("IMPACT", 43.0, 23.0, 1.25, 0.92, 0.98, 1.0);
    label("ACTION ARENA", 45.0, 68.0, 0.42, 0.3, 0.82, 0.85);
    label("WAVE", 882.0, 28.0, 0.38, 0.5, 0.62, 0.7);
    label(format("%02d / 03", arenaMin(gameState.wave, 3)), 882.0, 49.0, 0.95);
    label("ELIMINATIONS", 1074.0, 28.0, 0.38, 0.5, 0.62, 0.7);
    label(format("%02d", gameState.kills), 1074.0, 49.0, 0.95, 1.0, 0.73, 0.37);
    label("VITALITY", 330.0, 36.0, 0.36, 0.45, 0.68, 0.72);
    rect(330.0, 63.0, 420.0, 12.0, 0.12, 0.18, 0.23);
    rect(330.0, 63.0, 420.0 * player.view.health / player.view.maxHealth, 12.0, 0.23, 0.86, 0.8);
    label(format("%.0f / %.0f", player.view.health, player.view.maxHealth), 330.0, 83.0, 0.4);
    label(gameState.demo ? "AUTOPILOT / SAME COMBAT INPUTS" : "SURVIVE THREE WAVES", 43.0, 115.0,
        0.45, gameState.demo ? 1.0 : 0.54, 0.74, 0.8);
    label("HEAVY = CRITICAL  /  DODGE THROUGH RED ATTACKS", 750.0, 119.0, 0.35, 0.55, 0.64, 0.7);
    foreach (a in actors) renderActor(a);
    foreach (e in effects) {
        local t = 1.0 - e.life / 0.32;
        ring(e.x, e.y, 18.0 + t * 46.0, 1.0, e.critical ? 0.8 : 0.5, 0.25, 1.0 - t, 3.0);
        for (local i = 0; i < 8; ++i) {
            local angle = i * 0.7854;
            rect(e.x + cos(angle) * (22.0 + t * 55.0), e.y + sin(angle) * (22.0 + t * 55.0),
                4.0, 4.0, 1.0, 0.87, 0.5, 1.0 - t);
        }
    }
    checked(numberView.render(gfx, hudFont));
    label("WASD  MOVE", 44.0, 663.0, 0.44);
    label("J  SLASH", 235.0, 663.0, 0.44, 0.3, 0.88, 0.9);
    label("K  HEAVY", 407.0, 663.0, 0.44, 1.0, 0.77, 0.3);
    label("SPACE  DODGE", 574.0, 663.0, 0.44);
    label("P  PAUSE   R  RESTART   TAB  DEMO", 832.0, 663.0, 0.38, 0.55, 0.65, 0.73);
    label(format("LAST HIT  %.0f APPLIED / %.0f REQUESTED    |    %d CRITICALS    |    %d DODGES",
        gameState.lastApplied, gameState.lastRequested, gameState.criticals, gameState.dodges),
        44.0, 695.0, 0.3, 0.38, 0.5, 0.6);
    if (gameState.phase != "playing" || gameState.paused) {
        rect(0.0, 150.0, 1280.0, 492.0, 0.015, 0.025, 0.045, 0.8);
        local title = gameState.paused ? "PAUSED" : (gameState.phase == "victory" ? "ARENA CLEARED" : "YOU FELL");
        label(title, 410.0, 295.0, 1.2, 0.93, 0.98, 1.0);
        label(gameState.paused ? "P TO RESUME" : "R TO TRY AGAIN  /  TAB FOR AUTOPILOT", 408.0, 358.0, 0.47, 0.3, 0.86, 0.85);
        label(format("%d ELIMINATIONS    %d HITS    %d CRITICALS", gameState.kills, gameState.hits, gameState.criticals),
            408.0, 400.0, 0.4, 0.67, 0.74, 0.8);
    }
}
