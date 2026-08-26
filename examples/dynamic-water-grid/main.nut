dofile("water_field.nut");

if (!("waterDemo" in getroottable())) waterDemo <- null;
if (!("waterField" in getroottable())) waterField <- null;
if (!("waterMap" in getroottable())) waterMap <- null;
if (!("logicLayer" in getroottable())) logicLayer <- null;
if (!("terrainLayer" in getroottable())) terrainLayer <- null;
if (!("shoreLayer" in getroottable())) shoreLayer <- null;
if (!("heroLayer" in getroottable())) heroLayer <- null;
if (!("companionLayer" in getroottable())) companionLayer <- null;
if (!("fieldCanvas" in getroottable())) fieldCanvas <- null;
if (!("waterShader" in getroottable())) waterShader <- null;
if (!("shoreTexture" in getroottable())) shoreTexture <- null;
if (!("terrainTexture" in getroottable())) terrainTexture <- null;
if (!("heroTexture" in getroottable())) heroTexture <- null;
if (!("companionTexture" in getroottable())) companionTexture <- null;
if (!("previousKeys" in getroottable())) previousKeys <- {};
if (!("paused" in getroottable())) paused <- false;
if (!("sourceEnabled" in getroottable())) sourceEnabled <- true;
if (!("showNodes" in getroottable())) showNodes <- false;
if (!("elapsed" in getroottable())) elapsed <- 0.0;
if (!("fixedClock" in getroottable())) fixedClock <- 0.0;
if (!("heroClock" in getroottable())) heroClock <- 0.0;
if (!("heroDirection" in getroottable())) heroDirection <- 0;
if (!("hudBuilt" in getroottable())) hudBuilt <- false;

const FIELD_W = 16;
const FIELD_H = 12;
const TILE_W = 80.0;
const TILE_H = 40.0;
const ORIGIN_X = 650.0;
const ORIGIN_Y = 100.0;
const GID_RIVERBED = 1;
const FIXED_DT = 0.0333333;
const PATCH_SOURCE_W = 244;
const PATCH_SOURCE_H = 123;
const ATLAS_TILE_W = 61;
const ATLAS_TILE_H = 31;
const HERO_CELL = 64;
const HERO_COLS = 6;
const HERO_ROWS = 8;
// Squirrel const values must be scalar literals, not expressions.
const HERO_FRAMES = 48;
const HERO_X = 7;
const HERO_Y = 6;
const COMPANION_X = 9;
const COMPANION_Y = 6;


function edgePressed(name) {
    local down = keyboard.isDown(name);
    local old = (name in previousKeys) ? previousKeys[name] : false;
    previousKeys[name] <- down;
    return down && !old;
}

function rgbaPixel(target, x, y, r, g, b, a) {
    target.setPixel(x, y, r, g, b, a);
}

