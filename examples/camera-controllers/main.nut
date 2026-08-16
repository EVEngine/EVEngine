// 3D 摄像机控制器示例：演示 eve.CameraController 的多种视角行为。
//   - follow      自动追踪"核心玩家"（第三人称跟随）
//   - orbit       自动盘旋
//   - topdown     俯视
//   - firstperson 第一人称
//   - cinematic   过场（一组命名视角之间平滑自动切换）
// 运行：make run/win32-debug GAME=examples/camera-controllers

if (!("players" in getroottable())) players <- [];
if (!("markers" in getroottable())) markers <- [];
if (!("ctrl" in getroottable())) ctrl <- null;
if (!("cam" in getroottable())) cam <- null;
if (!("mode" in getroottable())) mode <- "follow";
if (!("spin" in getroottable())) spin <- true;
if (!("uiReady" in getroottable())) uiReady <- false;
if (!("animT" in getroottable())) animT <- 0.0;
if (!("statusWas" in getroottable())) statusWas <- "";

local MODES = ["follow", "orbit", "topdown", "firstperson", "cinematic"];

function makeSphere(r, g, b, metallic, roughness) {
    local ent = eve.Renderable3D();
    ent.setMesh(gfx.newMeshSphere(32, 16));
    ent.setTint(r, g, b, 1.0);
    ent.setMetallic(metallic);
    ent.setRoughness(roughness);
    ent.setCastShadow(true);
    ent.setReceiveShadow(true);
    return ent;
}

function buildScene() {
    // 地面
    local ground = makeSphere(0.35, 0.4, 0.45, 0.05, 0.9);
    ground.setScale(1.0, 0.15, 1.0);
    ground.setPosition(0.0, -1.6, 0.0);
    markers.push(ground);

    // 场景标记柱（orbit / cinematic 的观察对象）
    local markerColors = [[0.9, 0.35, 0.3], [0.3, 0.7, 0.9], [0.9, 0.75, 0.3]];
    for (local i = 0; i < 3; i++) {
        local a = i * (6.2832 / 3.0);
        local m = makeSphere(markerColors[i][0], markerColors[i][1], markerColors[i][2], 0.1, 0.4);
        m.setScale(0.8, 2.4, 0.8);
        m.setPosition(cos(a) * 6.0, 0.6, sin(a) * 6.0);
        markers.push(m);
    }

    // "核心玩家"：一个会移动的球，供 follow / firstperson 追踪
    for (local i = 0; i < 5; i++) {
        local p = makeSphere(0.25 + i * 0.1, 0.9, 0.4, 0.3, 0.5);
        p.setScale(0.55, 0.55, 0.55);
        players.push(p);
    }
}

function setupCameraController() {
    cam = eve.Camera3D();
    cam.setEye(0.0, 3.0, 8.0);
    cam.setTarget(0.0, 1.0, 0.0);
    cam.setFov(50.0);
    cam.setAmbient(0.3, 0.32, 0.34);
    gfx.setDirectionalLight(-0.4, 0.9, 0.35, 1.9, 1.6, 1.3);

    ctrl = eve.CameraController();
    ctrl.setCamera(cam);
    ctrl.setMode("follow");
    ctrl.setTarget(0.0, 1.0, 0.0);
    ctrl.setOffset(0.0, 2.6, 6.0);
    ctrl.setSmooth(6.0);

    // cinematic 用的一组命名视角
    ctrl.addView("front",  0.0, 3.0, 12.0, 0.0, 1.0, 0.0);
    ctrl.addView("side",   14.0, 3.0, 0.0, 0.0, 1.0, 0.0);
    ctrl.addView("high",   8.0, 9.0, 8.0, 0.0, 1.0, 0.0);
    ctrl.addView("close",  0.0, 2.0, 4.0, 0.0, 1.0, 0.0);

    ctrl.snap();
}

