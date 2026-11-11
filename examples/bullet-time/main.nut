// Sniper-Elite-style bullet-time interrupt: slow the world, follow the round,
// reveal the target through cover with stylize "xray", then resume gameplay.

persist btCamera = null
persist btFont = null
persist btXray = null
persist btFloor = null
persist btShooter = null
persist btTargetParts = null
persist btBullet = null
persist btTrailVis = null
persist btTrailPos = null
persist btSparksVis = null
persist btSparks = null
persist btRing = null
persist btReady = false
persist btPassPrinted = false
persist btCycles = 0

persist phase = "idle"          // idle | flight | impact | aftermath
persist phaseT = 0.0            // seconds inside current phase (real time)
persist idleWait = 0.75
persist timeScale = 1.0
persist cinemaClock = 0.0
persist worldClock = 0.0
persist fireRequested = false
persist spaceWasDown = false

persist bulletPos = null
persist bulletDir = null
persist bulletSpeed = 52.0
persist hitPos = null
persist camEye = null
persist camTarget = null
persist vignette = 0.0
persist xrayOn = false

SHOOTER <- { x = -7.2, y = 1.35, z = 0.4 };
TARGET <- { x = 4.6, y = 1.15, z = -0.2 };
COVER <- { x = 1.2, y = 1.4, z = 0.0, sx = 2.8, sy = 2.8, sz = 0.55 };
IDLE_CAM_EYE <- { x = -5.5, y = 3.8, z = 7.2 };
IDLE_CAM_TGT <- { x = 0.6, y = 1.2, z = 0.0 };

FLIGHT_SCALE <- 0.08;
IMPACT_SCALE <- 0.025;
FLIGHT_MAX_REAL <- 3.2;
IMPACT_REAL <- 1.2;
AFTERMATH_REAL <- 0.95;
IDLE_LOOP <- 3.0;