function buildImportedShoreAtlas() {
    local imageModule = eve.Image();
    local src = imageModule.newImageDataFromFile("assets/riverbed.png");
    local atlas = imageModule.newEmptyImageData(
        ATLAS_TILE_W * 4, ATLAS_TILE_H * 4, "RGBA8");

    // The supplied sheet lays its 16 frames out as an isometric 4x4 map.
    // Unskew every diamond into a conventional row-major atlas.
    for (local row = 0; row < 4; ++row) {
        for (local col = 0; col < 4; ++col) {
            local centerX = PATCH_SOURCE_W * 0.5 +
                (col - row) * ATLAS_TILE_W * 0.5;
            local sourceX = (centerX - ATLAS_TILE_W * 0.5 + 0.5).tointeger();
            local sourceY = ((col + row) * PATCH_SOURCE_H / 8.0 + 0.5).tointeger();
            local targetX = col * ATLAS_TILE_W;
            local targetY = row * ATLAS_TILE_H;

            for (local py = 0; py < ATLAS_TILE_H; ++py) {
                for (local px = 0; px < ATLAS_TILE_W; ++px) {
                    local diamond = abs((px + 0.5 - ATLAS_TILE_W * 0.5) /
                                            (ATLAS_TILE_W * 0.5)) +
                        abs((py + 0.5 - ATLAS_TILE_H * 0.5) /
                            (ATLAS_TILE_H * 0.5));
                    if (diamond > 1.0) continue;
                    local sx = sourceX + px;
                    local sy = sourceY + py;
                    if (sx < 0 || sy < 0 || sx >= PATCH_SOURCE_W || sy >= PATCH_SOURCE_H)
                        continue;
                    local r = src.getPixelR(sx, sy);
                    local g = src.getPixelG(sx, sy);
                    local b = src.getPixelB(sx, sy);
                    local a = src.getPixelA(sx, sy);
                    local neutral = abs(r - g) < 0.012 && abs(g - b) < 0.012;
                    local alpha = (neutral && r < 0.15) ? 0.0 : a;
                    rgbaPixel(atlas, targetX + px, targetY + py, r, g, b, alpha);
                }
            }
        }
    }
    local tex = gfx.newTexture(atlas, false, false);
    gfx.setTextureSampler(tex, "nearest", "none", 1.0, 0.0);
    return tex;
}

function buildTerrainAtlas() {
    // Imported from the user's C14 45-degree terrain pack. Atlas order:
    // yellow grass, green grass, sand, mud. riverbed.png's black regions are
    // transparent and reveal these tiles unchanged.
    local texture = gfx.newTextureFromFile("assets/terrain/c14-land-atlas.png");
    gfx.setTextureSampler(texture, "nearest", "none", 1.0, 0.0);
    return texture;
}

function configureIsoLayer(layer, drawLayer) {
    layer.applyConfig(@"{
      ""orientation"":""isometric"",
      ""tilewidth"":80,""tileheight"":40,
      ""offsetx"":650,""offsety"":100
    }");
    layer.setOrigin(ORIGIN_X, ORIGIN_Y);
    layer.setLayer(drawLayer);
}

function configureCharacterLayer(layer, texture) {
    configureIsoLayer(layer, 1);
    gfx.setTextureSampler(texture, "nearest", "none", 1.0, 0.0);
    layer.setTileset(texture, 1, HERO_COLS, 0, 0);
    layer.setTilesetTileSize(HERO_CELL, HERO_CELL);

    // Every frame shares one feet anchor at the projected tile centre.  Custom
    // visual regions retain the native 64px sprite cell while the map remains
    // an 80x40 isometric grid.
    for (local row = 0; row < HERO_ROWS; ++row) {
        for (local col = 0; col < HERO_COLS; ++col) {
            local gid = 1 + row * HERO_COLS + col;
            layer.setTileVisual(gid, col * HERO_CELL, row * HERO_CELL,
                HERO_CELL, HERO_CELL, -8.0, 24.0, 1.0);
        }
    }
}

function configureHeroLayer() {
    heroLayer = waterMap.newLayer(FIELD_W, FIELD_H, TILE_W, TILE_H);
    heroTexture = gfx.newTextureFromFile("assets/characters/white-haired-girl/walk-8dir.png");
    configureCharacterLayer(heroLayer, heroTexture);
    heroLayer.setTile(HERO_X, HERO_Y, 1);

    companionLayer = waterMap.newLayer(FIELD_W, FIELD_H, TILE_W, TILE_H);
    companionTexture = gfx.newTextureFromFile(
        "assets/characters/blue-haired-girl/blue-haired-girl-walk-8dir-64.png");
    configureCharacterLayer(companionLayer, companionTexture);
    companionLayer.setTile(COMPANION_X, COMPANION_Y, 1);
}

