// Reusable script-composed material studio. This file only declares types at boot;
// optional editor/graphics modules are first accessed when a studio is mounted.
class MaterialStudio {
    matEd = null;
    options = null;
    presets = null;
    panelBuilders = null;
    textureCache = null;
    schemaFields = null;
    selectedProperty = 0;
    mounted = false;
    previewError = "";
    previewWarning = "";
    SHADING_MODELS = null;
    SURFACE_MODES = null;
    BLEND_MODES = null;
    MESH_KINDS = null;
    previewScene = null;
    previewSettingsError = "";
    previewAA = null;
    previewCanvas = null;
    previewAAQuality = "high";
    constructor(settings = null) {
        options = { id="material.studio", title="Material Studio", width=1280, height=800,
                    path="material-studio-v9.json", screenshot="", antialiasing="high" };
        if (settings != null) foreach (key, value in settings) options[key] <- value;
        setAntialiasing(options.antialiasing);
        textureCache = {};
        schemaFields = [];
        SHADING_MODELS = ["pbr", "unlit", "hair", "custom"];
        SURFACE_MODES = ["opaque", "masked", "transparent"];
        BLEND_MODES = ["alpha", "premultiplied", "additive", "multiply"];
        MESH_KINDS = ["shaderball", "sphere", "cube", "cylinder", "torus"];
        previewScene = MaterialPreviewScene(options);
        matEd = {
    workspace = null,
    camera = null,
    ball = null,
    ground = null,
    material = null,
    probe = null,
    keyLight = null,
    fillLight = null,
    rimLight = null,
    meshKind = 0,
    environmentKind = 0,
    modelPath = "",
    skyboxPath = "",
    floorVisible = true,
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
    orbitYaw = -0.40,
    orbitPitch = 0.10,
    orbitDist = 4.6,
    orbiting = false,
    lastX = 0.0,
    lastY = 0.0,
    status = "Material Ball · choose a model and environment on the left",
    frame = 0,
    screenshotSaved = false,
    document = null,
    pendingEdit = false,
    savedJson = "",
    observedRevision = -1,
};
        presets = {
    plastic = { r=0.82, g=0.82, b=0.84, a=1.0, metallic=0.0, roughness=0.45 },
    rubber = { r=0.12, g=0.12, b=0.13, a=1.0, metallic=0.0, roughness=0.92 },
    brushed = { r=0.72, g=0.74, b=0.78, a=1.0, metallic=0.88, roughness=0.42 },
    chrome = { r=0.95, g=0.96, b=0.98, a=1.0, metallic=1.0, roughness=0.08 },
    gold = { r=1.0, g=0.72, b=0.22, a=1.0, metallic=1.0, roughness=0.22 },
    copper = { r=0.95, g=0.52, b=0.28, a=1.0, metallic=1.0, roughness=0.28 },
    glass = { r=0.72, g=0.88, b=0.95, a=0.35, metallic=0.0, roughness=0.06 },
};
        panelBuilders = {
            toolbar = function(studio) { studio.panelToolbar(); },
            preview = function(studio) { studio.panelPreview(); },
            inspector = function(studio) { studio.panelInspector(); },
            properties = function(studio) { studio.panelProperties(); },
            settings = function(studio) { studio.panelPreviewSettings(); },
        };
        if ("panels" in options) foreach (id, builder in options.panels) panelBuilders[id] <- builder;
        if ("previewModels" in options) foreach (entry in options.previewModels)
            previewScene.registerModel(entry.id,entry.label,entry.provider);
        if ("environments" in options) foreach (entry in options.environments)
            previewScene.registerEnvironment(entry.id,entry.label,entry.faces);
        if ("previewModel" in options) matEd.meshKind=previewScene.indexOf(previewScene.models,options.previewModel);

    }
function clamp(v, lo, hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

function eventParts(path) {
    local slash = path.find("/");
    return slash == null ? ["", path] : [path.slice(0, slash), path.slice(slash + 1)];
}


function controlsPatch() {
    return {
        ["shading.model"] = SHADING_MODELS[matEd.shadingModel],
        ["shading.tint"] = [matEd.tintR, matEd.tintG, matEd.tintB, matEd.tintA],
        ["shading.metallic"] = matEd.metallic, ["shading.roughness"] = matEd.roughness,
        ["surface.mode"] = SURFACE_MODES[matEd.surfaceMode],
        ["surface.blend"] = BLEND_MODES[matEd.blendMode],
        ["surface.alpha-cutoff"] = matEd.alphaCutoff, ["parallax.scale"] = matEd.parallax,
        ["surface.depth-write"] = matEd.depthWrite, ["surface.double-sided"] = matEd.doubleSided,
        ["lighting.receive"] = matEd.receiveLight, ["shadow.cast"] = matEd.castShadow,
        ["shadow.receive"] = matEd.receiveShadow,
    };
}

function syncFromDocument() {
    local p = checked(matEd.document.snapshot()).properties;
    matEd.shadingModel = SHADING_MODELS.find(p["shading.model"]);
    matEd.surfaceMode = SURFACE_MODES.find(p["surface.mode"]);
    matEd.blendMode = BLEND_MODES.find(p["surface.blend"]);
    local t = p["shading.tint"];
    matEd.tintR=t[0]; matEd.tintG=t[1]; matEd.tintB=t[2]; matEd.tintA=t[3];
    matEd.metallic=p["shading.metallic"]; matEd.roughness=p["shading.roughness"];
    matEd.alphaCutoff=p["surface.alpha-cutoff"]; matEd.parallax=p["parallax.scale"];
    matEd.depthWrite=p["surface.depth-write"]; matEd.doubleSided=p["surface.double-sided"];
    matEd.receiveLight=p["lighting.receive"]; matEd.castShadow=p["shadow.cast"];
    matEd.receiveShadow=p["shadow.receive"];
    matEd.observedRevision = matEd.document.getRevision();
}

function applyMaterial(commit = true) {
    if (commit && matEd.observedRevision != matEd.document.getRevision()) {
        matEd.pendingEdit = false;
        syncFromDocument();
        syncInspectorWidgets();
        applyMaterial(false);
        matEd.status = "Material changed during editing; draft discarded";
        return;
    }
    if (commit) {
        local result = matEd.document.patch(controlsPatch());
        if (!result.ok) {
            matEd.status = result.status.summary;
            syncFromDocument();
            syncInspectorWidgets();
            return;
        }
        matEd.pendingEdit = false;
        syncFromDocument();
    }
    try {
        publishPreview();
        previewError = "";
    } catch (error) {
        previewError = error.tostring();
    }
}

function publishPreview() {
    previewWarning = "";
    if (matEd.material == null) return;
    if ("applyPreview" in options) {
        options.applyPreview(this, checked(matEd.document.snapshot()));
        return;
    }
    local p = checked(matEd.document.snapshot()).properties;
    foreach (path, value in p) {
        if ((path.find("vegetation.") == 0 && path.find(".enabled") != null && value == true)
            || (path == "vegetation.motion.mode" && value != "disabled")
            || ((path == "textures.orm" || path == "textures.emissive") && value != ""))
            previewWarning = "Advanced values saved; this preview shows basic shading only";
    }
    local albedo = resolveTexture(p["textures.albedo"]);
    local normal = resolveTexture(p["textures.normal"]);
    local height = resolveTexture(p["textures.height"]);
    local shader = null;
    if (p["textures.shader"] != "") {
        if (!("resolveShader" in options)) throw "Custom shader preview requires options.resolveShader";
        shader = options.resolveShader(p["textures.shader"]);
        if (shader == null) throw "Shader could not be resolved: " + p["textures.shader"];
    }
    local candidate = gfx.newMaterial();
    local model = SHADING_MODELS[matEd.shadingModel];
    local surface = SURFACE_MODES[matEd.surfaceMode];
    local blend = BLEND_MODES[matEd.blendMode];
    candidate.setShadingModel(model);
    candidate.setTint(matEd.tintR, matEd.tintG, matEd.tintB, matEd.tintA);
    candidate.setMetallic(matEd.metallic);
    candidate.setRoughness(matEd.roughness);
    candidate.setSurfaceMode(surface);
    candidate.setBlendMode(blend);
    candidate.setAlphaCutoff(matEd.alphaCutoff);
    candidate.setParallax(matEd.parallax, 8.0, 24.0);
    if (albedo != null) candidate.setAlbedoTexture(albedo);
    if (normal != null) candidate.setNormalTexture(normal);
    if (height != null) candidate.setHeightTexture(height);
    if (shader != null) candidate.setShader(shader);
    candidate.setReceiveLight(matEd.receiveLight);
    candidate.setCastShadow(matEd.castShadow);
    candidate.setReceiveShadow(matEd.receiveShadow);
    candidate.setDoubleSided(matEd.doubleSided);
    candidate.setDepthWrite(matEd.depthWrite);
    if (matEd.ball != null) {
        matEd.ball.setMaterial(candidate);
        // Keep legacy MeshRenderer fields in sync for paths that still read them.
        matEd.ball.setTint(matEd.tintR, matEd.tintG, matEd.tintB, matEd.tintA);
        matEd.ball.setMetallic(matEd.metallic);
        matEd.ball.setRoughness(matEd.roughness);
        matEd.ball.setCastShadow(matEd.castShadow);
        matEd.ball.setReceiveShadow(matEd.receiveShadow);
    }
    foreach (entity in previewScene.modelEntities) {
        if (entity==matEd.ball) continue;
        entity.setMaterial(candidate);
        entity.setTint(matEd.tintR,matEd.tintG,matEd.tintB,matEd.tintA);
        entity.setMetallic(matEd.metallic); entity.setRoughness(matEd.roughness);
        entity.setCastShadow(matEd.castShadow); entity.setReceiveShadow(matEd.receiveShadow);
    }
    matEd.material = candidate;
}

function applyPreset(name) {
    if (!(name in presets)) return;
    if (matEd.pendingEdit) applyMaterial();
    syncFromDocument();
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

// Public preview operations throw on failure and preserve the active preview.
function selectPreviewModel(id) {
    if (matEd.pendingEdit) applyMaterial();
    previewScene.selectModel(id,matEd.material,matEd.yaw);
    matEd.meshKind=previewScene.indexOf(previewScene.models,id);
    matEd.ball=previewScene.modelEntities[0];
    syncFromDocument(); applyMaterial(false);
    matEd.status="Preview model · "+previewScene.models[matEd.meshKind].label;
    previewSettingsError="";
    syncPreviewSelection();
}
function loadPreviewModel(path) {
    local id=previewScene.importModel(path);
    selectPreviewModel(id); matEd.modelPath=path;
    if (mounted) rebuildPreviewSettings();
}
function registerPreviewModel(id,label,provider) {
    previewScene.registerModel(id,label,provider);
    if (mounted) rebuildPreviewSettings();
}
function selectEnvironment(id) {
    previewScene.selectEnvironment(id,matEd.camera);
    matEd.environmentKind=previewScene.indexOf(previewScene.environments,id);
    matEd.probe=previewScene.environmentCache[id].probe;
    previewSettingsError="";
    matEd.status="Environment · "+previewScene.environments[matEd.environmentKind].label;
    syncPreviewSelection();
}
function syncPreviewSelection() {
    if (!mounted) return;
    ui.select("settings");
    ui.setValue("preview-model",matEd.meshKind.tofloat());
    ui.setValue("environment",matEd.environmentKind.tofloat());
    ui.setText("settings-status",previewSettingsError);
}
function registerEnvironment(id,label,faces) {
    previewScene.registerEnvironment(id,label,faces);
    if (mounted) rebuildPreviewSettings();
}
function loadSkybox(folder) {
    if (folder=="") throw "Enter a skybox folder";
    local faces=[];
    foreach (name in ["px","nx","py","ny","pz","nz"]) faces.append(folder+"/"+name+".png");
    local id="skybox:"+folder;
    local asset={id=id,label="Custom: "+folder,faces=faces};
    // Validate and prepare all faces before adding the selection to the palette.
    local exists=false;
    foreach (entry in previewScene.environments) if (entry.id==id) exists=true;
    if (!exists) previewScene.environments.append(asset);
    try { selectEnvironment(id); }
    catch (error) {
        if (!exists) previewScene.environments.pop();
        throw error;
    }
    matEd.skyboxPath=folder;
    if (mounted) rebuildPreviewSettings();
}
function getPreviewRenderables() { return previewScene.modelEntities.slice(0); }
function rebuildPreviewMesh() { selectPreviewModel(previewScene.models[matEd.meshKind].id); }

// Preview-only quality: high = 2x per axis, ultra = 4x per axis.
// Does not change the material document or introduce temporal history/ghosting.
function setAntialiasing(quality) {
    if (quality != "off" && quality != "high" && quality != "ultra")
        throw "Antialiasing must be off, high, or ultra";
    previewAAQuality = quality;
    if (previewAA != null) previewAA.setQuality(quality == "ultra" ? "high" : "medium");
    if (mounted) {
        ui.select("settings");
        ui.setValue("antialiasing", quality == "off" ? 0.0 : (quality == "high" ? 1.0 : 2.0));
    }
}
function renderPreview(canvas) {
    if (previewAAQuality == "off") {
        gfx.renderScene3DWithShadowsToCanvas(canvas, matEd.camera);
        return;
    }
    if (previewAA == null) {
        previewAA = gfx.newAntiAliasing();
        previewAA.setMode("ssaa");
        previewAA.setQuality(previewAAQuality == "ultra" ? "high" : "medium");
    }
    // UI owns the destination; retain the supersampled source across frames.
    local width = previewAA.resolutionFor(canvas.getWidth());
    local height = previewAA.resolutionFor(canvas.getHeight());
    if (previewCanvas == null || previewCanvas.getWidth() != width || previewCanvas.getHeight() != height)
        previewCanvas = gfx.newCanvas(width, height);
    gfx.renderScene3DWithShadowsToCanvas(previewCanvas, matEd.camera);
    previewAA.applyCanvasTo(gfx, previewCanvas, canvas);
}

function panelPreviewSettings() {
    ui.combo("AA", "Off\nHigh · SSAA 2x\nUltra · SSAA 4x",
        previewAAQuality == "off" ? 0 : (previewAAQuality == "high" ? 1 : 2), "antialiasing");
    ui.beginRow("model-heading", 8.0); ui.icon("cube", "model-icon");
    ui.text("Model", "preview-model-title"); ui.end();
    ui.combo("##preview-model",previewScene.labels(previewScene.models),matEd.meshKind,"preview-model");
    ui.setItemAccessibility("list","Preview model","");
    ui.inputText("Path",matEd.modelPath,"model-path");
    ui.setItemTooltip("Project-relative GLB, glTF, OBJ or FBX path");
    ui.beginRow("model-actions", 8.0);
    iconAction("folder-open","load-model","Load preview model from the path above");
    helpIcon("model-help","GLB / glTF / OBJ / FBX. All mesh parts share the edited material.");
    ui.end();
    ui.separator("preview-settings-separator");
    ui.beginRow("environment-heading",8.0); ui.icon("image","environment-icon");
    ui.text("Environment", "environment-title"); ui.end();
    ui.combo("Skybox",previewScene.labels(previewScene.environments),matEd.environmentKind,"environment");
    ui.slider("IBL",previewScene.intensity,0.0,3.0,"environment-intensity");
    ui.beginRow("visibility-controls",10.0);
    ui.icon("image","sky-visible-icon"); ui.setItemTooltip("Skybox visibility");
    ui.checkbox("##sky",previewScene.visibleSky,"sky-visible"); ui.setItemSize(26.0,26.0);
    ui.setItemTooltip("Show skybox"); ui.setItemAccessibility("checkbox","Show skybox","");
    ui.icon("layers","floor-visible-icon"); ui.setItemTooltip("Ground visibility");
    ui.checkbox("##floor",matEd.floorVisible,"floor-visible"); ui.setItemSize(26.0,26.0);
    ui.setItemTooltip("Show floor"); ui.setItemAccessibility("checkbox","Show floor","");
    ui.end();
    ui.inputText("Folder",matEd.skyboxPath,"skybox-path");
    ui.setItemTooltip("Project-relative folder containing px, nx, py, ny, pz, nz PNG faces");
    ui.beginRow("sky-actions",8.0);
    iconAction("folder-open","load-skybox","Load skybox from the folder above");
    helpIcon("sky-help","Six PNG faces: px, nx, py, ny, pz, nz. Background and reflections use the same environment.");
    ui.end();
    ui.textWrapped(previewSettingsError,205.0,"settings-status");
}
function rebuildPreviewSettings() {
    ui.beginBuild(); ui.beginWindow("Preview Settings","root");
    panelBuilders.settings(this); ui.end(); ui.mountBuildAs("settings"); ui.select("settings");
    ui.setHostPos(matEd.workspace.getRegionX("left"),matEd.workspace.getRegionY("left"),0.0,0.0);
    ui.setHostSize(matEd.workspace.getRegionW("left"),matEd.workspace.getRegionH("left"));
    ui.setHostOverlay(false);
}
function handlePreviewSettings(id) {
    ui.select("settings");
    try {
        if (id=="antialiasing") setAntialiasing(["off","high","ultra"][ui.getValue(id).tointeger()]);
        else if (id=="load-model") loadPreviewModel(ui.getValueText("model-path"));
        else if (id=="preview-model") selectPreviewModel(previewScene.models[ui.getValue("preview-model").tointeger()].id);
        else if (id=="environment") selectEnvironment(previewScene.environments[ui.getValue("environment").tointeger()].id);
        else if (id=="environment-intensity") previewScene.setIntensity(ui.getValue(id),matEd.camera);
        else if (id=="sky-visible") previewScene.setSkyVisible(ui.getChecked(id));
        else if (id=="floor-visible") {
            matEd.floorVisible=ui.getChecked(id); matEd.ground.setVisible(matEd.floorVisible);
        }
        else if (id=="load-skybox") loadSkybox(ui.getValueText("skybox-path"));
        previewSettingsError="";
    } catch (error) {
        previewSettingsError=error.tostring();
        ui.select("settings");
        ui.setValue("preview-model",matEd.meshKind.tofloat());
        ui.setValue("environment",matEd.environmentKind.tofloat());
    }
    ui.select("settings"); ui.setText("settings-status",previewSettingsError);
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
    previewScene.syncSky(matEd.camera);
}

function configureWorkspace() {
    matEd.workspace = editor.newWorkspace(options.id, options.title);
    matEd.workspace.setRegionSize("top", 76.0);
    matEd.workspace.setRegionSize("right", 320.0);
    matEd.workspace.setRegionSize("left", 240.0);
    matEd.workspace.setRegionSize("bottom", 0.0);
    matEd.workspace.layout(options.width.tofloat(), options.height.tofloat());
    checked(matEd.document.configureWorkspace(matEd.workspace));
    matEd.workspace.setRegionSize("bottom", 144.0);
    if (!matEd.workspace.registerPanel("properties", "Schema Properties", "bottom", 30))
        throw "Could not compose material properties panel";
    if (!matEd.workspace.registerPanel("settings","Preview Settings","left",5))
        throw "Could not compose preview settings";
    matEd.workspace.layout(options.width.tofloat(), options.height.tofloat());
}

// Semantic icons retain stable action ids, hover help and accessible names.
function iconAction(icon, id, description) {
    ui.iconButton(icon, "", id);
    ui.setItemSize(30.0, 30.0);
    ui.setItemTooltip(description);
    ui.setItemAccessibility("button", description, description);
}
function helpIcon(id, description) {
    ui.icon("question", id);
    ui.setItemTooltip(description);
    ui.setItemAccessibility("text", "Help", description);
}
function panelToolbar() {
    ui.beginRow("toolbar-row", 6.0);
    foreach (name in ["plastic","rubber","brushed","chrome","gold","copper","glass"]) {
        local p=presets[name], style=options.id+".swatch."+name;
        foreach (status in [ui.defineStyleClass(style,""),
                            ui.setStyleClassColor(style,"background",p.r,p.g,p.b,1.0),
                            ui.setStyleClassColor(style,"text",p.r+p.g+p.b>1.5 ? 0.12 : 0.95,
                                p.r+p.g+p.b>1.5 ? 0.12 : 0.95,p.r+p.g+p.b>1.5 ? 0.12 : 0.95,1.0)])
            if (status!="applied") throw "Material swatch style: "+status;
        iconAction("paint-brush","preset-"+name,"Apply "+name+" material");
        ui.setItemStyleClass(style);
    }
    ui.separator("toolbar-sep");
    iconAction("undo", "undo", "Undo material edit");
    iconAction("redo", "redo", "Redo material edit");
    iconAction("save", "save", "Save material");
    iconAction("folder-open", "load", "Load material");
    iconAction("pause", "spin", "Pause turntable");
    iconAction("play", "spin-play", "Resume turntable");
    ui.text("", "toolbar-status");
    ui.end();
}

function panelPreview() {
    ui.beginRow("preview-tools", 10.0);
    ui.icon("move", "orbit-help");
    ui.setItemTooltip("Drag with the left mouse button to orbit");
    ui.icon("search", "zoom-help");
    ui.setItemTooltip("Scroll to zoom");
    helpIcon("preview-help", "Drag to orbit; scroll to zoom. Use the play/pause toolbar button for the turntable.");
    ui.end();
    ui.viewport("mat-vp", matEd.workspace.getRegionW("center") - 20.0,
                matEd.workspace.getRegionH("center") - 58.0);
    ui.text("", "preview-status");
}

function panelInspector() {
    ui.text("Shading", "shading-title");
    ui.combo("Model", "pbr\nunlit\nhair\ncustom", matEd.shadingModel, "shading");
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
    helpIcon("inspector-help", "Parameters edit the material document and update the preview. Model and skybox controls are on the left.");
}


function mountPanels() {
    for (local i = 0; i < matEd.workspace.getPanelCount(); ++i) {
        local id = matEd.workspace.getPanelId(i);
        if (!(id in panelBuilders)) continue;
        ui.beginBuild();
        ui.beginWindow(matEd.workspace.getPanelTitle(i), "root");
        panelBuilders[id](this);
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

}

function syncLabels() {
    ui.select("toolbar");
    ui.setText("toolbar-status", previewError != "" ? "Preview: " + previewError
        : (previewWarning != "" ? previewWarning : matEd.status));
    ui.setVisible("spin", matEd.autoSpin);
    ui.setVisible("spin-play", !matEd.autoSpin);
    ui.setEnabled("undo", matEd.document.canUndo());
    ui.setEnabled("redo", matEd.document.canRedo());
    ui.select("preview");
    ui.setText("preview-status", previewError);
}

function handleUiEvents() {
    local click = ui.consumeClick();
    while (click != "") {
        local event = eventParts(click);
        local id = event[1];
        if (event[0] == "settings") {
            handlePreviewSettings(id);
        } else if (id == "undo" || id == "redo") {
            if (matEd.pendingEdit) applyMaterial();
            local result = id == "undo" ? matEd.document.undo() : matEd.document.redo();
            matEd.status = result.ok ? id + " applied" : result.status.summary;
            syncFromDocument(); applyMaterial(false); syncInspectorWidgets(); rebuildProperties();
        } else if (id == "save") {
            local json = checked(matEd.document.toJson());
            if ("save" in options) options.save(json);
            else {
                if (!fs.writeTextAtomic(options.path, json)) {
                    matEd.status = "Save failed: " + options.path;
                    click = ui.consumeClick();
                    continue;
                }
            }
            matEd.savedJson = json;
            matEd.status = "Saved " + options.path;
        } else if (id == "load") {
            local json = ("load" in options) ? options.load() : fs.readText(options.path);
            local result = matEd.document.fromJson(json);
            matEd.status = result.ok ? "Material loaded" : result.status.summary;
            if (result.ok) {
                matEd.savedJson = checked(matEd.document.toJson());
                syncFromDocument(); applyMaterial(false); syncInspectorWidgets(); rebuildProperties();
            }
        } else if (id == "reset-property") {
            local field = schemaFields[selectedProperty];
            local result = matEd.document.set(field.path, field.defaultValue);
            matEd.status = result.ok ? "Reset " + field.path : result.status.summary;
            syncFromDocument(); applyMaterial(false); syncInspectorWidgets(); rebuildProperties();
        } else if (id == "spin" || id == "spin-play") {
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
                matEd.pendingEdit = mouse.isDown(1);
                applyMaterial(!matEd.pendingEdit);
                matEd.status = "Live edit · " + id;
            }
        }
        if (event[0] == "settings") handlePreviewSettings(event[1]);
        if (event[0] == "properties") handlePropertyEvent(event[1]);
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


function buildStudioLights() {
    matEd.keyLight = eve.Light3D();
    matEd.keyLight.setType("dir");
    matEd.keyLight.setDirection(-0.42, 0.88, 0.28);
    matEd.keyLight.setColor(1.0, 0.96, 0.90, 0.55);
    matEd.keyLight.setCastShadow(true);
    matEd.keyLight.setShadowMethod("csm");
    matEd.keyLight.setShadowStrength(0.88);

    matEd.fillLight = eve.Light3D();
    matEd.fillLight.setType("point");
    matEd.fillLight.setPosition(-3.2, 2.4, 3.6);
    matEd.fillLight.setColor(0.55, 0.70, 1.0, 0.30);
    matEd.fillLight.setRadius(18.0);

    matEd.rimLight = eve.Light3D();
    matEd.rimLight.setType("point");
    matEd.rimLight.setPosition(3.4, 1.8, -2.8);
    matEd.rimLight.setColor(1.0, 0.72, 0.45, 0.35);
    matEd.rimLight.setRadius(16.0);
}

function buildStudioScene() {
    matEd.camera = eve.Camera3D();
    matEd.camera.setFov(38.0);
    matEd.camera.setClipPlanes(0.1, 80.0);
    matEd.camera.setAmbient(0.08, 0.085, 0.09);
    matEd.camera.setActive(true);
    updateOrbitCamera();
    selectEnvironment(("environment" in options) ? options.environment : "studio");
    buildStudioLights();

    // Neutral studio floor so shadow / roughness response is readable.
    matEd.ground = eve.Renderable3D();
    matEd.ground.setMesh(gfx.newMeshCylinder(128, 1, true));
    matEd.ground.setPosition(0.0, -1.25, 0.0);
    matEd.ground.setScale(2.3, 0.04, 2.3);
    matEd.ground.setTint(0.18, 0.19, 0.21, 1.0);
    matEd.ground.setMetallic(0.05);
    matEd.ground.setRoughness(0.88);
    matEd.ground.setCastShadow(false);
    matEd.ground.setReceiveShadow(true);

    matEd.material = gfx.newMaterial();
    rebuildPreviewMesh();
}

function mount() {
    if (mounted) throw "MaterialStudio is already mounted";
    foreach (slot in ["materialEditor", "editor", "gfx", "ui"])
        if (!has_module(slot)) throw "MaterialStudio requires module: " + slot;
    // Soft dark grey like the UE material editor preview backdrop.
    matEd.document = checked(materialEditor.create(options.id));
    schemaFields = checked(matEd.document.schema()).properties;
    matEd.savedJson = checked(matEd.document.toJson());
    syncFromDocument();
    gfx.setBackgroundColor(0.12, 0.13, 0.145, 1.0);
    gfx.setDirectionalLight(-0.42, 0.88, 0.28, 0.35, 0.33, 0.30);
    configureWorkspace();
    buildStudioScene();
    ui.setTheme("dark");
    ui.setNavKeyboard(true);
    mountPanels();
    syncInspectorWidgets();
    syncLabels();
    local ibl = matEd.probe != null ? "ibl-on" : "ibl-off";
    mounted = true;
    print("material-editor: material studio ready (" + ibl + ")\n");
}

function update(dt) {
    handleUiEvents();
    if (matEd.pendingEdit && !mouse.isDown(1)) applyMaterial();
    if (!matEd.pendingEdit && matEd.observedRevision != matEd.document.getRevision()) {
        syncFromDocument(); applyMaterial(false); syncInspectorWidgets(); rebuildProperties();
    }
    handleViewportCamera();
    if (matEd.autoSpin && matEd.ball != null) {
        matEd.yaw += dt * 0.55;
        previewScene.setYaw(matEd.yaw);
    }
    syncLabels();
}

function render() {
    gfx.clear();
    ui.select("preview");
    local canvas = ui.viewportCanvas("mat-vp");
    if (canvas != null) renderPreview(canvas);
    ui.beginFrameAndRender();
    matEd.frame += 1;
    if (options.screenshot != "" && !matEd.screenshotSaved && matEd.frame > 24 && gfx.saveFramePng(options.screenshot)) {
        matEd.screenshotSaved = true;
        print("material-editor: saved material-editor.png\n");
    }
}

function checked(result) {
    if (!result.ok) throw result.status.summary;
    return result.value;
}

function resolveTexture(asset) {
    if (asset == "") return null;
    if (!(asset in textureCache)) {
        textureCache[asset] <- ("resolveTexture" in options)
            ? options.resolveTexture(asset) : gfx.newTextureFromFile(asset);
        if (textureCache[asset] == null) throw "Texture could not be resolved: " + asset;
    }
    return textureCache[asset];
}

function panelProperties() {
    local labels = "";
    foreach (i, field in schemaFields) labels += (i == 0 ? "" : "\n") + field.path;
    ui.combo("Property", labels, selectedProperty, "property");
    local field = schemaFields[selectedProperty];
    local value = checked(matEd.document.read(field.path));
    if (typeof value == "bool") ui.checkbox(field.path, value, "field");
    else if (field.choices.len() > 0) {
        local choices = "";
        foreach (i, choice in field.choices) choices += (i == 0 ? "" : "\n") + choice;
        ui.combo(field.path, choices, field.choices.find(value), "field");
    } else if (typeof value == "float" || typeof value == "integer") {
        local lo = ("minimum" in field) ? field.minimum : 0.0;
        local hi = ("maximum" in field) ? field.maximum : 1.0;
        ui.slider(field.path, value.tofloat(), lo, hi, "field");
    } else if (typeof value == "array" && field.type == 5)
        ui.colorPalette(field.path, value[0], value[1], value[2], value[3], "field");
    else if (typeof value == "array") {
        ui.beginRow("vector-row", 8.0);
        foreach (i, component in value) ui.inputText("" + i, component.tostring(), "vector-" + i);
        ui.end();
    }
    else if (typeof value == "string") ui.inputText(field.path, value, "field");
    else ui.text("Use document.set(path, value) for this structured property", "field-help");
    ui.beginRow("schema-actions",8.0);
    iconAction("refresh","reset-property","Reset the selected property to its default");
    helpIcon("schema-help","Choose a property, edit its value, or reset it. All changes support undo.");
    ui.end();
}

function rebuildProperties() {
    ui.beginBuild(); ui.beginWindow("Schema Properties", "root");
    panelBuilders.properties(this); ui.end(); ui.mountBuildAs("properties"); ui.select("properties");
    ui.setHostPos(matEd.workspace.getRegionX("bottom"), matEd.workspace.getRegionY("bottom"), 0.0, 0.0);
    ui.setHostSize(matEd.workspace.getRegionW("bottom"), matEd.workspace.getRegionH("bottom"));
    ui.setHostOverlay(false);
}

function handlePropertyEvent(id) {
    ui.select("properties");
    if (id == "property") {
        selectedProperty = ui.getValue("property").tointeger();
        rebuildProperties(); return;
    }
    if (id != "field" && id.find("vector-") != 0) return;
    local field = schemaFields[selectedProperty];
    local old = checked(matEd.document.read(field.path));
    local value = null;
    if (typeof old == "bool") value = ui.getChecked("field");
    else if (field.choices.len() > 0) value = field.choices[ui.getValue("field").tointeger()];
    else if (typeof old == "float") value = ui.getValue("field");
    else if (typeof old == "integer") value = ui.getValue("field").tointeger();
    else if (typeof old == "string") value = ui.getValueText("field");
    else if (typeof old == "array" && field.type == 5)
        value = [ui.getColorR("field"), ui.getColorG("field"), ui.getColorB("field"), ui.getColorA("field")];
    else if (typeof old == "array") {
        value = clone old;
        try {
            local component = id.slice(7).tointeger();
            value[component] = ui.getValueText(id).tofloat();
        } catch (error) {
            matEd.status = "Invalid vector component"; return;
        }
    }
    else return;
    local result = matEd.document.set(field.path, value);
    matEd.status = result.ok ? "Edited " + field.path : result.status.summary;
    syncFromDocument(); applyMaterial(false); syncInspectorWidgets();
}

// The native document is the source of truth; preview controls are a temporary gesture draft.
function getDocument() { return matEd.document; }
function getWorkspace() { return matEd.workspace; }
function getMaterial() { return matEd.material; }
function resize(width, height) {
    options.width = width; options.height = height;
    matEd.workspace.layout(width.tofloat(), height.tofloat());
    mountPanels(); syncInspectorWidgets();
}

}

function createMaterialStudio(options = null) {
    local studio = MaterialStudio(options);
    studio.mount();
    return studio;
}
