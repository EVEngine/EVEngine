// Project-composed material editor. Center viewport shows a UE5-style material
// sphere; the right inspector writes live Material knobs (no C++ material window).

persist matEd = {
    workspace = null,
    camera = null,
    ball = null,
    ground = null,
    material = null,
    probe = null,
    keyLight = null,
    fillLight = null,
    rimLight = null,
    meshKind = 0, // 0 sphere, 1 cube, 2 cylinder
    shadingModel = 0, // pbr, unlit, hair
    surfaceMode = 0, // opaque, masked, transparent
    blendMode = 0, // alpha, premultiplied, additive, multiply
    tintR = 0.82, tintG = 0.82, tintB = 0.84, tintA = 1.0,
    metallic = 0.0,
    roughness = 0.45,
    alphaCutoff = 0.5,
    parallax = 0.0,
    receiveLight = true,
    castShadow = true,
    receiveShadow = true,
    doubleSided = false,
    depthWrite = true,
    autoSpin = true,
    yaw = 0.35,
    orbitYaw = 0.55,
    orbitPitch = 0.38,
    orbitDist = 4.6,
    orbiting = false,
    lastX = 0.0,
    lastY = 0.0,
    status = "UE5 material sphere · edit params on the right",
    frame = 0,
    screenshotSaved = false,
};

SHADING_MODELS <- ["pbr", "unlit", "hair"];
SURFACE_MODES <- ["opaque", "masked", "transparent"];
BLEND_MODES <- ["alpha", "premultiplied", "additive", "multiply"];
MESH_KINDS <- ["sphere", "cube", "cylinder"];

presets <- {
    plastic = { r=0.82, g=0.82, b=0.84, a=1.0, metallic=0.0, roughness=0.45 },
    rubber = { r=0.12, g=0.12, b=0.13, a=1.0, metallic=0.0, roughness=0.92 },
    brushed = { r=0.72, g=0.74, b=0.78, a=1.0, metallic=0.88, roughness=0.42 },
    chrome = { r=0.95, g=0.96, b=0.98, a=1.0, metallic=1.0, roughness=0.08 },
    gold = { r=1.0, g=0.72, b=0.22, a=1.0, metallic=1.0, roughness=0.22 },
    copper = { r=0.95, g=0.52, b=0.28, a=1.0, metallic=1.0, roughness=0.28 },
    glass = { r=0.72, g=0.88, b=0.95, a=0.35, metallic=0.0, roughness=0.06 },
};

