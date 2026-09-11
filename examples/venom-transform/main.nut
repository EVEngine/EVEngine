// Venom-style body coverage demo (SakuraRabbit / Gwen-transform inspired).
// Height + noise shader spreads glossy black goo feet -> head, with an
// emissive frontier and decorative tendril ribbons.
// Space = play/pause | Left/Right = scrub | R = reset | P = screenshot

persist venomShader = null
persist venomCamera = null
persist venomParts = []
persist venomTendrils = []
persist venomDroplets = []
persist venomGround = null
persist venomCoverage = 0.0
persist venomPlaying = true
persist venomTime = 0.0
persist venomYaw = 0.35
persist venomFrame = 0
persist venomCaptureIdx = 0
persist venomCapturePending = false
persist venomCaptureName = ""
persist venomAutoMode = true
persist venomAutoHold = 0.0
persist venomDone = false

venomAutoShots <- [0.0, 0.28, 0.55, 0.82, 1.0]

function keyJust(k) {
    return key_just_pressed(k);
}

function pushUniforms() {
    venomShader.sendFloat("coverage", venomCoverage);
    venomShader.sendFloat("edgeWidth", 0.07);
    venomShader.sendFloat("noiseScale", 2.4);
    venomShader.sendFloat("time", venomTime);
    venomShader.sendFloat("gooR", 0.02);
    venomShader.sendFloat("gooG", 0.02);
    venomShader.sendFloat("gooB", 0.04);
    venomShader.sendFloat("edgeR", 0.55);
    venomShader.sendFloat("edgeG", 0.12);
    venomShader.sendFloat("edgeB", 0.85);
    venomShader.sendFloat("edgeGlow", 2.8);
    venomShader.sendFloat("gloss", 0.92);
}

function addPart(mesh, x, y, z, sx, sy, sz, r, g, b) {
    local m = eve.Renderable3D();
    m.setMesh(mesh);
    m.setShader(venomShader);
    m.setPosition(x, y, z);
    m.setScale(sx, sy, sz);
    m.setTint(r, g, b, 1.0);
    m.setRoughness(0.55);
    m.setCastShadow(true);
    m.setReceiveShadow(true);
    venomParts.push(m);
    return m;
}

function addTendril(seed) {
    local t = {
        body = eve.Renderable3D()
        seed = seed.tofloat()
        phase = seed * 0.73
    };
    t.body.setMesh(gfx.newMeshCylinder(10, 1, true));
    t.body.setShader(venomShader);
    t.body.setTint(0.03, 0.03, 0.05, 1.0);
    t.body.setRoughness(0.2);
    t.body.setCastShadow(false);
    t.body.setReceiveShadow(true);
    venomTendrils.push(t);
}

function addDroplet(seed) {
    local d = {
        body = eve.Renderable3D()
        seed = seed.tofloat()
        life = 0.0
    };
    d.body.setMesh(gfx.newMeshSphere(8, 6));
    d.body.setShader(venomShader);
    d.body.setTint(0.04, 0.04, 0.06, 1.0);
    d.body.setRoughness(0.15);
    d.body.setCastShadow(false);
    venomDroplets.push(d);
}

