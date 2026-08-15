// WEATHER LAB 鈥?a small 3D diorama that demonstrates the EVEngine weather
// module: rain, snow, lightning, wind and the storm mood (sky / fog / sun).
//
// Controls:
//   鈥?Panel buttons or keys 1..6  鈥?pick a preset
//   鈥?Space  /  "Cycle" button     鈥?auto-loop through every weather
//   鈥?S      /  "Strike" button    鈥?force a lightning bolt
//   鈥?Sliders tune intensity + wind live
//   鈥?Camera auto-orbits the diorama

if (!("uiReady" in getroottable())) uiReady <- false;
if (!("preset" in getroottable())) preset <- "clear";
if (!("camAngle" in getroottable())) camAngle <- 0.0;
if (!("autoOrbit" in getroottable())) autoOrbit <- true;
if (!("autoCycle" in getroottable())) autoCycle <- false;
if (!("cycleTimer" in getroottable())) cycleTimer <- 0.0;
if (!("props" in getroottable())) props <- [];
if (!("strikeWasDown" in getroottable())) strikeWasDown <- false;


presetOrder <- ["clear", "drizzle", "rain", "storm", "snow", "fog"];

presets <- {
    clear   = { intensity = 0.0,  wind = 1.5, dir = 0,  sky = [0.45, 0.53, 0.62], sun = 1.0,  fog = [0.55, 0.58, 0.62], fogD = 0.0015, lightning = false }
    drizzle = { intensity = 0.25, wind = 4.0, dir = 20, sky = [0.40, 0.46, 0.55], sun = 0.8, fog = [0.52, 0.54, 0.56], fogD = 0.006,  lightning = false }
    rain    = { intensity = 0.65, wind = 7.0, dir = 35, sky = [0.34, 0.39, 0.47], sun = 0.5, fog = [0.45, 0.48, 0.51], fogD = 0.012,  lightning = false }
    storm   = { intensity = 0.9,  wind = 13.0, dir = 55, sky = [0.22, 0.26, 0.32], sun = 0.25, fog = [0.30, 0.32, 0.36], fogD = 0.02,   lightning = true }
    snow    = { intensity = 0.75, wind = 5.0,  dir = 10, sky = [0.62, 0.68, 0.74], sun = 0.7, fog = [0.72, 0.76, 0.80], fogD = 0.01,   lightning = false }
    fog     = { intensity = 0.0,  wind = 1.0,  dir = 0,  sky = [0.45, 0.47, 0.50], sun = 0.4, fog = [0.60, 0.62, 0.65], fogD = 0.05,   lightning = false }
};

camera <- null;
lastFrameWasDown <- false;

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

function syncSliders() {
    local p = presets[preset];
    if (!uiReady) return;
    ui.select("panel");
    ui.setValue("intensity", p.intensity);
    ui.setValue("wind", p.wind);
    ui.setValue("winddir", p.dir);
}

function updateHud() {
    if (!uiReady) return;
    local p = presets[preset];
    ui.select("hud");
    ui.setText("hudPreset", "Preset:  " + preset);
    ui.setText("hudIntensity", "Intensity:  " + (p.intensity * 100).tointeger() + "%");
    ui.setText("hudWind", "Wind:  " + p.wind + " m/s @ " + p.dir + "\u00B0");
    ui.setText("hudFx", (p.lightning ? "Lightning:  ON" : "Lightning:  off") +
                        "   鈥?  Cycle: " + (autoCycle ? "ON" : "off"));
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
    syncSliders();
    updateHud();
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
    ui.button("Cycle all (Space)", "cycle");
    ui.text("Camera: space to orbit 鈥?S = bolt", "hint");
    ui.end();
    ui.mountBuildAs("panel");
    ui.select("panel");
    ui.setHostOverlay(true);
    ui.setHostPos(760.0, 24.0, 250.0, 620.0);

    ui.beginBuild();
    ui.beginWindow("Weather", "hudroot");
    ui.text("Preset", "hudPreset");
    ui.text("Intensity", "hudIntensity");
    ui.text("Wind", "hudWind");
    ui.text("FX", "hudFx");
    ui.end();
    ui.mountBuildAs("hud");
    ui.select("hud");
    ui.setHostOverlay(true);
    ui.setHostPos(20.0, 20.0, 300.0, 120.0);

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
            local idx = k.tointeger() - 1;
            if (idx >= 0 && idx < presetOrder.len() && preset != presetOrder[idx]) {
                preset = presetOrder[idx];
                applyPreset();
            }
        }
    }

    // Space toggles the auto-cycle; S strikes a bolt (edge-triggered).
    local space = keyboard.isDown("Space");
    if (space && !lastFrameWasDown) autoCycle = !autoCycle;
    lastFrameWasDown = space;

    if (keyboard.isDown("s") || keyboard.isDown("S")) {
        if (!strikeWasDown) weather.strike();
        strikeWasDown = true;
    } else {
        strikeWasDown = false;
    }

    // Auto-cycle through every weather preset.
    if (autoCycle) {
        cycleTimer -= dt;
        if (cycleTimer <= 0.0) {
            local idx = 0;
            foreach (i, name in presetOrder) {
                if (name == preset) { idx = i; break; }
            }
            preset = presetOrder[(idx + 1) % presetOrder.len()];
            applyPreset();
            cycleTimer = 6.0;
        }
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
        else if (clicked == "panel/cycle") autoCycle = !autoCycle;
        clicked = ui.consumeClick();
    }
    if (needApply) applyPreset();

    local changed = ui.consumeChange();
    while (changed != "") {
        ui.select("panel");
        if (changed == "panel/intensity") {
            weather.setIntensity(ui.getValue("intensity"));
            presets[preset].intensity = ui.getValue("intensity");
        } else if (changed == "panel/wind") {
            weather.setWindSpeed(ui.getValue("wind"));
            presets[preset].wind = ui.getValue("wind");
        } else if (changed == "panel/winddir") {
            weather.setWindDirection(ui.getValue("winddir"));
            presets[preset].dir = ui.getValue("winddir");
        }
        changed = ui.consumeChange();
    }
    updateHud();

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
