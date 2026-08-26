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
// Art: dungeon tileset is CC0 by "Buch" (OpenGameArt). The generated Grid2D
// is translated into atlas GIDs and rendered by map.TileLayer.

persist gen = null
persist seed = 20260815
persist roomCount = 9
persist corridorStyle = "l"
persist floorPattern = "brick"
persist decorSet = "mixed"
persist decorDensity = 0.10
persist view3D = false
persist wallHeight = 5.0
persist showTiles = false
persist tileTex = null
persist groundTex = null
persist dungeonLayer = null
persist decorLayer = null
persist prevKeys = {}
persist info = ""
persist objInfo = ""

persist TILE = 16.0
persist MAP_W = 26
persist MAP_H = 22
persist BASE_X = 300.0
persist BASE_Y = 112.0

function pressed(name) {
    local slot = "key_" + name;
    local down = keyboard.isDown(name);
    local was = (slot in prevKeys) ? prevKeys[slot] : false;
    prevKeys[slot] <- down;
    return down && !was;
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

function groundAtlasGid(col, row) {
    return row * 8 + col + 1;
}

function sourceAtlasGid(col, row) {
    return row * 24 + col + 1;
}

function isWalkableAt(x, y) {
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return false;
    local cell = gen.getCell(x, y);
    return cell == 2 || cell == 3;
}

function groundRuleGid(x, y) {
    // 4-bit terrain mask: E=1, S=2, W=4, N=8. The CC0 community
    // ground sheet stores all 16 valid combinations in its top-left 4 x 4.
    local mask = 0;
    if (isWalkableAt(x + 1, y)) mask = mask | 1;
    if (isWalkableAt(x, y + 1)) mask = mask | 2;
    if (isWalkableAt(x - 1, y)) mask = mask | 4;
    if (isWalkableAt(x, y - 1)) mask = mask | 8;
    local cols = [3, 0, 3, 0, 2, 1, 2, 1, 3, 0, 3, 0, 2, 1, 2, 1];
    local rows = [3, 3, 0, 0, 3, 3, 0, 0, 2, 2, 1, 1, 2, 2, 1, 1];
    return groundAtlasGid(cols[mask], rows[mask]);
}

function ensureConnected() {
    // Keep the presentation map as one playable dungeon even if a tight room
    // layout makes the generator drop a corridor at the boundary.
    local seen = {};
    local anchors = [];
    local dirs = [[1, 0], [-1, 0], [0, 1], [0, -1]];
    for (local sy = 0; sy < MAP_H; sy++) {
        for (local sx = 0; sx < MAP_W; sx++) {
            local startKey = sx + ":" + sy;
            if (!isWalkableAt(sx, sy) || startKey in seen) continue;
            anchors.append([sx, sy]);
            local queue = [[sx, sy]];
            seen[startKey] <- true;
            for (local head = 0; head < queue.len(); head++) {
                local p = queue[head];
                foreach (d in dirs) {
                    local nx = p[0] + d[0];
                    local ny = p[1] + d[1];
                    local key = nx + ":" + ny;
                    if (isWalkableAt(nx, ny) && !(key in seen)) {
                        seen[key] <- true;
                        queue.append([nx, ny]);
                    }
                }
            }
        }
    }
    if (anchors.len() < 2) return;
    local root = anchors[0];
    for (local i = 1; i < anchors.len(); i++) {
        local target = anchors[i];
        local x0 = root[0] < target[0] ? root[0] : target[0];
        local x1 = root[0] > target[0] ? root[0] : target[0];
        for (local x = x0; x <= x1; x++) {
            gen.setCell(x, root[1], 3);
            gen.setDetail(x, root[1], 1);
        }
        local y0 = root[1] < target[1] ? root[1] : target[1];
        local y1 = root[1] > target[1] ? root[1] : target[1];
        for (local y = y0; y <= y1; y++) {
            gen.setCell(target[0], y, 3);
            gen.setDetail(target[0], y, 1);
        }
    }
}

function syncTileLayers() {
    if (dungeonLayer == null || decorLayer == null || gen == null) return;
    for (local y = 0; y < MAP_H; y++) {
        for (local x = 0; x < MAP_W; x++) {
            if (!isWalkableAt(x, y)) {
                dungeonLayer.setTile(x, y, 0);
                decorLayer.setTile(x, y, 0);
                continue;
            }
            dungeonLayer.setTile(x, y, groundRuleGid(x, y));
            local detail = gen.getDetail(x, y);
            // Original sheet coordinates: small crate, barrel, standing torch.
            local decor = [sourceAtlasGid(9, 6), sourceAtlasGid(13, 6), sourceAtlasGid(12, 7)];
            local gid = detail >= 100 ? decor[(detail - 100) % decor.len()] : 0;
            local horizontalDoor = !isWalkableAt(x - 1, y) && !isWalkableAt(x + 1, y) &&
                                   isWalkableAt(x, y - 1) && isWalkableAt(x, y + 1);
            local verticalDoor = !isWalkableAt(x, y - 1) && !isWalkableAt(x, y + 1) &&
                                 isWalkableAt(x - 1, y) && isWalkableAt(x + 1, y);
            if ((horizontalDoor || verticalDoor) && ((x * 17 + y * 31 + seed) % 5 == 0))
                gid = sourceAtlasGid(10, 12);
            decorLayer.setTile(x, y, gid);
        }
    }

    // Generated objects are a third semantic stream. Place their actual atlas
    // art over the terrain instead of covering them with debug-color squares.
    local objectGids = {
        spawn = sourceAtlasGid(11, 10),
        stairs = sourceAtlasGid(14, 3),
        pillar = sourceAtlasGid(13, 6),
        chest = sourceAtlasGid(9, 6)
    };
    for (local i = 0; i < gen.getObjectCount(); i++) {
        local type = gen.getObjectType(i);
        if (!(type in objectGids)) continue;
        local x = gen.getObjectX(i).tointeger();
        local y = gen.getObjectY(i).tointeger();
        decorLayer.setTile(x, y, objectGids[type]);
    }
}

function regenerate(useRandom) {
    if (useRandom) seed = procgen.randomSeed();
    local p = procgen.newParams();
    p.setSeed(seed);
    p.setSize(MAP_W, MAP_H);
    p.setInt("roomCount", roomCount);
    p.setInt("roomMin", 3);
    p.setInt("roomMax", 6);
    p.setInt("spacing", 1);
    p.setInt("corridorWidth", 1);
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
    ensureConnected();

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
    syncTileLayers();
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
    gfx.drawSolidRect(4.0, 4.0, config.width - 8.0, 26.0, 0.12, 0.14, 0.18, 0.95);
    gfx.drawSolidRect(8.0, 8.0, 8.0, 8.0, colors().floor[0][0], colors().floor[0][1], colors().floor[0][2], 1.0);
    gfx.drawSolidRect(8.0, 18.0, 8.0, 8.0, colors().wall[0], colors().wall[1], colors().wall[2], 1.0);
    gfx.drawSolidRect(20.0, 8.0, 8.0, 8.0, colors().spawn[0], colors().spawn[1], colors().spawn[2], 1.0);
    gfx.drawSolidRect(20.0, 18.0, 8.0, 8.0, colors().stairs[0], colors().stairs[1], colors().stairs[2], 1.0);
}

function drawTilesPanel() {
    if (!showTiles || tileTex == null) return;
    // CC0 "dungeon tileset" (Buch) loaded from the game directory.
    gfx.drawTexturedRect(tileTex, config.width - 210.0, 40.0, 200.0, 200.0, 1.0, 1.0, 1.0, 1.0);
}

if (tileTex == null) {
    tileTex = gfx.newTextureFromFile("textures/dungeon_tiles.png");
}
if (groundTex == null) {
    groundTex = gfx.newTextureFromFile("textures/dungeon_tiles_ground.png");
}
if (dungeonLayer == null && groundTex != null) {
    dungeonLayer = map.newLayer(MAP_W, MAP_H, TILE, TILE);
    dungeonLayer.setOrigin(BASE_X, BASE_Y);
    dungeonLayer.setLayer(0);
}
if (decorLayer == null && groundTex != null) {
    decorLayer = map.newLayer(MAP_W, MAP_H, TILE, TILE);
    decorLayer.setOrigin(BASE_X, BASE_Y);
    decorLayer.setLayer(1);
}
if (dungeonLayer != null) {
    dungeonLayer.setTileset(groundTex, 1, 8, 0, 0);
    dungeonLayer.setTilesetTileSize(16, 16);
}
if (decorLayer != null) {
    decorLayer.setTileset(tileTex, 1, 24, 0, 0);
    decorLayer.setTilesetTileSize(16, 16);
}
if (gen == null) regenerate(false);
else syncTileLayers();
gfx.setBackgroundColor(colors().bg[0], colors().bg[1], colors().bg[2], 1.0);

function eve_update(dt) {
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

function eve_render() {
    gfx.clear();
    if (gen != null) {
        dungeonLayer.setVisible(!view3D);
        decorLayer.setVisible(!view3D);
        if (view3D) render25D(); else map.render(gfx);
        if (view3D) drawMarkers();
    }
    drawTilesPanel();
    drawLegend();
}