function buildMannequin() {
    local skinR = 0.78, skinG = 0.58, skinB = 0.48;
    local clothR = 0.18, clothG = 0.20, clothB = 0.28;

    addPart(gfx.newMeshSphere(28, 18), 0.0, 1.62, 0.0, 0.22, 0.26, 0.22, skinR, skinG, skinB);
    addPart(gfx.newMeshCylinder(18, 1, true), 0.0, 1.38, 0.0, 0.10, 0.14, 0.10, skinR, skinG, skinB);
    addPart(gfx.newMeshCylinder(20, 1, true), 0.0, 0.95, 0.0, 0.28, 0.55, 0.18, clothR, clothG, clothB);
    addPart(gfx.newMeshSphere(16, 12), 0.0, 1.18, 0.0, 0.30, 0.16, 0.20, clothR, clothG, clothB);

    addPart(gfx.newMeshCylinder(12, 1, true), -0.38, 1.05, 0.0, 0.08, 0.42, 0.08, skinR, skinG, skinB);
    addPart(gfx.newMeshCylinder(12, 1, true), 0.38, 1.05, 0.0, 0.08, 0.42, 0.08, skinR, skinG, skinB);
    addPart(gfx.newMeshSphere(10, 8), -0.38, 0.82, 0.0, 0.09, 0.09, 0.09, skinR, skinG, skinB);
    addPart(gfx.newMeshSphere(10, 8), 0.38, 0.82, 0.0, 0.09, 0.09, 0.09, skinR, skinG, skinB);

    addPart(gfx.newMeshCylinder(12, 1, true), -0.12, 0.40, 0.0, 0.10, 0.55, 0.10,
            clothR * 0.85, clothG * 0.85, clothB * 0.9);
    addPart(gfx.newMeshCylinder(12, 1, true), 0.12, 0.40, 0.0, 0.10, 0.55, 0.10,
            clothR * 0.85, clothG * 0.85, clothB * 0.9);
    addPart(gfx.newMeshSphere(10, 8), -0.12, 0.08, 0.18, 0.11, 0.06, 0.18, 0.08, 0.08, 0.10);
    addPart(gfx.newMeshSphere(10, 8), 0.12, 0.08, 0.18, 0.11, 0.06, 0.18, 0.08, 0.08, 0.10);

    for (local i = 0; i < 8; ++i) addTendril(i);
    for (local i = 0; i < 12; ++i) addDroplet(i);

    venomGround = eve.Renderable3D();
    venomGround.setMesh(gfx.newMeshCube(1.0));
    venomGround.setPosition(0.0, -0.05, 0.0);
    venomGround.setScale(6.0, 0.08, 6.0);
    venomGround.setTint(0.55, 0.16, 0.22, 1.0);
    venomGround.setRoughness(0.95);
    venomGround.setCastShadow(false);
    venomGround.setReceiveShadow(true);
}

function updateTendrils(_dt) {
    local frontY = venomCoverage * 1.75;
    local bloom = 1.0 - fabs(venomCoverage - 0.55) / 0.55;
    if (bloom < 0.0) bloom = 0.0;
    bloom = bloom * bloom;

    for (local i = 0; i < venomTendrils.len(); ++i) {
        local t = venomTendrils[i];
        local ang = t.phase + venomTime * (0.7 + t.seed * 0.05) + i * 0.785;
        local radius = 0.22 + 0.18 * bloom + 0.05 * sin(venomTime * 2.1 + t.seed);
        local y = frontY + 0.08 * sin(venomTime * 3.0 + t.seed) - 0.05;
        local len = (0.15 + 0.55 * bloom) * (0.7 + 0.3 * sin(venomTime * 1.7 + i));
        local thick = bloom > 0.05 ? (0.018 + 0.03 * bloom) : 0.001;
        local visibleLen = bloom > 0.05 ? len : 0.001;
        local pitch = 0.35 + 0.55 * sin(venomTime * 2.4 + t.seed);
        t.body.setPosition(cos(ang) * radius, y, sin(ang) * radius * 0.65);
        t.body.setScale(thick, visibleLen, thick);
        t.body.setRotation(ang, pitch, 0.0);
    }
}

function updateDroplets(dt) {
    local frontY = venomCoverage * 1.75;
    local active = venomCoverage > 0.05 && venomCoverage < 0.95;
    for (local i = 0; i < venomDroplets.len(); ++i) {
        local d = venomDroplets[i];
        d.life += dt * (0.8 + (i % 5) * 0.13);
        if (d.life > 1.0) d.life -= 1.0;
        local ang = d.seed * 1.7 + venomTime * 0.4;
        local spread = 0.15 + d.life * 0.55;
        local y = frontY + 0.25 * d.life - 0.05;
        local size = active ? (0.015 + 0.03 * (1.0 - d.life)) : 0.001;
        d.body.setPosition(cos(ang) * spread, y, sin(ang) * spread * 0.7);
        d.body.setScale(size, size * 1.4, size);
    }
}

