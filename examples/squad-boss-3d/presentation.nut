// 3D presentation only. Simulation does not depend on these objects.
persist sbCamera = null;
persist sbFont = null;
persist sbCube = null;
persist sbSphere = null;
persist sbObjects = [];
persist sbRobots = [];
persist sbBossParts = [];
persist sbHazardRings = [];
persist sbMarkers = [];

function sbPart(mesh, x, y, z, sx, sy, sz, r, g, b, lit = true) {
    local part = eve.Renderable3D();
    part.setMesh(mesh);
    part.setPosition(x, y, z);
    part.setScale(sx, sy, sz);
    part.setTint(r, g, b, 1.0);
    part.setRoughness(0.72);
    part.setReceiveLight(lit);
    sbObjects.push(part);
    return part;
}

function sbBox(x, y, z, sx, sy, sz, r, g, b, lit = true) {
    return sbPart(sbCube, x, y, z, sx, sy, sz, r, g, b, lit);
}

function sbRobot(r, g, b) {
    local parts = [];
    local definitions = [
        [0.0, 1.02, 0.0, 0.55, 0.62, 0.36, 0.16, 0.22, 0.3],
        [0.0, 1.08, 0.205, 0.36, 0.32, 0.055, r, g, b],
        [0.0, 0.64, 0.0, 0.38, 0.18, 0.3, 0.1, 0.14, 0.2],
        [-0.17, 0.32, 0.0, 0.2, 0.5, 0.23, 0.22, 0.29, 0.38],
        [0.17, 0.32, 0.0, 0.2, 0.5, 0.23, 0.22, 0.29, 0.38],
        [-0.17, 0.1, 0.08, 0.24, 0.15, 0.4, 0.1, 0.14, 0.2],
        [0.17, 0.1, 0.08, 0.24, 0.15, 0.4, 0.1, 0.14, 0.2],
        [-0.4, 1.15, 0.0, 0.26, 0.25, 0.36, r, g, b],
        [0.4, 1.15, 0.0, 0.26, 0.25, 0.36, r, g, b],
        [-0.4, 0.84, 0.0, 0.18, 0.42, 0.23, 0.26, 0.32, 0.42],
        [0.4, 0.84, 0.0, 0.18, 0.42, 0.23, 0.26, 0.32, 0.42],
        [0.0, 1.56, 0.0, 0.43, 0.43, 0.39, 0.22, 0.28, 0.36],
        [0.0, 1.57, 0.205, 0.32, 0.075, 0.04, r, g, b],
        [0.48, 0.84, 0.56, 0.065, 0.07, 0.95, r, g, b],
        [-0.47, 0.87, 0.2, 0.12, 0.45, 0.42, 0.12, 0.18, 0.25]
    ];
    foreach (d in definitions) {
        local p = sbBox(d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8],
            parts.len() != 12 && parts.len() != 13);
        parts.push({ object = p, x = d[0], y = d[1], z = d[2] });
    }
    return parts;
}

function sbBossMesh(r, g, b) {
    local parts = [];
    local definitions = [
        [0.0, 1.55, 0.0, 1.35, 1.55, 0.95, 0.18, 0.08, 0.1],
        [0.0, 1.7, 0.55, 0.95, 0.7, 0.12, r, g, b],
        [0.0, 0.85, 0.0, 1.05, 0.35, 0.8, 0.12, 0.06, 0.08],
        [-0.55, 0.45, 0.0, 0.45, 0.85, 0.5, 0.25, 0.12, 0.14],
        [0.55, 0.45, 0.0, 0.45, 0.85, 0.5, 0.25, 0.12, 0.14],
        [0.0, 2.55, 0.0, 0.85, 0.75, 0.7, 0.22, 0.1, 0.12],
        [0.0, 2.6, 0.4, 0.55, 0.12, 0.08, r, g, b],
        [0.95, 1.7, 0.15, 0.55, 0.45, 0.55, r, g, b],
        [-0.95, 1.7, 0.15, 0.55, 0.45, 0.55, r, g, b],
        [1.15, 1.05, 0.35, 0.22, 0.9, 0.35, 0.3, 0.14, 0.16],
        [-1.15, 1.05, 0.35, 0.22, 0.9, 0.35, 0.3, 0.14, 0.16]
    ];
    foreach (d in definitions) {
        local p = sbBox(d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8]);
        parts.push({ object = p, x = d[0], y = d[1], z = d[2] });
    }
    return parts;
}

