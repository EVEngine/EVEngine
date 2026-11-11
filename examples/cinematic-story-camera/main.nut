// Story overview camera: follow a moving hero, then Space pushes a plaza-wide
// establishing shot with dolly / crane / orbit-reveal film moves before restore.

persist cam = null
persist ctrl = null
persist director = null
persist hero = null
persist props = null
persist uiReady = false
persist ready = false
persist passPrinted = false
persist animT = 0.0
persist phase = "gameplay"   // gameplay | blend_in | dolly | crane | orbit | blend_out
persist phaseT = 0.0
persist spaceWas = false
persist escWas = false
persist status = "Space: story overview"
persist autoArmed = true

function makeMesh(r, g, b, metallic, roughness) {
    local ent = eve.Renderable3D();
    ent.setMesh(gfx.newMeshSphere(28, 16));
    ent.setTint(r, g, b, 1.0);
    ent.setMetallic(metallic);
    ent.setRoughness(roughness);
    ent.setCastShadow(true);
    ent.setReceiveShadow(true);
    return ent;
}

function setStatus(text) {
    status = text;
    if (uiReady) {
        ui.select("cinema");
        ui.setText("status", text);
        ui.setText("phase", "phase=" + phase + "  stack=" + director.depth());
    }
}

function requireOk(result, label) {
    if (!result.ok) {
        print("FAIL " + label + "\n");
        return false;
    }
    return true;
}

function startStoryBeat() {
    if (director.isActive()) return;
    if (!requireOk(director.pushOverview("plaza", 0.85), "pushOverview")) return;
    phase = "blend_in";
    phaseT = 0.0;
    setStatus("Overview: plaza establishing");
}

function forcePop() {
    if (!director.isActive()) return;
    eve.result.ignore(director.pop(0.55), "force-pop");
    phase = "gameplay";
    phaseT = 0.0;
    autoArmed = false;
    setStatus("Restored gameplay follow");
}

function buildScene() {
    props = [];
    local ground = makeMesh(0.28, 0.32, 0.30, 0.05, 0.92);
    ground.setScale(14.0, 0.12, 14.0);
    ground.setPosition(0.0, -1.2, 0.0);
    props.push(ground);

    local colors = [[0.85, 0.35, 0.28], [0.30, 0.55, 0.85], [0.90, 0.72, 0.28], [0.45, 0.75, 0.40]];
    for (local i = 0; i < 4; i++) {
        local a = i * 1.5708 + 0.4;
        local tower = makeMesh(colors[i][0], colors[i][1], colors[i][2], 0.15, 0.45);
        tower.setScale(1.1, 3.2 + (i % 2) * 1.4, 1.1);
        tower.setPosition(cos(a) * 7.5, 0.8, sin(a) * 7.5);
        props.push(tower);
    }

    hero = makeMesh(0.95, 0.55, 0.25, 0.35, 0.35);
    hero.setScale(0.7, 0.7, 0.7);
}

function setupCamera() {
    cam = eve.Camera3D();
    cam.setEye(0.0, 3.2, 9.0);
    cam.setTarget(0.0, 1.0, 0.0);
    cam.setFov(52.0);
    cam.setAmbient(0.28, 0.30, 0.34);
    gfx.setDirectionalLight(-0.35, 0.92, 0.28, 1.8, 1.55, 1.25);

    ctrl = eve.CameraController();
    ctrl.setCamera(cam);
    ctrl.setMode("follow");
    ctrl.setTarget(0.0, 1.0, 0.0);
    ctrl.setOffset(0.0, 2.4, 6.5);
    ctrl.setSmooth(7.0);
    ctrl.setFov(52.0);
    ctrl.addRig("gameplay", "follow", 10);
    ctrl.snap();

    director = eve.CinemaDirector();
    if (!requireOk(director.bind(ctrl), "bind")) return false;
    if (!requireOk(director.defineOverview("plaza", 16.0, 12.5, 16.0, 0.0, 1.2, 0.0, 42.0), "define plaza"))
        return false;
    if (!requireOk(director.defineOverview("tight", 4.5, 3.0, 7.0, 0.0, 1.0, 0.0, 38.0), "define tight"))
        return false;
    return true;
}