function updateHero(dt) {
    if (heroLayer == null || companionLayer == null) return;
    heroClock += dt;
    // Arrow keys preview all eight rows without changing the water simulation.
    local left = keyboard.isDown("left") || keyboard.isDown("a") || keyboard.isDown("A");
    local right = keyboard.isDown("right") || keyboard.isDown("d") || keyboard.isDown("D");
    local up = keyboard.isDown("up") || keyboard.isDown("w") || keyboard.isDown("W");
    local down = keyboard.isDown("down") || keyboard.isDown("s") || keyboard.isDown("S");
    if (down && !left && !right) heroDirection = 0;
    else if (down && right) heroDirection = 1;
    else if (right && !up && !down) heroDirection = 2;
    else if (up && right) heroDirection = 3;
    else if (up && !left && !right) heroDirection = 4;
    else if (up && left) heroDirection = 5;
    else if (left && !up && !down) heroDirection = 6;
    else if (down && left) heroDirection = 7;
    local frame = (heroClock / 0.12).tointeger() % HERO_COLS;
    local gid = 1 + heroDirection * HERO_COLS + frame;
    heroLayer.setTile(HERO_X, HERO_Y, gid);
    companionLayer.setTile(COMPANION_X, COMPANION_Y, gid);
}

function loadWaterShader() {
    local wgsl = fs.readText("shaders/water_field.wgsl");
    local glsl = fs.readText("shaders/water_field.frag");
    local shader = null;
    try {
        shader = gfx.newShaderFromSpvFile("shaders/water_field.frag.spv");
    } catch (vulkanError) {
        try {
            shader = gfx.newShaderFromWgsl("", wgsl);
        } catch (webgpuError) {
            shader = gfx.newShader(glsl);
        }
    }
    shader.declareVec2("screenSize");
    shader.declareVec2("origin");
    shader.declareVec2("tileSize");
    shader.declareVec2("fieldSize");
    shader.declareFloat("time");
    shader.declareFloat("wetThreshold");
    shader.declareFloat("opacity");
    return shader;
}

function syncTopology(force) {
    local changed = force;
    for (local y = 0; y < FIELD_H; ++y) {
        for (local x = 0; x < FIELD_W; ++x) {
            // Filled dual-grid corners mean riverbed, not land. The atlas's
            // black pixels stay transparent and reveal terrainLayer below.
            local gid = waterField.isWet(x, y) ? GID_RIVERBED : 0;
            if (logicLayer.getTile(x, y) != gid) {
                logicLayer.setTile(x, y, gid);
                changed = true;
            }
        }
    }
    if (changed && !waterMap.resolveDualGrid(logicLayer, shoreLayer))
        print("dynamic-water-grid: dual-grid resolve failed\n");
}

function rebuildExample() {
    waterField = WaterField(FIELD_W, FIELD_H);
    waterField.resetRiver();

    waterMap = eve.Map();
    logicLayer = waterMap.newLayer(FIELD_W, FIELD_H, TILE_W, TILE_H);
    configureIsoLayer(logicLayer, -1);

    terrainLayer = waterMap.newLayer(FIELD_W, FIELD_H, TILE_W, TILE_H);
    configureIsoLayer(terrainLayer, -2);
    terrainTexture = buildTerrainAtlas();
    terrainLayer.setTileset(terrainTexture, 1, 4, 0, 0);
    terrainLayer.setTilesetTileSize(TILE_W.tointeger(), TILE_H.tointeger());
    for (local y = 0; y < FIELD_H; ++y) {
        for (local x = 0; x < FIELD_W; ++x) {
            local material = (x < FIELD_W / 2 ? 0 : 1) +
                (y < FIELD_H / 2 ? 0 : 2);
            terrainLayer.setTile(x, y, 1 + material);
        }
    }

    shoreLayer = waterMap.newLayer(1, 1, TILE_W, TILE_H);
    configureIsoLayer(shoreLayer, -1);
    shoreTexture = buildImportedShoreAtlas();
    shoreLayer.setTileset(shoreTexture, 1, 4, 0, 0);
    shoreLayer.setTilesetTileSize(ATLAS_TILE_W, ATLAS_TILE_H);
    configureHeroLayer();

    fieldCanvas = gfx.newCanvas(FIELD_W, FIELD_H);
    waterShader = loadWaterShader();
    syncTopology(true);
}

