// Realtime fog: atmospheric-fog 雾都 display path + MAC fluid as an additive layer.
// Height fog, emissive proxy, and GPU wisps come from Volumetric. MAC / SceneWind /
// the sphere interactor only add density into the same froxel grid.
// 1/2/3 thin-medium-dense height fog, WASD wind, arrows move sphere, Space pause, R reseed.

persist rfCamera = null
persist rfVolume = null
persist rfSkyFog = null
persist rfFog = null
persist rfObjects = []
persist rfEnabled = true
persist rfPaused = false
persist rfSpaceDown = false
persist rfPreset = 2
persist rfWindX = 0.55
persist rfCurl = 0.45
persist rfProxyX = 0.0
persist rfProxyZ = -6.0
persist rfProxyPrevX = 0.0
persist rfProxyPrevZ = -6.0
persist rfFrame = 0
persist rfScreenshotSaved = false

function rfObject(mesh, x, y, z, sx, sy, sz, r, g, b) {
    local object = eve.Renderable3D();
    object.setMesh(mesh);
    object.setPosition(x, y, z);
    object.setScale(sx, sy, sz);
    object.setTint(r, g, b, 1.0);
    object.setRoughness(0.72);
    object.setCastShadow(true);
    object.setReceiveShadow(true);
    rfObjects.push(object);
    return object;
}

function rfApplyWind() {
    rfFog.setMainWind(rfWindX, 0.0, 0.12);
    rfFog.setCurlStrength(rfCurl);
}

function rfApplyProxy(dt) {
    local vx = (rfProxyX - rfProxyPrevX) / (dt > 0.0001 ? dt : 0.016);
    local vz = (rfProxyZ - rfProxyPrevZ) / (dt > 0.0001 ? dt : 0.016);
    rfFog.setSphereInteractor(rfProxyX, 1.1, rfProxyZ, 1.05, vx, 0.0, vz);
    rfProxyPrevX = rfProxyX;
    rfProxyPrevZ = rfProxyZ;
}

function rfRebuildDisplay() {
    // Same 80x45x32 frustum grid as examples/atmospheric-fog. Do not let MAC
    // resize this atlas — injectToVolumetric is additive on the existing media.
    rfVolume.clearFroxelGrid();

    local extinction = rfPreset == 1 ? 0.012 :
        (rfPreset == 2 ? 0.035 : 0.075);
    local falloff = rfPreset == 3 ? 0.09 : 0.16;
    rfVolume.injectFroxelHeightFog(
        extinction,
        0.72, 0.82, 0.98,
        0.0,
        falloff,
        -2.0, 10.0);
    rfVolume.injectEmissiveLightProxy(0.0, 1.2, -10.0, 1.0, 0.45, 0.18, 7.0, 1.8);
    rfFog.injectToVolumetric(rfVolume);
    rfVolume.integrateFroxel(0.55, 0.62, 0.78, 1.0);
    rfVolume.uploadFroxel(gfx);
}

function rfReseed() {
    rfFog.seedHeightFog(0.22, 0.0, 0.16, 0.45);
    rfApplyWind();
    rfApplyProxy(0.016);
    rfRebuildDisplay();
}

