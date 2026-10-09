// Lighting showcase: ambient, shadow-casting directional, rotating volumetric
// spots, glowing emitters, and optional ray-tracing / portable reflection chain.
// Keys: 1 ambient  2 directional  3 emitters  4 spots  5 volumetric
//       Space RT  R reset camera

if (!("litCamera" in getroottable())) litCamera <- null;
if (!("litSun" in getroottable())) litSun <- null;
if (!("litSpots" in getroottable())) litSpots <- [];
if (!("litObjects" in getroottable())) litObjects <- [];
if (!("litEmitters" in getroottable())) litEmitters <- [];
if (!("litEmitterLights" in getroottable())) litEmitterLights <- [];
if (!("litEmitterColors" in getroottable())) litEmitterColors <- [];
if (!("litVolume" in getroottable())) litVolume <- null;
if (!("litTime" in getroottable())) litTime <- 0.0;
if (!("litUiBuilt" in getroottable())) litUiBuilt <- false;
if (!("litAmbientOn" in getroottable())) litAmbientOn <- true;
if (!("litDirectionalOn" in getroottable())) litDirectionalOn <- true;
if (!("litEmittersOn" in getroottable())) litEmittersOn <- true;
if (!("litSpotOn" in getroottable())) litSpotOn <- true;
if (!("litVolumetricOn" in getroottable())) litVolumetricOn <- true;
if (!("litRayTracingOn" in getroottable())) litRayTracingOn <- false;
if (!("litHwRtAvailable" in getroottable())) litHwRtAvailable <- false;
if (!("litRtModeLabel" in getroottable())) litRtModeLabel <- "off";
if (!("litFrame" in getroottable())) litFrame <- 0;
if (!("litScreenshotSaved" in getroottable())) litScreenshotSaved <- false;

function makeBox(x, y, z, sx, sy, sz, r, g, b, metallic, roughness, castShadow, receiveShadow) {
    local o = eve.Renderable3D();
    o.setMesh(gfx.newMeshCube(1.0));
    o.setPosition(x, y, z);
    o.setScale(sx, sy, sz);
    o.setTint(r, g, b, 1.0);
    o.setMetallic(metallic);
    o.setRoughness(roughness);
    o.setCastShadow(castShadow);
    o.setReceiveShadow(receiveShadow);
    litObjects.append(o);
    return o;
}

function makeSphere(x, y, z, scale, r, g, b, metallic, roughness, castShadow, receiveShadow) {
    local o = eve.Renderable3D();
    o.setMesh(gfx.newMeshSphere(40, 24));
    o.setPosition(x, y, z);
    o.setScale(scale, scale, scale);
    o.setTint(r, g, b, 1.0);
    o.setMetallic(metallic);
    o.setRoughness(roughness);
    o.setCastShadow(castShadow);
    o.setReceiveShadow(receiveShadow);
    litObjects.append(o);
    return o;
}

function makeEmitter(x, y, z, r, g, b, intensity, radius) {
    // Colored mesh keeps its base material when the glow is off; a point light +
    // bloom provide the emissive look when on.
    local glow = makeSphere(x, y, z, 0.42, r, g, b, 0.12, 0.35, false, true);
    litEmitters.append(glow);
    litEmitterColors.append([r, g, b]);

    local light = eve.Light3D();
    light.setType("point");
    light.setPosition(x, y, z);
    light.setColor(r, g, b, intensity);
    light.setRadius(radius);
    light.setCastShadow(false);
    light.setEnabled(true);
    litEmitterLights.append(light);
    return light;
}

function makeRotatingSpot(r, g, b, intensity, angleDeg, soft, radius) {
    local spot = eve.Light3D();
    spot.setType("spot");
    spot.setColor(r, g, b, intensity);
    spot.setRadius(radius);
    spot.setSpotAngle(angleDeg);
    spot.setSpotSoftness(soft);
    spot.setCastShadow(true);
    spot.setShadowMethod("perspective");
    spot.setShadowStrength(0.90);
    spot.setVolumetric(true);
    spot.setVolumetricIntensity(1.35);
    spot.setEnabled(true);
    litSpots.append(spot);
    return spot;
}

function resetCamera() {
    litCamera.setEye(0.0, 5.2, 11.5);
    litCamera.setTarget(0.0, 1.1, -0.6);
}

function applyAmbient() {
    if (litAmbientOn)
        litCamera.setAmbient(0.22, 0.24, 0.30);
    else
        litCamera.setAmbient(0.02, 0.02, 0.025);
}

