// RoguelikeGenerator Pro — interactive procedural level showcase.
//
// Generates seed-driven room-and-corridor dungeons with the "level.roguelike"
// procgen algorithm and renders them in 2D / 2.5D (sprite-stack extrusion).
// The generator layers three kinds of "detail" on top of the plain wall/floor
// grid, all exposed on the Grid2D:
//   * wall cells   -> detail = 8-bit autotile direction mask (which sides are open)
//   * floor cells  -> detail = floor-pattern variant (1..N)
//   * floor cells  -> detail >= 100 = scattered decor tile
// It also emits objects (spawn / stairs / pillars / chests) and metadata.
//
// Controls:
//   R / Space  regenerate with a fresh random seed   S       set seed
//   1..4       room count preset                      5..8     corridor style
//   P          cycle floor pattern                    D       cycle decor set
//   V          toggle 2D / 2.5D view                  1..0     wall height
//   T          toggle the CC0 tileset preview panel
//
// Art: dungeon tileset is CC0 by "Buch" (OpenGameArt), used here as a loaded
// texture preview — see textures/README.

if (!("gen" in getroottable())) gen <- null;
if (!("seed" in getroottable())) seed <- 20260815;
if (!("roomCount" in getroottable())) roomCount <- 12;
if (!("corridorStyle" in getroottable())) corridorStyle <- "l";
if (!("floorPattern" in getroottable())) floorPattern <- "brick";
if (!("decorSet" in getroottable())) decorSet <- "mixed";
if (!("decorDensity" in getroottable())) decorDensity <- 0.06;
if (!("view3D" in getroottable())) view3D <- false;
if (!("wallHeight" in getroottable())) wallHeight <- 5.0;
if (!("showTiles" in getroottable())) showTiles <- true;
if (!("tileTex" in getroottable())) tileTex <- null;
if (!("prevKeys" in getroottable())) prevKeys <- {};
if (!("info" in getroottable())) info <- "";
if (!("objInfo" in getroottable())) objInfo <- "";

if (!("TILE" in getroottable())) TILE <- 16;
if (!("MAP_W" in getroottable())) MAP_W <- 48;
if (!("MAP_H" in getroottable())) MAP_H <- 34;
if (!("BASE_X" in getroottable())) BASE_X <- 40;
if (!("BASE_Y" in getroottable())) BASE_Y <- 40;

function pressed(name) {
    local down = keyboard.isDown(name);
    local old = ("k_" + name) in prevKeys ? prevKeys["k_" + name] : false;
    prevKeys["k_" + name] <- down;
    return down && !old;
}

function colors() {
    return {
        bg      = [0.07, 0.09, 0.12, 1.0],
        floor   = [ [0.30,0.27,0.23], [0.34,0.31,0.26], [0.28,0.30,0.26], [0.36,0.33,0.28] ],
        wall    = [0.16,0.17,0.19],
        wallOpen= [0.42,0.44,0.47],
        decor   = [ [0.42,0.55,0.30], [0.50,0.42,0.30], [0.55,0.50,0.55] ],
        spawn   = [0.25,0.80,0.90],
        stairs  = [0.95,0.60,0.20],
        pillar  = [0.55,0.52,0.48],
        chest   = [0.80,0.65,0.30],
    };
}

function regenerate(useRandom) {
    if (useRandom) seed = procgen.randomSeed();
    local p = procgen.newParams();
    p.setSeed(seed);
    p.setSize(MAP_W, MAP_H);
    p.setInt("roomCount", roomCount);
    p.setString("corridorStyle", corridorStyle);
    p.setString("floorPattern", floorPattern);
    p.setString("decorSet", decorSet);
    p.setFloat("decorDensity", decorDensity);
    p.setInt("autotile", 1);

    gen = procgen.generate("level.roguelike", p);
    if (gen == null) {
        info = "GENERATE FAILED: " + procgen.lastError();
        return;
    }

    objInfo = "";
    local n = gen.getObjectCount();
    for (local i = 0; i < n; i++) {
        if (i > 0) objInfo += "  ";
        objInfo += gen.getObjectType(i) + "@(" + gen.getObjectX(i).tointeger() + "," +
                   gen.getObjectY(i).tointeger() + ")";
    }
    info = "seed=" + seed + "  rooms=" + gen.getMeta("rooms", "?") +
           "  pattern=" + gen.getMeta("floorPattern", "?") +
           "  decor=" + gen.getMeta("decorTiles", "?") +
           "  style=" + gen.getMeta("corridorStyle", "?");
}

function cellColor(x, y) {
    // Returns [r,g,b] or null (void).
    local c = gen.getCell(x, y);
    local d = gen.getDetail(x, y);
    if (c == 1) {            // wall
        if (d > 0) return colors().wallOpen;
        return colors().wall;
    }
    if (c == 2 || c == 3) {  // floor / corridor
        if (d >= 100) {
            local k = d - 100;
            return colors().decor[k % colors().decor.len()];
        }
        local v = (d > 0) ? (d - 1) % colors().floor.len() : 0;
        return colors().floor[v];
    }
    return null;
}

function render2D() {
    for (local y = 0; y < MAP_H; y++) {
        for (local x = 0; x < MAP_W; x++) {
            local col = cellColor(x, y);
            if (col == null) continue;
            gfx.drawSolidRect(BASE_X + x * TILE, BASE_Y + y * TILE, TILE, TILE,
                              col[0], col[1], col[2], 1.0);
        }
    }
}

