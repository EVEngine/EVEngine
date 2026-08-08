// Runtime procedural dungeon + texture demo (Phase A/B).
// Maps:  R regenerate, 1 BSP, 2 cellular, 3 drunkard, 4 maze, 5 terrain
// Textures: T cycle recipe, N toggle normal preview, -/= change tex seed
// Run: make run/linux-debug GAME=examples/procgen

if (!("layer" in getroottable())) layer <- null;
if (!("seed" in getroottable())) seed <- 42;
if (!("algo" in getroottable())) algo <- "dungeon.bsp";
if (!("status" in getroottable())) status <- "";
if (!("prevKeys" in getroottable())) prevKeys <- {};
if (!("texRecipe" in getroottable())) texRecipe <- "tex.soil";
if (!("texSeed" in getroottable())) texSeed <- 1;
if (!("tex" in getroottable())) tex <- null;
if (!("showNormal" in getroottable())) showNormal <- false;
if (!("texRecipes" in getroottable())) texRecipes <- ["tex.soil", "tex.stone", "tex.marble", "tex.water", "tex.sky_cloud"];
if (!("texIndex" in getroottable())) texIndex <- 0;

TILE = 12;

function keyPressed(name) {
    local down = keyboard.isDown(name);
    local was = ("k_" + name) in prevKeys ? prevKeys["k_" + name] : false;
    prevKeys["k_" + name] <- down;
    return down && !was;
}

function ensureLayer(w, h) {
    if (layer == null) {
        layer = map.newLayer(w, h, TILE, TILE);
        layer.setOrigin(16, 48);
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

    local p = procgen.newParams();
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
    }

    local out = procgen.newOutput();
    out.setTarget("tilelayer");
    out.setLayer(layer);
    out.setPalette("demo");

    if (!procgen.generateTo(algo, p, out)) {
        status = "MAP FAIL: " + procgen.lastError();
        return;
    }

    local grid = procgen.generate(algo, p);
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
    local p = procgen.newParams();
    p.setSeed(texSeed);
    p.setSize(128, 128);
    p.setFloat("scale", 4.0);
    p.setInt("octaves", 4);
    p.setInt("colors", 6);
    p.setInt("pixelSize", 2);
    p.setInt("seamless", 1);
    if (showNormal) {
        local img = procgen.generateNormalImage(texRecipe, p);
        if (img == null) {
            status = "TEX FAIL: " + procgen.lastError();
            return;
        }
        tex = gfx.newTexture(img, true, true);
    } else {
        tex = procgen.generateTexture(texRecipe, p, gfx);
        if (tex == null) {
            status = "TEX FAIL: " + procgen.lastError();
            return;
        }
    }
    status = algo + " seed=" + seed + " | " + texRecipe + (showNormal ? " [normal]" : "") +
             " tseed=" + texSeed;
}

if (layer == null) {
    regenerateMap();
    regenerateTexture();
}

function update(dt) {
    if (keyPressed("r") || keyPressed("R")) {
        seed = seed + 1;
        regenerateMap();
    }
    if (keyPressed("1")) { algo = "dungeon.bsp"; regenerateMap(); }
    if (keyPressed("2")) { algo = "cave.cellular"; regenerateMap(); }
    if (keyPressed("3")) { algo = "cave.drunkard"; regenerateMap(); }
    if (keyPressed("4")) { algo = "maze.backtrack"; regenerateMap(); }
    if (keyPressed("5")) { algo = "noise.terrain"; regenerateMap(); }

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

function draw() {
    gfx.clear(0.08, 0.09, 0.12, 1.0);
    map.render(gfx);
    if (tex != null) {
        // Preview panel on the right
        gfx.drawTexturedRect(tex, 800, 48, 144, 144, 1, 1, 1, 1);
    }
}
