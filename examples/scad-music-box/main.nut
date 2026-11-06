// Articulated SCAD cylinder music box: rotating gears + pin-plucked PCM melody.
dofile("pose.nut");
dofile("assets/materials.nut");
dofile("synth.nut");

persist box = {
    camera = null,
    lights = [],
    floor = null,
    assembly = null,
    synth = null,
    stream = null,
    time = 0.0,
    playing = true,
    lidOpen = true,
    lidTarget = -72.0,
    barrelSpeed = 30.0, // deg/sec → one chorus every 12s
    selected = 4, // cylinder
    hud = true,
    orbit = 0.95,
    orbitRadius = 0.55,
    orbitElev = 0.28,
    keyWas = {space = false, l = false, h = false, r = false, m = false},
    jointNames = ["shell", "lid", "bed", "barrel", "cylinder", "comb", "governor", "dancer"]
};

function boxChecked(result) {
    if (!result.ok) throw result.status.summary;
    return result.value;
}

function boxPressed(name) {
    local down = keyboard.isDown(name);
    local was = box.keyWas[name];
    box.keyWas[name] = down;
    return down && !was;
}

function boxLoadPart(name) {
    local data = model3d.newModelDataFromFile("assets/" + name + ".obj");
    if (data == null || data.getMeshCount() == 0) throw "SCAD music-box part missing: " + name;
    local mat = boxMaterials[name];
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

function boxBuildAssembly() {
    local assembly = {
        x = 0.0, y = 0.0, z = 0.0, yaw = 0.0,
        angles = {
            shell = 0.0, lid = -72.0, bed = 0.0, barrel = 0.0,
            cylinder = 0.0, comb = 0.0, governor = 0.0, dancer = 0.0
        },
        parts = [],
        transforms = {},
        rig = boxLoadRig("assets/musicbox.rig.json")
    };
    foreach (name in box.jointNames) {
        local batch = boxLoadPart(name);
        foreach (part in batch) assembly.parts.push(part);
    }
    boxSyncGears(assembly);
    boxPose(assembly);
    return assembly;
}

function boxApplyOrbitCamera() {
    local a = box.orbit;
    local eyeX = sin(a) * box.orbitRadius;
    local eyeZ = cos(a) * box.orbitRadius;
    box.camera.setEye(eyeX, box.orbitElev, eyeZ);
    box.camera.setTarget(0.0, 0.06, 0.01);
}

function boxPumpAudio() {
    if (box.stream == null || box.synth == null) return;
    local info = box.stream.getInfo();
    // Keep the 8-block queue as full as possible (24 kHz → ~340ms when full).
    local budget = 8;
    while (budget > 0 && info.availableBlocks > 0) {
        local block = box.synth.renderBlock(1024);
        local submitted = box.stream.submit(block);
        if (!submitted.ok) break;
        info = box.stream.getInfo();
        budget -= 1;
    }
}

function boxApplyDemo(dt) {
    // Smooth lid toward open/closed target.
    local lid = box.assembly.angles.lid;
    local target = box.lidOpen ? box.lidTarget : -4.0;
    local lidSpeed = 55.0 * dt;
    if (lid < target) {
        lid += lidSpeed;
        if (lid > target) lid = target;
    } else if (lid > target) {
        lid -= lidSpeed;
        if (lid < target) lid = target;
    }
    box.assembly.angles.lid = lid;

    if (!box.playing) {
        boxSyncGears(box.assembly);
        return;
    }
    box.time += dt;
    box.assembly.angles.barrel = box.assembly.angles.barrel + box.barrelSpeed * dt;
    if (box.assembly.angles.barrel > 1e7 || box.assembly.angles.barrel < -1e7)
        box.assembly.angles.barrel = 0.0;
    boxSyncGears(box.assembly);

    local cyl = box.assembly.angles.cylinder;
    while (cyl < 0.0) cyl += 360.0;
    while (cyl >= 360.0) cyl -= 360.0;
    box.synth.advanceCylinder(cyl);
}

eve_init = function() {
    if ("dev" in eve) {
        eve.dev.ai.setVisible(false);
        eve.dev.console.setVisible(false);
    }
    gfx.setBackgroundColor(0.12, 0.11, 0.13, 1.0);
    box.camera = eve.Camera3D();
    box.camera.setUp(0.0, 1.0, 0.0);
    box.camera.setFov(40.0);
    box.camera.setAmbient(0.16, 0.15, 0.14);
    box.camera.setExposure(0.48);
    box.camera.setActive(true);
    boxApplyOrbitCamera();

    local sun = eve.Light3D();
    sun.setType("directional");
    sun.setDirection(-0.40, -1.0, -0.25);
    sun.setColor(1.0, 0.94, 0.86, 1.8);
    sun.setCastShadow(true);
    sun.setShadowMethod("csm");
    sun.setShadowStrength(0.85);
    box.lights.push(sun);
    gfx.setDirectionalLight(-0.40, 1.0, -0.25, 0.16, 0.15, 0.14);

    local lamp = eve.Light3D();
    lamp.setType("point");
    lamp.setPosition(0.15, 0.28, 0.12);
    lamp.setColor(1.0, 0.82, 0.55, 3.2);
    lamp.setRadius(1.4);
    lamp.setCastShadow(false);
    box.lights.push(lamp);

    box.floor = eve.Renderable3D();
    box.floor.setMesh(gfx.newMeshCube(1.0));
    box.floor.setPosition(0.0, -0.02, 0.0);
    box.floor.setScale(1.2, 0.04, 1.2);
    box.floor.setTint(0.14, 0.13, 0.14, 1.0);
    box.floor.setMetallic(0.05);
    box.floor.setRoughness(0.88);
    box.floor.setReceiveShadow(true);
    box.floor.setCastShadow(false);

    box.assembly = boxBuildAssembly();
    box.synth = BoxSynth();
    if (box.stream != null) box.stream.close();
    box.stream = boxChecked(audio.newPcmStream(24000, 1));
    // Prime the queue before the first present so Lavapipe hitch cannot starve PCM.
    boxPumpAudio();
    boxChecked(box.stream.play());
    boxPumpAudio();

    local rc = gfx.getRenderControl();
    foreach (feature in ["shadow", "gbuffer", "gbufferAlbedo", "ao", "aa"]) rc.enable(feature);
    rc.setPostProcessQuality("high");
    gfx.setMsaaSamples(4);
    rc.enable("msaa");
    rc.compile();

    ui.beginBuild();
    ui.beginWindow("ScadMusicBox", "root");
    ui.text("SCAD / CYLINDER MUSIC BOX", "title");
    ui.text("", "state");
    ui.text("Space: play/pause  L: lid  M: mute  Left/Right: orbit  R: reset  H: HUD", "help");
    ui.end();
    ui.mountBuildAs("box-hud");
    ui.select("box-hud");
    ui.setHostOverlay(true);
    ui.setHostPos(18.0, 16.0, 0.0, 0.0);
    print("SCAD_MUSIC_BOX_READY\n");
};

eve_update = function(dt) {
    if (boxPressed("h") || key_just_pressed("h")) box.hud = !box.hud;
    if (boxPressed("space") || key_just_pressed("space")) box.playing = !box.playing;
    if (boxPressed("l") || key_just_pressed("l")) box.lidOpen = !box.lidOpen;
    if (boxPressed("m") || key_just_pressed("m")) box.synth.muted = !box.synth.muted;
    if (boxPressed("r") || key_just_pressed("r")) {
        box.playing = true;
        box.lidOpen = true;
        box.time = 0.0;
        box.assembly.angles.barrel = 0.0;
        box.assembly.angles.lid = box.lidTarget;
        box.synth = BoxSynth();
        boxSyncGears(box.assembly);
        box.orbit = 0.85;
    }
    if (keyboard.isDown("left") || keyboard.isDown("a")) box.orbit -= 0.7 * dt;
    if (keyboard.isDown("right") || keyboard.isDown("d")) box.orbit += 0.7 * dt;
    if (keyboard.isDown("up") || keyboard.isDown("w")) {
        box.orbitElev += 0.12 * dt;
        if (box.orbitElev > 0.40) box.orbitElev = 0.40;
    }
    if (keyboard.isDown("down") || keyboard.isDown("s")) {
        box.orbitElev -= 0.12 * dt;
        if (box.orbitElev < 0.06) box.orbitElev = 0.06;
    }

    boxApplyDemo(dt);
    boxPose(box.assembly);
    boxPumpAudio();
    if (box.playing) box.orbit += dt * 0.08;
    boxApplyOrbitCamera();

    if (box.hud) {
        ui.select("box-hud");
        local cyl = box.assembly.angles.cylinder;
        while (cyl < 0.0) cyl += 360.0;
        while (cyl >= 360.0) cyl -= 360.0;
        local phase = box.stream != null ? box.stream.getInfo().phase : "none";
        ui.setText("state", (box.playing ? "PLAYING" : "PAUSED") +
            " | lid=" + (box.lidOpen ? "OPEN" : "SHUT") +
            " | mute=" + (box.synth.muted ? "ON" : "OFF") +
            " | drum=" + format("%.1f", cyl) + " deg" +
            " | notes=" + box.synth.noteCount +
            " | pcm=" + phase);
    }
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    if (box.hud) ui.beginFrameAndRender();
};