eve_init = function() {
    if (!has_module("realtimeFog")) {
        throw "examples/realtime-fog requires the graphics_fog module (slot realtimeFog)";
    }

    gfx.setBackgroundColor(0.045, 0.075, 0.13, 1.0);
    gfx.setDirectionalLight(-0.25, -1.0, -0.15, 1.05, 0.98, 0.88);

    rfCamera = eve.Camera3D();
    rfCamera.setEye(0.0, 5.0, 15.0);
    rfCamera.setTarget(0.0, 1.4, -15.0);
    rfCamera.setFov(55.0);
    rfCamera.setClipPlanes(0.1, 100.0);
    rfCamera.setAmbient(0.20, 0.25, 0.34);

    local cube = gfx.newMeshCube(1.0);
    rfObject(cube, 0.0, -0.65, -18.0, 24.0, 0.5, 55.0, 0.16, 0.20, 0.15);
    for (local row = 0; row < 6; ++row) {
        local z = 2.0 - row * 8.0;
        rfObject(cube, -4.2, 1.0, z, 1.5, 3.2, 1.5,
                 0.72, 0.26 + row * 0.045, 0.16);
        rfObject(cube, 4.2, 1.0, z - 3.0, 1.5, 3.2, 1.5,
                 0.16, 0.36 + row * 0.05, 0.72);
    }
    rfObject(cube, rfProxyX, 1.1, rfProxyZ, 1.7, 1.7, 1.7, 0.92, 0.86, 0.55);

    local rc = gfx.getRenderControl();
    rc.enable("gbuffer");
    rc.enable("atmosphere");
    rc.enable("volumetricFog");
    rc.compile();

    rfVolume = gfx.newVolumetric();
    rfVolume.setMode("froxel");
    rfVolume.setQuality("medium");
    rfVolume.setCamera(0.0, 5.0, 15.0, 0.0, 1.4, -15.0,
                       0.0, 1.0, 0.0, 55.0, 1.7777778, 0.1, 100.0);
    rfVolume.configureFroxelGrid(80, 45, 32, 0.1, 100.0);

    rfSkyFog = gfx.newVolumetric();
    rfSkyFog.setMode("fog");
    rfSkyFog.setQuality("high");
    rfSkyFog.setFogColor(0.72, 0.82, 0.98);
    rfSkyFog.setDensity(0.028);
    rfSkyFog.setIntensity(0.62);
    rfSkyFog.setFogHeight(0.0);
    rfSkyFog.setFogHeightFalloff(0.16);
    rfSkyFog.setFogStart(2.0);
    rfSkyFog.setFogEnd(70.0);
    rfSkyFog.setFogNoise(0.55);
    rfSkyFog.setCamera(0.0, 5.0, 15.0, 0.0, 1.4, -15.0,
                       0.0, 1.0, 0.0, 55.0, 1.7777778, 0.1, 100.0);

    rfFog = eve.RealtimeFog().newSystem();
    rfFog.setQuality("enhanced");
    rfFog.setProfile(0.04, 0.72, 0.82, 0.98, 0.15, 1.0, 0.55, 0.35);
    // Domain covers the camera and the pillar city so MAC is sampled by the frustum.
    rfFog.configureDomain(24, 12, 32, -16.0, -1.0, -48.0, 16.0, 10.0, 18.0);
    rfFog.setWindResponseRate(6.0);
    rfFog.setCurlTimeScale(1.1);
    rfReseed();

    print("Realtime fog: 1/2/3 density, WASD wind, arrows move sphere, Space pause, R reseed\n");
};

eve_update = function(dt) {
    if (key_just_pressed("1")) { rfPreset = 1; rfRebuildDisplay(); }
    if (key_just_pressed("2")) { rfPreset = 2; rfRebuildDisplay(); }
    if (key_just_pressed("3")) { rfPreset = 3; rfRebuildDisplay(); }
    if (key_just_pressed("r")) rfReseed();

    if (keyboard.isDown("a")) rfWindX -= 0.8 * dt;
    if (keyboard.isDown("d")) rfWindX += 0.8 * dt;
    if (keyboard.isDown("w")) rfCurl += 0.6 * dt;
    if (keyboard.isDown("s")) rfCurl = rfCurl - 0.6 * dt;
    if (rfCurl < 0.0) rfCurl = 0.0;
    if (rfCurl > 2.5) rfCurl = 2.5;
    if (rfWindX < -2.5) rfWindX = -2.5;
    if (rfWindX > 2.5) rfWindX = 2.5;

    if (keyboard.isDown("left")) rfProxyX -= 4.0 * dt;
    if (keyboard.isDown("right")) rfProxyX += 4.0 * dt;
    if (keyboard.isDown("up")) rfProxyZ -= 4.0 * dt;
    if (keyboard.isDown("down")) rfProxyZ += 4.0 * dt;
    if (rfProxyX < -8.0) rfProxyX = -8.0;
    if (rfProxyX > 8.0) rfProxyX = 8.0;
    if (rfProxyZ < -40.0) rfProxyZ = -40.0;
    if (rfProxyZ > 4.0) rfProxyZ = 4.0;

    if (rfObjects.len() > 0) {
        rfObjects[rfObjects.len() - 1].setPosition(rfProxyX, 1.1, rfProxyZ);
    }

    local down = keyboard.isDown("space");
    if (down && !rfSpaceDown) rfPaused = !rfPaused;
    rfSpaceDown = down;

    rfApplyWind();
    if (!rfPaused) {
        rfApplyProxy(dt);
        rfFog.stepSimulation(dt);
        rfRebuildDisplay();
    }

    rfFrame += 1;
    if (!rfScreenshotSaved && rfFrame > 8 && gfx.saveFramePng("realtime-fog.png")) {
        rfScreenshotSaved = true;
        print("realtime-fog: screenshot saved\n");
    }
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    if (rfEnabled) {
        local depth = gfx.getRenderControl().getGBuffer().getDepthTexture();
        rfSkyFog.applyFog(gfx, depth);
        rfVolume.applyFroxel(gfx, depth);
    }
};
