// Sky Lab — interactive procedural skyscraper recipe showcase.
// R regenerate, 1-4 fewer/more tiers, W window density, S spire toggle, T facade texture.

if (!("towerSeed" in getroottable())) towerSeed <- 2026;
if (!("tower" in getroottable())) tower <- null;
if (!("camera" in getroottable())) camera <- null;
if (!("tiers" in getroottable())) tiers <- 6;
if (!("windowCols" in getroottable())) windowCols <- 6;
if (!("windowRows" in getroottable())) windowRows <- 5;
if (!("spireHeight" in getroottable())) spireHeight <- 5.0;
if (!("setback" in getroottable())) setback <- 0.08;
if (!("facadeTex" in getroottable())) facadeTex <- null;
if (!("texName" in getroottable())) texName <- "tex.stone";
if (!("texRecipes" in getroottable())) texRecipes <- ["tex.stone", "tex.marble", "tex.soil", "tex.water", "tex.sky_cloud"];
if (!("texIndex" in getroottable())) texIndex <- 0;
if (!("yaw" in getroottable())) yaw <- 0.0;
if (!("prevKeys" in getroottable())) prevKeys <- {};

function pressed(k) {
    local down = keyboard.isDown(k);
    local old = k in prevKeys ? prevKeys[k] : false;
    prevKeys[k] <- down;
    return down && !old;
}

function rebuildTower() {
    local p = procgen.newParams();
    p.setSeed(towerSeed);
    p.setFloat("baseWidth", 10.0);
    p.setFloat("baseDepth", 10.0);
    p.setInt("tiers", tiers);
    p.setFloat("tierHeight", 6.0);
    p.setFloat("setback", setback);
    p.setInt("windowCols", windowCols);
    p.setInt("windowRows", windowRows);
    p.setFloat("windowDepth", 0.05);
    p.setFloat("spireHeight", spireHeight);

    local mesh = procgen.generateMesh("mesh.skyscraper", p, gfx);
    if (mesh == null) {
        print("tower generation failed: " + procgen.lastError());
        return;
    }
    if (tower == null) tower = eve.Renderable3D();
    tower.setMesh(mesh);
    tower.setTint(0.78, 0.80, 0.82, 1.0);
    tower.setRoughness(0.9);
    tower.setMetallic(0.02);
    tower.setCastShadow(true);
    tower.setReceiveShadow(true);
    tower.setScale(1.0, 1.0, 1.0);
}

function rebuildTexture() {
    local tp = procgen.newParams();
    tp.setSeed(towerSeed);
    tp.setSize(128, 256);
    tp.setFloat("scale", 3.0);
    tp.setInt("octaves", 4);
    tp.setInt("colors", 5);
    tp.setInt("pixelSize", 2);
    tp.setInt("seamless", 1);
    facadeTex = procgen.generateTexture(texName, tp, gfx);
}

if (camera == null) {
    camera = eve.Camera3D();
    camera.setEye(20.0, 16.0, 22.0);
    camera.setTarget(0.0, 16.0, 0.0);
    camera.setUp(0.0, 1.0, 0.0);
    camera.setFov(40.0);
    camera.setAmbient(0.20, 0.24, 0.30);
    camera.setActive(true);
    gfx.setDirectionalLight(-0.5, -1.0, -0.4, 1.5, 1.35, 1.1);
}
if (tower == null) { rebuildTexture(); rebuildTower(); }
gfx.setBackgroundColor(0.10, 0.13, 0.19, 1.0);

function eve_update(dt) {
    yaw += dt * 6.0;
    tower.setYaw(yaw);
    if (pressed("r") || pressed("R")) { towerSeed += 1; rebuildTower(); }
    if (pressed("1")) { tiers = max(1, tiers - 1); rebuildTower(); }
    if (pressed("2")) { tiers = min(24, tiers + 1); rebuildTower(); }
    if (pressed("w") || pressed("W")) {
        windowCols = windowCols == 6 ? 10 : 6;
        windowRows = windowRows == 5 ? 8 : 5;
        rebuildTower();
    }
    if (pressed("s") || pressed("S")) { spireHeight = spireHeight > 0.0 ? 0.0 : 5.0; rebuildTower(); }
    if (pressed("t") || pressed("T")) {
        texIndex = (texIndex + 1) % texRecipes.len();
        texName = texRecipes[texIndex];
        rebuildTexture();
    }
}

function eve_render() {
    gfx.clear();
    gfx.render3D();
    if (facadeTex != null) {
        gfx.drawTexturedRect(facadeTex, 28.0, 28.0, 96.0, 192.0, 1.0, 1.0, 1.0, 1.0);
    }
}
