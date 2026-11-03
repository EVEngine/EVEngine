// Stylized procedural water splash — native mesh.water_splash + staggered burst.

persist splashSeed = 20261103
persist splashHeight = 2.4
persist splashWidth = 3.2
persist splashLayers = 4
persist splashJagged = 0.78
persist splashFoam = 0.85
persist splashFlare = 1.0
persist splashRadial = 28
persist splashRings = 10
persist splashCamera = null
persist splashYaw = 0.35
persist splashUiReady = false
persist splashFrame = 0
persist splashScreenshotSaved = false
persist splashAge = 0.0
persist splashAuto = true
persist splashCycle = 1.85
persist splashGround = null
persist splashGrid = null
persist splashImpact = null
persist splashLayersFx = null
persist splashSpaceWasDown = false

function clamp(v, a, b) {
    return v < a ? a : (v > b ? b : v);
}

function easeOutCubic(t) {
    local u = 1.0 - clamp(t, 0.0, 1.0);
    return 1.0 - u * u * u;
}

function easeInCubic(t) {
    local u = clamp(t, 0.0, 1.0);
    return u * u * u;
}

function updateStatus(message) {
    if (!splashUiReady) return;
    ui.select("lab");
    ui.setText("status", message);
}

function makeParams(onlyLayer) {
    local paramsResult = procgen.newParams();
    if (!paramsResult.ok) {
        updateStatus("Parameter creation failed: " + paramsResult.status.summary);
        return null;
    }
    local p = paramsResult.value;
    p.setSeed(splashSeed);
    p.setFloat("height", splashHeight);
    p.setFloat("width", splashWidth);
    p.setInt("layers", splashLayers);
    p.setInt("onlyLayer", onlyLayer);
    p.setFloat("jaggedness", splashJagged);
    p.setFloat("foam", splashFoam);
    p.setFloat("flare", splashFlare);
    p.setInt("radialSegments", splashRadial);
    p.setInt("heightSegments", splashRings);
    return p;
}

function uploadLayer(onlyLayer) {
    local p = makeParams(onlyLayer);
    if (p == null) return null;
    local meshResult = procgen.generateMesh("mesh.water_splash", p, gfx);
    if (!meshResult.ok) {
        updateStatus("Generation failed: " + meshResult.status.summary);
        return null;
    }
    return meshResult.value;
}

function ensureLayerObject(index) {
    if (splashLayersFx == null) splashLayersFx = [];
    while (splashLayersFx.len() <= index) {
        local e = eve.Renderable3D();
        e.setReceiveLight(false);
        e.setCastShadow(false);
        e.setRoughness(1.0);
        e.setMetallic(0.0);
        e.setTint(1.0, 1.0, 1.0, 1.0);
        local mat = gfx.newMaterial();
        mat.setShadingModel("unlit");
        mat.setSurfaceMode("transparent");
        mat.setBlendMode("alpha");
        mat.setDepthWrite(false);
        mat.setDoubleSided(true);
        mat.setReceiveLight(false);
        mat.setCastShadow(false);
        mat.setReceiveShadow(false);
        e.setMaterial(mat);
        splashLayersFx.push({ object = e, mesh = null });
    }
    return splashLayersFx[index];
}

function rebuildSplash() {
    for (local i = 0; i < splashLayers; ++i) {
        local mesh = uploadLayer(i);
        if (mesh == null) return;
        local slot = ensureLayerObject(i);
        slot.mesh = mesh;
        slot.object.setMesh(mesh);
        slot.object.setVisible(true);
    }
    if (splashLayersFx != null) {
        for (local i = splashLayers; i < splashLayersFx.len(); ++i) {
            splashLayersFx[i].object.setVisible(false);
        }
    }
    splashAge = 0.0;
    updateStatus(format("seed %d  |  %d layers  |  jagged %.2f  |  SPACE / Burst", splashSeed, splashLayers,
                        splashJagged));
}

