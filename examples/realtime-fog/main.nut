// Realtime fog demo: MAC fluid + SceneWind + sphere interactor → froxel composite.
// Requires has_module("realtimeFog"). Controls: 1/2/3 quality, WASD wind, arrows move
// the solid sphere, Space pauses sim, R reseeds.
// Default quality is "fast" so headless Lavapipe can advance frames; press 2/3 on a
// real GPU for denser froxels.

persist rfCamera = null
persist rfVolume = null
persist rfFog = null
persist rfObjects = []
persist rfEnabled = true
persist rfPaused = false
persist rfSpaceDown = false
persist rfWindX = 0.55
persist rfCurl = 0.45
persist rfProxyX = 0.0
persist rfProxyZ = -6.0
persist rfProxyPrevX = 0.0
persist rfProxyPrevZ = -6.0
persist rfLightDir = [0.25, 1.0, 0.15]
persist rfFrame = 0
persist rfScreenshotSaved = false

function rfObject(mesh, x, y, z, sx, sy, sz, r, g, b) {
    local object = eve.Renderable3D();
    object.setMesh(mesh);
    object.setPosition(x, y, z);
    object.setScale(sx, sy, sz);
    object.setTint(r, g, b, 1.0);
    object.setRoughness(0.7);
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

function rfSyncVolume() {
    rfFog.syncToVolumetric(
        rfVolume,
        rfLightDir[0], rfLightDir[1], rfLightDir[2],
        0.85, 0.9, 1.0, 1.35);
    rfVolume.uploadFroxel(gfx);
}

function rfReseed() {
    rfFog.seedHeightFog(1.15, 0.0, 0.28, 0.35);
    rfApplyWind();
    rfApplyProxy(0.016);
    rfSyncVolume();
}

eve_init = function() {
    if (!has_module("realtimeFog")) {
        throw "examples/realtime-fog requires the graphics_fog module (slot realtimeFog)";
    }

    gfx.setBackgroundColor(0.035, 0.055, 0.09, 1.0);

    rfCamera = eve.Camera3D();
    rfCamera.setEye(0.0, 5.5, 14.0);
    rfCamera.setTarget(0.0, 1.2, -10.0);
    rfCamera.setFov(55.0);
    rfCamera.setAmbient(0.18, 0.22, 0.30);

    local cube = gfx.newMeshCube(1.0);
    rfObject(cube, 0.0, -0.6, -12.0, 28.0, 0.45, 48.0, 0.14, 0.18, 0.14);
    for (local row = 0; row < 5; ++row) {
        local z = 0.0 - row * 7.0;
        rfObject(cube, -5.0, 1.1, z, 1.4, 3.4, 1.4, 0.7, 0.28 + row * 0.04, 0.18);
        rfObject(cube, 5.0, 1.1, z - 2.5, 1.4, 3.4, 1.4, 0.18, 0.35 + row * 0.05, 0.72);
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
    rfVolume.setCamera(0.0, 5.5, 14.0, 0.0, 1.2, -10.0,
                       0.0, 1.0, 0.0, 55.0, 1.7777778, 0.1, 80.0);
    // syncToVolumetric will resize to the active FogQuality froxel budget.
    rfVolume.configureFroxelGrid(48, 27, 32, 0.1, 80.0);

    rfFog = eve.RealtimeFog().newSystem();
    rfFog.setQuality("fast");
    rfFog.configureDomain(16, 8, 16, -12.0, 0.0, -28.0, 12.0, 6.0, 4.0);
    rfFog.setWindResponseRate(6.0);
    rfFog.setCurlTimeScale(1.1);
    rfReseed();

    print("Realtime fog: 1/2/3 quality, WASD wind, arrows move sphere, Space pause, R reseed\n");
};

eve_update = function(dt) {
    if (key_just_pressed("1")) { rfFog.setQuality("fast"); rfSyncVolume(); }
    if (key_just_pressed("2")) { rfFog.setQuality("enhanced"); rfSyncVolume(); }
    if (key_just_pressed("3")) { rfFog.setQuality("physical_reference"); rfSyncVolume(); }
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
    if (rfProxyZ < -22.0) rfProxyZ = -22.0;
    if (rfProxyZ > 0.0) rfProxyZ = 0.0;

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
        rfSyncVolume();
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
        rfVolume.applyFroxel(gfx, depth);
    }
};
