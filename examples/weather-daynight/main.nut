dofile("river.nut");
dofile("vegetation.nut");
// LIVING SKY — unified DayNight + Weather + volumetric clouds / height fog.
// 1..5 weather, Space pause, Left/Right scrub time, S lightning, C camera.

persist skyUiReady = false
persist skyProps = []
persist skyCamera = null
persist skyClouds = null
persist skyFog = null
persist skyWeather = "clear"
persist skyCamAngle = 1.083
persist skyAutoCamera = false
persist skyStrikeDown = false
persist skyTerrain = null

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

function riverGround(wx,wz) {
    local cliff=clampf((-wz-5.4)/2.2,0.0,1.0);
    cliff=cliff*cliff*(3.0-2.0*cliff);
    local ground=0.55+cliff*4.8+abs(wx)*0.055;
    ground+=0.16*math.polarY(1.0,wx*1.14+wz*0.58);
    local nearest=100000.0, bed=0.0, width=1.0;
    foreach (r in riverSamples) {
        local d=(wx-r.x)*(wx-r.x)+(wz-r.z)*(wz-r.z);
        if (d<nearest) { nearest=d; bed=r.y-0.48; width=r.width; }
    }
    local bank=clampf((sqrt(nearest)-width*0.84)/1.3,0.0,1.0);
    bank=bank*bank*(3.0-2.0*bank);
    ground=bed*(1.0-bank)+ground*bank;
    return ground;
}

function buildLivingScene() {
    buildRiver();
    const TW = 100;
    const TH = 100;
    const CELL = 0.36;
    local heightmapResult = procgen.newHeightmap(TW, TH);
    if (!heightmapResult.ok) throw heightmapResult.status.summary;
    local hm = heightmapResult.value;
    for (local z = 0; z < TH; ++z) {
        for (local x = 0; x < TW; ++x) {
            local wx = (x - 50) * CELL;
            local wz = (z - 50) * CELL;
            local ground=riverGround(wx,wz);
            hm.setHeight(x,z,clampf((ground+1.5)/8.0,0.01,0.98));
        }
    }
    local terrainMesh = eve.HeightmapTargetModule().newMesh(hm, CELL, 8.0);
    skyTerrain = addObject(terrainMesh, -18.0, -1.5, -18.0,
        1.0, 1.0, 1.0, 0.17, 0.31, 0.16, 0.98);
    skyTerrain.setCastShadow(true);
    local pixels=eve.Image().newEmptyImageData(TW,TH,"RGBA8");
    for (local z=0;z<TH;++z) for (local x=0;x<TW;++x) {
        local x1=x<TW-1 ? x+1 : x, z1=z<TH-1 ? z+1 : z;
        local slope=(abs(hm.height(x1,z)-hm.height(x,z))+
                     abs(hm.height(x,z1)-hm.height(x,z)))*8.0/CELL;
        local rock=clampf((slope-0.35)/1.6,0.0,1.0);
        local grain=0.94+0.06*vegetationHash(x,z,97);
        local strata=0.88+0.12*math.polarY(1.0,hm.height(x,z)*180.0);
        pixels.setPixel(x,z,((1.0-rock)*0.065+rock*0.24*strata)*grain,
            ((1.0-rock)*0.14+rock*0.23*strata)*grain,
            ((1.0-rock)*0.065+rock*0.19*strata)*grain,1.0);
    }
    skyTerrain.setTexture(gfx.newTexture(pixels,false,false));
    skyTerrain.setTint(1.0,1.0,1.0,1.0);

    buildNativeTrees(hm, CELL);
    buildNativeGrass(hm, CELL);

    local stone=gfx.newMeshSphere(7,5);
    // Fixed placement sequence keeps captures and hot reload reproducible.
    for (local i=6;i<232;i+=6) {
        local r=riverSamples[i];
        foreach (sign in [-1.0,1.0]) {
            local offset=r.width+0.35+(i%5)*0.10;
            local x=r.x+r.sx*offset*sign, z=r.z+r.sz*offset*sign;
            local size=0.35+(i%7)*0.11;
            local y=riverGround(x,z);
            local rock=addObject(stone,x,y-0.12,z,size*1.5,size*0.75,size,
                0.12+(i%3)*0.025,0.16+(i%3)*0.025,0.15+(i%3)*0.025,0.85);
            rock.setYaw(i*37.0);
            rock.setCastShadow(true);
        }
    }
    // Large fractured boulders frame the waterfall without covering its lip.
    foreach (sign in [-1.0,1.0]) for (local i=0;i<5;++i) {
        local rock=addObject(stone,sign*(2.6+i*0.95),2.15+(i%2)*0.3,-6.7,
            1.2,2.4,1.2,0.14,0.18,0.17,0.92);
        rock.setYaw(i*31.0);
        rock.setCastShadow(true);
    }
    print("LIVING_SKY_WATER_READY upstream=river drop=continuous downstream=river\n");
}

function buildSkyPanel() {
    ui.setTheme("dark");
    ui.beginBuild();
    ui.beginWindow("LIVING SKY", "root");
    ui.text("Spline falls & river", "title");
    ui.text("", "clock");
    ui.text("", "phase");
    ui.text("Weather", "h1");
    ui.button("1  Clear", "clear");
    ui.button("2  Cloudy", "cloudy");
    ui.button("3  Rain", "rain");
    ui.button("4  Storm", "storm");
    ui.button("5  Snow", "snow");
    ui.button("Strike lightning (S)", "strike");
    ui.text("Space pause | arrows time | C orbit | G curve", "hint");
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
    skyCamera.setEye(9.0, 8.0, 17.0);
    skyCamera.setActive(true);
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
    updateRiver(dt);
    updateNativeVegetation(dt);
    if (skyAutoCamera) {
        skyCamAngle += dt * 0.018;
        skyCamera.setEye(math.polarX(19.2, skyCamAngle), 8.0,
                         math.polarY(19.2, skyCamAngle));
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

    local ex = math.polarX(19.2, skyCamAngle);
    local ez = math.polarY(19.2, skyCamAngle);
    skyClouds.setCamera(ex, 8.0, ez, 0.0, 1.8, -3.5, 0.0, 1.0, 0.0,
                        52.0, 1.7778, 0.1, 120.0);
    skyFog.setCamera(ex, 8.0, ez, 0.0, 1.8, -3.5, 0.0, 1.0, 0.0,
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
