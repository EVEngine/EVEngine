// All furniture is compiled from assets/living-room.scad. No runtime primitives.
persist roomCamera = null;
persist roomObjects = [];
persist roomLights = [];
persist roomVolume = null;
persist roomEffects = true;
persist roomHardwareRt = false;
persist roomRtOn = true;
persist roomOrbit = false;
persist roomAngle = 0.65;
persist roomFogTime = 0.0;
persist roomHud = true;

function roomResetCamera() {
    roomCamera.setEye(6.6, 4.45, 8.1);
    roomCamera.setTarget(0.0, 1.35, -0.4);
}

function roomLoad(name, r, g, b, metallic, roughness) {
    local data = model3d.newModelDataFromFile("assets/" + name + ".obj");
    if (data == null || data.getMeshCount() == 0) throw "SCAD batch missing: " + name;
    for (local i = 0; i < data.getMeshCount(); ++i) {
        local part = model3d.createRenderable(gfx, data, i);
        // Imported Material is authoritative; renderer fields alone are inspection mirrors.
        local material = part.getMaterial();
        material.setTint(r, g, b, 1.0);
        material.setMetallic(metallic);
        material.setRoughness(roughness);
        material.setCastShadow(true);
        material.setReceiveShadow(true);
        part.setTint(r, g, b, 1.0);
        part.setMetallic(metallic);
        part.setRoughness(roughness);
        part.setCastShadow(true);
        part.setReceiveShadow(true);
        if (name == "luminous") {
            part.getMaterial().setShadingModel("unlit");
            material.setTint(3.0, 2.0, 0.9, 1.0);
            material.setCastShadow(false);
            part.setCastShadow(false);
        }
        if (name == "glass" || name == "art-glass" || name == "tea-glass") {
            material.setSurfaceMode("transparent");
            material.setBlendMode("alpha");
            material.setTint(r, g, b, name == "art-glass" ? 0.07 : (name == "tea-glass" ? 0.24 : 0.16));
            material.setDoubleSided(true);
            material.setDepthWrite(false);
            material.setCastShadow(false);
            material.setReceiveShadow(false);
            part.setCastShadow(false);
        }
        roomObjects.append(part);
    }
}

function roomSpot(x, y, z, dx, dy, dz, intensity, angle) {
    local light = eve.Light3D();
    light.setType("spot");
    light.setPosition(x, y, z);
    light.setDirection(dx, dy, dz);
    light.setColor(1.0, 0.72, 0.43, intensity);
    light.setRadius(9.0);
    light.setSpotAngle(angle);
    light.setSpotSoftness(0.42);
    light.setCastShadow(true);
    light.setShadowMethod("perspective");
    light.setShadowStrength(0.85);
    light.setVolumetric(true);
    light.setVolumetricIntensity(0.45);
    roomLights.append(light);
}

function roomUpdateFog() {
    roomVolume.setCamera(roomCamera.getEyeX(), roomCamera.getEyeY(), roomCamera.getEyeZ(),
        roomCamera.getTargetX(), roomCamera.getTargetY(), roomCamera.getTargetZ(),
        0.0, 1.0, 0.0, roomCamera.getFov(), 1600.0 / 900.0, 0.1, 60.0);
    roomVolume.clearFroxelGrid();
    roomVolume.injectFroxelHeightFog(0.008, 0.90, 0.80, 0.66, 0.0, 0.18, -0.1, 3.2);
    local result = roomVolume.integrateFroxelFromSceneLights(
        0.10, 0.075, 0.05, -4.0, -0.2, -3.3, 4.0, 3.3, 3.2, 8);
    if (!result.ok) throw "SCAD room fog integration failed";
    roomVolume.uploadFroxel(gfx);
}

