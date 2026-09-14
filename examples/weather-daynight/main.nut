// LIVING SKY — unified DayNight + Weather + volumetric clouds / height fog.
// 1..5 weather, Space pause, Left/Right scrub time, S lightning, C camera.

persist skyUiReady = false
persist skyProps = []
persist skyCamera = null
persist skyClouds = null
persist skyFog = null
persist skyWeather = "clear"
persist skyCamAngle = 1.35
persist skyAutoCamera = true
persist skyStrikeDown = false
persist skyTerrain = null
persist skyWaterfall = null

skyPresets <- {
    clear =   { rain=0.0, wind=1.0, dir=15.0, cloud=0.18, cover=0.22, density=0.48,
                fog=0.006, fogEnd=72.0, turbidity=2.2, mie=0.8, lightning=false }
    cloudy =  { rain=0.0, wind=4.0, dir=25.0, cloud=0.55, cover=0.58, density=0.72,
                fog=0.012, fogEnd=60.0, turbidity=4.0, mie=1.4, lightning=false }
    rain =    { rain=0.65, wind=7.0, dir=35.0, cloud=0.78, cover=0.76, density=0.94,
                fog=0.025, fogEnd=48.0, turbidity=6.0, mie=2.0, lightning=false }
    storm =   { rain=0.92, wind=14.0, dir=55.0, cloud=0.96, cover=0.90, density=1.25,
                fog=0.045, fogEnd=38.0, turbidity=8.0, mie=2.8, lightning=true }
    snow =    { rain=0.72, wind=4.0, dir=10.0, cloud=0.68, cover=0.66, density=0.78,
                fog=0.022, fogEnd=46.0, turbidity=4.8, mie=1.7, lightning=false }
};
skyOrder <- ["clear", "cloudy", "rain", "storm", "snow"];

function addObject(mesh, x, y, z, sx, sy, sz, r, g, b, roughness) {
    local o = eve.Renderable3D();
    o.setMesh(mesh);
    o.setPosition(x, y, z);
    o.setScale(sx, sy, sz);
    o.setTint(r, g, b, 1.0);
    o.setRoughness(roughness);
    o.setReceiveShadow(true);
    skyProps.push(o);
    return o;
}

function addModel(path, x, y, z, scale, yaw) {
    local md = model3d.newModelDataFromFile(path);
    for (local i = 0; i < md.getMeshCount(); ++i) {
        local o = model3d.createRenderable(gfx, md, i);
        o.setPosition(x, y, z);
        o.setScale(scale, scale, scale);
        o.setYaw(yaw);
        o.setTint(0.58, 0.72, 0.50, 1.0);
        o.setCastShadow(true);
        o.setReceiveShadow(true);
        skyProps.push(o);
    }
}

