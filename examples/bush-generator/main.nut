// Bush Lab — interactive showcase for the procedural `mesh.bush` recipe.
// R regenerate seed, 1/2 mound/sphere, L leafMode, C cards toggle, [/] density, T texture.
// The default texture is derived from Kenney's CC0 mini-forest colormap
// (assets/bush_atlas.png); press T to reload it from disk at any time.

persist bushSeed = 20260815
persist bushStyle = "mound"
persist bushLeafMode = "mixed"
persist bushDensity = 0.62
persist bushMesh = null
persist bushObject = null
persist bushTexture = null
persist bushCamera = null
persist bushYaw = 0.0
function pressed(k) {
    return key_just_pressed(k);
}

function loadBushTexture() {
    // Derived from Kenney's CC0 mini-forest palette: left half is twig bark,
    // right half is foliage. See assets/bush_atlas.png and assets/kenney/.
    return gfx.newTextureFromFile("assets/bush_atlas.png");
}

function rebuildBush() {
    local paramsResult = procgen.newParams();
    if (!paramsResult.ok) {
        ui.select("lab");
        ui.setText("status", "Parameter creation failed: " + paramsResult.status.summary);
        return;
    }
    local p = paramsResult.value;
    p.setSeed(bushSeed);
    p.setString("style", bushStyle);
    p.setString("leafMode", bushLeafMode);
    p.setFloat("leafDensity", bushDensity);
    p.setFloat("height", 1.7);
    p.setFloat("width", 2.6);
    p.setInt("blobs", 12);
    p.setInt("rings", 5);
    p.setInt("radialSegments", 12);
    p.setFloat("leafSize", 0.32);
    p.setInt("twigs", 6);
    local meshResult = procgen.generateMesh("mesh.bush", p, gfx);
    if (!meshResult.ok) {
        ui.select("lab");
        ui.setText("status", "Generation failed: " + meshResult.status.summary);
        return;
    }
    bushMesh = meshResult.value;
    if (bushObject == null) {
        bushObject = eve.Renderable3D();
        bushObject.setPosition(0.0, 0.0, 0.0);
        bushObject.setTint(1.0, 1.0, 1.0, 1.0);
        bushObject.setRoughness(0.9);
        bushObject.setCastShadow(true);
        if (bushTexture == null) bushTexture = loadBushTexture();
        if (bushTexture != null) bushObject.setTexture(bushTexture);
    }
    bushObject.setMesh(bushMesh);
}

if (bushCamera == null) {
    bushCamera = eve.Camera3D();
    bushCamera.setEye(5.4, 3.0, 6.0);
    bushCamera.setTarget(0.6, 0.0, 0.0);
    bushCamera.setUp(0.0, 1.0, 0.0);
    bushCamera.setFov(38.0);
    bushCamera.setAmbient(0.34, 0.40, 0.32);
    bushCamera.setActive(true);
    gfx.setDirectionalLight(-0.45, -1.0, -0.35, 1.35, 1.24, 1.02);
}
if (bushMesh == null) rebuildBush();
gfx.setBackgroundColor(0.055, 0.075, 0.07, 1.0);

function eve_update(dt) {
    bushYaw += dt * 1.0;
    bushObject.setYaw(bushYaw);
    if (pressed("r") || pressed("R")) { bushSeed += 1; rebuildBush(); }
    if (pressed("1")) { bushStyle = "mound"; rebuildBush(); }
    if (pressed("2")) { bushStyle = "sphere"; rebuildBush(); }
    if (pressed("l") || pressed("L")) {
        bushLeafMode = bushLeafMode == "mixed" ? "blobs" :
                       bushLeafMode == "blobs" ? "cards" :
                       bushLeafMode == "cards" ? "none" : "mixed";
        rebuildBush();
    }
    if (pressed("c") || pressed("C")) {
        bushLeafMode = bushLeafMode == "cards" || bushLeafMode == "mixed" ? "blobs" : "mixed";
        rebuildBush();
    }
    if (pressed("t") || pressed("T")) {
        bushTexture = loadBushTexture();
        if (bushTexture != null) bushObject.setTexture(bushTexture);
    }
    if (pressed("leftbracket")) { bushDensity = max(0.0, bushDensity - 0.1); rebuildBush(); }
    if (pressed("rightbracket")) { bushDensity = min(1.0, bushDensity + 0.1); rebuildBush(); }
}

function eve_render() {
    gfx.clear();
    gfx.render3D();
}