function orbitCamera() {
    local radius = 3.4;
    local eyeY = 1.15;
    venomCamera.setEye(sin(venomYaw) * radius, eyeY, cos(venomYaw) * radius);
    venomCamera.setTarget(0.0, 0.95, 0.0);
}

function requestShot(name) {
    venomCaptureName = name;
    venomCapturePending = true;
}

eve_init = function() {
    gfx.setBackgroundColor(0.62, 0.18, 0.28, 1.0);
    if (venomShader == null) {
        venomShader = gfx.newMeshShader(fs.readText("shaders/venom.frag"));
        local names = ["coverage", "edgeWidth", "noiseScale", "time",
                       "gooR", "gooG", "gooB", "edgeR", "edgeG", "edgeB",
                       "edgeGlow", "gloss"];
        foreach (n in names) venomShader.declareFloat(n);
    }

    venomCamera = eve.Camera3D();
    venomCamera.setUp(0.0, 1.0, 0.0);
    venomCamera.setFov(40.0);
    venomCamera.setAmbient(0.22, 0.18, 0.24);
    venomCamera.setActive(true);
    gfx.setDirectionalLight(-0.35, 0.85, 0.40, 1.55, 1.35, 1.2);

    local rc = gfx.getRenderControl();
    rc.disable("ao");
    rc.disable("gi");
    rc.enable("msaa");
    rc.compile();

    if (venomParts.len() == 0) buildMannequin();
    orbitCamera();
    pushUniforms();
    print("[venom] Space play/pause | Left/Right scrub | R reset | P screenshot\n");
    print("[venom] auto demo will capture coverage steps then exit\n");
};

eve_update = function(dt) {
    venomFrame += 1;
    venomTime += dt;
    venomYaw += dt * 0.25;
    orbitCamera();

    if (keyJust("space")) venomPlaying = !venomPlaying;
    if (keyJust("r") || keyJust("R")) {
        venomCoverage = 0.0;
        venomPlaying = true;
        venomAutoMode = false;
    }
    if (keyJust("left")) {
        venomCoverage = max(0.0, venomCoverage - 0.05);
        venomPlaying = false;
        venomAutoMode = false;
    }
    if (keyJust("right")) {
        venomCoverage = min(1.0, venomCoverage + 0.05);
        venomPlaying = false;
        venomAutoMode = false;
    }
    if (keyJust("p") || keyJust("P"))
        requestShot(format("captures/venom-manual-%.0f.png", venomCoverage * 100.0));

    if (venomAutoMode) {
        if (venomCaptureIdx < venomAutoShots.len()) {
            local target = venomAutoShots[venomCaptureIdx];
            local diff = target - venomCoverage;
            if (fabs(diff) > 0.01) {
                local step = (diff > 0.0 ? 1.0 : -1.0) * dt * 0.55;
                if (fabs(step) > fabs(diff)) step = diff;
                venomCoverage += step;
                venomAutoHold = 0.0;
            } else {
                venomCoverage = target;
                venomAutoHold += dt;
                if (venomAutoHold > 0.35 && !venomCapturePending) {
                    requestShot(format("captures/venom-%02d-c%.0f.png",
                                       venomCaptureIdx, target * 100.0));
                }
            }
        } else if (!venomDone && !venomCapturePending) {
            venomDone = true;
            print("[venom] auto demo complete\n");
            // Prefer the load.nut bootBench early-exit so the window is not
            // destroyed mid-frame (win.close() here segfaults under Vulkan).
            eve.bootBench = true;
            return;
        }
    } else if (venomPlaying) {
        venomCoverage += dt * 0.22;
        if (venomCoverage >= 1.0) {
            venomCoverage = 1.0;
            venomPlaying = false;
        }
    }

    updateTendrils(dt);
    updateDroplets(dt);
    pushUniforms();
};

eve_render = function() {
    if (venomCapturePending && venomFrame > 8 &&
        gfx.saveFramePng(venomCaptureName)) {
        print("[venom] saved " + venomCaptureName + "\n");
        venomCapturePending = false;
        if (venomAutoMode) {
            venomCaptureIdx += 1;
            venomAutoHold = 0.0;
        }
    }
    gfx.clear();
    gfx.render3D();
};