function buildPanel() {
    ui.setTheme("dark");
    ui.setNavKeyboard(true);
    ui.beginBuild();
    ui.beginWindow("STORY CAMERA", "root");
    ui.text("CinemaDirector overview beat", "eyebrow");
    ui.text(status, "status");
    ui.text("phase=gameplay  stack=0", "phase");
    ui.button("Play beat (Space)", "play");
    ui.button("Pop now (Esc)", "pop");
    ui.end();
    ui.mountBuildAs("cinema");
    ui.select("cinema");
    ui.setHostOverlay(true);
    ui.setHostPos(24.0, 24.0, 320.0, 220.0);
    uiReady = true;
}

eve_init <- function() {
    gfx.setBackgroundColor(0.04, 0.05, 0.07, 1.0);
    buildScene();
    ready = setupCamera();
    if (ready) {
        buildPanel();
        print("cinematic-story-camera ready\n");
    }
};

eve_update <- function(dt) {
    if (!ready) return;
    animT += dt;
    phaseT += dt;

    local hx = cos(animT * 0.55) * 3.2;
    local hz = sin(animT * 0.55) * 3.2;
    local hy = 0.55 + sin(animT * 1.4) * 0.25;
    hero.setPosition(hx, hy, hz);

    if (!director.isActive()) {
        ctrl.setTarget(hx, hy, hz);
    }

    // Headless / smoke: auto-start one beat shortly after boot.
    if (autoArmed && phase == "gameplay" && animT >= 0.35) {
        autoArmed = false;
        startStoryBeat();
    }

    local space = keyboard.isDown("Space");
    if (space && !spaceWas && phase == "gameplay") startStoryBeat();
    spaceWas = space;

    local esc = keyboard.isDown("Escape");
    if (esc && !escWas) forcePop();
    escWas = esc;

    local clicked = ui.consumeClick();
    while (clicked != "") {
        if (clicked == "cinema/play" && phase == "gameplay") startStoryBeat();
        else if (clicked == "cinema/pop") forcePop();
        clicked = ui.consumeClick();
    }

    if (phase == "blend_in" && phaseT >= 0.9 && !director.isMovePlaying()) {
        if (requireOk(director.dolly(5.0, 1.5), "dolly")) {
            phase = "dolly";
            phaseT = 0.0;
            setStatus("Film move: dolly in");
        }
    } else if (phase == "dolly" && !director.isMovePlaying() && phaseT >= 1.55) {
        if (requireOk(director.crane(3.5, 1.3), "crane")) {
            phase = "crane";
            phaseT = 0.0;
            setStatus("Film move: crane up");
        }
    } else if (phase == "crane" && !director.isMovePlaying() && phaseT >= 1.35) {
        if (requireOk(director.orbitReveal(75.0, 2.0), "orbitReveal")) {
            phase = "orbit";
            phaseT = 0.0;
            setStatus("Film move: orbit reveal");
        }
    } else if (phase == "orbit" && !director.isMovePlaying() && phaseT >= 2.05) {
        if (requireOk(director.pop(0.8), "pop")) {
            phase = "blend_out";
            phaseT = 0.0;
            setStatus("Restoring gameplay");
        }
    } else if (phase == "blend_out" && !director.isActive() && phaseT >= 0.85) {
        phase = "gameplay";
        phaseT = 0.0;
        setStatus("Space: story overview");
        if (!passPrinted) {
            print("PASS cinematic-story-camera cycle\n");
            passPrinted = true;
        }
    }

    eve.result.ignore(director.update(dt), "director.update");
    ctrl.update(dt);
};

eve_render <- function() {
    gfx.clear();
    gfx.render3D();
    ui.beginFrameAndRender();
};
