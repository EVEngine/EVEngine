// Arbor Lab — interactive procedural tree recipe showcase.
// R regenerate, 1/2 Low Poly/Realistic, A algorithm, L leaves, [/] density, C canopy.

if (!("treeSeed" in getroottable())) treeSeed <- 31415;
if (!("treeStyle" in getroottable())) treeStyle <- "lowpoly";
if (!("treeAlgorithm" in getroottable())) treeAlgorithm <- "weberPenn";
if (!("leafMode" in getroottable())) leafMode <- "cards";
if (!("leafDensity" in getroottable())) leafDensity <- 0.65;
if (!("treeMesh" in getroottable())) treeMesh <- null;
if (!("treeObject" in getroottable())) treeObject <- null;
if (!("treeCamera" in getroottable())) treeCamera <- null;
if (!("treeYaw" in getroottable())) treeYaw <- 0.0;
function pressed(k) {
    return key_just_pressed(k);
}

function rebuildTree() {
    local paramsResult = procgen.newParams();
    if (!paramsResult.ok) return;
    local p = paramsResult.value;
    p.setSeed(treeSeed);
    p.setString("style", treeStyle);
    p.setString("branchAlgorithm", treeAlgorithm);
    p.setString("leafMode", leafMode);
    p.setFloat("leafDensity", leafDensity);
    p.setFloat("height", 6.2);
    p.setFloat("crownRadius", 2.15);
    p.setInt("branchLevels", treeStyle == "realistic" ? 3 : 2);
    p.setInt("branchCount", treeStyle == "realistic" ? 10 : 6);
    local meshResult = procgen.generateMesh("mesh.tree", p, gfx);
    if (!meshResult.ok) return;
    treeMesh = meshResult.value;
    if (treeObject == null) {
        treeObject = eve.Renderable3D();
        treeObject.setPosition(1.35, -3.0, 0.0);
        treeObject.setTint(0.34, 0.48, 0.22, 1.0);
        treeObject.setRoughness(0.86);
        treeObject.setCastShadow(true);
    }
    treeObject.setMesh(treeMesh);
}

if (treeCamera == null) {
    treeCamera = eve.Camera3D();
    treeCamera.setEye(10.0, 4.6, 12.0);
    treeCamera.setTarget(1.1, 0.2, 0.0);
    treeCamera.setUp(0.0, 1.0, 0.0);
    treeCamera.setFov(38.0);
    treeCamera.setAmbient(0.32, 0.38, 0.30);
    treeCamera.setActive(true);
    gfx.setDirectionalLight(-0.45, -1.0, -0.35, 1.35, 1.24, 1.02);
}
if (treeMesh == null) rebuildTree();
gfx.setBackgroundColor(0.055, 0.075, 0.07, 1.0);

function eve_update(dt) {
    treeYaw += dt * 7.0;
    treeObject.setYaw(treeYaw);
    if (pressed("r") || pressed("R")) { treeSeed += 1; rebuildTree(); }
    if (pressed("1")) { treeStyle = "lowpoly"; rebuildTree(); }
    if (pressed("2")) { treeStyle = "realistic"; rebuildTree(); }
    if (pressed("a") || pressed("A")) {
        treeAlgorithm = treeAlgorithm == "weberPenn" ? "spaceColonization" : "weberPenn";
        rebuildTree();
    }
    if (pressed("l") || pressed("L")) {
        leafMode = leafMode == "none" ? "cards" : "none";
        rebuildTree();
    }
    if (pressed("c") || pressed("C")) {
        leafMode = leafMode == "canopy" ? "cards" : "canopy";
        rebuildTree();
    }
    if (pressed("leftbracket")) { leafDensity = max(0.0, leafDensity - 0.1); rebuildTree(); }
    if (pressed("rightbracket")) { leafDensity = min(1.0, leafDensity + 0.1); rebuildTree(); }
}

function eve_render() {
    gfx.clear();
    gfx.render3D();
    gfx.drawSolidRect(28.0, 28.0, 310.0, 212.0, 0.92, 0.94, 0.86, 0.96);
}
