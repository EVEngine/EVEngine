// Deterministic falling-material playground. Left paints the selected material,
// right digs terrain, middle ignites it. The core owns no rendering dependency.

persist world = null;
persist accumulator = 0.0;
persist brushMaterial = "sand";
persist brushRadius = 4;
persist atlasRenderer = null;
persist showDiagnostics = true;

const WORLD_W = 160;
const WORLD_H = 96;
const SCALE = 6.0;
const STEP = 0.016666667;

function buildScene() {
    world.clear();
    // A thick authoritative stone field: caves and later mouse digging replace
    // cells with air instead of maintaining a separate destruction mask.
    for (local x = 0; x < WORLD_W; x += 1) {
        local surface = 66 + ((x / 13).tointeger() % 3) * 3;
        for (local y = surface; y < WORLD_H; y += 1)
            world.setMaterial(x, y, "stone");
    }
    for (local y = 42; y < WORLD_H; y += 1) {
        world.setMaterial(0, y, "stone");
        world.setMaterial(WORLD_W - 1, y, "stone");
    }
    world.paintCircle(44, 68, 15, "air");
    world.paintCircle(67, 74, 11, "air");
    world.paintCircle(116, 70, 17, "air");
    world.paintCircle(39, 18, 10, "sand");
    world.paintCircle(116, 30, 11, "water");
    world.paintCircle(83, 61, 7, "wood");
    world.paintCircle(96, 56, 5, "oil");
    world.paintCircle(135, 55, 5, "acid");
    world.paintCircle(22, 56, 4, "gunpowder");
    world.paintCircle(148, 53, 4, "lava");
}

function selectBrush(key, material) {
    if (key_just_pressed(key)) brushMaterial = material;
}

function updateBrush() {
    selectBrush("1", "sand");
    selectBrush("2", "water");
    selectBrush("3", "oil");
    selectBrush("4", "wood");
    selectBrush("5", "stone");
    selectBrush("6", "fire");
    selectBrush("7", "acid");
    selectBrush("8", "gunpowder");
    selectBrush("9", "lava");
    if (key_just_pressed("minus")) brushRadius = max(1, brushRadius - 1);
    if (key_just_pressed("plus") || key_just_pressed("equals"))
        brushRadius = min(12, brushRadius + 1);

    local cellX = (mouse.getX().tofloat() / SCALE).tointeger();
    local cellY = (mouse.getY().tofloat() / SCALE).tointeger();
    if (cellX < 0 || cellX >= WORLD_W || cellY < 0 || cellY >= WORLD_H) return;
    if (mouse.isDown(1)) world.paintCircle(cellX, cellY, brushRadius, brushMaterial);
    if (mouse.isDown(2)) world.paintCircle(cellX, cellY, brushRadius + 2, "air");
    if (mouse.isDown(3)) world.paintCircle(cellX, cellY, brushRadius, "fire");
}

eve_init = function() {
    gfx.setBackgroundColor(0.025, 0.025, 0.04, 1.0);
    if (world == null) {
        world = pixelworld.newWorld(1337);
        buildScene();
    }
    if (atlasRenderer == null)
        atlasRenderer = pixelworldGraphics.newRenderer(0, 0, WORLD_W, WORLD_H);
    pixelworldEditor.openCatalog();
};

eve_update = function(dt) {
    updateBrush();
    accumulator += dt;
    while (accumulator >= STEP) {
        world.step();
        accumulator -= STEP;
    }
    if (key_just_pressed("r")) buildScene();
    if (key_just_pressed("f")) world.paintCircle(96, 54, 5, "fire");
    if (key_just_pressed("o")) world.paintCircle(62, 8, 8, "oil");
    if (key_just_pressed("e")) {
        local x = (mouse.getX().tofloat() / SCALE).tointeger();
        local y = (mouse.getY().tofloat() / SCALE).tointeger();
        world.explode(x, y, 10, 190, 420);
    }
    if (key_just_pressed("d")) showDiagnostics = !showDiagnostics;
};

eve_render = function() {
    gfx.clear();
    atlasRenderer.sync(world, gfx);
    gfx.drawTexturedRect(atlasRenderer.getTexture(), 0.0, 0.0,
                         WORLD_W * SCALE, WORLD_H * SCALE,
                         1.0, 1.0, 1.0, 1.0);
    if (showDiagnostics) atlasRenderer.drawDiagnostics(world, gfx, SCALE);

    local cursorCellX = (mouse.getX().tofloat() / SCALE).tointeger();
    local cursorCellY = (mouse.getY().tofloat() / SCALE).tointeger();
    if (cursorCellX > 0 && cursorCellX < WORLD_W - 1 &&
        cursorCellY > 0 && cursorCellY < WORLD_H - 1) {
        local cursorX = cursorCellX * SCALE;
        local cursorY = cursorCellY * SCALE;
        local cursorSize = (brushRadius * 2 + 1) * SCALE;
        gfx.drawSolidRect(cursorX - brushRadius * SCALE, cursorY - brushRadius * SCALE,
                          cursorSize, 1.0, 1.0, 1.0, 1.0, 0.8);
        gfx.drawSolidRect(cursorX - brushRadius * SCALE,
                          cursorY + (brushRadius + 1) * SCALE,
                          cursorSize, 1.0, 1.0, 1.0, 1.0, 0.8);
    }
    ui.beginFrameAndRender();
};