function buildSceneProps() {
    local cube = gfx.newMeshCube(1.0);
    local cylinder = gfx.newMeshCylinder(64, 1, true);
    splashGround = eve.Renderable3D();
    splashGround.setMesh(cube);
    splashGround.setPosition(0.0, -0.08, 0.0);
    splashGround.setScale(14.0, 0.16, 14.0);
    splashGround.setTint(0.16, 0.18, 0.20, 1.0);
    splashGround.setReceiveLight(true);
    splashGround.setRoughness(0.95);

    splashGrid = [];
    for (local i = -6; i <= 6; ++i) {
        local gx = eve.Renderable3D();
        gx.setMesh(cube);
        gx.setPosition(i * 1.0, 0.005, 0.0);
        gx.setScale(0.02, 0.01, 12.0);
        gx.setTint(0.28, 0.32, 0.34, 1.0);
        gx.setReceiveLight(false);
        splashGrid.push(gx);

        local gz = eve.Renderable3D();
        gz.setMesh(cube);
        gz.setPosition(0.0, 0.006, i * 1.0);
        gz.setScale(12.0, 0.01, 0.02);
        gz.setTint(0.28, 0.32, 0.34, 1.0);
        gz.setReceiveLight(false);
        splashGrid.push(gz);
    }

    splashImpact = eve.Renderable3D();
    splashImpact.setMesh(cylinder);
    splashImpact.setPosition(0.0, 0.02, 0.0);
    splashImpact.setScale(0.55, 0.02, 0.55);
    splashImpact.setTint(0.05, 0.07, 0.09, 1.0);
    splashImpact.setReceiveLight(false);
}

function buildSplashUi() {
    ui.setScale(1.0);
    ui.setTheme("dark");
    ui.beginBuild();
    ui.beginWindow("Water Splash VFX", "root");
    ui.text("PROCEDURAL WATER SPLASH", "title");
    ui.text("Nested flared crowns with jagged foam lips. Burst plays a staggered impact.", "help");
    ui.beginRow("workspace", 16.0);
    ui.beginColumn("controls", 5.0);
    ui.slider("Height", splashHeight, 0.6, 5.0, "height");
    ui.slider("Width", splashWidth, 0.8, 7.0, "width");
    ui.slider("Layers", splashLayers.tofloat(), 2.0, 4.0, "layers");
    ui.slider("Jaggedness", splashJagged, 0.0, 1.0, "jagged");
    ui.slider("Foam", splashFoam, 0.0, 1.0, "foam");
    ui.slider("Flare", splashFlare, 0.4, 1.7, "flare");
    ui.slider("Radial segs", splashRadial.tofloat(), 12.0, 48.0, "radial");
    ui.slider("Height segs", splashRings.tofloat(), 4.0, 18.0, "rings");
    ui.beginRow("actions", 8.0);
    ui.button("Burst", "burst");
    ui.button("New seed", "new-seed");
    ui.button("Reset", "reset");
    ui.end();
    ui.text("", "status");
    ui.setItemSize(260.0, 0.0);
    ui.end();
    ui.beginColumn("preview-column", 4.0);
    ui.text("Auto-burst loops. Drag parameters to reshape the crowns.", "preview-help");
    ui.viewport("preview", 620.0, 560.0);
    ui.end();
    ui.end();
    ui.end();
    ui.mountBuildAs("lab");
    ui.select("lab");
    ui.setHostPos(0.0, 0.0, 0.0, 0.0);
    ui.setHostSize(config.width.tofloat(), config.height.tofloat());
    ui.setHostOverlay(false);
    splashUiReady = true;
}

function resetSplashParameters() {
    splashHeight = 2.4;
    splashWidth = 3.2;
    splashLayers = 4;
    splashJagged = 0.78;
    splashFoam = 0.85;
    splashFlare = 1.0;
    splashRadial = 28;
    splashRings = 10;
    ui.setValue("height", splashHeight);
    ui.setValue("width", splashWidth);
    ui.setValue("layers", splashLayers.tofloat());
    ui.setValue("jagged", splashJagged);
    ui.setValue("foam", splashFoam);
    ui.setValue("flare", splashFlare);
    ui.setValue("radial", splashRadial.tofloat());
    ui.setValue("rings", splashRings.tofloat());
}

function handleSplashUi() {
    ui.select("lab");
    local click = ui.consumeClick();
    while (click != "") {
        local slash = click.find("/");
        local id = slash == null ? click : click.slice(slash + 1);
        if (id == "burst") splashAge = 0.0;
        else if (id == "new-seed") {
            splashSeed += 1;
            rebuildSplash();
        } else if (id == "reset") {
            resetSplashParameters();
            rebuildSplash();
        }
        click = ui.consumeClick();
    }
    local changed = ui.consumeChange();
    while (changed != "") {
        local slash = changed.find("/");
        local id = slash == null ? changed : changed.slice(slash + 1);
        if (id == "height") splashHeight = ui.getValue(id);
        else if (id == "width") splashWidth = ui.getValue(id);
        else if (id == "layers") splashLayers = clamp(ui.getValue(id), 2.0, 4.0).tointeger();
        else if (id == "jagged") splashJagged = ui.getValue(id);
        else if (id == "foam") splashFoam = ui.getValue(id);
        else if (id == "flare") splashFlare = ui.getValue(id);
        else if (id == "radial") splashRadial = clamp(ui.getValue(id), 12.0, 48.0).tointeger();
        else if (id == "rings") splashRings = clamp(ui.getValue(id), 4.0, 18.0).tointeger();
        rebuildSplash();
        changed = ui.consumeChange();
    }
}

