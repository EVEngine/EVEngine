// Articulated SCAD overshot watermill: rotatable gears + walkable interior tour.
dofile("pose.nut");
dofile("assets/materials.nut");

persist mill = {
    camera = null,
    lights = [],
    water = null,
    tailWater = null,
    axisMarkers = [],
    assembly = null,
    time = 0.0,
    auto = true,
    wheelSpeed = 28.0, // deg/sec at the waterwheel
    selected = 3, // drive
    hud = true,
    mode = "orbit", // visit | orbit — open on the gallery looking into the mill
    orbit = -0.85,
    orbitRadius = 20.0,
    orbitElev = 9.0,
    visit = {x = 0.35, y = 1.70, z = -2.35, yaw = 3.05, pitch = -0.05},
    keyWas = {v = false, o = false, space = false, h = false, r = false},
    jointNames = ["building", "race", "sluice", "drive", "main", "runner", "hoist"]
};

function millPressed(name) {
    local down = keyboard.isDown(name);
    local was = mill.keyWas[name];
    mill.keyWas[name] = down;
    return down && !was;
}

function millLoadPart(name) {
    local data = model3d.newModelDataFromFile("assets/" + name + ".obj");
    if (data == null || data.getMeshCount() == 0) throw "SCAD watermill part missing: " + name;
    local mat = millMaterials[name];
    local parts = [];
    for (local i = 0; i < data.getMeshCount(); ++i) {
        local part = model3d.createRenderable(gfx, data, i);
        local material = part.getMaterial();
        // Preserve the material groups authored by design.py / mill.mtl.
        material.setMetallic(mat.metal);
        material.setRoughness(mat.rough);
        material.setCastShadow(true);
        material.setReceiveShadow(true);
        part.setTint(1.0, 1.0, 1.0, 1.0);
        part.setMetallic(mat.metal);
        part.setRoughness(mat.rough);
        part.setCastShadow(true);
        part.setReceiveShadow(true);
        parts.push({e = part, j = name});
    }
    return parts;
}

function millBuildAssembly() {
    local assembly = {
        x = 0.0, y = 0.0, z = 0.0, yaw = 0.0,
        angles = {building = 0.0, race = 0.0, sluice = 18.0, drive = 0.0, main = 0.0, runner = 0.0, hoist = 0.0},
        parts = [],
        transforms = {},
        rig = millLoadRig("assets/mill.rig.json")
    };
    foreach (name in mill.jointNames) {
        local batch = millLoadPart(name);
        foreach (part in batch) {
            assembly.parts.push(part);
        }
    }
    millSyncGears(assembly);
    millPose(assembly);
    return assembly;
}

function millApplyOrbitCamera() {
    local a = mill.orbit;
    local eyeX = sin(a) * mill.orbitRadius;
    local eyeZ = cos(a) * mill.orbitRadius;
    // Bias the look toward the open gallery so machinery stays readable.
    mill.camera.setEye(eyeX, mill.orbitElev, eyeZ);
    mill.camera.setTarget(0.4, 2.6, 0.2);
}

function millApplyVisitCamera() {
    local v = mill.visit;
    local cy = cos(v.yaw), sy = sin(v.yaw);
    local cp = cos(v.pitch), sp = sin(v.pitch);
    local dx = sy * cp, dy = sp, dz = -cy * cp;
    mill.camera.setEye(v.x, v.y, v.z);
    mill.camera.setTarget(v.x + dx, v.y + dy, v.z + dz);
}

function millResetVisit() {
    // Start on the open gallery looking into the gear floor / stones.
    mill.visit = {x = 0.35, y = 1.70, z = -2.35, yaw = 3.05, pitch = -0.05};
}

function millClampVisit() {
    local v = mill.visit;
    // Soft bounds of the hollow mill interior + gallery porch.
    if (v.x < -2.7) v.x = -2.7;
    if (v.x > 2.7) v.x = 2.7;
    if (v.z < -2.0) v.z = -2.0;
    if (v.z > 3.4) v.z = 3.4;
    // Two floors + loft via stairs corridor (x > 1.6).
    local onStairs = v.x > 1.55;
    if (!onStairs) {
        if (v.y < 1.55) v.y = 1.55;
        if (v.y > 1.75 && v.y < 3.8) v.y = 1.65; // snap near ground eye
        if (v.y >= 3.8 && v.y < 4.2) v.y = 4.0;  // stone-floor eye
        if (v.y >= 5.8) v.y = 6.0;               // loft eye
    }
    if (v.pitch < -1.1) v.pitch = -1.1;
    if (v.pitch > 1.1) v.pitch = 1.1;
}

