// Runtime procedural dungeon + texture demo (Phase A/B + WFC).
// Maps:  R regenerate, 1 BSP, 2 cellular, 3 drunkard, 4 maze, 5 terrain, 6 WFC
// Textures: T cycle recipe, N toggle normal preview, -/= change tex seed
// Run: make run/linux-debug GAME=examples/procgen

if (!("layer" in getroottable())) layer <- null;
if (!("seed" in getroottable())) seed <- 42;
if (!("algo" in getroottable())) algo <- "dungeon.bsp";
if (!("status" in getroottable())) status <- "";
if (!("texRecipe" in getroottable())) texRecipe <- "tex.soil";
if (!("texSeed" in getroottable())) texSeed <- 1;
if (!("tex" in getroottable())) tex <- null;
if (!("showNormal" in getroottable())) showNormal <- false;
if (!("texRecipes" in getroottable())) texRecipes <- ["tex.soil", "tex.stone", "tex.marble", "tex.water", "tex.sky_cloud"];
if (!("texIndex" in getroottable())) texIndex <- 0;
if (!("wfcPreset" in getroottable())) wfcPreset <- "dungeon";

TILE <- 12.0;

gfx.setBackgroundColor(0.08, 0.09, 0.12, 1.0);

function keyPressed(name) {
    return key_just_pressed(name);
}

function ensureLayer(w, h) {
    if (layer == null) {
        layer = map.newLayer(w, h, TILE, TILE);
        layer.setOrigin(16.0, 48.0);
        layer.setLayer(0);
        layer.setVisible(true);
    } else if (layer.getMapWidth() != w || layer.getMapHeight() != h) {
        layer.resize(w, h);
    }
}

function bindPalette() {
    procgen.setPaletteGid("demo", "wall", 1);
    procgen.setPaletteGid("demo", "floor", 2);
    procgen.setPaletteGid("demo", "corridor", 3);
    procgen.setPaletteGid("demo", "water", 4);
    procgen.setPaletteGid("demo", "sand", 5);
    procgen.setPaletteGid("demo", "grass", 6);
    procgen.setPaletteGid("demo", "dirt", 7);
    procgen.setPaletteGid("demo", "stone", 8);
    procgen.setPaletteGid("demo", "snow", 9);
    procgen.setPaletteGid("demo", "door", 10);
}

function regenerateMap() {
    bindPalette();
    local w = 64;
    local h = 40;
    ensureLayer(w, h);

    local paramsResult = procgen.newParams();
    if (!paramsResult.ok) {
        status = "MAP PARAMS FAIL: " + paramsResult.status.summary;
        return;
    }
    local p = paramsResult.value;
    p.setSeed(seed);
    p.setSize(w, h);
    if (algo == "cave.cellular") {
        p.setInt("loops", 5);
        p.setFloat("fill", 0.45);
    } else if (algo == "cave.drunkard") {
        p.setFloat("floorPct", 0.42);
    } else if (algo == "noise.terrain") {
        p.setFloat("frequency", 5.0);
        p.setInt("octaves", 4);
    } else if (algo == "wfc.simple") {
        p.setString("preset", wfcPreset);
        p.setInt("maxAttempts", 64);
    }

    local outputResult = procgen.newOutput();
    if (!outputResult.ok) {
        status = "OUTPUT FAIL: " + outputResult.status.summary;
        return;
    }
    local out = outputResult.value;
    out.setTarget("tilelayer");
    out.setLayer(layer);
    out.setPalette("demo");

    local outputWriteResult = procgen.generateTo(algo, p, out);
    if (!outputWriteResult.ok) {
        status = "MAP FAIL: " + outputWriteResult.status.summary;
        return;
    }

    local gridResult = procgen.generate(algo, p);
    if (!gridResult.ok) {
        status = "MAP GRID FAIL: " + gridResult.status.summary;
        return;
    }
    local grid = gridResult.value;
    local objInfo = "";
    if (grid != null) {
        local n = grid.getObjectCount();
        local i = 0;
        while (i < n) {
            if (i > 0) objInfo += "  ";
            objInfo += grid.getObjectType(i) + "@(" + grid.getObjectX(i).tointeger() + "," +
                       grid.getObjectY(i).tointeger() + ")";
            i += 1;
        }
    }
    status = algo + " seed=" + seed + " | " + texRecipe + " tseed=" + texSeed + "  " + objInfo;
}

function regenerateTexture() {
    local paramsResult = procgen.newParams();
    if (!paramsResult.ok) {
        status = "TEX PARAMS FAIL: " + paramsResult.status.summary;
        return;
    }
    local p = paramsResult.value;
    p.setSeed(texSeed);
    p.setSize(128, 128);
    p.setFloat("scale", 4.0);
    p.setInt("octaves", 4);
    p.setInt("colors", 6);
    p.setInt("pixelSize", 2);
    p.setInt("seamless", 1);
    if (showNormal) {
        local imageResult = procgen.generateNormalImage(texRecipe, p);
        if (!imageResult.ok) {
            status = "TEX FAIL: " + imageResult.status.summary;
            return;
        }
        tex = gfx.newTexture(imageResult.value, true, true);
    } else {
        local textureResult = procgen.generateTexture(texRecipe, p, gfx);
        if (!textureResult.ok) {
            status = "TEX FAIL: " + textureResult.status.summary;
            return;
        }
        tex = textureResult.value;
    }
    status = algo + " seed=" + seed + " | " + texRecipe + (showNormal ? " [normal]" : "") +
             " tseed=" + texSeed;
}

if (layer == null) {
    regenerateMap();
    regenerateTexture();
}

function eve_update(dt) {
    if (keyPressed("r") || keyPressed("R")) {
        seed = seed + 1;
        regenerateMap();
    }
    if (keyPressed("1")) { algo = "dungeon.bsp"; regenerateMap(); }
    if (keyPressed("2")) { algo = "cave.cellular"; regenerateMap(); }
    if (keyPressed("3")) { algo = "cave.drunkard"; regenerateMap(); }
    if (keyPressed("4")) { algo = "maze.backtrack"; regenerateMap(); }
    if (keyPressed("5")) { algo = "noise.terrain"; regenerateMap(); }
    if (keyPressed("6")) { algo = "wfc.simple"; wfcPreset = "dungeon"; regenerateMap(); }
    if (keyPressed("7")) { algo = "wfc.simple"; wfcPreset = "cave"; regenerateMap(); }
    if (keyPressed("8")) { algo = "wfc.simple"; wfcPreset = "terrain"; regenerateMap(); }

    if (keyPressed("t") || keyPressed("T")) {
        texIndex = (texIndex + 1) % texRecipes.len();
        texRecipe = texRecipes[texIndex];
        regenerateTexture();
    }
    if (keyPressed("n") || keyPressed("N")) {
        showNormal = !showNormal;
        regenerateTexture();
    }
    if (keyPressed("minus") || keyPressed("-")) {
        texSeed = texSeed - 1;
        if (texSeed < 0) texSeed = 0;
        regenerateTexture();
    }
    if (keyPressed("equals") || keyPressed("=") || keyPressed("plus")) {
        texSeed = texSeed + 1;
        regenerateTexture();
    }

    map.update(dt);
}

function eve_render() {
    gfx.clear();
    map.render(gfx);
    if (tex != null) {
        // Preview panel on the right
        gfx.drawTexturedRect(tex, 800.0, 48.0, 144.0, 144.0, 1.0, 1.0, 1.0, 1.0);
    }
}