function sbPresentationInit() {
    if (sbCamera != null) return;
    sbCube = gfx.newMeshCube(1.0);
    sbSphere = gfx.newMeshSphere(12, 8);
    gfx.setBackgroundColor(0.02, 0.03, 0.055, 1.0);
    gfx.setDirectionalLight(-0.35, 0.92, 0.4, 0.92, 0.95, 1.0);
    sbCamera = eve.Camera3D();
    sbCamera.setEye(10.5, 9.2, 12.8);
    sbCamera.setTarget(0.0, 0.6, 0.0);
    sbCamera.setFov(42.0);
    sbCamera.setAmbient(0.26, 0.3, 0.4);
    sbCamera.setActive(true);

    sbBox(0.0, -0.35, 0.0, 16.0, 0.5, 12.0, 0.05, 0.07, 0.12);
    sbBox(0.0, -0.05, 0.0, 15.2, 0.1, 11.2, 0.11, 0.15, 0.22);
    for (local x = -7; x <= 7; ++x)
        sbBox(x.tofloat(), 0.01, 0.0, 0.015, 0.01, 10.8, 0.18, 0.26, 0.34);
    for (local z = -5; z <= 5; ++z)
        sbBox(0.0, 0.01, z.tofloat(), 14.8, 0.01, 0.015, 0.18, 0.26, 0.34);
    foreach (x in [-7.2, 7.2]) foreach (z in [-5.0, 5.0]) {
        sbBox(x, 0.7, z, 0.5, 1.4, 0.5, 0.07, 0.1, 0.16);
        sbBox(x, 1.45, z, 0.6, 0.14, 0.6, 0.2, 0.28, 0.38);
    }

    local colors = [
        [0.15, 0.78, 0.95],
        [1.0, 0.7, 0.28],
        [0.35, 0.95, 0.68],
        [0.78, 0.45, 0.95]
    ];
    foreach (c in colors) sbRobots.push(sbRobot(c[0], c[1], c[2]));
    sbBossParts = sbBossMesh(0.95, 0.28, 0.18);
    for (local i = 0; i < 48; ++i) {
        local p = sbBox(0.0, -20.0, 0.0, 0.08, 0.04, 0.08, 1.0, 0.35, 0.15, false);
        sbHazardRings.push(p);
    }
    for (local i = 0; i < 3; ++i) {
        local p = sbPart(sbSphere, 0.0, -20.0, 0.0, 0.18, 0.08, 0.18, 0.35, 0.85, 1.0, false);
        sbMarkers.push(p);
    }
    local fontData = eve.Font().newFontDataFromFile("assets/fonts/DejaVuSans-Bold.ttf", 26);
    sbFont = gfx.newFont(fontData,
        " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-:/+.%[]()_>=!|,");
}

function sbPoseRobot(parts, u, scale = 1.0) {
    local yaw = u.yaw;
    local swing = u.strike * 1.35;
    local alive = u.hp > 0.0;
    foreach (i, p in parts) {
        local lx = p.x * scale;
        local ly = p.y * scale;
        local lz = p.z * scale;
        if (i == 13) { lx += sin(swing) * 0.55 * scale; lz += cos(swing) * 0.25 * scale; }
        local wx = u.x + lx * cos(yaw) + lz * sin(yaw);
        local wz = u.z - lx * sin(yaw) + lz * cos(yaw);
        p.object.setPosition(wx, alive ? ly : ly * 0.35, wz);
        p.object.setYaw(yaw + (i == 13 ? swing : 0.0));
        p.object.setVisible(true);
        if (!alive) p.object.setTint(0.25, 0.25, 0.28, 1.0);
    }
}

