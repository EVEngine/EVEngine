// DAY / NIGHT CYCLE — demonstrates the EVEngine DayNight module: a solar orbit
// that drives the sun direction, a procedural skybox (IBL env) that rotates
// with the sun, and switchable night lighting systems (moonlight, starlight,
// fire, fireflies).
//
// Controls
//   Space   : pause / resume time
//   F       : toggle fireflies
//   L       : toggle moonlight
//   M       : toggle fire
//   Up/Down : speed up / slow down the clock
//   Left    : reset to solar noon

if (!("uiReady" in getroottable())) uiReady <- false;
if (!("camAngle" in getroottable())) camAngle <- 0.0;
if (!("autoOrbit" in getroottable())) autoOrbit <- true;
if (!("props" in getroottable())) props <- [];

// Firefly spawn ring around the campfire.
fireflies <- [];
for (local i = 0; i < 6; ++i) {
    local a = i * 0.5236;  // ~30° apart
    fireflies.push([math.polarX(3.0, a), 0.8 + (i % 2) * 0.5, math.polarY(3.0, a)]);
}

function buildProp(kind, x, z) {
    local root = eve.Renderable3D();
    if (kind == "tree") {
        root.setMesh(gfx.newMeshCylinder(10, 1, true));
        root.setPosition(x, 0.6, z);
        root.setScale(0.5, 1.2, 0.5);
        root.setTint(0.30, 0.22, 0.15, 1.0);
        root.setRoughness(0.9);
        local canopy = eve.Renderable3D();
        canopy.setMesh(gfx.newMeshSphere(20, 12));
        canopy.setPosition(x, 2.2, z);
        canopy.setScale(1.4, 1.0, 1.4);
        canopy.setTint(0.20, 0.45, 0.22, 1.0);
        canopy.setRoughness(0.85);
        props.push(canopy);
    } else if (kind == "pillar") {
        root.setMesh(gfx.newMeshCylinder(16, 1, true));
        root.setPosition(x, 1.0, z);
        root.setScale(0.6, 2.0, 0.6);
        root.setTint(0.62, 0.58, 0.54, 1.0);
        root.setRoughness(0.6);
        root.setMetallic(0.05);
    } else if (kind == "rock") {
        root.setMesh(gfx.newMeshSphere(12, 8));
        root.setPosition(x, 0.4, z);
        root.setScale(0.8, 0.8, 0.8);
        root.setTint(0.45, 0.42, 0.38, 1.0);
        root.setRoughness(0.95);
    }
    root.setReceiveShadow(true);
    props.push(root);
}

function buildScene() {
    local ground = eve.Renderable3D();
    ground.setMesh(gfx.newMeshCylinder(48, 1, true));
    ground.setPosition(0.0, 0.0, 0.0);
    ground.setScale(30.0, 0.06, 30.0);
    ground.setTint(0.28, 0.34, 0.25, 1.0);
    ground.setRoughness(0.95);
    ground.setReceiveShadow(true);
    props.push(ground);

    buildProp("tree", -7.0, -4.0);
    buildProp("tree", -6.0, 3.0);
    buildProp("tree", 8.0, -3.0);
    buildProp("tree", 5.0, 6.0);
    buildProp("pillar", 0.0, -8.0);
    buildProp("pillar", 3.0, 8.0);
    buildProp("pillar", -9.0, 6.0);
    buildProp("rock", -3.0, 5.0);
    buildProp("rock", 7.0, 5.0);
    buildProp("rock", -1.0, -9.0);
}

function buildPanel() {
    ui.setTheme("dark");
    ui.setNavKeyboard(true);
    ui.beginBuild();
    ui.beginWindow("DAY / NIGHT", "root");
    ui.text("Time of day", "h1");
    ui.text("", "clock");
    ui.text("", "sunElev");
    ui.text("Night lighting", "h1");
    ui.text("Moonlight (L)", "moon");
    ui.text("Fireflies (F)", "flies");
    ui.text("Fire (M)", "fire");
    ui.text("Starlight", "stars");
    ui.text("Space: pause • Up/Down: speed", "hint");
    ui.end();
    ui.mountBuildAs("panel");
    ui.select("panel");
    ui.setHostOverlay(true);
    ui.setHostPos(760.0, 24.0, 250.0, 260.0);
    uiReady = true;
}

eve_init = function() {
    camera = eve.Camera3D();
    camera.setEye(0.0, 7.0, 16.0);
    camera.setTarget(0.0, 1.0, 0.0);
    camera.setFov(55.0);

    buildScene();
    if (!uiReady) buildPanel();

    // Configure the cycle.
    daynight.init(gfx);
    daynight.setTimeOfDay(9.0);
    daynight.setSpeed(0.5);              // 1 full day every ~48 s
    daynight.setFirePosition(0.0, 0.6, 0.0);
    foreach (f in fireflies) {
        daynight.addFirefly(f[0], f[1], f[2]);
    }
    daynight.setNightLight("moonlight", true);
    daynight.setNightLight("starlight", true);
    daynight.setNightLight("fire", true);
    daynight.setNightLight("fireflies", true);
    daynight.setSkyboxEnabled(true);
};

eve_update = function(dt) {
    if (autoOrbit) {
        camAngle += dt * 0.18;
        local r = 17.0;
        camera.setEye(math.polarX(r, camAngle), 7.5, math.polarY(r, camAngle));
    }

    // Hotkeys.
    if (keyboard.wasPressed("space")) daynight.setPaused(!daynight.isPaused());
    if (keyboard.wasPressed("l")) daynight.setNightLight("moonlight", !daynight.isNightLight("moonlight"));
    if (keyboard.wasPressed("f")) daynight.setNightLight("fireflies", !daynight.isNightLight("fireflies"));
    if (keyboard.wasPressed("m")) daynight.setNightLight("fire", !daynight.isNightLight("fire"));
    if (keyboard.isDown("up")) daynight.setSpeed(daynight.getSpeed() + dt * 2.0);
    if (keyboard.isDown("down")) daynight.setSpeed(math.max(0.0, daynight.getSpeed() - dt * 2.0));
    if (keyboard.wasPressed("left")) daynight.setTimeOfDay(12.0);

    daynight.update(dt, gfx);

    // Feed the module's ambient / sky into the camera each frame.
    camera.setAmbient(daynight.getAmbientR(), daynight.getAmbientG(), daynight.getAmbientB());

    // Refresh the status panel.
    if (uiReady) {
        ui.select("panel");
        ui.setText("clock", "Clock: " + format("%.1f h", daynight.getTimeOfDay())
            + (daynight.isPaused() ? " (paused)" : ""));
        ui.setText("sunElev", "Sun elev: " + format("%.0f°", daynight.getSunElevation())
            + (daynight.isNight() ? "  NIGHT" : ""));
        ui.setText("moon", "Moonlight: " + (daynight.isNightLight("moonlight") ? "ON" : "off"));
        ui.setText("flies", "Fireflies: " + (daynight.isNightLight("fireflies") ? "ON" : "off"));
        ui.setText("fire", "Fire: " + (daynight.isNightLight("fire") ? "ON" : "off"));
        ui.setText("stars", "Starlight: " + (daynight.isNightLight("starlight") ? "ON" : "off"));
    }
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    ui.beginFrameAndRender();
};