function clamp(v, lo, hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

function eventParts(path) {
    local slash = path.find("/");
    return slash == null ? ["", path] : [path.slice(0, slash), path.slice(slash + 1)];
}

function previewMesh() {
    if (matEd.meshKind == 1) return gfx.newMeshCube(1.0);
    if (matEd.meshKind == 2) return gfx.newMeshCylinder(48, 1, true);
    // High-tessellation UV sphere mirrors the UE5 Material Editor default ball.
    return gfx.newMeshSphere(64, 32);
}

function applyMaterial() {
    if (matEd.material == null) return;
    local model = SHADING_MODELS[matEd.shadingModel];
    local surface = SURFACE_MODES[matEd.surfaceMode];
    local blend = BLEND_MODES[matEd.blendMode];
    matEd.material.setShadingModel(model);
    matEd.material.setTint(matEd.tintR, matEd.tintG, matEd.tintB, matEd.tintA);
    matEd.material.setMetallic(matEd.metallic);
    matEd.material.setRoughness(matEd.roughness);
    matEd.material.setSurfaceMode(surface);
    matEd.material.setBlendMode(blend);
    matEd.material.setAlphaCutoff(matEd.alphaCutoff);
    matEd.material.setParallax(matEd.parallax, 8.0, 24.0);
    matEd.material.setReceiveLight(matEd.receiveLight);
    matEd.material.setCastShadow(matEd.castShadow);
    matEd.material.setReceiveShadow(matEd.receiveShadow);
    matEd.material.setDoubleSided(matEd.doubleSided);
    matEd.material.setDepthWrite(matEd.depthWrite);
    if (matEd.ball != null) {
        matEd.ball.setMaterial(matEd.material);
        // Keep legacy MeshRenderer fields in sync for paths that still read them.
        matEd.ball.setTint(matEd.tintR, matEd.tintG, matEd.tintB, matEd.tintA);
        matEd.ball.setMetallic(matEd.metallic);
        matEd.ball.setRoughness(matEd.roughness);
        matEd.ball.setCastShadow(matEd.castShadow);
        matEd.ball.setReceiveShadow(matEd.receiveShadow);
    }
}

function applyPreset(name) {
    if (!(name in presets)) return;
    local p = presets[name];
    matEd.tintR = p.r; matEd.tintG = p.g; matEd.tintB = p.b; matEd.tintA = p.a;
    matEd.metallic = p.metallic;
    matEd.roughness = p.roughness;
    matEd.shadingModel = 0;
    if (name == "glass") {
        matEd.surfaceMode = 2;
        matEd.blendMode = 0;
        matEd.depthWrite = false;
    } else {
        matEd.surfaceMode = 0;
        matEd.blendMode = 0;
        matEd.depthWrite = true;
    }
    applyMaterial();
    syncInspectorWidgets();
    matEd.status = "Preset · " + name;
}

function rebuildPreviewMesh() {
    if (matEd.ball == null) matEd.ball = eve.Renderable3D();
    matEd.ball.setMesh(previewMesh());
    if (matEd.meshKind == 0) {
        matEd.ball.setScale(1.35, 1.35, 1.35);
        matEd.ball.setPosition(0.0, 0.15, 0.0);
    } else if (matEd.meshKind == 1) {
        matEd.ball.setScale(1.55, 1.55, 1.55);
        matEd.ball.setPosition(0.0, 0.05, 0.0);
    } else {
        matEd.ball.setScale(1.15, 1.7, 1.15);
        matEd.ball.setPosition(0.0, 0.05, 0.0);
    }
    matEd.ball.setYaw(matEd.yaw);
    applyMaterial();
    matEd.status = "Preview mesh · " + MESH_KINDS[matEd.meshKind];
}

function updateOrbitCamera() {
    local cy = cos(matEd.orbitYaw);
    local sy = sin(matEd.orbitYaw);
    local cp = cos(matEd.orbitPitch);
    local sp = sin(matEd.orbitPitch);
    local x = matEd.orbitDist * cp * sy;
    local y = matEd.orbitDist * sp + 0.2;
    local z = matEd.orbitDist * cp * cy;
    matEd.camera.setEye(x, y, z);
    matEd.camera.setTarget(0.0, 0.1, 0.0);
    matEd.camera.setUp(0.0, 1.0, 0.0);
}

function configureWorkspace() {
    matEd.workspace = editor.newWorkspace("material.studio", "Material Studio");
    matEd.workspace.setRegionSize("top", 46.0);
    matEd.workspace.setRegionSize("right", 320.0);
    matEd.workspace.setRegionSize("left", 0.0);
    matEd.workspace.setRegionSize("bottom", 0.0);
    matEd.workspace.layout(config.width.tofloat(), config.height.tofloat());
    matEd.workspace.registerPanel("toolbar", "Material Studio", "top", 0);
    matEd.workspace.registerPanel("preview", "Material Sphere", "center", 10);
    matEd.workspace.registerPanel("inspector", "Parameters", "right", 20);
    matEd.workspace.setPanelCapability("preview", "scene.viewport.3d");
    matEd.workspace.setPanelCapability("inspector", "property.material");
}

function panelToolbar() {
    ui.beginRow("toolbar-row", 8.0);
    ui.button("Plastic", "preset-plastic");
    ui.button("Rubber", "preset-rubber");
    ui.button("Brushed", "preset-brushed");
    ui.button("Chrome", "preset-chrome");
    ui.button("Gold", "preset-gold");
    ui.button("Copper", "preset-copper");
    ui.button("Glass", "preset-glass");
    ui.separator("toolbar-sep");
    ui.button(matEd.autoSpin ? "Pause spin" : "Resume spin", "spin");
    ui.text("", "toolbar-status");
    ui.end();
}

function panelPreview() {
    ui.text("UE5 Material Sphere · LMB orbit · wheel zoom · auto turntable", "preview-help");
    ui.viewport("mat-vp", matEd.workspace.getRegionW("center") - 20.0,
                matEd.workspace.getRegionH("center") - 58.0);
    ui.text("", "preview-status");
}

function panelInspector() {
    ui.text("Shading", "shading-title");
    ui.combo("Model", "pbr\nunlit\nhair", matEd.shadingModel, "shading");
    ui.colorPalette("Tint", matEd.tintR, matEd.tintG, matEd.tintB, matEd.tintA, "tint");
    ui.slider("Metallic", matEd.metallic, 0.0, 1.0, "metallic");
    ui.slider("Roughness", matEd.roughness, 0.04, 1.0, "roughness");
    ui.separator("sep-surface");
    ui.text("Surface", "surface-title");
    ui.combo("Mode", "opaque\nmasked\ntransparent", matEd.surfaceMode, "surface");
    ui.combo("Blend", "alpha\npremultiplied\nadditive\nmultiply", matEd.blendMode, "blend");
    ui.slider("Alpha cutoff", matEd.alphaCutoff, 0.0, 1.0, "alpha-cutoff");
    ui.slider("Parallax", matEd.parallax, 0.0, 0.08, "parallax");
    ui.checkbox("Depth write", matEd.depthWrite, "depth-write");
    ui.checkbox("Double sided", matEd.doubleSided, "double-sided");
    ui.separator("sep-light");
    ui.text("Lighting / Shadow", "light-title");
    ui.checkbox("Receive light", matEd.receiveLight, "receive-light");
    ui.checkbox("Cast shadow", matEd.castShadow, "cast-shadow");
    ui.checkbox("Receive shadow", matEd.receiveShadow, "receive-shadow");
    ui.separator("sep-mesh");
    ui.text("Preview mesh", "mesh-title");
    ui.combo("Mesh", "UE5 Sphere\nCube\nCylinder", matEd.meshKind, "mesh");
    ui.textWrapped("Schema-aligned knobs mirror MaterialDocumentTarget shading/surface/lighting fields. Values write a live gfx.Material on the preview ball.",
                   280.0, "inspector-help");
}

panelBuilders <- {
    toolbar = panelToolbar,
    preview = panelPreview,
    inspector = panelInspector,
};

function mountPanels() {
    for (local i = 0; i < matEd.workspace.getPanelCount(); ++i) {
        local id = matEd.workspace.getPanelId(i);
        if (!(id in panelBuilders)) continue;
        ui.beginBuild();
        ui.beginWindow(matEd.workspace.getPanelTitle(i), "root");
        panelBuilders[id]();
        ui.end();
        ui.mountBuildAs(id);
        ui.select(id);
        local region = matEd.workspace.getPanelRegion(i);
        ui.setHostPos(matEd.workspace.getRegionX(region), matEd.workspace.getRegionY(region), 0.0, 0.0);
        ui.setHostSize(matEd.workspace.getRegionW(region), matEd.workspace.getRegionH(region));
        ui.setHostOverlay(false);
    }
}

function syncInspectorWidgets() {
    ui.select("inspector");
    ui.setValue("shading", matEd.shadingModel.tofloat());
    ui.setColor("tint", matEd.tintR, matEd.tintG, matEd.tintB, matEd.tintA);
    ui.setValue("metallic", matEd.metallic);
    ui.setValue("roughness", matEd.roughness);
    ui.setValue("surface", matEd.surfaceMode.tofloat());
    ui.setValue("blend", matEd.blendMode.tofloat());
    ui.setValue("alpha-cutoff", matEd.alphaCutoff);
    ui.setValue("parallax", matEd.parallax);
    ui.setChecked("depth-write", matEd.depthWrite);
    ui.setChecked("double-sided", matEd.doubleSided);
    ui.setChecked("receive-light", matEd.receiveLight);
    ui.setChecked("cast-shadow", matEd.castShadow);
    ui.setChecked("receive-shadow", matEd.receiveShadow);
    ui.setValue("mesh", matEd.meshKind.tofloat());
}

function syncLabels() {
    ui.select("toolbar");
    ui.setText("toolbar-status", matEd.status);
    ui.setText("spin", matEd.autoSpin ? "Pause spin" : "Resume spin");
    ui.select("preview");
    ui.setText("preview-status",
               SHADING_MODELS[matEd.shadingModel] + " · M=" +
               format("%.2f", matEd.metallic) + " R=" + format("%.2f", matEd.roughness) +
               " · " + MESH_KINDS[matEd.meshKind]);
}

function handleUiEvents() {
    local click = ui.consumeClick();
    while (click != "") {
        local event = eventParts(click);
        local id = event[1];
        if (id == "spin") {
            matEd.autoSpin = !matEd.autoSpin;
            matEd.status = matEd.autoSpin ? "Turntable spinning" : "Turntable paused";
        } else if (id.find("preset-") == 0) {
            applyPreset(id.slice(7));
        }
        click = ui.consumeClick();
    }

    local changed = ui.consumeChange();
    while (changed != "") {
        local event = eventParts(changed);
        if (event[0] == "inspector") {
            ui.select("inspector");
            local id = event[1];
            if (id == "shading") matEd.shadingModel = ui.getValue("shading").tointeger();
            else if (id == "tint") {
                matEd.tintR = ui.getColorR("tint");
                matEd.tintG = ui.getColorG("tint");
                matEd.tintB = ui.getColorB("tint");
                matEd.tintA = ui.getColorA("tint");
            }
            else if (id == "metallic") matEd.metallic = ui.getValue("metallic");
            else if (id == "roughness") matEd.roughness = ui.getValue("roughness");
            else if (id == "surface") matEd.surfaceMode = ui.getValue("surface").tointeger();
            else if (id == "blend") matEd.blendMode = ui.getValue("blend").tointeger();
            else if (id == "alpha-cutoff") matEd.alphaCutoff = ui.getValue("alpha-cutoff");
            else if (id == "parallax") matEd.parallax = ui.getValue("parallax");
            else if (id == "depth-write") matEd.depthWrite = ui.getChecked("depth-write");
            else if (id == "double-sided") matEd.doubleSided = ui.getChecked("double-sided");
            else if (id == "receive-light") matEd.receiveLight = ui.getChecked("receive-light");
            else if (id == "cast-shadow") matEd.castShadow = ui.getChecked("cast-shadow");
            else if (id == "receive-shadow") matEd.receiveShadow = ui.getChecked("receive-shadow");
            else if (id == "mesh") {
                matEd.meshKind = ui.getValue("mesh").tointeger();
                rebuildPreviewMesh();
            }
            if (id != "mesh") {
                applyMaterial();
                matEd.status = "Live edit · " + id;
            }
        }
        changed = ui.consumeChange();
    }
}

function handleViewportCamera() {
    ui.select("preview");
    local hovered = ui.viewportHovered("mat-vp");
    local mouseX = hovered ? ui.viewportMouseX("mat-vp") : matEd.lastX;
    local mouseY = hovered ? ui.viewportMouseY("mat-vp") : matEd.lastY;
    if (hovered) {
        local wheel = ui.viewportWheel("mat-vp");
        if (wheel != 0.0) {
            if (wheel > 0.0) matEd.orbitDist = clamp(matEd.orbitDist / 1.1, 2.2, 12.0);
            else matEd.orbitDist = clamp(matEd.orbitDist * 1.1, 2.2, 12.0);
            updateOrbitCamera();
        }
    }
    local orbit = hovered && mouse.isDown(1);
    if (orbit && matEd.orbiting) {
        matEd.orbitYaw += (mouseX - matEd.lastX) * 0.01;
        matEd.orbitPitch = clamp(matEd.orbitPitch + (mouseY - matEd.lastY) * 0.008, 0.08, 1.35);
        updateOrbitCamera();
    }
    matEd.orbiting = orbit;
    matEd.lastX = mouseX;
    matEd.lastY = mouseY;
}

function attachStudioIbl(camera) {
    // Metals zero diffuse GI; without an env cubemap chrome/gold read as black disks.
    // Bake a cheap six-face studio sky (same spirit as RenderImageAudit::makeStudioCubemap).
    local probe = gfx.newReflectionProbeCapture();
    probe.configure(0.0, 0.5, 0.0, 32, 0.1, 40.0);
    probe.setSkyFaceColor(0, 0.86, 0.71, 0.55); // +X warm
    probe.setSkyFaceColor(1, 0.47, 0.59, 0.82); // -X cool
    probe.setSkyFaceColor(2, 0.96, 0.96, 0.98); // +Y bright
    probe.setSkyFaceColor(3, 0.16, 0.16, 0.18); // -Y floor
    probe.setSkyFaceColor(4, 0.78, 0.82, 0.90); // +Z
    probe.setSkyFaceColor(5, 0.71, 0.63, 0.55); // -Z
    probe.setCaptureMask(0); // sky only — cheap on Lavapipe
    probe.requestCapture();
    local guard = 0;
    while (!probe.isCaptureComplete() && guard < 24) {
        probe.update(6);
        guard += 1;
    }
    if (!probe.isCaptureComplete()) {
        print("material-editor: studio IBL capture incomplete\n");
        return null;
    }
    if (!probe.stageCapturedFaces()) {
        print("material-editor: studio IBL stage failed\n");
        return null;
    }
    if (!probe.filterAndPublish(32)) {
        print("material-editor: studio IBL filter failed\n");
        return null;
    }
    local cube = probe.getActiveCubemap();
    if (cube == null) {
        print("material-editor: studio IBL cubemap missing\n");
        return null;
    }
    camera.setEnvMap(cube);
    camera.setEnvIntensity(1.45);
    return probe;
}

function buildStudioLights() {
    matEd.keyLight = eve.Light3D();
    matEd.keyLight.setType("dir");
    matEd.keyLight.setDirection(-0.42, 0.88, 0.28);
    matEd.keyLight.setColor(1.0, 0.96, 0.90, 2.2);
    matEd.keyLight.setCastShadow(true);

    matEd.fillLight = eve.Light3D();
    matEd.fillLight.setType("point");
    matEd.fillLight.setPosition(-3.2, 2.4, 3.6);
    matEd.fillLight.setColor(0.55, 0.70, 1.0, 2.0);
    matEd.fillLight.setRadius(18.0);

    matEd.rimLight = eve.Light3D();
    matEd.rimLight.setType("point");
    matEd.rimLight.setPosition(3.4, 1.8, -2.8);
    matEd.rimLight.setColor(1.0, 0.72, 0.45, 1.6);
    matEd.rimLight.setRadius(16.0);
}

function buildStudioScene() {
    matEd.camera = eve.Camera3D();
    matEd.camera.setFov(38.0);
    matEd.camera.setAmbient(0.18, 0.19, 0.21);
    matEd.camera.setActive(true);
    updateOrbitCamera();
    matEd.probe = attachStudioIbl(matEd.camera);
    buildStudioLights();

    // Neutral studio floor so shadow / roughness response is readable.
    matEd.ground = eve.Renderable3D();
    matEd.ground.setMesh(gfx.newMeshCube(1.0));
    matEd.ground.setPosition(0.0, -1.05, 0.0);
    matEd.ground.setScale(8.0, 0.08, 8.0);
    matEd.ground.setTint(0.18, 0.19, 0.21, 1.0);
    matEd.ground.setMetallic(0.05);
    matEd.ground.setRoughness(0.88);
    matEd.ground.setCastShadow(false);
    matEd.ground.setReceiveShadow(true);

    matEd.material = gfx.newMaterial();
    rebuildPreviewMesh();
}

eve_init = function() {
    // Soft dark grey like the UE material editor preview backdrop.
    gfx.setBackgroundColor(0.12, 0.13, 0.145, 1.0);
    gfx.setDirectionalLight(-0.42, 0.88, 0.28, 1.55, 1.48, 1.35);
    configureWorkspace();
    buildStudioScene();
    ui.setTheme("dark");
    ui.setNavKeyboard(true);
    mountPanels();
    syncInspectorWidgets();
    syncLabels();
    local ibl = matEd.probe != null ? "ibl-on" : "ibl-off";
    print("material-editor: UE5 material sphere ready (" + ibl + ")\n");
};

eve_update = function(dt) {
    handleUiEvents();
    handleViewportCamera();
    if (matEd.autoSpin && matEd.ball != null) {
        matEd.yaw += dt * 0.55;
        matEd.ball.setYaw(matEd.yaw);
    }
    syncLabels();
};

eve_render = function() {
    gfx.clear();
    ui.select("preview");
    local canvas = ui.viewportCanvas("mat-vp");
    if (canvas != null) gfx.renderScene3DToCanvas(canvas, matEd.camera);
    ui.beginFrameAndRender();
    matEd.frame += 1;
    if (!matEd.screenshotSaved && matEd.frame > 24 && gfx.saveFramePng("material-editor.png")) {
        matEd.screenshotSaved = true;
        print("material-editor: saved material-editor.png\n");
    }
};