function millUpdateVisit(dt) {
    local v = mill.visit;
    local turn = 1.35 * dt;
    local look = 1.1 * dt;
    if (keyboard.isDown("left") || keyboard.isDown("q")) v.yaw -= turn;
    if (keyboard.isDown("right") || keyboard.isDown("e")) v.yaw += turn;
    if (keyboard.isDown("up")) v.pitch += look;
    if (keyboard.isDown("down")) v.pitch -= look;

    local speed = (keyboard.isDown("lshift") || keyboard.isDown("rshift") || keyboard.isDown("shift")) ? 4.2 : 2.4;
    local cy = cos(v.yaw), sy = sin(v.yaw);
    local forwardX = sy, forwardZ = -cy;
    local rightX = cy, rightZ = sy;
    local mx = 0.0, mz = 0.0;
    if (keyboard.isDown("w")) { mx += forwardX; mz += forwardZ; }
    if (keyboard.isDown("s")) { mx -= forwardX; mz -= forwardZ; }
    if (keyboard.isDown("a")) { mx -= rightX; mz -= rightZ; }
    if (keyboard.isDown("d")) { mx += rightX; mz += rightZ; }
    local len = sqrt(mx * mx + mz * mz);
    if (len > 1e-5) {
        mx /= len; mz /= len;
        v.x += mx * speed * dt;
        v.z += mz * speed * dt;
    }
    // Stair corridor auto-level: climb with W/S while in the stair bay.
    if (v.x > 1.55 && v.x < 2.7 && v.z > 0.4 && v.z < 2.6) {
        if (keyboard.isDown("w")) v.y += 1.6 * dt;
        if (keyboard.isDown("s")) v.y -= 1.6 * dt;
        if (v.y < 1.55) v.y = 1.55;
        if (v.y > 6.2) v.y = 6.2;
    } else {
        // Snap to nearest floor eye height when leaving stairs.
        if (v.y < 2.8) v.y = 1.65;
        else if (v.y < 5.0) v.y = 4.0;
        else v.y = 6.0;
    }
    millClampVisit();
    millApplyVisitCamera();
}

function millApplyDemo(dt) {
    if (!mill.auto) return;
    mill.time += dt;
    local flowing = mill.assembly.angles.sluice > 8.0;
    if (flowing) {
        mill.assembly.angles.drive = mill.assembly.angles.drive + mill.wheelSpeed * dt;
        // Keep angles from growing without bound for HUD readability.
        if (mill.assembly.angles.drive > 1e7 || mill.assembly.angles.drive < -1e7)
            mill.assembly.angles.drive = 0.0;
    }
    millSyncGears(mill.assembly);
}

function millNudge(delta) {
    local name = mill.jointNames[mill.selected];
    mill.auto = false;
    if (name == "building" || name == "race") return;
    if (name == "sluice") {
        mill.assembly.angles.sluice = mill.assembly.angles.sluice + delta;
        return;
    }
    if (name == "drive" || name == "main" || name == "runner" || name == "hoist") {
        // Manual crank acts on the waterwheel; gears stay ratio-locked.
        mill.assembly.angles.drive = mill.assembly.angles.drive + delta;
        millSyncGears(mill.assembly);
    }
}

eve_init = function() {
    if ("dev" in eve) {
        eve.dev.ai.setVisible(false);
        eve.dev.console.setVisible(false);
    }
    // Soft overcast: enough ambient to read timber, mild sun for form, no hot lamps.
    gfx.setBackgroundColor(0.16, 0.20, 0.26, 1.0);
    mill.camera = eve.Camera3D();
    mill.camera.setUp(0.0, 1.0, 0.0);
    mill.camera.setFov(48.0);
    mill.camera.setAmbient(0.35, 0.36, 0.38);
    mill.camera.setExposure(0.3);
    mill.camera.setBloom(0.0, 1.0);
    mill.camera.setActive(true);
    millApplyOrbitCamera();

    local sun = eve.Light3D();
    sun.setType("directional");
    sun.setDirection(-0.35, 1.0, 0.25);
    sun.setColor(0.95, 0.92, 0.88, 1.0);
    sun.setCastShadow(true);
    sun.setShadowMethod("csm");
    sun.setShadowStrength(0.78);
    mill.lights.push(sun);
    gfx.setDirectionalLight(-0.35, 1.0, -0.25, 0.09, 0.10, 0.11);
    // No interior point lamps — they washed out timber and left hot spots on the stone floor.

    // Pond + tail-race water plates (opaque teal accents on stone racework).
    mill.water = eve.Renderable3D();
    mill.water.setMesh(gfx.newMeshCube(1.0));
    mill.water.setPosition(-1.95, 3.32, 3.45);
    mill.water.setScale(4.15, 0.025, 0.78);
    mill.water.setTint(0.16, 0.42, 0.50, 1.0);
    mill.water.setMetallic(0.15);
    mill.water.setRoughness(0.18);
    mill.water.setCastShadow(false);
    mill.water.setReceiveShadow(true);
    mill.tailWater = eve.Renderable3D();
    mill.tailWater.setMesh(gfx.newMeshCube(1.0));
    mill.tailWater.setPosition(-3.90, 0.10, 2.70);
    mill.tailWater.setScale(1.15, 0.02, 4.10);
    mill.tailWater.setTint(0.14, 0.38, 0.46, 1.0);
    mill.tailWater.setMetallic(0.12);
    mill.tailWater.setRoughness(0.22);
    mill.tailWater.setCastShadow(false);

    foreach (name in mill.jointNames) {
        local marker = eve.Renderable3D();
        marker.setMesh(gfx.newMeshSphere(10, 8));
        marker.setScale(0.06, 0.06, 0.06);
        marker.setTint(0.15, 0.95, 0.55, 1.0);
        marker.setReceiveLight(false);
        marker.setCastShadow(false);
        mill.axisMarkers.push({name = name, e = marker});
    }

    mill.assembly = millBuildAssembly();

    local rc = gfx.getRenderControl();
    foreach (feature in ["shadow", "gbuffer", "gbufferAlbedo", "ao", "aa"]) rc.enable(feature);
    rc.setPostProcessQuality("high");
    gfx.setMsaaSamples(4);
    rc.enable("msaa");
    rc.compile();

    ui.beginBuild();
    ui.beginWindow("ScadWatermill", "root");
    ui.text("SCAD / OVERSHOT WATERMILL", "title");
    ui.text("", "state");
    ui.text("V: visit interior  O: orbit  Space: flow  [/]: sluice  Left/Right: joint  Up/Down: nudge  R: reset  H: HUD", "help");
    ui.end();
    ui.mountBuildAs("mill-hud");
    ui.select("mill-hud");
    ui.setHostOverlay(true);
    ui.setHostPos(18.0, 16.0, 0.0, 0.0);
    print("SCAD_WATERMILL_READY\n");
};