function layerTiming(index, age) {
    // Outer crown leads, central jet arrives last — impact then rise.
    local delay = (splashLayers - 1 - index).tofloat() * 0.06;
    local localAge = age - delay;
    local rise = 0.42;
    local hold = 0.28;
    local fade = 0.55;
    if (localAge < 0.0) return { visible = false, scale = 0.01, alpha = 0.0, y = 0.0 };
    if (localAge < rise) {
        local t = easeOutCubic(localAge / rise);
        return { visible = true, scale = 0.15 + 0.85 * t, alpha = 0.35 + 0.65 * t, y = 0.05 * t };
    }
    if (localAge < rise + hold) {
        return { visible = true, scale = 1.0, alpha = 1.0, y = 0.05 };
    }
    local t = (localAge - rise - hold) / fade;
    if (t >= 1.0) return { visible = false, scale = 0.01, alpha = 0.0, y = 0.0 };
    local s = 1.0 + 0.18 * easeInCubic(t);
    local a = 1.0 - easeInCubic(t);
    return { visible = true, scale = s, alpha = a, y = 0.05 - 0.12 * t };
}

function animateSplash(dt) {
    splashAge += dt;
    if (splashAuto && splashAge > splashCycle) splashAge = 0.0;
    if (splashLayersFx == null) return;
    for (local i = 0; i < splashLayers && i < splashLayersFx.len(); ++i) {
        local slot = splashLayersFx[i];
        local pose = layerTiming(i, splashAge);
        slot.object.setVisible(pose.visible);
        if (!pose.visible) continue;
        local sx = pose.scale * (1.0 + i * 0.012);
        local sy = pose.scale;
        slot.object.setScale(sx, sy, sx);
        slot.object.setPosition(0.0, pose.y, 0.0);
        slot.object.setYaw(splashYaw * 0.15);
        slot.object.setTint(1.0, 1.0, 1.0, pose.alpha);
    }
    if (splashImpact != null) {
        local ring = clamp(splashAge / 0.35, 0.0, 1.0);
        local fade = clamp(1.0 - (splashAge - 0.45) / 0.8, 0.0, 1.0);
        local r = 0.35 + 1.8 * easeOutCubic(ring);
        splashImpact.setScale(r, 0.02, r);
        splashImpact.setTint(0.05, 0.08, 0.10, 0.55 * fade);
        splashImpact.setVisible(fade > 0.02);
    }
}

eve_init = function() {
    buildSplashUi();
    buildSceneProps();
    splashCamera = eve.Camera3D();
    splashCamera.setEye(5.4, 3.2, 6.2);
    splashCamera.setTarget(0.0, 0.85, 0.0);
    splashCamera.setUp(0.0, 1.0, 0.0);
    splashCamera.setFov(34.0);
    splashCamera.setAmbient(0.22, 0.28, 0.32);
    splashCamera.setActive(true);
    gfx.setDirectionalLight(-0.35, -1.0, -0.45, 1.15, 1.25, 1.35);
    gfx.setBackgroundColor(0.10, 0.12, 0.14, 1.0);
    rebuildSplash();
    print("water-splash-vfx: mesh.water_splash ready (SPACE / Burst)\n");
};

eve_update = function(dt) {
    handleSplashUi();
    local space = keyboard.isDown("space") || keyboard.isDown("Space");
    if (space && !splashSpaceWasDown) splashAge = 0.0;
    splashSpaceWasDown = space;
    splashYaw += dt * 0.18;
    animateSplash(dt);
};

eve_render = function() {
    gfx.clear();
    ui.select("lab");
    local canvas = ui.viewportCanvas("preview");
    if (canvas != null) gfx.renderScene3DToCanvas(canvas, splashCamera);
    ui.beginFrameAndRender();
    splashFrame += 1;
    if (!splashScreenshotSaved && splashFrame > 24 && gfx.saveFramePng("water-splash-vfx.png")) {
        splashScreenshotSaved = true;
        print("water-splash-vfx: saved water-splash-vfx.png\n");
    }
};