function roomConfigureEffects() {
    local rc = gfx.getRenderControl();
    // TAA + explicit 4x MSAA; outline/decal/sky are content-specific effects.
    foreach (feature in ["shadow", "gbuffer", "gbufferAlbedo", "ao", "gi", "aa",
                         "reflectionChain", "volumetricFog", "fogTemporal"] ) {
        if (roomEffects) rc.enable(feature); else rc.disable(feature);
    }
    // Hardware reflections plus the portable RTGI/SSR chain; RTX is never claimed on fallback.
    if (roomEffects && roomHardwareRt && roomRtOn) rc.enable("rtx"); else rc.disable("rtx");
    rc.setPostProcessQuality("high");
    // Explicit spatial coverage AA in addition to the temporal reflection chain.
    // Enabling TAA clears MSAA by default, so request MSAA after TAA.
    gfx.setMsaaSamples(4);
    if (roomEffects) { rc.enable("taa"); rc.enable("msaa"); }
    else rc.disable("msaa");
    roomCamera.setBloom(roomEffects ? 0.16 : 0.0, 1.15);
    rc.compile();
    ui.select("scad-room-hud");
    ui.setText("mode", roomEffects ?
        (roomHardwareRt && roomRtOn ? "HIGH | 4x MSAA + TAA + RT / SSR / SSGI / AO" :
                          (roomHardwareRt ? "HIGH | 4x MSAA + TAA / SSR / SSGI / AO | RT disabled" : "HIGH | 4x MSAA + TAA / SSR / SSGI / AO | RT unavailable")) : "BASELINE | Effects disabled");
    print("scad-living-room: effects=" + roomEffects + " hardwareRT=" + roomHardwareRt + "\n");
}

