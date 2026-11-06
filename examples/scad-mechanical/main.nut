// Articulated SCAD inspection boom: named parts + ScadRig joint tree + per-frame pose.
dofile("pose.nut");
dofile("assets/materials.nut");

persist mech = {
    camera = null,
    lights = [],
    floor = null,
    axisMarkers = [],
    boom = null,
    time = 0.0,
    auto = true,
    selected = 1,
    hud = true,
    jointNames = ["pedestal", "slew", "shoulder", "elbow", "wrist", "tool"]
};

function mechLoadPart(name) {
    local data = model3d.newModelDataFromFile("assets/" + name + ".obj");
    if (data == null || data.getMeshCount() == 0) throw "SCAD mechanical part missing: " + name;
    local mat = mechMaterials[name];
    local parts = [];
    for (local i = 0; i < data.getMeshCount(); ++i) {
        local part = model3d.createRenderable(gfx, data, i);
        local material = part.getMaterial();
        material.setTint(mat.r, mat.g, mat.b, 1.0);
        material.setMetallic(mat.metal);
        material.setRoughness(mat.rough);
        material.setCastShadow(true);
        material.setReceiveShadow(true);
        part.setTint(mat.r, mat.g, mat.b, 1.0);
        part.setMetallic(mat.metal);
        part.setRoughness(mat.rough);
        part.setCastShadow(true);
        part.setReceiveShadow(true);
        parts.push({e = part, j = name});
    }
    return parts;
}

function mechBuildBoom() {
    local boom = {
        x = 0.0, y = 0.0, z = 0.0, yaw = 0.0,
        angles = {pedestal = 0.0, slew = 25.0, shoulder = 35.0, elbow = -55.0, wrist = 15.0, tool = -10.0},
        parts = [],
        transforms = {},
        rig = mechLoadRig("assets/mechanism.rig.json")
    };
    foreach (name in mech.jointNames) {
        local batch = mechLoadPart(name);
        foreach (part in batch) boom.parts.push(part);
    }
    mechPose(boom);
    return boom;
}

function mechResetCamera() {
    mech.camera.setEye(3.4, 2.4, 3.8);
    mech.camera.setTarget(0.0, 1.1, 0.0);
}

function mechApplyDemo(dt) {
    if (!mech.auto) return;
    mech.time += dt;
    local t = mech.time;
    mech.boom.angles.slew = 35.0 * sin(t * 0.55);
    mech.boom.angles.shoulder = 28.0 + 18.0 * sin(t * 0.70 + 0.4);
    mech.boom.angles.elbow = -40.0 - 35.0 * (0.5 + 0.5 * sin(t * 0.85));
    mech.boom.angles.wrist = 40.0 * sin(t * 1.15);
    mech.boom.angles.tool = -20.0 + 25.0 * sin(t * 1.4 + 1.0);
}

function mechNudge(delta) {
    local name = mech.jointNames[mech.selected];
    if (name == "pedestal") return;
    mech.auto = false;
    mech.boom.angles[name] = mech.boom.angles[name] + delta;
}