function applyDirectional() {
    if (litSun == null) return;
    litSun.setEnabled(litDirectionalOn);
    // Soft fill when the sun is on; zeroed when the demo isolates other lights.
    if (litDirectionalOn)
        gfx.setDirectionalLight(-0.48, 0.86, 0.28, 0.55, 0.50, 0.42);
    else
        gfx.setDirectionalLight(0.0, 1.0, 0.0, 0.0, 0.0, 0.0);
}

function applyEmitters() {
    foreach (light in litEmitterLights)
        light.setEnabled(litEmittersOn);
    for (local i = 0; i < litEmitters.len(); ++i) {
        local mesh = litEmitters[i];
        local c = litEmitterColors[i];
        // Always keep the authored albedo. Glow is the point light + bloom, not
        // a near-black stub mesh when the toggle is off.
        mesh.setTint(c[0], c[1], c[2], 1.0);
        if (litEmittersOn) {
            mesh.setMetallic(0.05);
            mesh.setRoughness(0.18);
            mesh.setReceiveShadow(true);
            mesh.setCastShadow(false);
        } else {
            mesh.setMetallic(0.12);
            mesh.setRoughness(0.45);
            mesh.setReceiveShadow(true);
            mesh.setCastShadow(true);
        }
    }
}

function applySpots() {
    foreach (spot in litSpots)
        spot.setEnabled(litSpotOn);
}

function syncVolumeCamera() {
    if (litVolume == null || litCamera == null) return;
    litVolume.setCamera(
        litCamera.getEyeX(), litCamera.getEyeY(), litCamera.getEyeZ(),
        litCamera.getTargetX(), litCamera.getTargetY(), litCamera.getTargetZ(),
        0.0, 1.0, 0.0,
        litCamera.getFov(), 1280.0 / 720.0, 0.1, 80.0);
}

function rebuildVolumetricMedia() {
    if (litVolume == null) return;
    syncVolumeCamera();
    litVolume.configureFroxelGrid(72, 40, 28, 0.1, 64.0);
    litVolume.clearFroxelGrid();
    // Thin dusty medium so rotating spot shafts read as volumetric beams.
    litVolume.injectFroxelHeightFog(
        litVolumetricOn ? 0.018 : 0.0,
        0.78, 0.84, 0.95,
        -0.5, 0.12,
        -1.0, 8.0);
    local integ = litVolume.integrateFroxelFromSceneLights(
        0.18, 0.20, 0.26,
        -10.0, -1.0, -8.0,
        10.0, 8.0, 10.0,
        8);
    if (!integ.ok)
        print("lighting-showcase: integrateFroxelFromSceneLights failed: " + integ.error + "\n");
    litVolume.uploadFroxel(gfx);
}

function updateRotatingSpots() {
    if (!litSpotOn || litSpots.len() == 0) return;

    // Warm spot: orbit + yaw so the perspective cone and volumetric shaft sweep.
    local a0 = litTime * 0.55;
    local x0 = cos(a0) * 3.6;
    local z0 = sin(a0) * 3.6;
    local y0 = 4.6;
    litSpots[0].setPosition(x0, y0, z0);
    litSpots[0].setDirection(-x0 * 0.35, -1.0, -z0 * 0.35);

    if (litSpots.len() >= 2) {
        // Cool spot: counter-rotate on a tighter radius / lower height.
        local a1 = -litTime * 0.72 + 1.8;
        local x1 = cos(a1) * 2.4;
        local z1 = sin(a1) * 2.4;
        local y1 = 3.8;
        litSpots[1].setPosition(x1, y1, z1);
        litSpots[1].setDirection(-x1 * 0.25, -1.0, -z1 * 0.25);
    }
}

function configureRenderFeatures() {
    local rc = gfx.getRenderControl();
    rc.enable("shadow");
    rc.enable("gbuffer"); // linear depth for froxel composite
    if (litVolumetricOn)
        rc.enable("volumetricFog");
    else
        rc.disable("volumetricFog");
    rc.setPostProcessQuality("high");

    if (litRayTracingOn) {
        if (litHwRtAvailable) {
            rc.enable("rtx");
            rc.disable("reflectionChain");
            litRtModeLabel = "hardware RTX";
        } else {
            // Portable path used by rendering-chain-lab on software Vulkan.
            rc.disable("rtx");
            rc.enable("reflectionChain");
            litRtModeLabel = "reflectionChain (portable)";
        }
        litCamera.setExposure(litVolumetricOn ? 0.24 : 0.28);
        litCamera.setBloom(0.10, 1.35);
    } else {
        rc.disable("rtx");
        rc.disable("reflectionChain");
        litRtModeLabel = "off";
        litCamera.setExposure(litVolumetricOn ? 0.78 : 0.90);
        litCamera.setBloom(0.28, 1.05);
    }
    rc.compile();
    refreshHud();
}