function buildLivingScene() {
    const TW = 72;
    const TH = 72;
    const CELL = 0.5;
    local heightmapResult = procgen.newHeightmap(TW, TH);
    if (!heightmapResult.ok) throw heightmapResult.status.summary;
    local hm = heightmapResult.value;
    for (local z = 0; z < TH; ++z) {
        for (local x = 0; x < TW; ++x) {
            local wx = (x - 36) * CELL;
            local wz = (z - 36) * CELL;
            local side = abs(wx) / 18.0;
            local backRaw = (-wz - 4.0) / 14.0;
            local channelRaw = 1.0 - abs(wx) / 3.0;
            local back = backRaw > 0.0 ? backRaw : 0.0;
            local channel = channelRaw > 0.0 ? channelRaw : 0.0;
            local valleySide = abs(wx) / 18.0;
            local cliffStart = 5.5 + valleySide * valleySide * 5.8;
            local cliffT = clampf((-wz - cliffStart) / (2.4 + valleySide * 3.8), 0.0, 1.0);
            cliffT = cliffT * cliffT * (3.0 - 2.0 * cliffT);
            local cliffRise = cliffT * 0.46;
            local basin = clampf(1.0 - ((wx * wx) / 30.0 + ((wz - 4.5) * (wz - 4.5)) / 55.0),
                                 0.0, 1.0) * 0.12;
            local ripple = 0.035 * math.polarY(1.0, x * 0.61 + z * 0.37) +
                           0.02 * math.polarX(1.0, x * 0.19 - z * 0.43);
            local h = 0.12 + side * side * 0.28 + back * 0.22 + cliffRise -
                      channel * 0.16 - basin + ripple;
            hm.setHeight(x, z, clampf(h, 0.03, 0.92));
        }
    }
    local terrainMesh = eve.HeightmapTargetModule().newMesh(hm, CELL, 7.0);
    skyTerrain = addObject(terrainMesh, -18.0, -0.85, -18.0,
        1.0, 1.0, 1.0, 0.17, 0.31, 0.16, 0.98);
    skyTerrain.setCastShadow(true);

    local treeSites = [
        [-12.0, 0.35, -4.2, 1.55, 8.0], [11.7, 0.35, -3.8, 1.45, -18.0],
        [-8.7, 4.15, -10.1, 1.80, 24.0], [8.4, 4.05, -10.0, 1.75, -31.0],
        [-14.5, 0.20, 5.4, 1.25, 46.0], [14.3, 0.18, 6.0, 1.30, -44.0]
    ];
    for (local i = 0; i < treeSites.len(); ++i) {
        local t = treeSites[i];
        local path = i < 4 ? "assets/tree_pineTallA_detailed.obj" :
            "assets/tree_pineSmallB.obj";
        addModel(path, t[0], t[1], t[2], t[3], t[4]);
    }
    local rimPines = [[-5.8,4.25,-11.0,1.25],[5.5,4.15,-10.8,1.15],
                      [-12.2,2.0,-8.5,1.15],[12.0,1.9,-8.0,1.10],
                      [-10.5,0.25,1.0,0.90],[10.2,0.20,1.5,0.85]];
    for (local i = 0; i < rimPines.len(); ++i) {
        local t = rimPines[i];
        addModel("assets/tree_pineTallA_detailed.obj", t[0], t[1], t[2], t[3], i * 41.0);
    }

    skyWaterfall = gfx.newWaterfall();
    skyWaterfall.createCurvedSheet(2.65, 6.8, 28, 48, 0.75, 0.85);
    skyWaterfall.setFlowSpeed(1.35);
    skyWaterfall.setTurbulence(0.72);
    skyWaterfall.setStreakCount(7);
    skyWaterfall.setStreakScale(7.5);
    skyWaterfall.setTopFoam(0.10);
    skyWaterfall.setBottomFoam(0.22);
    skyWaterfall.setFoamAmount(0.95);
    skyWaterfall.setWaterColor(0.055, 0.28, 0.36);
    skyWaterfall.setReflectionIntensity(0.68);
    skyWaterfall.setSunIntensity(1.05);
    local fallEnt = eve.Renderable3D();
    fallEnt.setMesh(skyWaterfall.getMesh());
    fallEnt.setShader(skyWaterfall.getShader());
    fallEnt.setPosition(0.0, 1.45, -7.08);
    skyProps.push(fallEnt);
}

function buildSkyPanel() {
    ui.setTheme("dark");
    ui.beginBuild();
    ui.beginWindow("LIVING SKY", "root");
    ui.text("Unified atmosphere", "title");
    ui.text("", "clock");
    ui.text("", "phase");
    ui.text("Weather", "h1");
    ui.button("1  Clear", "clear");
    ui.button("2  Cloudy", "cloudy");
    ui.button("3  Rain", "rain");
    ui.button("4  Storm", "storm");
    ui.button("5  Snow", "snow");
    ui.button("Strike lightning (S)", "strike");
    ui.text("Space pause  •  ←/→ time  •  C camera", "hint");
    ui.end();
    ui.mountBuildAs("skyPanel");
    ui.select("skyPanel");
    ui.setHostOverlay(true);
    ui.setHostPos(24.0, 24.0, 265.0, 390.0);
    skyUiReady = true;
}

function applySkyPreset(name) {
    skyWeather = name;
    local p = skyPresets[name];
    weather.setPreset(name == "cloudy" ? "clear" : name);
    weather.setIntensity(p.rain);
    weather.setWindSpeed(p.wind);
    weather.setWindDirection(p.dir);
    weather.setLightningEnabled(p.lightning);
    skyClouds.setCloudCoverage(p.cover);
    skyClouds.setCloudDensity(p.density);
    local windAngle = p.dir * 0.0174533;
    skyClouds.setCloudWind(math.polarX(p.wind * 0.06, windAngle),
                           math.polarY(p.wind * 0.06, windAngle));
    skyFog.setDensity(p.fog);
    skyFog.setFogEnd(p.fogEnd);
    daynight.setTurbidity(p.turbidity);
    daynight.setMieStrength(p.mie);
}

