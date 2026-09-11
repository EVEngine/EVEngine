// Bush Lab — a live parameter editor for the native `mesh.bush` recipe.

persist bushSeed = 20260815
persist bushStyle = "mound"
persist bushLeafMode = "mixed"
persist bushHeight = 1.75
persist bushWidth = 2.65
persist bushBlobs = 18
persist bushDensity = 0.86
persist bushLobeScale = 0.66
persist bushIrregularity = 0.72
persist bushLeafSize = 0.25
persist bushTwigs = 7
persist bushTwigLength = 0.48
persist bushMesh = null
persist bushObject = null
persist bushTexture = null
persist bushCamera = null
persist bushYaw = 0.0
persist bushUiReady = false
persist bushFrame = 0
persist bushScreenshotSaved = false

function loadBushTexture() {
    return gfx.newTextureFromFile("assets/bush_atlas.png");
}

function updateStatus(message) {
    if (!bushUiReady) return;
    ui.select("lab");
    ui.setText("status", message);
}

function rebuildBush() {
    local paramsResult = procgen.newParams();
    if (!paramsResult.ok) {
        updateStatus("Parameter creation failed: " + paramsResult.status.summary);
        return;
    }
    local p = paramsResult.value;
    p.setSeed(bushSeed);
    p.setString("style", bushStyle);
    p.setString("leafMode", bushLeafMode);
    p.setFloat("height", bushHeight);
    p.setFloat("width", bushWidth);
    p.setInt("blobs", bushBlobs);
    p.setFloat("leafDensity", bushDensity);
    p.setFloat("lobeScale", bushLobeScale);
    p.setFloat("irregularity", bushIrregularity);
    p.setInt("rings", 5);
    p.setInt("radialSegments", 10);
    p.setFloat("leafSize", bushLeafSize);
    p.setInt("twigs", bushTwigs);
    p.setFloat("twigLength", bushTwigLength);

    local meshResult = procgen.generateMesh("mesh.bush", p, gfx);
    if (!meshResult.ok) {
        updateStatus("Generation failed: " + meshResult.status.summary);
        return;
    }
    bushMesh = meshResult.value;
    if (bushObject == null) {
        bushObject = eve.Renderable3D();
        bushObject.setPosition(0.0, -0.82, 0.0);
        bushObject.setTint(1.0, 1.0, 1.0, 1.0);
        bushObject.setRoughness(0.92);
        bushObject.setCastShadow(true);
        bushTexture = loadBushTexture();
        if (bushTexture != null) bushObject.setTexture(bushTexture);
    }
    bushObject.setMesh(bushMesh);
    updateStatus(format("seed %d  |  %s / %s  |  %d lobes", bushSeed, bushStyle, bushLeafMode, bushBlobs));
}

function buildBushUi() {
    ui.setScale(1.0);
    ui.setTheme("dark");
    ui.beginBuild();
    ui.beginWindow("Procedural Bush Lab", "root");
    ui.text("PROCEDURAL BUSH LAB", "title");
    ui.text("Every control rebuilds the native mesh immediately.", "help");
    ui.beginRow("workspace", 18.0);
    ui.beginColumn("controls", 5.0);
    ui.combo("Silhouette", "mound\nsphere", 0, "style");
    ui.combo("Foliage", "mixed\ncards\nblobs\nnone", 0, "leaf-mode");
    ui.slider("Height", bushHeight, 0.6, 3.2, "height");
    ui.slider("Width", bushWidth, 0.8, 4.2, "width");
    ui.slider("Foliage lobes", bushBlobs.tofloat(), 4.0, 32.0, "blobs");
    ui.slider("Lobe scale", bushLobeScale, 0.35, 1.1, "lobe-scale");
    ui.slider("Irregularity", bushIrregularity, 0.0, 1.0, "irregularity");
    ui.slider("Loose leaf density", bushDensity, 0.0, 1.0, "density");
    ui.slider("Leaf size", bushLeafSize, 0.08, 0.55, "leaf-size");
    ui.slider("Twig count", bushTwigs.tofloat(), 0.0, 16.0, "twigs");
    ui.slider("Twig length", bushTwigLength, 0.12, 0.9, "twig-length");
    ui.beginRow("actions", 8.0);
    ui.button("New seed", "new-seed");
    ui.button("Reset", "reset");
    ui.end();
    ui.text("", "status");
    ui.setItemSize(250.0, 0.0);
    ui.end();
    ui.beginColumn("preview-column", 4.0);
    ui.text("Drag parameters to compare silhouette, structure and foliage.", "preview-help");
    ui.viewport("preview", 490.0, 500.0);
    ui.end();
    ui.end();
    ui.end();
    ui.mountBuildAs("lab");
    ui.select("lab");
    ui.setHostPos(0.0, 0.0, 0.0, 0.0);
    ui.setHostSize(config.width.tofloat(), config.height.tofloat());
    ui.setHostOverlay(false);
    bushUiReady = true;
}