function configureRayTracing(enabled) {
    litRayTracingOn = enabled;
    configureRenderFeatures();
}

function applyVolumetric() {
    configureRenderFeatures();
    rebuildVolumetricMedia();
}

function refreshHud() {
    if (!litUiBuilt) return;
    ui.select("lighting-hud");
    ui.setText("ambient", litAmbientOn ? "Ambient: ON  (1)" : "Ambient: OFF (1)");
    ui.setText("dir", litDirectionalOn ? "Directional CSM: ON  (2)" : "Directional: OFF (2)");
    ui.setText("emit", litEmittersOn ? "Emitters: ON  (3)" : "Emitters: OFF (3)");
    ui.setText("spot", litSpotOn ? "Rotating spots + shadow: ON  (4)" : "Spots: OFF (4)");
    ui.setText("vol", litVolumetricOn ? "Volumetric shafts: ON  (5)" : "Volumetric: OFF (5)");
    ui.setText("rt", "Ray tracing: " + litRtModeLabel + "  (Space)");
    local hw = litHwRtAvailable ? "device RT available" : "no hardware RT — Space uses portable chain";
    ui.setText("hint", hw + "    R: reset camera");
}

function buildScene() {
    litObjects.clear();
    litEmitters.clear();
    litEmitterLights.clear();
    litEmitterColors.clear();

    // Shadow-receiving ground and a slight rear wall for contact shadows / GI.
    makeBox(0.0, -0.12, -0.4, 16.0, 0.16, 12.0, 0.18, 0.19, 0.22, 0.08, 0.90, false, true);
    makeBox(0.0, 2.4, -5.6, 16.0, 5.0, 0.22, 0.22, 0.24, 0.28, 0.05, 0.78, true, true);

    // Occluders that cast crisp directional + spot shadows onto the ground.
    makeBox(-3.6, 1.15, -1.2, 0.9, 2.3, 0.9, 0.55, 0.42, 0.32, 0.10, 0.65, true, true);
    makeBox(0.0, 1.55, -2.0, 1.1, 3.1, 1.1, 0.36, 0.40, 0.48, 0.35, 0.40, true, true);
    makeBox(3.4, 0.95, -0.6, 1.2, 1.9, 0.8, 0.48, 0.52, 0.38, 0.08, 0.72, true, true);

    // Material response balls under the sun + emitter mix.
    local roughness = [0.08, 0.28, 0.55, 0.88];
    for (local i = 0; i < 4; ++i) {
        local x = -3.3 + i * 2.2;
        makeSphere(x, 0.55, 1.6, 0.55, 0.82, 0.84, 0.88, 0.70, roughness[i], true, true);
    }

    // Warm / cool / magenta emitters: surface point lights + bloom-friendly meshes.
    makeEmitter(-4.2, 1.35, 2.8, 1.0, 0.55, 0.22, 3.2, 9.0);
    makeEmitter(4.0, 1.15, 2.4, 0.35, 0.65, 1.0, 2.8, 8.5);
    makeEmitter(0.2, 2.35, -0.4, 0.95, 0.35, 0.85, 2.4, 7.0);
}