eve_init = function() {
    if ("dev" in eve) {
        eve.dev.ai.setVisible(false);
        eve.dev.console.setVisible(false);
    }
    gfx.setBackgroundColor(0.075, 0.065, 0.055, 1.0);
    roomCamera = eve.Camera3D();
    roomCamera.setUp(0.0, 1.0, 0.0);
    roomCamera.setFov(40.0);
    roomCamera.setAmbient(0.24, 0.19, 0.14);
    roomCamera.setExposure(0.50);
    roomCamera.setActive(true);
    roomResetCamera();
    roomLoad("plaster", 0.78, 0.72, 0.62, 0.0, 0.92);
    roomLoad("oak", 0.38, 0.21, 0.095, 0.0, 0.32);
    roomLoad("linen", 0.78, 0.70, 0.56, 0.0, 0.94);
    roomLoad("terracotta", 0.50, 0.16, 0.075, 0.0, 0.85);
    roomLoad("brass", 0.72, 0.47, 0.18, 0.92, 0.18);
    roomLoad("charcoal", 0.045, 0.053, 0.052, 0.18, 0.36);
    roomLoad("ceramic", 0.76, 0.69, 0.56, 0.0, 0.18);
    roomLoad("foliage", 0.075, 0.21, 0.085, 0.0, 0.72);
    roomLoad("rug", 0.49, 0.43, 0.34, 0.0, 1.0);
    roomLoad("shade", 0.92, 0.77, 0.51, 0.0, 0.80);
    roomLoad("luminous", 1.0, 0.85, 0.58, 0.0, 0.18);
    roomLoad("art", 0.65, 0.32, 0.16, 0.0, 0.84);
    roomLoad("window-frame", 0.055, 0.065, 0.068, 0.65, 0.25);
    roomLoad("glass", 0.40, 0.64, 0.72, 0.0, 0.06);
    roomLoad("art-glass", 0.78, 0.88, 0.92, 0.0, 0.025);
    roomLoad("tea-glass", 0.70, 0.88, 0.94, 0.0, 0.045);
    roomLoad("celadon", 0.20, 0.39, 0.31, 0.0, 0.15);

    gfx.setShadowSchemeDirectionalEnabled(true);
    gfx.setShadowSchemeSpotEnabled(true);
    gfx.setShadowSchemePointEnabled(false); // Point cube shadows are reserved in this engine.
    gfx.setShadowSchemeMaxSpotCasters(4);
    gfx.setShadowSchemePagingEnabled(true);
    gfx.setShadowSchemeMaxLocalUpdates(4);
    local sun = eve.Light3D();
    sun.setType("dir");
    sun.setDirection(-0.7, 0.65, 0.28);
    sun.setColor(1.0, 0.87, 0.68, 1.25);
    sun.setCastShadow(true);
    sun.setShadowMethod("csm");
    sun.setShadowStrength(0.88);
    sun.setVolumetric(true);
    sun.setVolumetricIntensity(0.20);
    roomLights.append(sun);
    gfx.setDirectionalLight(-0.7, 0.65, 0.28, 0.32, 0.27, 0.20);
    foreach (x in [-0.8, 0.3, 1.4]) roomSpot(x, 2.32, -0.2, 0.0, -1.0, 0.0, 3.2, 48.0);
    roomSpot(-2.2, 1.82, -1.25, 0.0, -1.0, 0.0, 3.8, 60.0);
    local cove = eve.Light3D();
    cove.setType("point");
    cove.setPosition(0.0, 2.72, -2.65);
    cove.setColor(1.0, 0.62, 0.30, 1.6);
    cove.setRadius(5.0);
    cove.setCastShadow(false);
    roomLights.append(cove);

    roomVolume = gfx.newVolumetric();
    roomVolume.setMode("froxel");
    roomVolume.setQuality("high");
    roomVolume.configureFroxelGrid(80, 45, 48, 0.1, 30.0);
    local rt = eve.RayTracing();
    roomHardwareRt = rt != null && rt.isAvailable();
    // A local authoring flag permits previews on CPU Vulkan drivers exposing RT.
    roomRtOn = !file_exists("portable-preview.flag");
    ui.beginBuild();
    ui.beginWindow("ScadLivingRoom", "root");
    ui.text("SCAD / WARM MODERN LIVING ROOM", "title");
    ui.text("", "mode");
    ui.text("Space: effects | T: ray tracing | O: camera orbit | R: reset | H: hide HUD", "help");
    ui.end();
    ui.mountBuildAs("scad-room-hud");
    ui.select("scad-room-hud");
    ui.setHostOverlay(true);
    ui.setHostPos(18.0, 16.0, 0.0, 0.0);
    roomConfigureEffects();
    roomUpdateFog();
    local features = gfx.getRenderControl();
    print("SCAD_ROOM_READY batches=" + roomObjects.len() + " AA: TAA=" + features.isEnabled("taa") + " MSAA=" + features.isEnabled("msaa") + "\n");
};

eve_update = function(dt) {
    if (key_just_pressed("space")) { roomEffects = !roomEffects; roomConfigureEffects(); }
    if (key_just_pressed("t")) { roomRtOn = !roomRtOn; roomConfigureEffects(); }
    if (key_just_pressed("o")) roomOrbit = !roomOrbit;
    if (key_just_pressed("h")) roomHud = !roomHud;
    if (key_just_pressed("r")) { roomOrbit = false; roomResetCamera(); roomUpdateFog(); }
    if (roomOrbit) {
        // Stay in the open front/right quadrant: closed walls never hide the room.
        roomAngle += dt * 0.18;
        local angle = 0.65 + sin(roomAngle) * 0.30;
        roomCamera.setEye(sin(angle) * 10.4, 4.45, cos(angle) * 10.4);
    }
    roomFogTime += dt;
    if (roomEffects && roomFogTime > 0.10) { roomFogTime = 0.0; roomUpdateFog(); }
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    if (roomEffects) {
        local gb = gfx.getRenderControl().getGBuffer();
        if (gb != null && gb.isValid()) {
            local depth = gb.getDepthTexture();
            if (depth != null) roomVolume.applyFroxel(gfx, depth);
        }
    }
    if (roomHud) ui.beginFrameAndRender();
};
