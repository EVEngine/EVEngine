// Sci-fi surface vines: 7–10 crawl tips stick to World3D colliders, fork, and leave trails.
dofile("vines.nut");

persist vineCamera = null
persist vineWorld = null
persist vineSystem = null
persist vineCube = null
persist vineSphere = null
persist vineTipMarkers = []
persist vineOriginMarker = null
persist vineAge = 0.0
persist vineOrbit = 0.35
persist vinePaused = false
persist vineCasts = 0
persist vineLastSpace = false
persist vineReadyPrinted = false
persist vineScreenshotSaved = false
persist vineSettings = {
    seed = 20261105,
    rootCount = 8,
    speed = 1.55,
    tipLife = 7.5,
    forkSpacing = 1.15,
    forkAngle = 0.72,
    forkChance = 0.82,
    maxDepth = 2,
    maxTips = 36,
    yawNoise = 0.55,
    perturb = 1.1,
    minPointSpacing = 0.08
}

function vineRequire(result, context) {
    if (!result.ok) {
        local summary = ("status" in result) ? result.status.summary : (("message" in result) ? result.message : "unknown");
        throw context + ": " + summary;
    }
    return result.value;
}

function vinePlace(mesh, x, y, z, sx, sy, sz, r, g, b, lit = true) {
    local e = eve.Renderable3D();
    e.setMesh(mesh);
    e.setPosition(x, y, z);
    e.setScale(sx, sy, sz);
    e.setTint(r, g, b, 1.0);
    e.setReceiveLight(lit);
    e.setRoughness(0.88);
    e.setMetallic(0.05);
    e.setCastShadow(lit);
    e.setReceiveShadow(lit);
    return e;
}

function vineAddBoxCollider(world, x, y, z, sx, sy, sz) {
    local body = world.newBody("static", x, y, z);
    body.newBoxShape(sx, sy, sz, 1.0, 0.6, 0.0);
    return body;
}

function vineBuildScene(world, cube) {
    // Ground pad connecting the house pad and mountain foothills.
    vinePlace(cube, 0.0, -0.2, 0.0, 28.0, 0.4, 22.0, 0.12, 0.16, 0.20);
    vineAddBoxCollider(world, 0.0, -0.2, 0.0, 28.0, 0.4, 22.0);

    // Courtyard plate where the vines erupt.
    vinePlace(cube, 0.0, 0.02, 0.0, 7.5, 0.05, 7.5, 0.18, 0.22, 0.26);
    vineAddBoxCollider(world, 0.0, 0.02, 0.0, 7.5, 0.05, 7.5);

    // House cluster (walls + roof) on the -X side.
    vinePlace(cube, -6.2, 1.15, -1.2, 3.4, 2.3, 3.0, 0.42, 0.36, 0.30);
    vineAddBoxCollider(world, -6.2, 1.15, -1.2, 3.4, 2.3, 3.0);
    vinePlace(cube, -6.2, 2.55, -1.2, 3.8, 0.55, 3.4, 0.55, 0.28, 0.22);
    vineAddBoxCollider(world, -6.2, 2.55, -1.2, 3.8, 0.55, 3.4);
    vinePlace(cube, -4.2, 0.55, 1.4, 1.4, 1.1, 1.5, 0.38, 0.33, 0.28);
    vineAddBoxCollider(world, -4.2, 0.55, 1.4, 1.4, 1.1, 1.5);

    // Mountain / ridge terraces climbing toward +X/+Z.
    local terraces = [
        [5.5, 0.55, 3.5, 6.0, 1.1, 5.5, 0.28, 0.30, 0.34],
        [7.8, 1.55, 4.8, 5.2, 1.4, 4.6, 0.32, 0.34, 0.38],
        [9.5, 2.85, 5.6, 4.0, 1.8, 3.6, 0.36, 0.38, 0.42],
        [10.8, 4.2, 6.1, 2.8, 1.6, 2.6, 0.42, 0.44, 0.48],
        [4.2, 0.85, -3.8, 4.5, 1.7, 3.2, 0.30, 0.32, 0.36],
        [6.6, 2.1, -4.4, 3.4, 1.5, 2.4, 0.34, 0.36, 0.40]
    ];
    foreach (t in terraces) {
        vinePlace(cube, t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7], t[8]);
        vineAddBoxCollider(world, t[0], t[1], t[2], t[3], t[4], t[5]);
    }

    // Connecting berms so vines can crawl from courtyard onto house / mountain.
    vinePlace(cube, -3.0, 0.28, -0.2, 2.4, 0.55, 2.0, 0.22, 0.25, 0.28);
    vineAddBoxCollider(world, -3.0, 0.28, -0.2, 2.4, 0.55, 2.0);
    vinePlace(cube, 2.6, 0.32, 1.8, 2.8, 0.65, 2.6, 0.24, 0.27, 0.30);
    vineAddBoxCollider(world, 2.6, 0.32, 1.8, 2.8, 0.65, 2.6);
}

