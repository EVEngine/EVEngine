// Lighting showcase: ambient, shadow-casting directional, glowing emitters,
// and optional ray-tracing / portable reflection chain.
// Keys: 1 ambient  2 directional  3 emitters  Space RT  R reset camera

if (!("litCamera" in getroottable())) litCamera <- null;
if (!("litSun" in getroottable())) litSun <- null;
if (!("litObjects" in getroottable())) litObjects <- [];
if (!("litEmitters" in getroottable())) litEmitters <- [];
if (!("litEmitterLights" in getroottable())) litEmitterLights <- [];
if (!("litEmitterColors" in getroottable())) litEmitterColors <- [];
if (!("litTime" in getroottable())) litTime <- 0.0;
if (!("litUiBuilt" in getroottable())) litUiBuilt <- false;
if (!("litAmbientOn" in getroottable())) litAmbientOn <- true;
if (!("litDirectionalOn" in getroottable())) litDirectionalOn <- true;
if (!("litEmittersOn" in getroottable())) litEmittersOn <- true;
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
    // Bright mesh + bloom reads as a glowing body; a normal point light lights
    // nearby surfaces (createEmissiveLight3D is volumetricOnly and skips them).
    local glow = makeSphere(x, y, z, 0.42, r, g, b, 0.05, 0.18, false, false);
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
        if (litEmittersOn) {
            local c = litEmitterColors[i];
            mesh.setTint(c[0], c[1], c[2], 1.0);
        } else {
            mesh.setTint(0.08, 0.08, 0.09, 1.0);
        }
    }
}

function configureRayTracing(enabled) {
    litRayTracingOn = enabled;
    local rc = gfx.getRenderControl();
    rc.enable("shadow");
    rc.setPostProcessQuality("high");

    if (enabled) {
        if (litHwRtAvailable) {
            rc.enable("rtx");
            rc.disable("reflectionChain");
            litRtModeLabel = "hardware RTX";
        } else {
            // Portable fallback used by rendering-chain-lab on software Vulkan.
            rc.disable("rtx");
            rc.enable("reflectionChain");
            litRtModeLabel = "reflectionChain (portable)";
        }
        // GI / SSR add a lot of energy on Lavapipe; pull exposure down so
        // emitter colors and contact shadows stay readable.
        litCamera.setExposure(0.62);
    } else {
        rc.disable("rtx");
        rc.disable("reflectionChain");
        litRtModeLabel = "off";
        litCamera.setExposure(0.90);
    }
    rc.compile();
    refreshHud();
}

function refreshHud() {
    if (!litUiBuilt) return;
    ui.select("lighting-hud");
    ui.setText("ambient", litAmbientOn ? "Ambient: ON  (1)" : "Ambient: OFF (1)");
    ui.setText("dir", litDirectionalOn ? "Directional + shadows: ON  (2)" : "Directional: OFF (2)");
    ui.setText("emit", litEmittersOn ? "Emitters: ON  (3)" : "Emitters: OFF (3)");
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

    // Occluders that cast crisp directional shadows onto the ground.
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
    litCamera.setExposure(0.90);
    litCamera.setBloom(0.28, 1.05);
    litCamera.setActive(true);
    resetCamera();
    applyAmbient();

    // Shadow-casting directional (gfx.setDirectionalLight alone does not cast shadows).
    litSun = eve.Light3D();
    litSun.setType("dir");
    litSun.setDirection(-0.48, 0.86, 0.28);
    litSun.setColor(1.0, 0.95, 0.88, 2.15);
    litSun.setCastShadow(true);
    litSun.setShadowStrength(0.88);
    applyDirectional();

    buildScene();
    applyEmitters();

    if (!litUiBuilt) {
        ui.beginBuild();
        ui.beginWindow("LightingShowcase", "root");
        ui.text("EVENGINE / LIGHTING SHOWCASE", "title");
        ui.text("", "ambient");
        ui.text("", "dir");
        ui.text("", "emit");
        ui.text("", "rt");
        ui.text("", "hint");
        ui.end();
        ui.mountBuildAs("lighting-hud");
        ui.select("lighting-hud");
        ui.setHostOverlay(true);
        ui.setHostPos(16.0, 14.0, 0.0, 0.0);
        litUiBuilt = true;
    }

    // Shadows on by default; ray tracing starts off and is optional via Space.
    configureRayTracing(false);
    print("lighting-showcase: ambient/dir/emitters on, shadows on, RT optional (Space)\n");
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
    if (key_just_pressed("space")) configureRayTracing(!litRayTracingOn);
    if (key_just_pressed("r") || key_just_pressed("R")) resetCamera();
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    ui.beginFrameAndRender();
    litFrame += 1;
    if (!litScreenshotSaved && litFrame > 36 && gfx.saveFramePng("lighting-showcase.png")) {
        litScreenshotSaved = true;
        print("lighting-showcase: saved lighting-showcase.png\n");
    }
};
