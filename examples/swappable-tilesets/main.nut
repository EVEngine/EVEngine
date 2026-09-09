// A2 floor quarters + ordinary B-sheet atlas regions, using original 48px art.
// The small logical brush below is example-local, not the RPG Maker importer.
const W = 21;
const H = 12;
const OX = 48;
const OY = 96;
persist terrain = [];
persist ground = null;
persist quarters = null;
persist objects = null;
persist textures = [];
persist season = 0;
persist brush = 1;

function occupied(x, y, kind) {
    return x >= 0 && x < W && y >= 0 && y < H && terrain[y * W + x] == kind;
}

function rebuild() {
    quarters.fill(0);
    for (local y = 0; y < H; ++y) {
        for (local x = 0; x < W; ++x) {
            local kind = terrain[y * W + x];
            if (kind == 0) continue;
            // A2 blocks are 96x144. Sand is block 1, stone is block 2.
            local bx = kind * 4;
            for (local qy = 0; qy < 2; ++qy) {
                for (local qx = 0; qx < 2; ++qx) {
                    local dx = qx == 0 ? -1 : 1;
                    local dy = qy == 0 ? -1 : 1;
                    local side = occupied(x + dx, y, kind);
                    local vert = occupied(x, y + dy, kind);
                    local diag = occupied(x + dx, y + dy, kind);
                    local sx = qx == 0 ? 2 : 1;
                    local sy = qy == 0 ? 4 : 3;
                    if (!side) sx = qx == 0 ? 0 : 3;
                    if (!vert) sy = qy == 0 ? 2 : 5;
                    if (side && vert && !diag) {
                        sx = qx == 0 ? 2 : 3;
                        sy = qy == 0 ? 0 : 1;
                    }
                    quarters.setTile(x * 2 + qx, y * 2 + qy, 1 + sy * 32 + bx + sx);
                }
            }
        }
    }
}

function resetTerrain() {
    terrain = array(W * H, 0);
    for (local y = 0; y < H; ++y)
        for (local x = 0; x < W; ++x) {
            if ((x >= 2 && x <= 7 && y >= 2 && y <= 8) ||
                (x >= 5 && x <= 16 && y >= 5 && y <= 7) ||
                (x >= 14 && x <= 17 && y >= 2 && y <= 9)) terrain[y * W + x] = 1;
            if (x >= 9 && x <= 12 && y >= 1 && y <= 3) terrain[y * W + x] = 2;
        }
    // Concave corners, a hole, an isolated cell and a narrow connection.
    terrain[3 * W + 4] = 0;
    terrain[4 * W + 4] = 0;
    terrain[10 * W + 10] = 1;
    rebuild();
}

function applySeason() {
    ground.setTileset(textures[season], 1, 32, 0, 0);
    ground.setTilesetTileSize(24, 24);
    quarters.setTileset(textures[season], 1, 32, 0, 0);
    quarters.setTilesetTileSize(24, 24);
}

eve_init = function() {
    gfx.setBackgroundColor(0.055, 0.075, 0.065, 1.0);
    if (ground != null) return;
    if (map == null) map = eve.Map();
    foreach (path in ["assets/ground-summer.png", "assets/ground-autumn.png"]) {
        local tex = gfx.newTextureFromFile(path);
        gfx.setTextureSampler(tex, "nearest", "none", 1.0, 0.0);
        textures.append(tex);
    }
    ground = map.newLayer(W * 2, H * 2, 24.0, 24.0);
    ground.setOrigin(OX.tofloat(), OY.tofloat());
    ground.setLayer(0);
    // Sample the repeating grass interior from A2 block 0.
    for (local y = 0; y < H * 2; ++y)
        for (local x = 0; x < W * 2; ++x)
            ground.setTile(x, y, 1 + (2 + y % 2) * 32 + x % 2);
    quarters = map.newLayer(W * 2, H * 2, 24.0, 24.0);
    quarters.setOrigin(OX.tofloat(), OY.tofloat());
    quarters.setLayer(1);
    applySeason();
    objects = map.newLayer(W, H, 48.0, 48.0);
    objects.setOrigin(OX.tofloat(), OY.tofloat());
    objects.setLayer(2);
    local tex = gfx.newTextureFromFile("assets/objects.png");
    gfx.setTextureSampler(tex, "nearest", "none", 1.0, 0.0);
    objects.setTileset(tex, 1, 16, 0, 0);
    objects.setTilesetTileSize(48, 48);
    // Full sprite regions retain the original B-sheet art without cropping files.
    objects.setTileVisual(1, 96, 96, 96, 144, 24.0, 96.0, 0.0);
    objects.setTileVisual(2, 96, 240, 96, 144, 24.0, 96.0, 0.0);
    objects.setTileVisual(3, 432, 48, 48, 48, 0.0, 0.0, 0.0);
    foreach (pos in [[1,2], [8,2], [19,3], [1,9], [8,10], [19,10]])
        objects.setTile(pos[0], pos[1], 1);
    objects.setTile(12, 9, 2);
    objects.setTile(18, 6, 3);
    resetTerrain();
    ui.beginBuild();
    ui.beginWindow("SwappableTilesets", "root");
    ui.text("SWAPPABLE TILESETS / A2 QUARTER TILES + B SPRITES", "title");
    ui.text("LMB paint | RMB erase | 1 sand | 2 stone | T summer/autumn | R reset", "help");
    ui.end();
    ui.mountBuildAs("hud");
    ui.select("hud");
    ui.setHostOverlay(true);
    ui.setHostPos(48.0, 12.0, 0.0, 0.0);
};

eve_update = function(dt) {
    if (key_just_pressed("1")) brush = 1;
    if (key_just_pressed("2")) brush = 2;
    if (key_just_pressed("t")) { season = 1 - season; applySeason(); }
    if (key_just_pressed("r")) resetTerrain();
    local mx = mouse.getX() - OX;
    local my = mouse.getY() - OY;
    if (mx >= 0 && my >= 0 && mx < W * 48 && my < H * 48) {
        local x = (mx / 48).tointeger();
        local y = (my / 48).tointeger();
        local value = mouse.isDown(2) ? 0 : brush;
        if ((mouse.isDown(1) || mouse.isDown(2)) && terrain[y * W + x] != value) {
            terrain[y * W + x] = value;
            rebuild();
        }
    }
    map.update(dt);
};
eve_render = function() { gfx.clear(); map.render(gfx); ui.beginFrameAndRender(); };