function vineMakePolyline(points, r, g, b, a, width) {
    return vineRequire(gfx.newPrimitivePolyline3D(points, false, r, g, b, a, width), "vine polyline");
}

function vineSyncBranchVisuals() {
    if (vineSystem == null) return;
    foreach (branch in vineSystem.getBranches()) {
        local pts = branch.points;
        if (pts.len() < 6) continue;
        if (branch.polyline == null) {
            branch.polyline = vineMakePolyline(pts, 0.25, 1.0, 0.55, 1.0, 3.2);
            branch.glow = vineMakePolyline(pts, 0.05, 0.55, 0.35, 0.45, 7.5);
            branch.dirty = false;
        } else if (branch.dirty) {
            vineRequire(branch.polyline.setPolyline(pts, false), "update vine core");
            vineRequire(branch.glow.setPolyline(pts, false), "update vine glow");
            branch.dirty = false;
        }
        local aliveAlpha = branch.alive ? 1.0 : 0.55;
        vineRequire(branch.polyline.setColor(0.35, 1.0, 0.62, aliveAlpha), "vine core color");
        vineRequire(branch.glow.setColor(0.08, 0.65, 0.42, 0.28 * aliveAlpha), "vine glow color");
    }
}

function vineSyncTipMarkers() {
    if (vineSystem == null) return;
    local tips = vineSystem.getTips();
    local i = 0;
    foreach (tip in tips) {
        if (i >= vineTipMarkers.len()) break;
        local marker = vineTipMarkers[i];
        if (tip.alive) {
            marker.setVisible(true);
            marker.setPosition(tip.x + tip.nx * 0.04, tip.y + tip.ny * 0.04, tip.z + tip.nz * 0.04);
            local pulse = 0.08 + 0.04 * sin(vineAge * 14.0 + i.tofloat());
            marker.setScale(pulse, pulse, pulse);
        } else {
            marker.setVisible(false);
        }
        i += 1;
    }
    for (; i < vineTipMarkers.len(); ++i) vineTipMarkers[i].setVisible(false);
}

function vineClearVisuals() {
    if (vineSystem == null) return;
    foreach (branch in vineSystem.getBranches()) {
        if (branch.polyline != null) {
            vineRequire(branch.polyline.remove(), "remove vine core");
            branch.polyline = null;
        }
        if (branch.glow != null) {
            vineRequire(branch.glow.remove(), "remove vine glow");
            branch.glow = null;
        }
    }
}

function vineCast() {
    vineClearVisuals();
    vineSettings.seed = vineSettings.seed + 17 + vineCasts * 3;
    vineSystem = vineCreateSystem(vineSettings);
    local count = vineSystem.spawnBurst(vineWorld, 0.0, 0.08, 0.0);
    vineAge = 0.0;
    vineCasts += 1;
    vineSyncBranchVisuals();
    vineSyncTipMarkers();
    print(format("SURFACE_VINE_CAST roots=%d seed=%d cast=%d\n", count, vineSettings.seed, vineCasts));
}

function vineControl(id) {
    if (id == "cast") vineCast();
    if (id == "pause") {
        vinePaused = !vinePaused;
        ui.setText("pause", vinePaused ? "Resume" : "Pause");
    }
}

function vineBuildUi() {
    ui.setTheme("dark");
    ui.setScale(0.78);
    ui.beginBuild();
    ui.beginWindow("SURFACE VINES", "root");
    ui.text("贴面爬藤 / surface crawl", "title");
    ui.text("7-10 tips stick to house / ground / mountain and fork", "subtitle");
    ui.beginRow("transport", 6.0);
    ui.button("Cast", "cast");
    ui.button("Pause", "pause");
    ui.end();
    ui.slider("Speed", vineSettings.speed, 0.4, 3.5, "speed");
    ui.slider("Fork spacing / m", vineSettings.forkSpacing, 0.4, 2.5, "forkSpacing");
    ui.slider("Fork angle", vineSettings.forkAngle, 0.2, 1.4, "forkAngle");
    ui.slider("Yaw noise", vineSettings.yawNoise, 0.0, 1.5, "yawNoise");
    ui.slider("Orbit", vineOrbit, 0.0, 6.28318530718, "orbit");
    ui.text("Space = cast   ·   tips crawl Box3D surfaces", "hint");
    ui.text("", "status");
    ui.end();
    ui.mountBuildAs("vine");
    ui.select("vine");
    ui.setHostPos(12.0, 12.0, 0.0, 0.0);
    ui.setHostSize(300.0, 420.0);
    ui.setHostOverlay(true);
}