eve_init = function() {
    gfx.setBackgroundColor(0.04, 0.05, 0.08, 1.0);

    local rtProbe = eve.RayTracing();
    litHwRtAvailable = rtProbe != null && rtProbe.isAvailable();

    litCamera = eve.Camera3D();
    litCamera.setUp(0.0, 1.0, 0.0);
    litCamera.setFov(46.0);
    litCamera.setExposure(0.78);
    litCamera.setBloom(0.28, 1.05);
    litCamera.setActive(true);
    resetCamera();
    applyAmbient();

    // Shadow scheme: dir CSM + spot perspective; local atlas pages by importance.
    gfx.setShadowSchemeDirectionalEnabled(true);
    gfx.setShadowSchemeSpotEnabled(true);
    gfx.setShadowSchemePointEnabled(false);
    gfx.setShadowSchemeMaxSpotCasters(4);
    gfx.setShadowSchemePagingEnabled(true);
    gfx.setShadowSchemeMaxLocalUpdates(2);
    gfx.setShadowSchemeHysteresisBonus(0.35);

    // Shadow-casting directional (gfx.setDirectionalLight alone does not cast shadows).
    litSun = eve.Light3D();
    litSun.setType("dir");
    litSun.setDirection(-0.48, 0.86, 0.28);
    litSun.setColor(1.0, 0.95, 0.88, 2.15);
    litSun.setCastShadow(true);
    litSun.setShadowMethod("csm");
    litSun.setShadowStrength(0.88);
    litSun.setVolumetric(true);
    litSun.setVolumetricIntensity(0.55);
    applyDirectional();

    litSpots.clear();
    // Warm + cool rotating spots with perspective shadows and volumetric shafts.
    makeRotatingSpot(1.0, 0.88, 0.62, 7.5, 28.0, 0.30, 16.0);
    makeRotatingSpot(0.45, 0.75, 1.0, 6.0, 24.0, 0.40, 14.0);
    updateRotatingSpots();
    applySpots();

    litVolume = gfx.newVolumetric();
    litVolume.setMode("froxel");
    litVolume.setQuality("medium");

    buildScene();
    applyEmitters();

    if (!litUiBuilt) {
        ui.beginBuild();
        ui.beginWindow("LightingShowcase", "root");
        ui.text("EVENGINE / LIGHTING SHOWCASE", "title");
        ui.text("", "ambient");
        ui.text("", "dir");
        ui.text("", "emit");
        ui.text("", "spot");
        ui.text("", "vol");
        ui.text("", "rt");
        ui.text("", "hint");
        ui.end();
        ui.mountBuildAs("lighting-hud");
        ui.select("lighting-hud");
        ui.setHostOverlay(true);
        ui.setHostPos(16.0, 14.0, 0.0, 0.0);
        litUiBuilt = true;
    }

    configureRenderFeatures();
    rebuildVolumetricMedia();
    print("lighting-showcase: ambient/dir/rotating volumetric spots/emitters on; Space=RT\n");
};

eve_update = function(dt) {
    litTime += dt;

    // Gentle orbit of the side emitters so moving colored light is obvious.
    if (litEmittersOn && litEmitterLights.len() >= 2) {
        local warmX = -4.2 + sin(litTime * 0.55) * 0.35;
        local coolX = 4.0 + cos(litTime * 0.48) * 0.35;
        litEmitterLights[0].setPosition(warmX, 1.35, 2.8);
        litEmitters[0].setPosition(warmX, 1.35, 2.8);
        litEmitterLights[1].setPosition(coolX, 1.15, 2.4);
        litEmitters[1].setPosition(coolX, 1.15, 2.4);
        litEmitters[2].setYaw(litTime * 0.7);
    }

    updateRotatingSpots();

    // Keep froxel lighting in sync with moving volumetric spots.
    if (litVolumetricOn)
        rebuildVolumetricMedia();

    if (key_just_pressed("1")) {
        litAmbientOn = !litAmbientOn;
        applyAmbient();
        refreshHud();
    }
    if (key_just_pressed("2")) {
        litDirectionalOn = !litDirectionalOn;
        applyDirectional();
        refreshHud();
    }
    if (key_just_pressed("3")) {
        litEmittersOn = !litEmittersOn;
        applyEmitters();
        refreshHud();
    }
    if (key_just_pressed("4")) {
        litSpotOn = !litSpotOn;
        applySpots();
        if (litVolumetricOn) rebuildVolumetricMedia();
        refreshHud();
    }
    if (key_just_pressed("5")) {
        litVolumetricOn = !litVolumetricOn;
        applyVolumetric();
    }
    if (key_just_pressed("space")) configureRayTracing(!litRayTracingOn);
    if (key_just_pressed("r") || key_just_pressed("R")) resetCamera();
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();

    if (litVolumetricOn && litVolume != null) {
        local rc = gfx.getRenderControl();
        local gb = rc.getGBuffer();
        if (gb != null && gb.isValid()) {
            local depth = gb.getDepthTexture();
            if (depth != null)
                litVolume.applyFroxel(gfx, depth);
        }
    }

    ui.beginFrameAndRender();
    litFrame += 1;
    if (!litScreenshotSaved && litFrame > 48 && gfx.saveFramePng("lighting-showcase.png")) {
        litScreenshotSaved = true;
        print("lighting-showcase: saved lighting-showcase.png\n");
    }
};