// 2.5D: rows get a subtle vertical offset and walls are extruded upward like
// sprite-stacked pseudo-3D columns, so open directions read as lighter steps.
function render25D() {
    for (local y = 0; y < MAP_H; y++) {
        local lift = y * 2.0;
        for (local x = 0; x < MAP_W; x++) {
            local c = gen.getCell(x, y);
            local d = gen.getDetail(x, y);
            local px = BASE_X + x * TILE;
            local py = BASE_Y + y * TILE + lift;

            if (c == 1) {  // wall -> extruded column
                for (local h = 0; h < wallHeight.tointeger(); h++) {
                    local col = (d > 0) ? colors().wallOpen : colors().wall;
                    local f = 1.0 - h * 0.10;
                    gfx.drawSolidRect(px, py - h * 3.0, TILE, TILE,
                                      col[0] * f, col[1] * f, col[2] * f, 1.0);
                }
            } else if (c == 2 || c == 3) {
                local col = cellColor(x, y);
                gfx.drawSolidRect(px, py, TILE, TILE, col[0], col[1], col[2], 1.0);
            }
        }
    }
}

function drawMarkers() {
    local n = gen.getObjectCount();
    for (local i = 0; i < n; i++) {
        local t = gen.getObjectType(i);
        local x = gen.getObjectX(i).tointeger();
        local y = gen.getObjectY(i).tointeger();
        local col = colors().chest;
        if (t == "spawn") col = colors().spawn;
        else if (t == "stairs") col = colors().stairs;
        else if (t == "pillar") col = colors().pillar;
        local px = BASE_X + x * TILE;
        local py = BASE_Y + y * TILE + (view3D ? y * 2.0 : 0);
        gfx.drawSolidRect(px + 3, py + 3, TILE - 6, TILE - 6, col[0], col[1], col[2], 1.0);
    }
}

function drawLegend() {
    // Status bar.
    gfx.drawSolidRect(4, 4, config.width - 8, 26, 0.12, 0.14, 0.18, 0.95);
    gfx.drawSolidRect(8, 8, 8, 8, colors().floor[0][0], colors().floor[0][1], colors().floor[0][2], 1.0);
    gfx.drawSolidRect(8, 18, 8, 8, colors().wall[0], colors().wall[1], colors().wall[2], 1.0);
    gfx.drawSolidRect(20, 8, 8, 8, colors().spawn[0], colors().spawn[1], colors().spawn[2], 1.0);
    gfx.drawSolidRect(20, 18, 8, 8, colors().stairs[0], colors().stairs[1], colors().stairs[2], 1.0);
}

function drawTilesPanel() {
    if (!showTiles || tileTex == null) return;
    // CC0 "dungeon tileset" (Buch) loaded from the game directory.
    gfx.drawTexturedRect(tileTex, config.width - 210, 40, 200, 200, 1, 1, 1, 1);
}

if (tileTex == null) {
    tileTex = gfx.newTextureFromFile("textures/dungeon_tiles.png");
}
if (gen == null) regenerate(false);

function update(dt) {
    if (pressed("r") || pressed("R") || pressed("space")) { regenerate(true); }
    if (pressed("s") || pressed("S")) { seed = (seed * 1664525 + 1013904223) & 0x7FFFFFFF; regenerate(false); }
    if (pressed("1")) { roomCount = 6;  regenerate(false); }
    if (pressed("2")) { roomCount = 12; regenerate(false); }
    if (pressed("3")) { roomCount = 18; regenerate(false); }
    if (pressed("4")) { roomCount = 26; regenerate(false); }
    if (pressed("5")) { corridorStyle = "l";         regenerate(false); }
    if (pressed("6")) { corridorStyle = "straight";  regenerate(false); }
    if (pressed("7")) { corridorStyle = "diagonal";  regenerate(false); }
    if (pressed("p") || pressed("P")) {
        local pats = ["brick", "checker", "plank", "cobble", "plain"];
        local idx = 0;
        for (local i = 0; i < pats.len(); i++) if (pats[i] == floorPattern) idx = (i + 1) % pats.len();
        floorPattern = pats[idx];
        regenerate(false);
    }
    if (pressed("d") || pressed("D")) {
        local sets = ["none", "pillars", "treasure", "nature", "mixed"];
        local idx = 0;
        for (local i = 0; i < sets.len(); i++) if (sets[i] == decorSet) idx = (i + 1) % sets.len();
        decorSet = sets[idx];
        regenerate(false);
    }
    if (pressed("v") || pressed("V")) view3D = !view3D;
    if (pressed("t") || pressed("T")) showTiles = !showTiles;
    if (pressed("upbracket")) { wallHeight = (wallHeight + 1.0 < 12.0) ? wallHeight + 1.0 : 12.0; }
    if (pressed("rightbracket")) { wallHeight = (wallHeight - 1.0 > 1.0) ? wallHeight - 1.0 : 1.0; }
}

function render() {
    gfx.clear(colors().bg[0], colors().bg[1], colors().bg[2], 1.0);
    if (gen != null) {
        if (view3D) render25D(); else render2D();
        drawMarkers();
    }
    drawTilesPanel();
    drawLegend();
}