function vineApplyUi() {
    ui.select("vine");
    local click = ui.consumeClick();
    while (click != "") {
        local slash = click.find("/");
        vineControl(slash == null ? click : click.slice(slash + 1));
        click = ui.consumeClick();
    }
    local change = ui.consumeChange();
    while (change != "") {
        if (change == "vine/speed") vineSettings.speed = ui.getValue("speed");
        else if (change == "vine/forkSpacing") vineSettings.forkSpacing = ui.getValue("forkSpacing");
        else if (change == "vine/forkAngle") vineSettings.forkAngle = ui.getValue("forkAngle");
        else if (change == "vine/yawNoise") vineSettings.yawNoise = ui.getValue("yawNoise");
        else if (change == "vine/orbit") vineOrbit = ui.getValue("orbit");
        change = ui.consumeChange();
    }
    local living = vineSystem == null ? 0 : vineSystem.livingTipCount();
    local branches = vineSystem == null ? 0 : vineSystem.branchCount();
    ui.setText("status", format("tips=%d  branches=%d  age=%.1fs  %s",
        living, branches, vineAge, vinePaused ? "PAUSED" : "GROWING"));
}

eve_init <- function() {
    if ("dev" in eve) {
        eve.dev.console.setVisible(false);
        eve.dev.ai.setVisible(false);
    }
    gfx.setBackgroundColor(0.015, 0.028, 0.045, 1.0);
    vineCube = gfx.newMeshCube(1.0);
    vineSphere = gfx.newMeshSphere(12, 8);

    local physicsMod = eve.Physics();
    vineWorld = physicsMod.newWorld3D(0.0, 0.0, 0.0, false);
    vineBuildScene(vineWorld, vineCube);

    vineOriginMarker = vinePlace(vineSphere, 0.0, 0.12, 0.0, 0.18, 0.18, 0.18, 0.55, 1.0, 0.75, false);
    for (local i = 0; i < vineSettings.maxTips; ++i) {
        local tip = vinePlace(vineSphere, 0.0, 0.0, 0.0, 0.1, 0.1, 0.1, 0.4, 1.0, 0.85, false);
        tip.setVisible(false);
        vineTipMarkers.append(tip);
    }

    vineCamera = eve.Camera3D();
    vineCamera.setTarget(0.5, 1.4, 0.8);
    vineCamera.setUp(0.0, 1.0, 0.0);
    vineCamera.setFov(46.0);
    vineCamera.setAmbient(0.22, 0.28, 0.34);
    vineCamera.setActive(true);
    gfx.setDirectionalLight(-0.45, -1.0, -0.25, 1.05, 1.0, 0.92);

    vineBuildUi();
    vineCast();
    print("SURFACE_VINE_READY roots=8 house+mountain colliders=active\n");
    vineReadyPrinted = true;
};

eve_update <- function(dt) {
    local step = dt;
    if (step > 0.05) step = 0.05;
    vineApplyUi();

    local space = keyboard.isDown("SPACE") || keyboard.isDown("Space") || keyboard.isDown("space");
    if (space && !vineLastSpace) vineCast();
    vineLastSpace = space;

    if (!vinePaused && vineSystem != null) {
        vineAge += step;
        vineSystem.update(vineWorld, step);
        vineSyncBranchVisuals();
        vineSyncTipMarkers();
    }

    local eyeR = 14.5;
    local eyeY = 7.8;
    vineCamera.setEye(0.5 + cos(vineOrbit) * eyeR, eyeY, 0.8 + sin(vineOrbit) * eyeR);
    vineCamera.setTarget(0.5, 1.4, 0.8);

    if (!vineScreenshotSaved && vineAge > 2.8) {
        if (gfx.saveFramePng("surface-vine-growth.png")) {
            vineScreenshotSaved = true;
            print("SURFACE_VINE_SCREENSHOT path=surface-vine-growth.png\n");
        }
    }
};

eve_render <- function() {
    gfx.clear();
    gfx.render3D();
    ui.select("vine");
    ui.beginFrameAndRender();
};