eve_init = function() {
    skyCamera = eve.Camera3D();
    skyCamera.setEye(0.0, 6.2, 18.0);
    skyCamera.setTarget(0.0, 1.8, -3.5);
    skyCamera.setFov(52.0);
    buildLivingScene();
    if (!skyUiReady) buildSkyPanel();

    daynight.init(gfx);
    daynight.setTimeOfDay(14.8);
    daynight.setSpeed(0.05);
    daynight.setSkyExposure(1.18);
    daynight.setSkyboxEnabled(true);
    daynight.setNightLight("moonlight", true);
    daynight.setNightLight("starlight", true);

    weather.init(gfx);
    weather.setEnvironmentEnabled(false);

    local rc = gfx.getRenderControl();
    rc.enable("gbuffer");
    rc.compile();

    skyClouds = gfx.newVolumetric();
    skyClouds.setMode("cloud");
    skyClouds.setQuality("high");
    skyClouds.setCloudLayer(8.0, 18.0);
    skyClouds.setCloudScale(26.0);

    skyFog = gfx.newVolumetric();
    skyFog.setMode("fog");
    skyFog.setQuality("medium");
    skyFog.setFogHeight(1.2);
    skyFog.setFogHeightFalloff(0.18);
    skyFog.setFogStart(4.0);
    skyFog.setFogNoise(0.42);

    applySkyPreset(skyWeather);
};

eve_update = function(dt) {
    if (skyWaterfall != null) skyWaterfall.update(dt);
    if (skyAutoCamera) {
        skyCamAngle += dt * 0.018;
        skyCamera.setEye(math.polarX(18.0, skyCamAngle), 6.2,
                         math.polarY(18.0, skyCamAngle));
        skyCamera.setTarget(0.0, 1.8, -3.5);
    }

    if (key_just_pressed("space")) daynight.setPaused(!daynight.isPaused());
    if (keyboard.isDown("left")) daynight.setTimeOfDay(daynight.getTimeOfDay() - dt * 2.2);
    if (keyboard.isDown("right")) daynight.setTimeOfDay(daynight.getTimeOfDay() + dt * 2.2);
    if (key_just_pressed("c")) skyAutoCamera = !skyAutoCamera;
    for (local i = 0; i < skyOrder.len(); ++i)
        if (key_just_pressed((i + 1).tostring())) applySkyPreset(skyOrder[i]);

    local strike = keyboard.isDown("s");
    if (strike && !skyStrikeDown) weather.strike();
    skyStrikeDown = strike;

    local clicked = ui.consumeClick();
    while (clicked != "") {
        if (clicked == "skyPanel/strike") weather.strike();
        else foreach (name in skyOrder)
            if (clicked == "skyPanel/" + name) applySkyPreset(name);
        clicked = ui.consumeClick();
    }

    weather.update(dt, gfx);
    local p = skyPresets[skyWeather];
    daynight.setWeatherInfluence(p.cloud, weather.getFlash());
    daynight.update(dt, gfx);

    local ex = math.polarX(18.0, skyCamAngle);
    local ez = math.polarY(18.0, skyCamAngle);
    skyClouds.setCamera(ex, 6.2, ez, 0.0, 1.8, -3.5, 0.0, 1.0, 0.0,
                        52.0, 1.7778, 0.1, 120.0);
    skyFog.setCamera(ex, 6.2, ez, 0.0, 1.8, -3.5, 0.0, 1.0, 0.0,
                     52.0, 1.7778, 0.1, 120.0);
    daynight.applyAtmosphere(skyClouds);
    daynight.applyAtmosphere(skyFog);
    skyCamera.setAmbient(daynight.getAmbientR() * 1.35, daynight.getAmbientG() * 1.35,
                         daynight.getAmbientB() * 1.35);

    if (skyUiReady) {
        ui.select("skyPanel");
        ui.setText("clock", format("%02.0f:%02.0f", daynight.getTimeOfDay(),
            (daynight.getTimeOfDay() % 1.0) * 60.0) +
            (daynight.isPaused() ? "  PAUSED" : ""));
        local phase = daynight.isNight() ? "Moonlit night" :
            (daynight.getSunElevation() < 12.0 ? "Golden hour" : "Daylight");
        ui.setText("phase", phase + "  •  " + skyWeather);
    }
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    local depth = gfx.getRenderControl().getGBuffer().getDepthTexture();
    skyClouds.renderClouds(gfx, depth);
    skyFog.applyFog(gfx, depth);
    ui.beginFrameAndRender();
};