function rasterizeWaterField() {
    gfx.setShader(null);
    gfx.setCanvas(fieldCanvas);
    for (local y = 0; y < FIELD_H; ++y) {
        for (local x = 0; x < FIELD_W; ++x) {
            local i = waterField.index(x, y);
            local depth = waterField.normalizedDepth(x, y);
            local vx = clampValue(waterField.flowX[i] * 18.0, -1.0, 1.0);
            local vy = clampValue(waterField.flowY[i] * 18.0, -1.0, 1.0);
            gfx.drawSolidRect(x.tofloat(), y.tofloat(), 1.0, 1.0,
                depth, vx * 0.5 + 0.5, vy * 0.5 + 0.5, 1.0);
        }
    }
    gfx.setCanvas(null);
}

function updateShaderUniforms() {
    waterShader.sendVec2("screenSize", config.width.tofloat(), config.height.tofloat());
    // TileProjection returns the top-left of each 80x40 bounding box. The
    // diamond centre is half a tile farther right/down.
    waterShader.sendVec2("origin", ORIGIN_X + TILE_W * 0.5, ORIGIN_Y + TILE_H * 0.5);
    waterShader.sendVec2("tileSize", TILE_W, TILE_H);
    waterShader.sendVec2("fieldSize", FIELD_W.tofloat(), FIELD_H.tofloat());
    waterShader.sendFloat("time", elapsed);
    waterShader.sendFloat("wetThreshold", waterField.wetThreshold / waterField.maxVisualDepth);
    waterShader.sendFloat("opacity", 0.92);
}

function mouseCell() {
    if (logicLayer == null) return [-1, -1];
    return [logicLayer.worldToTileX(mouse.getX() - TILE_W * 0.5,
                                    mouse.getY() - TILE_H * 0.5),
            logicLayer.worldToTileY(mouse.getX() - TILE_W * 0.5,
                                    mouse.getY() - TILE_H * 0.5)];
}

function updateHud() {
    if (!hudBuilt || waterField == null) return;
    local cell = mouseCell();
    local amount = waterField.getAmount(cell[0], cell[1]);
    local total10 = (waterField.totalAmount() * 10.0).tointeger();
    local amount100 = (amount * 100.0).tointeger();
    ui.setText("stats", "cell (" + cell[0] + ", " + cell[1] + ") water=" +
        amount100 + "%  total=" + total10 + "/10");
    ui.setText("mode", (paused ? "PAUSED" : "SIMULATING") +
        "  source=" + (sourceEnabled ? "ON" : "OFF") +
        "  nodes=" + (showNodes ? "ON" : "OFF"));
}

function buildHud() {
    if (hudBuilt) return;
    ui.beginBuild();
    ui.beginWindow("Dynamic Water Grid", "root");
    ui.text("Dual-grid + per-cell water amount", "title");
    ui.text("", "stats");
    ui.text("", "mode");
    ui.text("LMB add water | RMB drain | drag supported", "mouseHelp");
    ui.text("Space pause | R reset | F source | N node markers", "keyHelp");
    ui.text("Arrow / WASD preview both heroes' 8 walk directions", "heroHelp");
    ui.text("Land below | riverbed dual-grid | transparent water above", "assetHelp");
    ui.end();
    ui.mountBuildAs("water-hud");
    ui.select("water-hud");
    ui.setHostOverlay(true);
    ui.setHostPos(14.0, 12.0, 0.0, 0.0);
    hudBuilt = true;
}

eve_init = function() {
    gfx.setBackgroundColor(0.045, 0.055, 0.065, 1.0);
    rebuildExample();
    buildHud();
    updateHud();
    print("dynamic-water-grid ready: LMB add, RMB drain, Space pause\n");
};