function resetBushParameters() {
    bushStyle = "mound"; bushLeafMode = "mixed";
    bushHeight = 1.75; bushWidth = 2.65; bushBlobs = 18;
    bushDensity = 0.86; bushLobeScale = 0.66; bushIrregularity = 0.72;
    bushLeafSize = 0.25; bushTwigs = 7; bushTwigLength = 0.48;
    ui.setValue("height", bushHeight); ui.setValue("width", bushWidth);
    ui.setValue("blobs", bushBlobs.tofloat()); ui.setValue("lobe-scale", bushLobeScale);
    ui.setValue("irregularity", bushIrregularity); ui.setValue("density", bushDensity);
    ui.setValue("leaf-size", bushLeafSize); ui.setValue("twigs", bushTwigs.tofloat());
    ui.setValue("twig-length", bushTwigLength);
    ui.setValue("style", 0.0); ui.setValue("leaf-mode", 0.0);
}

function handleBushUi() {
    ui.select("lab");
    local click = ui.consumeClick();
    while (click != "") {
        local slash = click.find("/");
        local id = slash == null ? click : click.slice(slash + 1);
        if (id == "new-seed") { bushSeed += 1; rebuildBush(); }
        else if (id == "reset") { resetBushParameters(); rebuildBush(); }
        click = ui.consumeClick();
    }
    local changed = ui.consumeChange();
    while (changed != "") {
        local slash = changed.find("/");
        local id = slash == null ? changed : changed.slice(slash + 1);
        if (id == "style") bushStyle = ui.getValueText(id);
        else if (id == "leaf-mode") bushLeafMode = ui.getValueText(id);
        else if (id == "height") bushHeight = ui.getValue(id);
        else if (id == "width") bushWidth = ui.getValue(id);
        else if (id == "blobs") bushBlobs = ui.getValue(id).tointeger();
        else if (id == "lobe-scale") bushLobeScale = ui.getValue(id);
        else if (id == "irregularity") bushIrregularity = ui.getValue(id);
        else if (id == "density") bushDensity = ui.getValue(id);
        else if (id == "leaf-size") bushLeafSize = ui.getValue(id);
        else if (id == "twigs") bushTwigs = ui.getValue(id).tointeger();
        else if (id == "twig-length") bushTwigLength = ui.getValue(id);
        rebuildBush();
        changed = ui.consumeChange();
    }
}

eve_init = function() {
    buildBushUi();
    bushCamera = eve.Camera3D();
    bushCamera.setEye(4.8, 2.5, 5.6);
    bushCamera.setTarget(0.0, 0.15, 0.0);
    bushCamera.setUp(0.0, 1.0, 0.0);
    bushCamera.setFov(35.0);
    bushCamera.setAmbient(0.28, 0.34, 0.27);
    bushCamera.setActive(true);
    gfx.setDirectionalLight(-0.45, -1.0, -0.35, 1.38, 1.26, 1.02);
    gfx.setBackgroundColor(0.055, 0.075, 0.07, 1.0);
    rebuildBush();
};

eve_update = function(dt) {
    handleBushUi();
    bushYaw += dt * 0.34;
    if (bushObject != null) bushObject.setYaw(bushYaw);
};

eve_render = function() {
    gfx.clear();
    ui.select("lab");
    local canvas = ui.viewportCanvas("preview");
    if (canvas != null) gfx.renderScene3DToCanvas(canvas, bushCamera);
    ui.beginFrameAndRender();
    bushFrame += 1;
    if (!bushScreenshotSaved && bushFrame > 20 && gfx.saveFramePng("bush-generator.png")) {
        bushScreenshotSaved = true;
        print("bush-generator: saved bush-generator.png\n");
    }
};