function clamp(v, lo, hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

function lerp(a, b, t) {
    return a + (b - a) * t;
}

function smooth(t) {
    t = clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

function len3(x, y, z) {
    return sqrt(x * x + y * y + z * z);
}

function norm3(x, y, z) {
    local n = len3(x, y, z);
    if (n < 1e-6) return [0.0, 0.0, 1.0];
    return [x / n, y / n, z / n];
}

function makePart(mesh, x, y, z, sx, sy, sz, r, g, b, roughness) {
    local p = eve.Renderable3D();
    p.setMesh(mesh);
    p.setPosition(x, y, z);
    p.setScale(sx, sy, sz);
    p.setTint(r, g, b, 1.0);
    p.setRoughness(roughness);
    p.setMetallic(0.08);
    p.setCastShadow(true);
    p.setReceiveShadow(true);
    return p;
}

function setXrayHighlight(on) {
    if (btTargetParts == null) return;
    xrayOn = on;
    for (local i = 0; i < btTargetParts.len(); ++i)
        btTargetParts[i].setXRayHighlight(on);
}

function hideFx() {
    if (btBullet != null) {
        btBullet.setVisible(false);
        btBullet.setPosition(0.0, -40.0, 0.0);
    }
    if (btRing != null) {
        btRing.setVisible(false);
        btRing.setScale(0.001, 0.001, 0.001);
    }
    if (btSparksVis != null) {
        for (local i = 0; i < btSparksVis.len(); ++i) {
            btSparksVis[i].setVisible(false);
            btSparksVis[i].setPosition(0.0, -40.0, 0.0);
        }
    }
    if (btTrailVis != null) {
        for (local i = 0; i < btTrailVis.len(); ++i) {
            btTrailVis[i].setVisible(false);
            btTrailVis[i].setPosition(0.0, -40.0, 0.0);
            btTrailPos[i] = [0.0, -40.0, 0.0];
        }
    }
}

function buildScene() {
    gfx.setBackgroundColor(0.035, 0.045, 0.06, 1.0);
    gfx.setDirectionalLight(-0.45, -1.0, -0.2, 1.45, 1.28, 1.12);

    local rc = gfx.getRenderControl();
    rc.disable("ao");
    rc.enable("gbuffer");
    rc.compile();

    local stylize = eve.Stylize();
    if (!stylize.hasStyle("xray")) throw "bullet-time: stylize xray style missing";
    btXray = stylize.newMeshShader(gfx, "xray");
    btXray.sendFloat("colorR", 0.95);
    btXray.sendFloat("colorG", 0.18);
    btXray.sendFloat("colorB", 0.12);
    btXray.sendFloat("alpha", 0.98);
    btXray.sendFloat("rimPower", 2.4);
    btXray.sendFloat("rimStrength", 1.35);
    btXray.sendFloat("bias", 0.002);

    btCamera = eve.Camera3D();
    btCamera.setEye(IDLE_CAM_EYE.x, IDLE_CAM_EYE.y, IDLE_CAM_EYE.z);
    btCamera.setTarget(IDLE_CAM_TGT.x, IDLE_CAM_TGT.y, IDLE_CAM_TGT.z);
    btCamera.setUp(0.0, 1.0, 0.0);
    btCamera.setFov(48.0);
    btCamera.setAmbient(0.16, 0.18, 0.22);
    btCamera.setActive(true);
    camEye = [IDLE_CAM_EYE.x, IDLE_CAM_EYE.y, IDLE_CAM_EYE.z];
    camTarget = [IDLE_CAM_TGT.x, IDLE_CAM_TGT.y, IDLE_CAM_TGT.z];

    local cube = gfx.newMeshCube(1.0);
    local cyl = gfx.newMeshCylinder(20, 1, true);
    local sphere = gfx.newMeshSphere(14, 10);

    btFloor = [];
    local tile = 1.1;
    for (local z = -6; z <= 6; ++z) {
        for (local x = -9; x <= 8; ++x) {
            local shade = ((x + z) % 2 == 0) ? 0.18 : 0.11;
            local t = makePart(cube, x * tile, -0.08, z * tile, tile * 0.98, 0.12, tile * 0.98,
                               shade, shade * 1.05, shade * 1.1, 0.9);
            t.setCastShadow(false);
            btFloor.push(t);
        }
    }

    makePart(cube, COVER.x, COVER.y, COVER.z, COVER.sx, COVER.sy, COVER.sz,
             0.42, 0.38, 0.34, 0.75);
    // Side wing so the target is clearly occluded from the idle / impact cameras.
    makePart(cube, COVER.x - 0.2, COVER.y - 0.1, COVER.z + 1.15, 2.2, 2.5, 0.7,
             0.36, 0.34, 0.32, 0.8);
    makePart(cube, COVER.x + 0.9, COVER.y + 0.2, COVER.z - 0.9, 1.4, 2.4, 0.55,
             0.4, 0.37, 0.33, 0.78);

    // Shooter (kneeling silhouette).
    btShooter = [];
    btShooter.push(makePart(cyl, SHOOTER.x, 0.55, SHOOTER.z, 0.22, 0.7, 0.22, 0.25, 0.32, 0.28, 0.55));
    btShooter.push(makePart(sphere, SHOOTER.x, 1.22, SHOOTER.z + 0.02, 0.17, 0.17, 0.17, 0.7, 0.58, 0.48, 0.45));
    btShooter.push(makePart(cyl, SHOOTER.x + 0.35, 1.32, SHOOTER.z + 0.15, 0.05, 0.85, 0.05, 0.18, 0.18, 0.2, 0.35));
    btShooter[2].setRotation(0.15, 0.05, 1.45);

    // Target mannequin — every part gets the X-ray shader; highlight toggled on impact.
    local tx = TARGET.x;
    local tz = TARGET.z;
    btTargetParts = [
        makePart(cyl, tx, 0.95, tz, 0.28, 0.95, 0.22, 0.55, 0.22, 0.2, 0.5),
        makePart(sphere, tx, 1.85, tz, 0.18, 0.2, 0.18, 0.72, 0.55, 0.45, 0.4),
        makePart(cyl, tx - 0.12, 0.28, tz, 0.09, 0.55, 0.09, 0.2, 0.22, 0.28, 0.6),
        makePart(cyl, tx + 0.12, 0.28, tz, 0.09, 0.55, 0.09, 0.2, 0.22, 0.28, 0.6),
        makePart(cyl, tx - 0.34, 1.15, tz, 0.07, 0.42, 0.07, 0.55, 0.22, 0.2, 0.5),
        makePart(cyl, tx + 0.34, 1.2, tz + 0.05, 0.07, 0.42, 0.07, 0.55, 0.22, 0.2, 0.5)
    ];
    for (local i = 0; i < btTargetParts.len(); ++i) {
        btTargetParts[i].setXRayShader(btXray);
        btTargetParts[i].setXRayHighlight(false);
    }

    btBullet = makePart(sphere, 0.0, -40.0, 0.0, 0.08, 0.08, 0.08, 1.0, 0.85, 0.35, 0.2);
    btBullet.setCastShadow(false);
    btBullet.setVisible(false);

    btTrailVis = [];
    btTrailPos = [];
    for (local i = 0; i < 24; ++i) {
        local s = makePart(sphere, 0.0, -40.0, 0.0, 0.04, 0.04, 0.04, 1.0, 0.7, 0.25, 0.15);
        s.setCastShadow(false);
        s.setVisible(false);
        btTrailVis.push(s);
        btTrailPos.push([0.0, -40.0, 0.0]);
    }

    btSparksVis = [];
    btSparks = [];
    for (local i = 0; i < 18; ++i) {
        local s = makePart(cube, 0.0, -40.0, 0.0, 0.05, 0.05, 0.05, 1.0, 0.45, 0.15, 0.25);
        s.setCastShadow(false);
        s.setVisible(false);
        btSparksVis.push(s);
        btSparks.push({ x = 0.0, y = -40.0, z = 0.0, vx = 0.0, vy = 0.0, vz = 0.0, life = 0.0 });
    }

    btRing = makePart(cyl, 0.0, -40.0, 0.0, 0.001, 0.02, 0.001, 1.0, 0.35, 0.2, 0.2);
    btRing.setCastShadow(false);
    btRing.setVisible(false);

    local data = eve.Font().newFontDataFromFile("assets/fonts/DejaVuSans-Bold.ttf", 28);
    btFont = gfx.newFont(data,
        " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-:/.");
    gfx.setFont(btFont);

    hideFx();
    btReady = true;
    print("BULLET_TIME_READY xray=1 gbuffer=1\n");
}

function beginFire() {
    if (phase != "idle") return;
    phase = "flight";
    phaseT = 0.0;
    timeScale = FLIGHT_SCALE;
    vignette = 0.0;
    setXrayHighlight(false);
    hideFx();

    local dx = TARGET.x - SHOOTER.x;
    local dy = (TARGET.y + 0.35) - SHOOTER.y;
    local dz = TARGET.z - SHOOTER.z;
    bulletDir = norm3(dx, dy, dz);
    bulletPos = [SHOOTER.x + 0.55, SHOOTER.y + 0.05, SHOOTER.z + 0.25];
    hitPos = [TARGET.x, TARGET.y + 0.35, TARGET.z];
    btBullet.setVisible(true);
    btBullet.setPosition(bulletPos[0], bulletPos[1], bulletPos[2]);
    for (local i = 0; i < btTrailPos.len(); ++i)
        btTrailPos[i] = [bulletPos[0], bulletPos[1], bulletPos[2]];
    btCycles += 1;
    fireRequested = false;
    print("BULLET_TIME_FIRE cycle=" + btCycles + " scale=" + timeScale + "\n");
}

function updateTrail() {
    if (btTrailVis == null || bulletPos == null) return;
    for (local i = btTrailPos.len() - 1; i > 0; --i)
        btTrailPos[i] = [btTrailPos[i - 1][0], btTrailPos[i - 1][1], btTrailPos[i - 1][2]];
    btTrailPos[0] = [bulletPos[0], bulletPos[1], bulletPos[2]];
    for (local i = 0; i < btTrailVis.len(); ++i) {
        local p = btTrailPos[i];
        local fade = 1.0 - i.tofloat() / btTrailVis.len();
        local sc = 0.03 + fade * 0.07;
        btTrailVis[i].setPosition(p[0], p[1], p[2]);
        btTrailVis[i].setScale(sc, sc, sc);
        btTrailVis[i].setTint(1.0, 0.55 + fade * 0.3, 0.18, 1.0);
        btTrailVis[i].setVisible(true);
    }
}

function beginImpact() {
    phase = "impact";
    phaseT = 0.0;
    timeScale = IMPACT_SCALE;
    setXrayHighlight(true);
    btBullet.setVisible(false);
    hitPos = [bulletPos[0], bulletPos[1], bulletPos[2]];
    btRing.setVisible(true);
    btRing.setPosition(hitPos[0], hitPos[1], hitPos[2]);
    for (local i = 0; i < btSparks.len(); ++i) {
        local a = i * 0.35;
        local s = btSparks[i];
        s.x = hitPos[0];
        s.y = hitPos[1];
        s.z = hitPos[2];
        s.vx = cos(a) * (2.5 + (i % 5) * 0.4);
        s.vy = 1.2 + (i % 4) * 0.55;
        s.vz = sin(a) * (2.5 + (i % 3) * 0.5);
        s.life = 1.0;
        btSparksVis[i].setVisible(true);
        btSparksVis[i].setPosition(s.x, s.y, s.z);
    }
    print("BULLET_TIME_IMPACT cycle=" + btCycles + " xray=1\n");
}

function updateCamera(realDt) {
    local desiredEye = camEye;
    local desiredTgt = camTarget;
    if (phase == "idle" || phase == "aftermath") {
        desiredEye = [IDLE_CAM_EYE.x, IDLE_CAM_EYE.y, IDLE_CAM_EYE.z];
        desiredTgt = [IDLE_CAM_TGT.x, IDLE_CAM_TGT.y + 0.15 * sin(worldClock * 0.7), IDLE_CAM_TGT.z];
        btCamera.setFov(lerp(btCamera.getFov(), 48.0, clamp(realDt * 3.0, 0.0, 1.0)));
    } else if (phase == "flight" && bulletPos != null) {
        local back = [-bulletDir[0], -bulletDir[1], -bulletDir[2]];
        desiredEye = [
            bulletPos[0] + back[0] * 1.8 + 0.15,
            bulletPos[1] + 0.55,
            bulletPos[2] + back[2] * 1.8 + 0.85
        ];
        desiredTgt = [
            bulletPos[0] + bulletDir[0] * 2.5,
            bulletPos[1] + bulletDir[1] * 2.5,
            bulletPos[2] + bulletDir[2] * 2.5
        ];
        btCamera.setFov(lerp(btCamera.getFov(), 38.0, clamp(realDt * 4.0, 0.0, 1.0)));
    } else if (phase == "impact" && hitPos != null) {
        // Stay on the sniper side of the cover so the target is occluded and
        // the X-ray silhouette reads through the wall (Sniper Elite framing).
        local ang = 0.35 + phaseT * 0.25;
        desiredEye = [
            COVER.x - 2.8 + cos(ang) * 0.6,
            hitPos[1] + 1.35,
            COVER.z + 3.2 + sin(ang) * 0.8
        ];
        desiredTgt = [hitPos[0], hitPos[1] + 0.15, hitPos[2]];
        btCamera.setFov(lerp(btCamera.getFov(), 40.0, clamp(realDt * 3.0, 0.0, 1.0)));
    }

    local k = clamp(realDt * 6.5, 0.0, 1.0);
    camEye = [
        lerp(camEye[0], desiredEye[0], k),
        lerp(camEye[1], desiredEye[1], k),
        lerp(camEye[2], desiredEye[2], k)
    ];
    camTarget = [
        lerp(camTarget[0], desiredTgt[0], k),
        lerp(camTarget[1], desiredTgt[1], k),
        lerp(camTarget[2], desiredTgt[2], k)
    ];
    btCamera.setEye(camEye[0], camEye[1], camEye[2]);
    btCamera.setTarget(camTarget[0], camTarget[1], camTarget[2]);
}

function updateIdle(simDt) {
    local sway = sin(worldClock * 1.1) * 0.12;
    if (btTargetParts != null && btTargetParts.len() > 0) {
        btTargetParts[0].setPosition(TARGET.x + sway, 0.95, TARGET.z);
        btTargetParts[1].setPosition(TARGET.x + sway, 1.85, TARGET.z);
    }
    idleWait -= simDt;
    if (idleWait <= 0.0 || fireRequested) beginFire();
}

function updateFlight(simDt, realDt) {
    vignette = lerp(vignette, 0.72, clamp(realDt * 2.5, 0.0, 1.0));
    local step = bulletSpeed * simDt;
    bulletPos[0] += bulletDir[0] * step;
    bulletPos[1] += bulletDir[1] * step;
    bulletPos[2] += bulletDir[2] * step;
    btBullet.setPosition(bulletPos[0], bulletPos[1], bulletPos[2]);
    updateTrail();

    local toHit = [
        hitPos[0] - bulletPos[0],
        hitPos[1] - bulletPos[1],
        hitPos[2] - bulletPos[2]
    ];
    local along = toHit[0] * bulletDir[0] + toHit[1] * bulletDir[1] + toHit[2] * bulletDir[2];
    if (along <= 0.08 || phaseT >= FLIGHT_MAX_REAL) beginImpact();
}

function updateImpact(simDt, realDt) {
    vignette = lerp(vignette, 0.85, clamp(realDt * 2.0, 0.0, 1.0));
    local pulse = 0.35 + phaseT * 1.8;
    btRing.setScale(pulse, 0.025, pulse);
    btRing.setTint(1.0, 0.25 + 0.2 * sin(phaseT * 18.0), 0.12, 1.0);

    for (local i = 0; i < btSparks.len(); ++i) {
        local s = btSparks[i];
        if (s.life <= 0.0) continue;
        s.life -= realDt;
        s.x += s.vx * realDt;
        s.y += s.vy * realDt;
        s.z += s.vz * realDt;
        s.vy -= 4.5 * realDt;
        local sc = 0.04 + 0.05 * clamp(s.life, 0.0, 1.0);
        btSparksVis[i].setPosition(s.x, s.y, s.z);
        btSparksVis[i].setScale(sc, sc, sc);
        btSparksVis[i].setRotation(phaseT * 4.0 + i, phaseT * 3.0, i * 0.2);
        if (s.life <= 0.0) btSparksVis[i].setVisible(false);
    }

    if (btTargetParts != null && btTargetParts.len() > 0) {
        local glow = 0.55 + 0.25 * (0.5 + 0.5 * sin(phaseT * 10.0));
        btTargetParts[0].setTint(glow, 0.18, 0.14, 1.0);
    }

    if (phaseT >= IMPACT_REAL) {
        phase = "aftermath";
        phaseT = 0.0;
        if (btTargetParts != null && btTargetParts.len() > 0)
            btTargetParts[0].setTint(0.55, 0.22, 0.2, 1.0);
    }
}

function updateAftermath(realDt) {
    local u = smooth(phaseT / AFTERMATH_REAL);
    timeScale = lerp(IMPACT_SCALE, 1.0, u);
    vignette = lerp(vignette, 0.0, clamp(realDt * 1.8, 0.0, 1.0));
    if (u > 0.55) setXrayHighlight(false);
    if (phaseT >= AFTERMATH_REAL) {
        phase = "idle";
        phaseT = 0.0;
        timeScale = 1.0;
        vignette = 0.0;
        idleWait = IDLE_LOOP;
        hideFx();
        setXrayHighlight(false);
        if (!btPassPrinted && btCycles >= 1) {
            btPassPrinted = true;
            print("BULLET_TIME_PASS cycles=" + btCycles + "\n");
        }
    }
}

eve_init <- function() {
    buildScene();
    phase = "idle";
    phaseT = 0.0;
    idleWait = 0.75;
    timeScale = 1.0;
    cinemaClock = 0.0;
    worldClock = 0.0;
    spaceWasDown = false;
};

eve_update <- function(dt) {
    if (!btReady) return;
    local realDt = dt;
    if (realDt > 0.05) realDt = 0.05;
    cinemaClock += realDt;
    phaseT += realDt;

    local spaceDown = keyboard.isDown("space");
    if (spaceDown && !spaceWasDown) fireRequested = true;
    spaceWasDown = spaceDown;

    local simDt = realDt * timeScale;
    worldClock += simDt;

    if (phase == "idle") updateIdle(simDt);
    else if (phase == "flight") updateFlight(simDt, realDt);
    else if (phase == "impact") updateImpact(simDt, realDt);
    else if (phase == "aftermath") updateAftermath(realDt);

    updateCamera(realDt);
};

function drawVignette() {
    if (vignette <= 0.01) return;
    local a = vignette * 0.48;
    local w = 1280.0;
    local h = 720.0;
    local band = 72.0 + vignette * 28.0;
    gfx.drawSolidRect(0.0, 0.0, w, band, 0.0, 0.0, 0.0, a);
    gfx.drawSolidRect(0.0, h - band, w, band, 0.0, 0.0, 0.0, a);
    gfx.drawSolidRect(0.0, 0.0, band * 0.45, h, 0.0, 0.0, 0.0, a * 0.7);
    gfx.drawSolidRect(w - band * 0.45, 0.0, band * 0.45, h, 0.0, 0.0, 0.0, a * 0.7);
}

eve_render <- function() {
    gfx.clear();
    gfx.render3D();
    drawVignette();

    local title = "BULLET TIME";
    local sub = "Space  fire    auto loop";
    if (phase == "flight") sub = "FOLLOWING ROUND    scale " + format("%.2f", timeScale);
    else if (phase == "impact") sub = "X-RAY IMPACT    scale " + format("%.2f", timeScale);
    else if (phase == "aftermath") sub = "RESUMING    scale " + format("%.2f", timeScale);

    gfx.drawText(btFont, title, 36.0, 28.0, 0.92, 0.95, 1.0, 1.0, 1.55);
    gfx.drawText(btFont, sub, 38.0, 68.0, 0.55, 0.72, 0.85, 1.0, 0.85);
    gfx.drawText(btFont, "cycles " + btCycles, 1100.0, 32.0, 0.4, 0.55, 0.65, 1.0, 0.8);
    if (xrayOn)
        gfx.drawText(btFont, "XRAY", 1100.0, 62.0, 0.95, 0.25, 0.18, 1.0, 0.9);
};
