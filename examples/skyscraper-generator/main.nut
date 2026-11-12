// Sky Lab — interactive procedural skyscraper recipe showcase.
// R regenerate, 1-4 fewer/more tiers, W window density, S spire toggle, T facade texture.

persist towerSeed = 2026
persist tower = null
// Avoid the engine-provided 2D camera global.
persist towerCamera = null
persist tiers = 6
persist windowCols = 6
persist windowRows = 5
persist spireHeight = 5.0
persist setback = 0.08
persist facadeTex = null
persist texIndex = 0
persist yaw = 0.0
persist prevKeys = {}
persist towerGround = null
persist towerUiReady = false
persist towerOrbit = true

function pressed(k) {
    return key_just_pressed(k);
}

function rebuildTower() {
    local paramsResult = procgen.newParams();
    if (!paramsResult.ok) {
        print("tower parameter creation failed: " + paramsResult.status.summary);
        return;
    }
    local p = paramsResult.value;
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

    local meshResult = procgen.generateMesh("mesh.skyscraper", p, gfx);
    if (!meshResult.ok) {
        print("tower generation failed: " + meshResult.status.summary);
        return;
    }
    local mesh = meshResult.value;
    if (tower == null) tower = eve.Renderable3D();
    tower.setMesh(mesh);
    tower.setTexture(facadeTex);
    tower.setTint(1.0, 1.0, 1.0, 1.0);
    tower.setRoughness(0.55);
    tower.setMetallic(0.02);
    tower.setCastShadow(true);
    tower.setReceiveShadow(true);
    tower.setScale(1.0, 1.0, 1.0);
    if(towerCamera != null) {
        local h = tiers * 6.0 + spireHeight;
        towerCamera.setTarget(0.0,h*0.5,0.0);
        towerCamera.setEye(h*1.15,h*0.60,h*1.45);
        towerCamera.setClipPlanes(0.1,h*6.0);
    }
    if(towerUiReady) ui.setText("stats", "Seed " + towerSeed + " / tiers " + tiers + " / windows " + windowCols + " x " + windowRows);
}

function rebuildTexture() {
    local image = eve.Image().newEmptyImageData(32, 8, "RGBA8");
    local palettes = [
        [0.62,0.65,0.66, 0.035,0.12,0.20],
        [0.67,0.57,0.43, 0.045,0.16,0.19],
        [0.28,0.32,0.37, 0.84,0.58,0.22]
    ];
    local palette = palettes[texIndex % palettes.len()];
    for (local y=0; y<8; ++y) for (local x=0; x<32; ++x) {
        local offset = x < 16 ? 0 : 3;
        image.setPixel(x,y,palette[offset],palette[offset+1],palette[offset+2],1.0);
    }
    facadeTex = gfx.newTexture(image, false, false);
    if(tower != null) tower.setTexture(facadeTex);
}

if (towerCamera == null) {
    towerCamera = eve.Camera3D();
    towerCamera.setEye(38.0, 23.0, 48.0);
    towerCamera.setTarget(0.0, 20.0, 0.0);
    towerCamera.setUp(0.0, 1.0, 0.0);
    towerCamera.setFov(40.0);
    towerCamera.setAmbient(0.20, 0.24, 0.30);
    towerCamera.setActive(true);
    gfx.setDirectionalLight(-0.5, 0.85, 0.6, 1.35, 1.28, 1.15);
}
if (tower == null) { rebuildTexture(); rebuildTower(); }
gfx.setBackgroundColor(0.10, 0.13, 0.19, 1.0);

function eve_update(dt) {
    if(towerOrbit) yaw += dt * 0.105;
    if(pressed("space")) towerOrbit = !towerOrbit;
    tower.setYaw(yaw);
    if (pressed("r") || pressed("R")) { towerSeed += 1; tiers = 4 + towerSeed % 4; rebuildTower(); }
    if (pressed("1")) { tiers = max(1, tiers - 1); rebuildTower(); }
    if (pressed("2")) { tiers = min(24, tiers + 1); rebuildTower(); }
    if (pressed("w") || pressed("W")) {
        windowCols = windowCols == 6 ? 10 : 6;
        windowRows = windowRows == 5 ? 8 : 5;
        rebuildTower();
    }
    if (pressed("s") || pressed("S")) { spireHeight = spireHeight > 0.0 ? 0.0 : 5.0; rebuildTower(); }
    if (pressed("t") || pressed("T")) {
        texIndex = (texIndex + 1) % 3;
        rebuildTexture();
    }
}

function eve_render() {
    gfx.clear();
    gfx.render3D();
    ui.beginFrameAndRender();

}

function eve_init() {
    towerGround = eve.Renderable3D(); towerGround.setMesh(gfx.newMeshCube(1.0));
    towerGround.setPosition(0.0,-0.55,0.0); towerGround.setScale(18.0,1.0,18.0);
    towerGround.setTint(0.16,0.20,0.24,1.0); towerGround.setRoughness(0.9);
    towerGround.setCastShadow(false);
    ui.beginBuild(); ui.beginWindow("Skyscraper generator", "root");
    ui.text("SKY LAB / SETBACK TOWER", "title");
    ui.text("Seed " + towerSeed + " / tiers " + tiers + " / windows " + windowCols + " x " + windowRows, "stats");
    ui.text("1/2: tiers   W: windows   S: spire", "keys");
    ui.text("T: facade palette   R: rebuild   Space: pause", "keys2");
    ui.end(); ui.mountBuildAs("tower-hud"); ui.select("tower-hud");
    ui.setHostOverlay(true); ui.setHostPos(18.0,16.0,0.0,0.0); ui.setHostSize(390.0,176.0);
    towerUiReady = true;
}