eve_update = function(dt) {
    // Edge-detect held keys so MCP eve_input_batch injection also switches modes.
    if (millPressed("h") || key_just_pressed("h")) mill.hud = !mill.hud;
    if (millPressed("r") || key_just_pressed("r")) {
        mill.auto = true;
        mill.time = 0.0;
        mill.assembly.angles.drive = 0.0;
        mill.assembly.angles.sluice = 18.0;
        millSyncGears(mill.assembly);
        mill.orbit = -0.85;
        millResetVisit();
        if (mill.mode == "orbit") millApplyOrbitCamera();
        else millApplyVisitCamera();
    }
    if (millPressed("space") || key_just_pressed("space")) mill.auto = !mill.auto;
    if (millPressed("v") || key_just_pressed("v")) {
        mill.mode = "visit";
        millApplyVisitCamera();
    }
    if (millPressed("o") || key_just_pressed("o")) {
        mill.mode = "orbit";
        millApplyOrbitCamera();
    }
    if (key_just_pressed("left") || key_just_pressed("a")) {
        if (mill.mode == "orbit") {
            mill.selected -= 1;
            if (mill.selected < 0) mill.selected = mill.jointNames.len() - 1;
        }
    }
    if (key_just_pressed("right") || key_just_pressed("d")) {
        if (mill.mode == "orbit") {
            mill.selected += 1;
            if (mill.selected >= mill.jointNames.len()) mill.selected = 0;
        }
    }
    if (key_just_pressed("up") || key_just_pressed("w")) {
        if (mill.mode == "orbit") millNudge(6.0);
    }
    if (key_just_pressed("down") || key_just_pressed("s")) {
        if (mill.mode == "orbit") millNudge(-6.0);
    }
    if (key_just_pressed("leftbracket") || key_just_pressed("[")) {
        mill.assembly.angles.sluice = mill.assembly.angles.sluice - 5.0;
        if (mill.assembly.angles.sluice < 0.0) mill.assembly.angles.sluice = 0.0;
    }
    if (key_just_pressed("rightbracket") || key_just_pressed("]")) {
        mill.assembly.angles.sluice = mill.assembly.angles.sluice + 5.0;
        if (mill.assembly.angles.sluice > 75.0) mill.assembly.angles.sluice = 75.0;
    }

    millApplyDemo(dt);
    millPose(mill.assembly);

    if (mill.mode == "orbit") {
        if (mill.auto) mill.orbit += dt * 0.12;
        millApplyOrbitCamera();
    } else {
        millUpdateVisit(dt);
    }

    for (local i = 0; i < mill.axisMarkers.len(); ++i) {
        local marker = mill.axisMarkers[i];
        local t = mill.assembly.transforms[marker.name];
        marker.e.setPosition(t.p[0].tofloat(), t.p[1].tofloat(), t.p[2].tofloat());
        if (i == mill.selected) marker.e.setTint(1.0, 0.45, 0.12, 1.0);
        else marker.e.setTint(0.15, 0.95, 0.55, 1.0);
        // Hide markers while visiting so they do not clutter the interior.
        marker.e.setVisible(mill.hud && mill.mode == "orbit");
    }

    if (mill.hud) {
        ui.select("mill-hud");
        local name = mill.jointNames[mill.selected];
        local flow = mill.assembly.angles.sluice > 8.0 ? "FLOWING" : "GATED";
        ui.setText("state", mill.mode.toupper() + " | " + (mill.auto ? "AUTO" : "MANUAL") +
            " | " + flow + " | joint=" + name +
            " wheel=" + format("%.1f", mill.assembly.angles.drive) +
            " sluice=" + format("%.0f", mill.assembly.angles.sluice) + " deg");
    }
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    if (mill.hud) ui.beginFrameAndRender();
};
