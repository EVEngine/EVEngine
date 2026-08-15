// WEATHER LAB — a small 3D diorama that demonstrates the EVEngine weather
// module: rain, snow, lightning, wind and the storm mood (sky / fog / sun).
// Use the left panel to pick a preset, or press 1..6 to switch quickly.

if (!("uiReady" in getroottable())) uiReady <- false;
if (!("preset" in getroottable())) preset <- "clear";
if (!("camAngle" in getroottable())) camAngle <- 0.0;
if (!("autoOrbit" in getroottable())) autoOrbit <- true;
if (!("props" in getroottable())) props <- [];
if (!("strikeWasDown" in getroottable())) strikeWasDown <- false;

presets <- {
    clear   = { intensity = 0.0, wind = 1.5, dir = 0,  sky = [0.45, 0.53, 0.62], sun = 1.0,  fog = [0.55, 0.58, 0.62], fogD = 0.0015, lightning = false }
    drizzle = { intensity = 0.25, wind = 4.0, dir = 20, sky = [0.40, 0.46, 0.55], sun = 0.8, fog = [0.52, 0.54, 0.56], fogD = 0.006,  lightning = false }
    rain    = { intensity = 0.65, wind = 7.0, dir = 35, sky = [0.34, 0.39, 0.47], sun = 0.5, fog = [0.45, 0.48, 0.51], fogD = 0.012,  lightning = false }
    storm   = { intensity = 0.9,  wind = 13.0, dir = 55, sky = [0.22, 0.26, 0.32], sun = 0.25, fog = [0.30, 0.32, 0.36], fogD = 0.02,   lightning = true }
    snow    = { intensity = 0.75, wind = 5.0,  dir = 10, sky = [0.62, 0.68, 0.74], sun = 0.7, fog = [0.72, 0.76, 0.80], fogD = 0.01,   lightning = false }
    fog     = { intensity = 0.0,  wind = 1.0,  dir = 0,  sky = [0.45, 0.47, 0.50], sun = 0.4, fog = [0.60, 0.62, 0.65], fogD = 0.05,   lightning = false }
};

camera <- null;

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

        local cap = eve.Renderable3D();
        cap.setMesh(gfx.newMeshSphere(16, 8));
        cap.setPosition(x, 2.1, z);
        cap.setScale(0.8, 0.25, 0.8);
        cap.setTint(0.55, 0.52, 0.48, 1.0);
        props.push(cap);
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

function applyPreset() {
    local p = presets[preset];
    weather.setPreset(preset);
    weather.setIntensity(p.intensity);
    weather.setWindSpeed(p.wind);
    weather.setWindDirection(p.dir);
    weather.setLightningEnabled(p.lightning);
    weather.setSkyColor(p.sky[0], p.sky[1], p.sky[2]);
    weather.setSunIntensity(p.sun);
    weather.setFogColor(p.fog[0], p.fog[1], p.fog[2]);
    weather.setFogDensity(p.fogD);
    if (uiReady) {
        ui.select("panel");
        ui.setText("status", "Preset: " + preset);
    }
}

function buildPanel() {
    ui.setTheme("dark");
    ui.setNavKeyboard(true);
    ui.beginBuild();
    ui.beginWindow("WEATHER LAB", "root");
    ui.text("Presets", "h1");
    ui.button("1  Clear", "pClear");
    ui.button("2  Drizzle", "pDrizzle");
    ui.button("3  Rain", "pRain");
    ui.button("4  Storm", "pStorm");
    ui.button("5  Snow", "pSnow");
    ui.button("6  Fog", "pFog");
    ui.text("Control", "h1");
    ui.slider("Intensity", presets[preset].intensity, 0.0, 1.0, "intensity");
    ui.slider("Wind speed", presets[preset].wind, 0.0, 20.0, "wind");
    ui.slider("Wind dir (deg)", presets[preset].dir, 0.0, 360.0, "winddir");
    ui.button("Strike lightning (S)", "strike");
    ui.text(preset, "status");
    ui.text("Camera: space to orbit • S = bolt", "hint");
    ui.end();
    ui.mountBuildAs("panel");
    ui.select("panel");
    ui.setHostOverlay(true);
    ui.setHostPos(760.0, 24.0, 250.0, 600.0);
    uiReady = true;
}

eve_init = function() {
    gfx.setBackgroundColor(0.35, 0.42, 0.5, 1.0);
    camera = eve.Camera3D();
    camera.setEye(0.0, 7.0, 16.0);
    camera.setTarget(0.0, 1.0, 0.0);
    camera.setFov(55.0);
    camera.setAmbient(0.35, 0.4, 0.45);
    gfx.setDirectionalLight(-0.4, 0.75, 0.5, 1.0, 0.95, 0.88);

    buildScene();
    if (!uiReady) buildPanel();
    applyPreset();
};

eve_update = function(dt) {
    if (autoOrbit) {
        camAngle += dt * 0.22;
        local r = 17.0;
        camera.setEye(math.polarX(r, camAngle), 7.5, math.polarY(r, camAngle));
    }

    // Preset hotkeys 1..6.
    local keys = ["1", "2", "3", "4", "5", "6"];
    foreach (k in keys) {
        if (keyboard.isDown(k)) {
            local names = ["clear", "drizzle", "rain", "storm", "snow", "fog"];
            local idx = k.tointeger() - 1;
            if (idx >= 0 && idx < names.len() && preset != names[idx]) {
                preset = names[idx];
                applyPreset();
            }
        }
    }
    if (keyboard.isDown("s") || keyboard.isDown("S")) {
        if (!strikeWasDown) weather.strike();
        strikeWasDown = true;
    } else {
        strikeWasDown = false;
    }

    local clicked = ui.consumeClick();
    local needApply = false;
    while (clicked != "") {
        if (clicked == "panel/pClear") { preset = "clear"; needApply = true; }
        else if (clicked == "panel/pDrizzle") { preset = "drizzle"; needApply = true; }
        else if (clicked == "panel/pRain") { preset = "rain"; needApply = true; }
        else if (clicked == "panel/pStorm") { preset = "storm"; needApply = true; }
        else if (clicked == "panel/pSnow") { preset = "snow"; needApply = true; }
        else if (clicked == "panel/pFog") { preset = "fog"; needApply = true; }
        else if (clicked == "panel/strike") weather.strike();
        clicked = ui.consumeClick();
    }
    if (needApply) applyPreset();

    local changed = ui.consumeChange();
    while (changed != "") {
        ui.select("panel");
        if (changed == "panel/intensity") weather.setIntensity(ui.getValue("intensity"));
        else if (changed == "panel/wind") weather.setWindSpeed(ui.getValue("wind"));
        else if (changed == "panel/winddir") weather.setWindDirection(ui.getValue("winddir"));
        changed = ui.consumeChange();
    }

    weather.update(dt, gfx);
    camera.setAmbient(
        weather.getAmbientBrightness(),
        weather.getAmbientBrightness(),
        weather.getAmbientBrightness());
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    ui.beginFrameAndRender();
};