eve_init = function() {
    if ("dev" in eve) {
        eve.dev.ai.setVisible(false);
        eve.dev.console.setVisible(false);
    }
    gfx.setBackgroundColor(0.10, 0.12, 0.15, 1.0);
    mech.camera = eve.Camera3D();
    mech.camera.setUp(0.0, 1.0, 0.0);
    mech.camera.setFov(42.0);
    mech.camera.setAmbient(0.18, 0.20, 0.24);
    mech.camera.setExposure(0.55);
    mech.camera.setActive(true);
    mechResetCamera();

    local sun = eve.Light3D();
    sun.setType("directional");
    sun.setDirection(-0.45, -1.0, -0.35);
    sun.setColor(1.0, 0.95, 0.88, 2.4);
    sun.setCastShadow(true);
    sun.setShadowMethod("csm");
    sun.setShadowStrength(0.88);
    mech.lights.push(sun);
    gfx.setDirectionalLight(-0.45, 1.0, -0.35, 0.22, 0.24, 0.28);

    local fill = eve.Light3D();
    fill.setType("point");
    fill.setPosition(2.2, 2.6, 1.4);
    fill.setColor(0.55, 0.70, 1.0, 4.5);
    fill.setRadius(8.0);
    fill.setCastShadow(false);
    mech.lights.push(fill);

    mech.floor = eve.Renderable3D();
    mech.floor.setMesh(gfx.newMeshCube(1.0));
    mech.floor.setPosition(0.0, -0.04, 0.0);
    mech.floor.setScale(6.0, 0.08, 6.0);
    mech.floor.setTint(0.16, 0.17, 0.19, 1.0);
    mech.floor.setMetallic(0.05);
    mech.floor.setRoughness(0.85);
    mech.floor.setReceiveShadow(true);
    mech.floor.setCastShadow(false);

    foreach (name in mech.jointNames) {
        local marker = eve.Renderable3D();
        marker.setMesh(gfx.newMeshSphere(10, 8));
        marker.setScale(0.045, 0.045, 0.045);
        marker.setTint(0.15, 0.95, 0.55, 1.0);
        marker.setReceiveLight(false);
        marker.setCastShadow(false);
        mech.axisMarkers.push({name = name, e = marker});
    }

    mech.boom = mechBuildBoom();

    local rc = gfx.getRenderControl();
    foreach (feature in ["shadow", "gbuffer", "gbufferAlbedo", "ao", "aa"]) rc.enable(feature);
    rc.setPostProcessQuality("high");
    gfx.setMsaaSamples(4);
    rc.enable("msaa");
    rc.compile();

    ui.beginBuild();
    ui.beginWindow("ScadMechanical", "root");
    ui.text("SCAD / ARTICULATED MECHANICAL BOOM", "title");
    ui.text("", "state");
    ui.text("Space: auto  Left/Right: joint  Up/Down: angle  R: reset  H: HUD", "help");
    ui.end();
    ui.mountBuildAs("mech-hud");
    ui.select("mech-hud");
    ui.setHostOverlay(true);
    ui.setHostPos(18.0, 16.0, 0.0, 0.0);
    print("SCAD_MECHANICAL_READY\n");
};

eve_update = function(dt) {
    if (key_just_pressed("h")) mech.hud = !mech.hud;
    if (key_just_pressed("r")) {
        mech.auto = true;
        mech.time = 0.0;
        mech.boom.yaw = 0.0;
        mechResetCamera();
    }
    if (key_just_pressed("space")) mech.auto = !mech.auto;
    if (key_just_pressed("left") || key_just_pressed("a")) {
        mech.selected -= 1;
        if (mech.selected <= 0) mech.selected = mech.jointNames.len() - 1;
    }
    if (key_just_pressed("right") || key_just_pressed("d")) {
        mech.selected += 1;
        if (mech.selected >= mech.jointNames.len()) mech.selected = 1;
    }
    if (key_just_pressed("up") || key_just_pressed("w")) mechNudge(4.0);
    if (key_just_pressed("down") || key_just_pressed("s")) mechNudge(-4.0);

    mechApplyDemo(dt);
    mechPose(mech.boom);

    for (local i = 0; i < mech.axisMarkers.len(); ++i) {
        local marker = mech.axisMarkers[i];
        local t = mech.boom.transforms[marker.name];
        marker.e.setPosition(t.p[0].tofloat(), t.p[1].tofloat(), t.p[2].tofloat());
        if (i == mech.selected) marker.e.setTint(1.0, 0.45, 0.12, 1.0);
        else marker.e.setTint(0.15, 0.95, 0.55, 1.0);
    }

    if (mech.hud) {
        ui.select("mech-hud");
        local name = mech.jointNames[mech.selected];
        ui.setText("state", (mech.auto ? "AUTO" : "MANUAL") + " | joint=" + name +
            " angle=" + format("%.1f", mech.boom.angles[name]) + " deg");
    }
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    if (mech.hud) ui.beginFrameAndRender();
};