function buildPanel() {
    ui.setTheme("dark");
    ui.setNavKeyboard(true);
    ui.beginBuild();
    ui.beginWindow("CAMERA CONTROLLERS", "root");
    ui.text("Choose a camera behavior", "eyebrow");
    ui.text(mode, "mode");
    ui.button("follow (1)", "m_follow");
    ui.button("orbit (2)", "m_orbit");
    ui.button("topdown (3)", "m_topdown");
    ui.button("firstperson (4)", "m_firstperson");
    ui.button("cinematic (5)", "m_cinematic");
    ui.text("Spin enabled (space)", "spinlab");
    ui.text("Ready.", "status");
    ui.end();
    ui.mountBuildAs("cam");
    ui.select("cam");
    ui.setHostOverlay(true);
    ui.setHostPos(900.0, 30.0, 280.0, 380.0);
    uiReady = true;
}

function setMode(m) {
    mode = m;
    ctrl.setMode(m);
    if (m == "cinematic") {
        ctrl.playSequence(3.0);
    } else {
        ctrl.stopSequence();
        ctrl.setTarget(0.0, 1.0, 0.0);
        if (m == "orbit") { ctrl.setRadius(10.0); ctrl.setElevation(30.0); ctrl.setOrbitSpeed(20.0); }
        else if (m == "topdown") { ctrl.setRadius(12.0); }
    }
    if (uiReady) {
        ui.select("cam");
        ui.setText("mode", m);
        ui.setText("status", "Mode: " + m);
    }
}

eve_init = function() {
    gfx.setBackgroundColor(0.05, 0.06, 0.08, 1.0);
    buildScene();
    setupCameraController();
    if (!uiReady) buildPanel();
};

eve_update = function(dt) {
    animT += dt;

    // 让"核心玩家"沿圆环移动，便于观察自动追踪 / 盘旋
    local px = cos(animT * 0.6) * 3.0;
    local pz = sin(animT * 0.6) * 3.0;
    local py = 0.7 + sin(animT * 1.3) * 0.6;
    for (local i = 0; i < players.len(); i++) {
        local lead = i * 0.5;
        players[i].setPosition(cos(animT * 0.6 + lead) * 3.0,
                               py,
                               sin(animT * 0.6 + lead) * 3.0);
    }

    // follow / firstperson 追踪移动的"核心玩家"
    if (mode == "follow" || mode == "firstperson") {
        ctrl.setTarget(px, py, pz);
    }

    // 空格切换自动盘旋（orbit）
    local space = keyboard.isDown("Space");
    local spinPressed = space && !("spinWas" in getroottable() ? spinWas : false);
    spinWas <- space;
    if (spinPressed) {
        spin = !spin;
        ctrl.setOrbitSpeed(spin ? 20.0 : 0.0);
    }

    // firstperson：方向键调整朝向
    if (mode == "firstperson") {
        if (!("fpYaw" in getroottable())) fpYaw <- 0.0;
        if (!("fpPitch" in getroottable())) fpPitch <- 0.0;
        if (keyboard.isDown("left"))  fpYaw = fpYaw - 90.0 * dt;
        if (keyboard.isDown("right")) fpYaw = fpYaw + 90.0 * dt;
        if (keyboard.isDown("up"))    fpPitch = fpPitch + 90.0 * dt;
        if (keyboard.isDown("down"))  fpPitch = fpPitch - 90.0 * dt;
        ctrl.setYaw(fpYaw);
        ctrl.setPitch(fpPitch);
    }

    ctrl.update(dt);

    // UI 交互
    local clicked = ui.consumeClick();
    while (clicked != "") {
        ui.select("cam");
        if (clicked == "cam/m_follow") setMode("follow");
        else if (clicked == "cam/m_orbit") setMode("orbit");
        else if (clicked == "cam/m_topdown") setMode("topdown");
        else if (clicked == "cam/m_firstperson") setMode("firstperson");
        else if (clicked == "cam/m_cinematic") setMode("cinematic");
        clicked = ui.consumeClick();
    }

    // 数字键 1-5 直接切换模式
    local keyModes = [["1", "follow"], ["2", "orbit"], ["3", "topdown"], ["4", "firstperson"], ["5", "cinematic"]];
    foreach (pair in keyModes) {
        if (keyboard.isDown(pair[0])) { setMode(pair[1]); }
    }
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    ui.beginFrameAndRender();
};