eve_reload <- function() {
    waterShader = loadWaterShader();
    syncTopology(true);
};

eve_asset_reload <- function(path) {
    if (path.find("water_field") != null)
        waterShader = loadWaterShader();
    if (path.find("riverbed.png") != null) {
        shoreTexture = buildImportedShoreAtlas();
        shoreLayer.setTileset(shoreTexture, 1, 4, 0, 0);
        shoreLayer.setTilesetTileSize(ATLAS_TILE_W, ATLAS_TILE_H);
        syncTopology(true);
    }
    if (path.find("c14-land-atlas.png") != null) {
        terrainTexture = buildTerrainAtlas();
        terrainLayer.setTileset(terrainTexture, 1, 4, 0, 0);
        terrainLayer.setTilesetTileSize(TILE_W.tointeger(), TILE_H.tointeger());
    }
};

eve_update = function(dt) {
    elapsed += dt;
    if (edgePressed("space")) paused = !paused;
    if (edgePressed("r") || edgePressed("R")) {
        waterField.resetRiver();
        syncTopology(true);
    }
    if (edgePressed("f") || edgePressed("F")) sourceEnabled = !sourceEnabled;
    if (edgePressed("n") || edgePressed("N")) showNodes = !showNodes;

    local cell = mouseCell();
    if (mouse.isDown(1)) waterField.addAmount(cell[0], cell[1], dt * 1.8);
    if (mouse.isDown(2)) waterField.addAmount(cell[0], cell[1], -dt * 2.4);

    if (!paused) {
        fixedClock += minValue(dt, 0.1);
        while (fixedClock >= FIXED_DT) {
            if (sourceEnabled) {
                local sx = 1;
                local sy = waterField.channelCenter(sx).tointeger();
                waterField.addAmount(sx, sy, 0.075 * FIXED_DT);
            }
            local topologyChanged = waterField.step(FIXED_DT);
            if (topologyChanged) syncTopology(false);
            fixedClock -= FIXED_DT;
        }
    } else if (waterField.refreshWetState()) {
        syncTopology(false);
    }

    waterMap.update(dt);
    updateHero(dt);
    updateHud();
};

eve_render = function() {
    // Update the offscreen data texture before starting any swapchain draws.
    // Switching back to an offscreen Canvas after screen batches were queued
    // would reopen the Vulkan frame target and discard the earlier pass.
    rasterizeWaterField();
    updateShaderUniforms();

    gfx.clear();

    // Pass 1: replaceable land materials, then the beige riverbed overlay.
    terrainLayer.setVisible(true);
    shoreLayer.setVisible(true);
    heroLayer.setVisible(false);
    companionLayer.setVisible(false);
    waterMap.render(gfx);

    // Pass 2: transparent animated water exists only where amount > threshold.
    gfx.setShader(waterShader);
    gfx.drawCanvas(fieldCanvas, 0.0, 0.0, config.width.tofloat(), config.height.tofloat());
    gfx.setShader(null);

    // Pass 3: characters stay above the water surface.
    terrainLayer.setVisible(false);
    shoreLayer.setVisible(false);
    heroLayer.setVisible(true);
    companionLayer.setVisible(true);
    waterMap.render(gfx);
    terrainLayer.setVisible(true);
    shoreLayer.setVisible(true);

    if (showNodes) {
        for (local y = 0; y < FIELD_H; ++y) {
            for (local x = 0; x < FIELD_W; ++x) {
                local wx = logicLayer.tileToWorldX(x, y);
                local wy = logicLayer.tileToWorldY(x, y);
                local d = waterField.normalizedDepth(x, y);
                gfx.drawSolidRect(wx + TILE_W * 0.5 - 2.0,
                    wy + TILE_H * 0.5 - 2.0, 5.0, 5.0,
                    1.0 - d, 0.25 + d * 0.65, 0.95, 1.0);
            }
        }
    }

    ui.beginFrameAndRender();
};