function sbPoseBoss(parts, u) {
    local yaw = u.yaw;
    local pulse = u.flash > 0.0 ? 1.0 : 0.0;
    foreach (i, p in parts) {
        local lx = p.x; local ly = p.y; local lz = p.z;
        local wx = u.x + lx * cos(yaw) + lz * sin(yaw);
        local wz = u.z - lx * sin(yaw) + lz * cos(yaw);
        p.object.setPosition(wx, u.hp > 0.0 ? ly : ly * 0.4, wz);
        p.object.setYaw(yaw);
        if (i == 1 || i == 6)
            p.object.setTint(0.95 + pulse * 0.05, 0.28 + pulse * 0.5, 0.18 + pulse * 0.4, 1.0);
    }
}

function sbUpdatePresentation(sim) {
    for (local i = 0; i < 4; ++i) sbPoseRobot(sbRobots[i], sim.units[i]);
    sbPoseBoss(sbBossParts, sim.units[sim.bossId]);

    local ring = 0;
    foreach (h in sim.hazards) {
        if (h.remaining <= -0.2) continue;
        local portion = sbClamp(h.remaining / h.total, 0.0, 1.0);
        local radius = h.radius * (1.15 - portion * 0.35);
        for (local i = 0; i < 16; ++i) {
            if (ring >= sbHazardRings.len()) break;
            local a = i * 6.283185 / 16.0;
            local p = sbHazardRings[ring];
            p.setVisible(true);
            p.setPosition(h.x + cos(a) * radius, 0.08 + (1.0 - portion) * 0.35, h.z + sin(a) * radius);
            p.setScale(0.12, 0.04, 0.12);
            p.setTint(1.0, 0.25 + portion * 0.45, 0.1, 1.0);
            ring += 1;
        }
    }
    for (local i = ring; i < sbHazardRings.len(); ++i) sbHazardRings[i].setVisible(false);

    for (local i = 0; i < 3; ++i) {
        local u = sim.units[i + 1];
        local m = sbMarkers[i];
        if (u.position != null && u.hp > 0.0) {
            m.setVisible(true);
            m.setPosition(u.position.x, 0.12, u.position.z);
        } else m.setVisible(false);
    }

    local focus = sim.units[0];
    sbCamera.setTarget(focus.x * 0.35 + sim.units[sim.bossId].x * 0.65, 0.8,
        focus.z * 0.35 + sim.units[sim.bossId].z * 0.65);
}

function sbText(text, x, y, scale = 0.5, r = 0.78, g = 0.86, b = 0.94) {
    gfx.print(text, x, y, r, g, b, 1.0, scale);
}

function sbPanel(x, y, w, h) {
    gfx.drawSolidRect(x, y, w, h, 0.02, 0.035, 0.06, 0.92);
    gfx.drawSolidRect(x, y, 2.0, h, 0.15, 0.75, 0.9, 1.0);
}

