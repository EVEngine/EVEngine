// Bush Lab — interactive showcase for the procedural `mesh.bush` recipe.
// R regenerate seed, 1/2 mound/sphere, L leafMode, C cards toggle, [/] density, T texture.
// The default texture is derived from Kenney's CC0 mini-forest colormap
// (assets/bush_atlas.png); press T to reload it from disk at any time.

if (!("bushSeed" in getroottable())) bushSeed <- 20260815;
if (!("bushStyle" in getroottable())) bushStyle <- "mound";
if (!("bushLeafMode" in getroottable())) bushLeafMode <- "mixed";
if (!("bushDensity" in getroottable())) bushDensity <- 0.62;
if (!("bushMesh" in getroottable())) bushMesh <- null;
if (!("bushObject" in getroottable())) bushObject <- null;
if (!("bushTexture" in getroottable())) bushTexture <- null;
if (!("bushCamera" in getroottable())) bushCamera <- null;
if (!("bushYaw" in getroottable())) bushYaw <- 0.0;
function pressed(k) {
    return key_just_pressed(k);
}

function loadBushTexture() {
    // Derived from Kenney's CC0 mini-forest palette: left half is twig bark,
    // right half is foliage. See assets/bush_atlas.png and assets/kenney/.
    return gfx.newTextureFromFile("assets/bush_atlas.png");
}

function rebuildBush() {
    local p = procgen.newParams();
    p.setSeed(bushSeed);
    p.setString("style", bushStyle);
    p.setString("leafMode", bushLeafMode);
    p.setFloat("leafDensity", bushDensity);
    p.setFloat("height", 1.7);
    p.setFloat("width", 2.6);
    p.setInt("blobs", 12);
    p.setInt("rings", 3);
    p.setInt("radialSegments", 8);
    p.setFloat("leafSize", 0.32);
    p.setInt("twigs", 6);
    bushMesh = procgen.generateMesh("mesh.bush", p, gfx);
    if (bushObject == null) {
        bushObject = eve.Renderable3D();
        bushObject.setPosition(1.4, -3.0, 0.0);
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
    bushYaw += dt * 7.0;
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
    gfx.drawSolidRect(28.0, 28.0, 320.0, 200.0, 0.92, 0.94, 0.86, 0.96);
}
