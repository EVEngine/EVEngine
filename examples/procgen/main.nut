// Runtime procedural dungeon demo (Phase A).
// Keys: R regenerate (new seed), 1 BSP, 2 cellular cave, 3 drunkard, 4 maze, 5 terrain
// Run: make run/linux-debug GAME=examples/procgen

if (!("layer" in getroottable())) layer <- null;
if (!("seed" in getroottable())) seed <- 42;
if (!("algo" in getroottable())) algo <- "dungeon.bsp";
if (!("status" in getroottable())) status <- "";
if (!("prevKeys" in getroottable())) prevKeys <- {};

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
    // Solid-color debug tileset is fine: GIDs just tint via missing texture path.
    // Semantic → GID mapping for pixel RPG style.
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

function regenerate() {
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
        status = "FAIL: " + procgen.lastError();
        return;
    }

    // Pull objects from a grid generate for HUD (spawn/stairs).
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
    status = algo + " seed=" + seed + "  " + objInfo;
}

function drawHud() {
    // Minimal overlay via print each regenerate; map renders tiles.
}

if (layer == null) {
    regenerate();
}

function update(dt) {
    if (keyPressed("r") || keyPressed("R")) {
        seed = seed + 1;
        regenerate();
    }
    if (keyPressed("1")) { algo = "dungeon.bsp"; regenerate(); }
    if (keyPressed("2")) { algo = "cave.cellular"; regenerate(); }
    if (keyPressed("3")) { algo = "cave.drunkard"; regenerate(); }
    if (keyPressed("4")) { algo = "maze.backtrack"; regenerate(); }
    if (keyPressed("5")) { algo = "noise.terrain"; regenerate(); }

    map.update(dt);
}

function draw() {
    gfx.clear(0.08, 0.09, 0.12, 1.0);
    map.render(gfx);
}