function sbRenderHud(sim) {
    gfx.setFont(sbFont);
    sbText("SQUAD BOSS 3D", 28.0, 18.0, 0.78, 0.9, 0.96, 1.0);
    sbText("EQS  +  GAS  +  COMPANION TACTICS", 29.0, 48.0, 0.34, 0.4, 0.62, 0.74);

    sbPanel(28.0, 88.0, 280.0, 210.0);
    sbText("PARTY", 42.0, 100.0, 0.4, 0.25, 0.8, 0.92);
    for (local i = 0; i < 4; ++i) {
        local u = sim.units[i];
        local y = 128.0 + i * 40.0;
        sbText(u.name, 42.0, y, 0.38);
        gfx.drawSolidRect(110.0, y + 8.0, 170.0, 6.0, 0.1, 0.16, 0.22, 1.0);
        gfx.drawSolidRect(110.0, y + 8.0, 170.0 * u.hp / u.maxHp, 6.0, 0.2, 0.85, 0.55, 1.0);
        if (u.agent != null) sbText(u.state.toupper(), 110.0, y + 18.0, 0.28, 0.55, 0.7, 0.8);
    }

    local boss = sim.units[sim.bossId];
    sbPanel(980.0, 28.0, 350.0, 130.0);
    sbText("COLOSSUS", 995.0, 42.0, 0.46, 1.0, 0.45, 0.28);
    sbText(format("%.0f / %.0f HP", boss.hp, boss.maxHp), 995.0, 74.0, 0.7);
    gfx.drawSolidRect(995.0, 112.0, 320.0, 7.0, 0.15, 0.1, 0.1, 1.0);
    gfx.drawSolidRect(995.0, 112.0, 320.0 * boss.hp / boss.maxHp, 7.0, 1.0, 0.35, 0.18, 1.0);
    sbText("COMMAND / " + sim.command, 995.0, 132.0, 0.34);

    sbPanel(28.0, 316.0, 280.0, 150.0);
    sbText("GAS / PLAYER", 42.0, 328.0, 0.4, 0.25, 0.8, 0.92);
    sbText(format("ENERGY %.0f", sim.playerStats.getBase("Attribute.Energy", 0.0)), 42.0, 358.0, 0.42);
    sbText(format("ATTACK %.0f > %.0f", sim.playerStats.getBase("Attribute.Attack", 0.0),
        sim.playerStats.getFinal("Attribute.Attack", 0.0)), 42.0, 384.0, 0.38);
    local buff = sim.buffRemaining();
    sbText(buff > 0.0 ? format("RALLY %.1fs", buff) : "RALLY ready", 42.0, 410.0, 0.36,
        buff > 0.0 ? 1.0 : 0.5, 0.72, 0.4);
    sbText("PHASE / " + sim.phase.toupper(), 42.0, 436.0, 0.32);

    sbPanel(980.0, 175.0, 350.0, 150.0);
    sbText("EQS / COORD", 995.0, 188.0, 0.4, 0.25, 0.8, 0.92);
    sbText(format("last candidates %d", sim.eqsLastCount), 995.0, 218.0, 0.36);
    sbText(format("queries %d   dodges %d", sim.stats.eqs_queries, sim.stats.dodges), 995.0, 244.0, 0.34);
    sbText(format("heals %d  guards %d  gas hits %d", sim.stats.heals, sim.stats.guards, sim.stats.gas_hits),
        995.0, 270.0, 0.34);
    sbText(format("slams %d  absorbed %d", sim.stats.boss_slams, sim.stats.absorbed), 995.0, 296.0, 0.34);

    foreach (i, s in sim.playerSkills) {
        local px = 340.0 + i * 200.0;
        sbPanel(px, 650.0, 185.0, 85.0);
        sbText("[" + s.key.toupper() + "] " + s.label, px + 12.0, 662.0, 0.4);
        local status = s.cooldown > 0.0 ? format("CD %.1fs", s.cooldown) :
            (sim.playerStats.getBase("Attribute.Energy", 0.0) < s.cost ? "LOW ENERGY" : "READY");
        sbText(status, px + 12.0, 692.0, 0.34, 0.25, 0.85, 0.9);
        sbText(format("COST %.0f", s.cost), px + 12.0, 714.0, 0.3);
    }

    sbText("EVENTS", 28.0, 485.0, 0.34, 0.35, 0.6, 0.72);
    local start = sbMax(0, sim.events.len() - 5);
    for (local i = start; i < sim.events.len(); ++i)
        sbText(sim.events[i].text, 28.0, 508.0 + (i - start) * 20.0, 0.3);

    sbText("WASD move / J slash / K breaker / L rally / 1-4 commands / P auto / R reset",
        28.0, 770.0, 0.3);
    if (boss.hp <= 0.0) sbText("COLOSSUS DOWN", 520.0, 360.0, 0.9, 1.0, 0.75, 0.35);
    if (sim.units[0].hp <= 0.0) sbText("PARTY WIPE / PRESS R", 480.0, 400.0, 0.8, 1.0, 0.4, 0.35);
}
