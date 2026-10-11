// examples/hex-terrain-3d -- interactive 3D hex map.
//
// This is the reference example for EVEngine's `hexmap` module:
//
//   * the editable pointy-top cell grid, its chunk partition, the picking
//     query, the mesh builder and the fog-of-war / unit / pathfinding state all
//     live in C++ (`src/modules/hexmap`);
//   * this script owns the camera, the input mapping, the brush tool, the
//     per-chunk renderables, the unit renderable pool and the HUD.
//
// Working today: elevation with terraced slopes and cliffs, water and shore
// bands, drag-painted rivers and roads, orbit/zoom/pan camera, mouse picking
// with a hex cursor, a hex-shaped brush over five edit modes, the fog-of-war
// overlay, starter units with a planned-path preview and travel, a binary
// save/load round trip, and -- round three -- the city/farm wall, tower and
// bridge geometry plus the urban / farm / plant / special decorations, on both
// the hand-authored starter map and a procedurally generated one.
//
// The fog, the units and the explored latch are round-two additions: the module
// owns their state, this script only reads samples and pushes the results into
// renderables. Round three keeps that split: the module owns the wall flags, the
// feature levels and the generator, and this script only calls the setters and
// binds the two new surface streams.

// Surface streams produced by the C++ mesh builder, in `hexmap.HexSurface` order.
const SURFACE_TERRAIN = 0;
const SURFACE_WATER   = 1;
const SURFACE_RIVER   = 2;
const SURFACE_ROAD    = 3;
const SURFACE_FOG     = 4;
const SURFACE_WALL    = 5;
const SURFACE_FEATURE = 6;
const SURFACE_COUNT   = 7;

// Procedural map tunables, passed straight through to `hexmap.generateMap` and
// identical to the module's own `HexMapGeneratorSettings` defaults.
const SEED_LAND_PERCENT = 50;   // target share of the map left above the water line
const SEED_WATER_LEVEL  = 3;    // cells at or below this elevation are flooded
const SEED_RIVER_PERCENT = 10;  // target share of the map covered by rivers

// Hand-authored walled district for the starter map. It is a (2r+1) square of
// walled cells whose centre is offset from the map centre, so the same two numbers
// place it on every entry in `MAP_SIZES`. The wall mesh only draws the borders a
// walled cell shares with an unwalled one, so the filled square renders as an
// enclosing ring; the road corridor cut through it opens a gate on each side.
const DISTRICT_RADIUS   = 2;    // block half-extent in cells: a (2r+1) square
const DISTRICT_SETBACK  = 0;    // shift of the block's centre from the map centre

// Brush modes.
const MODE_ELEVATION = 0;
const MODE_WATER     = 1;
const MODE_TERRAIN   = 2;
const MODE_RIVER     = 3;
const MODE_ROAD      = 4;
const MODE_COUNT     = 5;

// Terrain palette order matches the reference editor: Sand, Grass, Mud, Stone, Snow.
TERRAIN_NAMES <- ["Sand", "Grass", "Mud", "Stone", "Snow"];

// Selectable map sizes; the grid requires multiples of the 5x5 chunk size.
MAP_SIZES <- [20, 30, 40];
const MAX_CHUNK_COLUMNS = 8;

// Units.
//
// Unit ids are positional inside the module (a removal shifts every later id
// down by one), so the renderable pool is indexed by unit id directly instead of
// by a second id-to-slot table: slot `i` always draws unit `i`, and slots past
// `unitCount()` are hidden.
const MAX_UNITS = 16;
const UNIT_SCALE = 1.6;      // scale of the shared 1x1x1 cube mesh
const UNIT_LIFT = 2.2;       // cube centre above the cell surface
const UNIT_TURN = 0.0;       // starter facing, in degrees
const PATH_LIFT = 0.45;      // keeps the planned path just above the hex tops
const DEG_TO_RAD = 0.017453292519943295;

// Camera ranges follow the reference HexMapCamera, but the far distance is
// derived from the map extent: the reference ships a 20x15 editor map, while this
// example can build a 40x40 one whose diagonal is far wider than 250 world units.
// Without that, the far end of the zoom range could never frame the map.
const ZOOM_NEAR_DISTANCE = 45.0;
const ZOOM_MIN_FAR       = 250.0;
const ZOOM_FAR_MARGIN    = 1.55;   // far distance as a multiple of the map span
const ZOOM_NEAR_SWIVEL   = 45.0;
const ZOOM_FAR_SWIVEL    = 62.0;   // never fully horizontal: a flat map goes edge-on
const ZOOM_NEAR_SPEED    = 100.0;
const ZOOM_FAR_SPEED     = 400.0;
const ROTATE_SPEED       = 90.0;
const ZOOM_STEP          = 0.09;
// Framing: the camera must sit back far enough for the map to fit the vertical
// field of view, with a little margin.
const CAMERA_FOV_DEGREES = 50.0;
const ZOOM_FIT_MARGIN    = 1.15;

// Hex geometry, mirrored from the C++ metrics (outerRadius 10).
const HEX_OUTER = 10.0;
const HEX_INNER = 8.6602540378;

persist hexMap = null;

function createHexState() {
    return {
        renderables = [],   // flat pool: (chunkRow * MAX_CHUNK_COLUMNS + chunkColumn) * SURFACE_COUNT + surface
        configured = [],
        shaders = [],
        highlight = null,
        camera = null,
        yaw = 35.0,
        zoom = 0.5,
        pivotX = 0.0,
        pivotZ = 0.0,
        sizeIndex = 0,
        seed = 20260915,
        mode = MODE_ELEVATION,
        brushRadius = 1,
        terrainType = 1,
        hoverX = -1,
        hoverZ = -1,
        hasHover = false,
        hoverChanged = true,
        dragX = -1,
        dragZ = -1,
        dragging = false,
        elapsed = 0.0,
        frameCount = 0,
        screenshotSaved = false,
        meshCount = 0,
        statusText = "building...",
        uiBuilt = false,
        lastLeft = false,
        lastRight = false,

        // Round-two state: one shared unit mesh plus a pool of unit renderables,
        // the selected unit id, the planned path and the save file path.
        unitMesh = null,
        unitRenderables = [],
        selectedId = 0,
        selectedTraveling = false,
        selectedCellX = -1,
        selectedCellZ = -1,
        pathPreview = [],
        pathDirty = false,
        pathLine = null,
        savePath = "",
        saveFs = null,
    };
}

// --- camera -----------------------------------------------------------------

/** Largest XZ extent of the current map, in world units; 0 when no grid exists. */
function mapSpan(st) {
    if (!hexmap.hasGrid()) return 0.0;
    local lastX = hexmap.cellCountX() - 1;
    local lastZ = hexmap.cellCountZ() - 1;
    local spanX = hexmap.cellPositionX(lastX, lastZ) - hexmap.cellPositionX(0, 0);
    local spanZ = hexmap.cellPositionZ(lastX, lastZ) - hexmap.cellPositionZ(0, 0);
    return spanX > spanZ ? spanX : spanZ;
}

function cameraFarDistance(st) {
    local needed = mapSpan(st) * ZOOM_FAR_MARGIN;
    return needed > ZOOM_MIN_FAR ? needed : ZOOM_MIN_FAR;
}

function cameraDistance(st) {
    // zoom 0 = closest, zoom 1 = farthest.
    local near = ZOOM_NEAR_DISTANCE;
    local far = cameraFarDistance(st);
    return near + (far - near) * st.zoom;
}

function cameraSwivel(st) {
    return ZOOM_NEAR_SWIVEL + (ZOOM_FAR_SWIVEL - ZOOM_NEAR_SWIVEL) * st.zoom;
}

/**
 * Zoom that frames the whole map: the distance at which the map span fits the
 * vertical field of view, converted back into the 0..1 zoom range.
 */
function framingZoom(st) {
    local span = mapSpan(st);
    if (span <= 0.0) return 0.35;
    local near = ZOOM_NEAR_DISTANCE;
    local far = cameraFarDistance(st);
    local halfFov = CAMERA_FOV_DEGREES * 0.5 * 0.017453292519943295;
    local wanted = span * ZOOM_FIT_MARGIN / (2.0 * tan(halfFov));
    return clampf((wanted - near) / (far - near), 0.0, 1.0);
}

function cameraMoveSpeed(st) {
    return ZOOM_NEAR_SPEED + (ZOOM_FAR_SPEED - ZOOM_NEAR_SPEED) * st.zoom;
}

function applyCamera(st) {
    if (st.camera == null) return;
    local distance = cameraDistance(st);
    local swivel = cameraSwivel(st) * 0.017453292519943295;
    local yaw = st.yaw * 0.017453292519943295;
    local horizontal = distance * cos(swivel);
    // `eve.Camera3D()` defaults to a 0.1..100 clip range, which is smaller than a
    // single map span once the grid grows past a few chunks: everything beyond 100
    // units was silently clipped away, so the far end of the zoom range showed an
    // empty frame. Track the map extent instead of hard-coding a range.
    local far = distance + mapSpan(st) * 1.5 + 100.0;
    st.camera.setClipPlanes(0.5, far);
    st.camera.setEye(st.pivotX - horizontal * sin(yaw),
                     distance * sin(swivel),
                     st.pivotZ - horizontal * cos(yaw));
    st.camera.setTarget(st.pivotX, 0.0, st.pivotZ);
}

function clampPivot(st) {
    if (!hexmap.hasGrid()) return;
    local lastX = hexmap.cellCountX() - 1;
    local lastZ = hexmap.cellCountZ() - 1;
    local margin = 30.0;
    local minX = hexmap.cellPositionX(0, 0) - margin;
    local maxX = hexmap.cellPositionX(lastX, lastZ) + margin;
    local minZ = hexmap.cellPositionZ(0, 0) - margin;
    local maxZ = hexmap.cellPositionZ(lastX, lastZ) + margin;
    st.pivotX = clampf(st.pivotX, minX, maxX);
    st.pivotZ = clampf(st.pivotZ, minZ, maxZ);
}

function updateCamera(st, dt) {
    local speed = cameraMoveSpeed(st);
    // Reference panning: camera-relative on the XZ plane.
    local yaw = st.yaw * 0.017453292519943295;
    local forwardX = -sin(yaw), forwardZ = -cos(yaw);
    local rightX = cos(yaw), rightZ = -sin(yaw);
    local move = speed * dt;
    if (keyboard.isDown("w") || keyboard.isDown("up")) {
        st.pivotX += forwardX * move; st.pivotZ += forwardZ * move;
    }
    if (keyboard.isDown("s") || keyboard.isDown("down")) {
        st.pivotX -= forwardX * move; st.pivotZ -= forwardZ * move;
    }
    if (keyboard.isDown("d") || keyboard.isDown("right")) {
        st.pivotX += rightX * move; st.pivotZ += rightZ * move;
    }
    if (keyboard.isDown("a") || keyboard.isDown("left")) {
        st.pivotX -= rightX * move; st.pivotZ -= rightZ * move;
    }
    if (keyboard.isDown("q")) st.yaw += ROTATE_SPEED * dt;
    if (keyboard.isDown("e")) st.yaw -= ROTATE_SPEED * dt;
    if (keyboard.isDown("z")) st.zoom = clampf(st.zoom - dt * 0.6, 0.0, 1.0);
    if (keyboard.isDown("x")) st.zoom = clampf(st.zoom + dt * 0.6, 0.0, 1.0);

    local wheel = mouse.getWheelY();
    if (wheel != 0.0) st.zoom = clampf(st.zoom - wheel * ZOOM_STEP, 0.0, 1.0);

    clampPivot(st);
    applyCamera(st);
}

// --- shaders and renderables ------------------------------------------------

function createShaders(st) {
    if (st.shaders.len() > 0) return;
    // The engine's built-in Mesh3D vertex stage already produces the varyings these
    // fragment shaders consume, so only the fragment stages are shipped (as SPIR-V:
    // runtime GLSL compilation is unavailable on Windows). The list is indexed by
    // surface stream, so it must stay in `HexSurface` order; the wall and the
    // feature stream share one shader, which is therefore listed twice.
    local frags = ["shaders/hex_map_terrain.frag.spv", "shaders/hex_map_water.frag.spv",
                   "shaders/hex_map_river.frag.spv", "shaders/hex_map_road.frag.spv",
                   "shaders/hex_map_fog.frag.spv", "shaders/hex_map_feature.frag.spv",
                   "shaders/hex_map_feature.frag.spv"];
    for (local s = 0; s < SURFACE_COUNT; s++) {
        local loaded = gfx.loadMeshShaderSpv("", frags[s]);
        if (!loaded.ok) {
            print("hex map: shader " + frags[s] + " failed: " + loaded.status.summary + "\n");
            st.shaders.append(null);
            continue;
        }
        st.shaders.append(loaded.value);
    }
}

function renderableSlot(chunkColumn, chunkRow, surface) {
    return (chunkRow * MAX_CHUNK_COLUMNS + chunkColumn) * SURFACE_COUNT + surface;
}

function ensureRenderable(st, slot) {
    while (st.renderables.len() <= slot) {
        st.renderables.append(null);
        st.configured.append(false);
    }
    if (st.renderables[slot] == null) {
        local r = eve.Renderable3D();
        r.setVisible(false);
        st.renderables[slot] = r;
    }
    return st.renderables[slot];
}

function hideAllRenderables(st) {
    foreach (r in st.renderables) {
        if (r != null) r.setVisible(false);
    }
}

/** Hides every pooled unit renderable; `updateUnits` shows the live ones again. */
function hideUnitRenderables(st) {
    for (local id = 0; id < st.unitRenderables.len(); id++)
        st.unitRenderables[id].setVisible(false);
}

function syncChunk(st, chunkIndex) {
    local columns = hexmap.chunkCountX();
    local column = chunkIndex % columns;
    local row = chunkIndex / columns;
    // The loop covers all seven streams, so adding `Wall` and `Feature` needed no
    // change here: `renderableSlot` folds `SURFACE_COUNT` in, and the mesh lookup
    // is by surface index, not by name.
    for (local s = 0; s < SURFACE_COUNT; s++) {
        local slot = renderableSlot(column, row, s);
        local r = ensureRenderable(st, slot);
        if (!st.configured[slot]) {
            // Bind the surface shader and shadow flags exactly once per pool slot.
            // The length guard only matters for a session hot-reloaded from a script
            // revision with fewer surfaces; a fresh run always fills all seven.
            local shader = (s < st.shaders.len()) ? st.shaders[s] : null;
            if (shader != null) r.setShader(shader);
            // The terrain casts onto everything else but does not receive: its very thin
            // blend/terrace quads self-shadow as a sawtooth acne band along every hex edge.
            // Water, river, road, the opaque fog columns and both new streams (walls and
            // decorations are small free-standing geometry) neither cast nor receive, so
            // only the terrain slot gets either flag.
            r.setCastShadow(s == SURFACE_TERRAIN);
            r.setReceiveShadow(s == SURFACE_ROAD);
            st.configured[slot] = true;
        }
        local mesh = hexmap.chunkMeshAt(chunkIndex, s);
        if (mesh == null) {
            r.setVisible(false);
        } else {
            r.setMesh(mesh);
            r.setVisible(true);
        }
    }
}

function rebuildAllChunks(st) {
    local rebuilt = 0;
    for (;;) {
        local chunkIndex = hexmap.takeDirtyChunk();
        if (chunkIndex < 0) break;
        rebuilt += hexmap.rebuildChunk(gfx, chunkIndex);
        syncChunk(st, chunkIndex);
    }
    st.meshCount = rebuilt;
}

/**
 * Rebuilds the dirty chunks, if any.
 *
 * Fog visibility is module-owned and already marks the affected chunks dirty when
 * a unit is added/removed, walks into a new cell or when the explored latch flips,
 * so the frame loop must not mark whole chunks dirty by hand: it only has to drain
 * whatever the module flagged.
 */
function updateDirtyChunks(st) {
    if (hexmap.dirtyChunkCount() <= 0) return;
    rebuildAllChunks(st);
}

function hideUnusedRenderables(st) {
    local columns = hexmap.chunkCountX();
    local rows = hexmap.chunkCountZ();
    // Also folded on `SURFACE_COUNT`, so the two new streams are covered too.
    for (local row = 0; row < MAX_CHUNK_COLUMNS; row++) {
        for (local column = 0; column < MAX_CHUNK_COLUMNS; column++) {
            if (row < rows && column < columns) continue;
            for (local s = 0; s < SURFACE_COUNT; s++) {
                local slot = renderableSlot(column, row, s);
                if (slot < st.renderables.len() && st.renderables[slot] != null)
                    st.renderables[slot].setVisible(false);
            }
        }
    }
}

// --- map lifecycle ----------------------------------------------------------

function buildMap(st) {
    hideAllRenderables(st);
    hideUnitRenderables(st);
    local size = MAP_SIZES[st.sizeIndex];
    local created = hexmap.newGrid(gfx, size, size, st.seed);
    if (!created.ok) {
        st.statusText = "newGrid failed: " + created.status.summary;
        print("hex map: " + st.statusText + "\n");
        return;
    }
    createShaders(st);
    seedStarterMap(st);
    exploreAll(st);
    placeStarterUnits(st);
    rebuildAllChunks(st);
    hideUnusedRenderables(st);

    st.pivotX = (hexmap.cellPositionX(0, 0) + hexmap.cellPositionX(size - 1, size - 1)) * 0.5;
    st.pivotZ = (hexmap.cellPositionZ(0, 0) + hexmap.cellPositionZ(size - 1, size - 1)) * 0.5;
    st.zoom = framingZoom(st);
    st.hoverX = -1; st.hoverZ = -1; st.hasHover = false;
    clearPathPreview(st);
    st.selectedId = 0;
    st.statusText = "chunks " + hexmap.chunkCountX() + "x" + hexmap.chunkCountZ();

    print("hex map ready: " + hexmap.cellCountX() + "x" + hexmap.cellCountZ() + " cells, " +
          hexmap.chunkCount() + " chunks, " + hexmap.unitCount() + " units\n");
}

/**
 * Replaces the grid with a procedurally generated map at the current size.
 *
 * `generateMap` re-seeds the grid at the size that is already set, releases every
 * chunk mesh and drops all units and all fog state. Its contract then mirrors the
 * end of `buildMap` exactly, so this runs the same sequence: the caller places new
 * units, re-explores (a generated map starts with no explored cell, and the
 * pathfinder refuses to enter one), rebuilds every chunk -- the module marked them
 * all dirty, and no renderable points at the new meshes yet -- and hides the pool
 * slots outside the new chunk count. The generator honours the generator seed, so
 * the same seed and the same size always produce the same map.
 */
function reseedProceduralMap(st) {
    st.seed = procgen.randomSeed();
    local generated = hexmap.generateMap(gfx, st.seed, SEED_LAND_PERCENT, SEED_WATER_LEVEL, SEED_RIVER_PERCENT);
    if (!generated.ok) {
        st.statusText = "generate failed: " + generated.status.summary;
        print("hex map: " + st.statusText + "\n");
        return;
    }

    // The map size did not change, so `sizeIndex` stays valid; the module already
    // released the old meshes, so every pool slot has to be re-pointed below.
    hideAllRenderables(st);
    hideUnitRenderables(st);
    st.hasHover = false;
    st.hoverX = -1; st.hoverZ = -1;
    clearPathPreview(st);
    exploreAll(st);
    placeStarterUnits(st);
    hexmap.markAllChunksDirty();
    rebuildAllChunks(st);
    hideUnusedRenderables(st);

    local size = MAP_SIZES[st.sizeIndex];
    st.pivotX = (hexmap.cellPositionX(0, 0) + hexmap.cellPositionX(size - 1, size - 1)) * 0.5;
    st.pivotZ = (hexmap.cellPositionZ(0, 0) + hexmap.cellPositionZ(size - 1, size - 1)) * 0.5;
    st.zoom = framingZoom(st);
    st.statusText = "generated seed " + st.seed + ", " + hexmap.chunkCountX() + "x" + hexmap.chunkCountZ() +
                    " chunks, " + hexmap.unitCount() + " units";
    print("hex map: " + st.statusText + "\n");
}

// --- picking ----------------------------------------------------------------

function updateHover(st) {
    local previousX = st.hoverX, previousZ = st.hoverZ;
    st.hasHover = false;
    if (hexmap.hasGrid() && st.camera != null) {
        st.camera.screenToRay(mouse.getX(), mouse.getY(),
                              gfx.getWidth().tofloat(), gfx.getHeight().tofloat());
        local hit = hexmap.pickCell(st.camera.getScreenRayOriginX(), st.camera.getScreenRayOriginY(),
                                    st.camera.getScreenRayOriginZ(), st.camera.getScreenRayDirX(),
                                    st.camera.getScreenRayDirY(), st.camera.getScreenRayDirZ());
        if (hit.ok) {
            st.hoverX = hit.value[0].tointeger();
            st.hoverZ = hit.value[1].tointeger();
            st.hasHover = true;
        }
    }
    st.hoverChanged = (st.hoverX != previousX) || (st.hoverZ != previousZ);
}

function updateHighlight(st) {
    if (st.highlight == null) return;
    if (!st.hasHover) {
        st.highlight.setVisible(false);
        return;
    }
    local cx = hexmap.cellPositionX(st.hoverX, st.hoverZ);
    local cy = hexmap.cellPositionY(st.hoverX, st.hoverZ) + 0.18;
    local cz = hexmap.cellPositionZ(st.hoverX, st.hoverZ);
    local points = [
        cx, cy, cz + HEX_OUTER,
        cx + HEX_INNER, cy, cz + 0.5 * HEX_OUTER,
        cx + HEX_INNER, cy, cz - 0.5 * HEX_OUTER,
        cx, cy, cz - HEX_OUTER,
        cx - HEX_INNER, cy, cz - 0.5 * HEX_OUTER,
        cx - HEX_INNER, cy, cz + 0.5 * HEX_OUTER
    ];
    st.highlight.setPolyline(points, true);
    st.highlight.setVisible(true);
}

// --- hex cell addressing ----------------------------------------------------
//
// The module addresses cells with odd-row offset `(column, row)` pairs; the
// step table is defined on axial coordinates, so every neighbour query converts
// offset -> axial -> offset.

function axialX(offsetX, offsetZ) {
    return offsetX - offsetZ / 2;
}

function neighborOffset(x, z, direction) {
    local ax = axialX(x, z), az = z;
    if (direction == 0) az += 1;                 // NE
    else if (direction == 1) ax += 1;            // E
    else if (direction == 2) { ax += 1; az -= 1; } // SE
    else if (direction == 3) az -= 1;            // SW
    else if (direction == 4) ax -= 1;            // W
    else { ax -= 1; az += 1; }                   // NW
    if (az < 0) return null;
    return [ax + az / 2, az];
}

function directionBetween(ax, az, bx, bz) {
    local dx = axialX(bx, bz) - axialX(ax, az), dz = bz - az;
    if (dx == 0 && dz == 1) return 0;    // NE
    if (dx == 1 && dz == 0) return 1;    // E
    if (dx == 1 && dz == -1) return 2;   // SE
    if (dx == 0 && dz == -1) return 3;   // SW
    if (dx == -1 && dz == 0) return 4;   // W
    if (dx == -1 && dz == 1) return 5;   // NW
    return -1;
}

// --- starter scene ----------------------------------------------------------

/** Carves a river from `(x, z)` by repeatedly flowing to the lowest neighbour. */
function carveRiver(st, x, z, maxSteps) {
    local size = MAP_SIZES[st.sizeIndex];
    local previousX = -1, previousZ = -1;
    // A channel that re-enters a cell it already carved would call
    // `setOutgoingRiver` again, and that removes the cell's previous connection
    // first --?which erases the stretch of channel behind it. Remembering the
    // visited cells turns any such cycle into a clean stop. A linear scan is used
    // rather than a table keyed by the cell, because a plain array is the only
    // container this script dialect is guaranteed to have.
    local visited = [];
    for (local step = 0; step < maxSteps; step++) {
        visited.append(x * 1000 + z);
        // The lowest neighbour wins. `bestElevation` must start above every real
        // elevation, not at the current cell's: on flat ground (which is most of this
        // map) no neighbour is strictly lower, and seeding it with the current
        // elevation made the search give up on step zero and carve nothing at all.
        local bestDirection = -1, bestElevation = 1000;
        for (local d = 0; d < 6; d++) {
            local n = neighborOffset(x, z, d);
            if (n == null) continue;
            if (n[0] < 0 || n[1] < 0 || n[0] >= size || n[1] >= size) continue;
            // Never step straight back: two cells at the same elevation would
            // otherwise bounce the channel between them for the whole budget.
            if (n[0] == previousX && n[1] == previousZ) continue;
            if (visited.find(n[0] * 1000 + n[1]) != null) continue;
            local e = hexmap.elevation(n[0], n[1]);
            if (e < bestElevation) { bestElevation = e; bestDirection = d; }
        }
        // A river may cross level ground, but it may never flow uphill; the module
        // rejects that itself, so stopping here keeps the failure off the log.
        if (bestDirection < 0) break;
        if (bestElevation > hexmap.elevation(x, z)) break;
        local next = neighborOffset(x, z, bestDirection);
        // Flowing into an existing channel is a confluence, not a continuation.
        local joinsExisting = hexmap.hasRiver(next[0], next[1]);
        if (!hexmap.setOutgoingRiver(x, z, bestDirection).ok) break;
        previousX = x;
        previousZ = z;
        x = next[0];
        z = next[1];
        if (joinsExisting) break;
    }
}

/**
 * Walls or unwalls a solid rectangular block of cells.
 *
 * The wall mesh stream draws the borders between a walled and an unwalled cell, so
 * a filled block yields the enclosing ring: the cells outside it stay unwalled and
 * the interior borders are never emitted. A block crossed by a road therefore
 * leaves a gap in the ring exactly where the road enters and leaves.
 */
function wallBlock(st, x0, z0, x1, z1, walled) {
    for (local z = z0; z <= z1; z++) {
        for (local x = x0; x <= x1; x++)
            hexmap.setWalled(x, z, walled);
    }
}

/**
 * Builds the deterministic starter map so the terrain, terrace, cliff, water,
 * river, road, wall and decoration systems are all visible on the first frame.
 *
 * The module ships a procedural generator; this hand-authored map is the stand-in
 * for it and `[M]` runs the real generator on demand. The walled district below
 * deliberately reproduces the shape the generator produces for a walled city --
 * a ring of walled cells with a road corridor running through it and urban / farm
 * / plant / special features around it -- so both paths exercise the same geometry.
 */
function seedStarterMap(st) {
    local size = MAP_SIZES[st.sizeIndex];
    local mid = size / 2;

    // Fill every terrain band first, then raise and flood features over it.
    for (local z = 0; z < size; z++) {
        for (local x = 0; x < size; x++)
            hexmap.setTerrainType(x, z, ((x / 6) + (z / 6)) % TERRAIN_NAMES.len());
    }

    // A two-step central plateau: elevation 2 over radius 6, elevation 5 over radius 2.
    // Terrain is painted widest-first so the narrower bands stay on top.
    hexmap.editElevation(mid, mid, 6, 2);
    hexmap.editElevation(mid, mid, 2, 3);
    hexmap.editTerrainType(mid, mid, 5, 1);
    hexmap.editTerrainType(mid, mid, 2, 3);

    // A flooded basin in the south-west corner: carved down, then filled.
    local basinX = 3, basinZ = size - 4;
    hexmap.editElevation(basinX, basinZ, 3, -2);
    hexmap.editTerrainType(basinX, basinZ, 3, 2);
    hexmap.editWaterLevel(basinX, basinZ, 3, 2);

    // Rivers run off the plateau to the map edge.
    carveRiver(st, mid, mid + 7, 40);
    carveRiver(st, mid + 7, mid - 5, 40);

    // A road crossing the lowland, one west-east line and one north-south spur.
    // The spur is the one that threads the walled district below.
    for (local x = 1; x + 1 < size; x++) hexmap.addRoad(x, size / 4, 1);
    for (local z = size / 4; z < size - 2; z++) hexmap.addRoad(mid + 8, z, 3);

    // --- round three: the walled district and its decorations ---
    //
    // A square block of walled cells centred on the plateau, so the ring sits on
    // flat land and the road spur at column `mid + 8` enters and leaves through the
    // ring's east side: those two borders are where the wall mesh must leave the
    // road gap instead of drawing a segment. The block corners are also where the
    // tower pieces appear.
    local district = mid + DISTRICT_SETBACK;
    local radius = DISTRICT_RADIUS;
    wallBlock(st, district - radius, district - radius, district + radius, district + radius, true);

    // The road spur the district was built around runs at column `mid + 8`, one
    // column east of the block. A road has to exist on *both* sides of a wall
    // border for the wall mesh to open a gate there, so the spur is continued
    // straight through the block: it now enters the ring's east side and runs out
    // through its west side, and those two borders are the gates. Every wall piece
    // the ring would otherwise draw across the road is skipped by the mesh builder.
    for (local z = district - radius; z <= district + radius; z++) {
        for (local x = district - radius; x < district + radius + 1; x++)
            hexmap.addRoad(x, z, 1);
    }

    // Urban fabric inside the district, farmland on the low ground south of the
    // plateau, and vegetation on the plateau shoulders. Setting a feature level
    // never touches the terrain, so an already-rivered or flooded cell simply ends
    // up with the decoration the module picks for it. The block's middle row is the
    // road corridor, so every feature here sits off it.
    hexmap.setUrbanLevel(district - 1, district - 1, 3);
    hexmap.setUrbanLevel(district, district + 1, 2);
    hexmap.setUrbanLevel(district + 1, district - 1, 2);
    hexmap.setUrbanLevel(district - 1, district + 1, 1);
    hexmap.setFarmLevel(district + 3, district + 2, 3);
    hexmap.setFarmLevel(district + 3, district + 3, 2);
    hexmap.setFarmLevel(district + 4, district + 2, 2);
    hexmap.setFarmLevel(district - 4, district + 2, 1);
    hexmap.setPlantLevel(district, district - 1, 2);
    hexmap.setPlantLevel(district + 2, district + 1, 2);
    hexmap.setPlantLevel(district - 3, district - 1, 2);
    hexmap.setPlantLevel(district - 1, district + radius + 1, 1);

    // One special feature inside the ring, clear of the road corridor through the
    // middle: a special feature suppresses the cell's ordinary decoration, and the
    // module drops the cell's roads when one is set, so it is placed off the
    // carriageway rather than on it.
    hexmap.setSpecialIndex(district + 1, district + 1, 1);
}

function applyBrush(st, raise) {
    if (!st.hasHover) return;
    local delta = raise ? 1 : -1;
    local radius = st.brushRadius;
    local x = st.hoverX, z = st.hoverZ;
    if (st.mode == MODE_ELEVATION) {
        hexmap.editElevation(x, z, radius, delta);
    } else if (st.mode == MODE_WATER) {
        hexmap.editWaterLevel(x, z, radius, delta);
    } else if (st.mode == MODE_TERRAIN) {
        if (raise) hexmap.editTerrainType(x, z, radius, st.terrainType);
    } else if (st.mode == MODE_RIVER) {
        if (!raise) { hexmap.removeRiver(x, z); }
    } else if (st.mode == MODE_ROAD) {
        if (!raise) { hexmap.removeRoads(x, z); }
    }
}

function paintDrag(st, x, z) {
    // Rivers and roads are painted by dragging from a cell to its neighbour,
    // matching the reference editor.
    if (st.dragX < 0) { st.dragX = x; st.dragZ = z; return; }
    local direction = directionBetween(st.dragX, st.dragZ, x, z);
    if (direction < 0) { st.dragX = x; st.dragZ = z; return; }
    if (st.mode == MODE_RIVER) {
        hexmap.setOutgoingRiver(st.dragX, st.dragZ, direction);
    } else if (st.mode == MODE_ROAD) {
        hexmap.addRoad(st.dragX, st.dragZ, direction);
    }
    st.dragX = x;
    st.dragZ = z;
}

function updateBrush(st) {
    local left = mouse.isDown(1);
    local right = mouse.isDown(2);
    local leftPressed = left && !st.lastLeft;
    local rightPressed = right && !st.lastRight;
    st.lastLeft = left;
    st.lastRight = right;

    if (st.uiBuilt && ui.wantCaptureMouse()) return;

    local dragMode = (st.mode == MODE_RIVER || st.mode == MODE_ROAD);
    if (!left) {
        st.dragging = false;
        st.dragX = -1;
        st.dragZ = -1;
    }

    if (st.hasHover) {
        if (dragMode) {
            if (left) {
                st.dragging = true;
                if (leftPressed || st.hoverChanged) paintDrag(st, st.hoverX, st.hoverZ);
            } else if (rightPressed) {
                applyBrush(st, false);
            }
        } else if (st.mode == MODE_TERRAIN) {
            if (left && (leftPressed || st.hoverChanged)) applyBrush(st, true);
            if (right && (rightPressed || st.hoverChanged)) hexmap.removeRoads(st.hoverX, st.hoverZ);
        } else {
            if (left && (leftPressed || st.hoverChanged)) applyBrush(st, true);
            if (right && (rightPressed || st.hoverChanged)) applyBrush(st, false);
        }
    }

    if (leftPressed || rightPressed || (left && st.hoverChanged) || (right && st.hoverChanged)) {
        rebuildAllChunks(st);
    }
}

// --- fog of war -------------------------------------------------------------
//
// The module owns the explored latch and the per-cell visibility counters; the
// script only reads them (`isExplored` / `isExplorable` / `isCellVisible`) and
// flips them with `setExplored` / `setExplorable`. The fog overlay itself is an
// ordinary surface stream (`HexSurface::Fog`), so it needs no extra renderable
// setup beyond the shader it is bound to in `syncChunk`.

/**
 * Latches every cell of the freshly generated grid as explored.
 *
 * A search may only enter explored *and* explorable cells (`isValidDestination`),
 * so an un-explored grid cannot be pathed through at all: no path ever leaves the
 * origin. The editor therefore starts with the whole map "known" -- the fog
 * overlay still dims every cell no unit currently sees -- and `[J]` flips a single
 * cell back to unexplored to show the latch.
 */
function exploreAll(st) {
    local size = MAP_SIZES[st.sizeIndex];
    for (local z = 0; z < size; z++) {
        for (local x = 0; x < size; x++)
            hexmap.setExplored(x, z, true);
    }
}

/** Toggles the explored latch of the hovered cell (and therefore its fog shade). */
function toggleHoverExplored(st) {
    if (!st.hasHover) { st.statusText = "explored: no cell under the cursor"; return; }
    local explored = !hexmap.isExplored(st.hoverX, st.hoverZ);
    local changed = hexmap.setExplored(st.hoverX, st.hoverZ, explored);
    if (!changed.ok) { st.statusText = "explored failed: " + changed.status.summary; return; }
    st.statusText = "cell (" + st.hoverX + ", " + st.hoverZ + ") explored=" + explored;
}

/**
 * Toggles whether the hovered cell may be entered at all.
 *
 * `isExplorable` is the second half of the search's destination test, so clearing
 * it makes the cell -- and everything only reachable through it -- unreachable.
 */
function toggleHoverExplorable(st) {
    if (!st.hasHover) { st.statusText = "explorable: no cell under the cursor"; return; }
    local explorable = !hexmap.isExplorable(st.hoverX, st.hoverZ);
    local changed = hexmap.setExplorable(st.hoverX, st.hoverZ, explorable);
    if (!changed.ok) { st.statusText = "explorable failed: " + changed.status.summary; return; }
    st.statusText = "cell (" + st.hoverX + ", " + st.hoverZ + ") explorable=" + explorable;
}

// --- units ------------------------------------------------------------------

/**
 * Builds the shared unit mesh and the renderable pool once.
 *
 * Every pool entry is a cube with its own tint; the mesh itself is shared, and the
 * pool is indexed by unit id so no id-to-slot bookkeeping is needed. The cube is
 * stretched along its local Z axis so `setYaw` (driven by the unit's orientation)
 * is visible on screen.
 */
function createUnitPool(st) {
    if (st.unitRenderables.len() > 0) return;
    st.unitMesh = gfx.newMeshCube(1.0);
    for (local index = 0; index < MAX_UNITS; index++) {
        local r = eve.Renderable3D();
        r.setMesh(st.unitMesh);
        r.setScale(UNIT_SCALE, UNIT_SCALE * 1.15, UNIT_SCALE * 1.9);
        local tint = unitTint(index);
        r.setTint(tint[0], tint[1], tint[2], 1.0);
        r.setCastShadow(true);
        r.setReceiveShadow(false);
        r.setVisible(false);
        st.unitRenderables.append(r);
    }
}

/** Distinct, deterministic tint per pool slot so two units never look alike. */
function unitTint(index) {
    local phase = index % 6;
    local bright = ((index / 6) % 2 == 0) ? 1.0 : 0.62;
    local r = 0.0, g = 0.0, b = 0.0;
    if (phase == 0) r = 1.0;
    else if (phase == 1) { r = 1.0; g = 0.72; }
    else if (phase == 2) g = 1.0;
    else if (phase == 3) { g = 0.85; b = 1.0; }
    else if (phase == 4) b = 1.0;
    else { r = 0.72; b = 1.0; }
    return [r * bright, g * bright, b * bright];
}

/**
 * Searches outwards from `(cx, cz)` for a cell a unit may stand on.
 *
 * The module rejects an occupied cell, so the ring scan also skips cells another
 * unit already holds; underwater cells are skipped because a unit may not be
 * added there in the first place.
 *
 * @return `[x, z]` of a free land cell, or null when the ring is exhausted.
 */
function findFreeLandCell(st, cx, cz, maxRadius) {
    local size = MAP_SIZES[st.sizeIndex];
    for (local radius = 0; radius <= maxRadius; radius++) {
        for (local dz = -radius; dz <= radius; dz++) {
            for (local dx = -radius; dx <= radius; dx++) {
                if (radius > 0 && dx > -radius && dx < radius && dz > -radius && dz < radius) continue;
                local x = cx + dx, z = cz + dz;
                if (x < 0 || z < 0 || x >= size || z >= size) continue;
                if (hexmap.isUnderwater(x, z)) continue;
                if (hexmap.unitIdAt(x, z) >= 0) continue;
                return [x, z];
            }
        }
    }
    return null;
}

/**
 * Places the starter units near the middle of the map.
 *
 * The offsets are a flat `[dx, dz, ...]` list (no nested array literals): each
 * candidate is nudged to the nearest free land cell, so the seed always produces
 * three units even when the plateau's rivers or the flooded basin sit in the way.
 */
function placeStarterUnits(st) {
    hexmap.removeAllUnits();
    local size = MAP_SIZES[st.sizeIndex];
    local mid = size / 2;
    local offsets = [0, 0, 2, -1, -2, 1, 1, 2, -1, -2];
    local placed = 0;
    for (local i = 0; i + 1 < offsets.len() && placed < 3; i += 2) {
        local found = findFreeLandCell(st, mid + offsets[i], mid + offsets[i + 1], 6);
        if (found == null) continue;
        local added = hexmap.addUnit(found[0], found[1], UNIT_TURN);
        if (!added.ok) {
            print("hex map: starter unit at (" + found[0] + ", " + found[1] + ") failed: " +
                  added.status.summary + "\n");
            continue;
        }
        placed += 1;
    }
    st.selectedId = 0;
}

/**
 * Pushes one `unitSample` per live pool slot into its renderable.
 *
 * Unit ids are positional: slot `id` always draws unit `id`, and every slot from
 * `unitCount()` up is hidden. A removal therefore never leaves a stale renderable
 * behind, but the selected id may point at a different unit afterwards, which is
 * why it is clamped here instead of at the call sites.
 */
function updateUnits(st) {
    if (st.unitRenderables.len() == 0) return;
    local count = hexmap.unitCount();
    if (count <= 0) st.selectedId = -1;
    else if (st.selectedId < 0 || st.selectedId >= count) st.selectedId = 0;

    st.selectedTraveling = false;
    st.selectedCellX = -1;
    st.selectedCellZ = -1;
    for (local id = 0; id < st.unitRenderables.len(); id++) {
        local r = st.unitRenderables[id];
        if (id >= count) { r.setVisible(false); continue; }
        local sample = hexmap.unitSample(id);
        if (!sample.ok) { r.setVisible(false); continue; }
        local v = sample.value;
        r.setPosition(v[2], v[3] + UNIT_LIFT, v[4]);
        r.setYaw(v[5] * DEG_TO_RAD);
        r.setVisible(true);
        if (id == st.selectedId) {
            st.selectedCellX = v[0];
            st.selectedCellZ = v[1];
            st.selectedTraveling = (v[6] != 0);
        }
    }
}

// --- planned path and travel ------------------------------------------------

function clearPathPreview(st) {
    st.pathPreview = [];
    st.pathDirty = true;
}

/**
 * Plans a path from the selected unit to the hovered cell and stores it.
 *
 * A unit that is already walking reports `travelingFlag == 1` and its sampled cell
 * is the reserved destination, so a second plan is refused instead of producing a
 * path that does not start where the unit is standing.
 */
function planPath(st) {
    local count = hexmap.unitCount();
    if (count <= 0) { st.statusText = "plan failed: no units"; return; }
    if (!st.hasHover) { st.statusText = "plan failed: no cell under the cursor"; return; }
    local sample = hexmap.unitSample(st.selectedId);
    if (!sample.ok) { st.statusText = "plan failed: " + sample.status.summary; return; }
    if (sample.value[6] != 0) { st.statusText = "plan failed: unit " + st.selectedId + " is travelling"; return; }

    local found = hexmap.findPath(sample.value[0], sample.value[1], st.hoverX, st.hoverZ);
    if (!found.ok) {
        clearPathPreview(st);
        st.statusText = "path failed: " + found.status.summary;
        return;
    }
    st.pathPreview = found.value;
    st.pathDirty = true;
    st.statusText = "path " + st.pathPreview.len() + " cells, " + pathTurns(st) + " turns [space] go";
}

/** Arrival turn of the planned path: the last entry's turn value. */
function pathTurns(st) {
    if (st.pathPreview.len() == 0) return 0;
    return st.pathPreview[st.pathPreview.len() - 1][2];
}

/** Commits the planned path, or reports why there is nothing to commit. */
function commitPath(st) {
    if (st.pathPreview.len() == 0) { st.statusText = "move failed: no planned path (press G)"; return; }
    local moved = hexmap.travelUnit(st.selectedId, st.pathPreview);
    if (!moved.ok) {
        st.statusText = "move failed: " + moved.status.summary;
        return;
    }
    st.statusText = "unit " + st.selectedId + " travelling " + pathTurns(st) + " turns";
    clearPathPreview(st);
}

/** Adds a unit on the hovered cell; the module reports every rejection reason. */
function addUnitAtHover(st) {
    if (!st.hasHover) { st.statusText = "add unit failed: no cell under the cursor"; return; }
    if (hexmap.unitCount() >= MAX_UNITS) {
        st.statusText = "add unit failed: the " + MAX_UNITS + " unit pool is full";
        return;
    }
    local added = hexmap.addUnit(st.hoverX, st.hoverZ, UNIT_TURN);
    if (!added.ok) { st.statusText = "add unit failed: " + added.status.summary; return; }
    st.selectedId = added.value;
    st.statusText = "unit " + added.value + " added at (" + st.hoverX + ", " + st.hoverZ + ")";
}

/** Removes the selected unit and drops any preview that pointed at it. */
function removeSelectedUnit(st) {
    if (hexmap.unitCount() <= 0) { st.statusText = "remove unit failed: there are no units"; return; }
    local removed = hexmap.removeUnit(st.selectedId);
    if (!removed.ok) { st.statusText = "remove unit failed: " + removed.status.summary; return; }
    st.statusText = "unit " + st.selectedId + " removed";
    clearPathPreview(st);
}

/**
 * Redraws the world-space path polyline when the plan changed.
 *
 * The line reuses the hex-cursor primitive recipe: a depth-ignoring polyline lifted
 * `PATH_LIFT` above the cell centres. A one-cell plan (the unit is already there)
 * has no segment to draw, so it hides the line instead.
 */
function updatePathPreview(st) {
    if (st.pathLine == null || !st.pathDirty) return;
    st.pathDirty = false;
    if (st.pathPreview.len() < 2) {
        st.pathLine.setVisible(false);
        return;
    }
    local points = [];
    foreach (cell in st.pathPreview) {
        points.append(hexmap.cellPositionX(cell[0], cell[1]));
        points.append(hexmap.cellPositionY(cell[0], cell[1]) + PATH_LIFT);
        points.append(hexmap.cellPositionZ(cell[0], cell[1]));
    }
    local applied = st.pathLine.setPolyline(points, false);
    if (!applied.ok) {
        st.pathLine.setVisible(false);
        return;
    }
    st.pathLine.setVisible(true);
}

// --- save and load ----------------------------------------------------------

/**
 * Lazily prepares the app-data save folder and returns the filesystem to use.
 *
 * The engine leaves the save identity to the game: without one `getSaveDirectory`
 * is empty and every write is refused, so the identity is declared here exactly
 * once (the same recipe `examples/terrain-editor` uses).
 *
 * @return The prepared filesystem, or null when the app-data folder is unusable.
 */
function prepareSaveFilesystem(st) {
    if (st.saveFs == null) {
        local fs = eve.Filesystem();
        if (fs.getIdentity() == "") fs.setIdentity("hex-terrain-3d", true);
        st.saveFs = fs;
    }
    if (st.savePath == "") {
        if (!st.saveFs.setupWriteDirectory()) return null;
        st.savePath = st.saveFs.getSaveDirectory() + "/hex-terrain-3d-save.bin";
    }
    return st.saveFs;
}

/**
 * Writes the module's whole payload (grid + units + explored flags) to disk.
 *
 * The payload is documented as byte-safe Squirrel string, so `writeText` stores it
 * verbatim. Success is reported only after both the module call and the write
 * reported success.
 */
function saveMapToDisk(st) {
    local fs = prepareSaveFilesystem(st);
    if (fs == null) { st.statusText = "save failed: the app-data save directory is not writable"; return; }
    local saved = hexmap.saveMap();
    if (!saved.ok) { st.statusText = "save failed: " + saved.status.summary; return; }
    if (!fs.writeText(st.savePath, saved.value)) {
        st.statusText = "save failed: could not write " + st.savePath;
        return;
    }
    st.statusText = "saved " + saved.value.len() + " bytes";
}

/**
 * Re-syncs every script-side renderable after the module adopted a new grid.
 *
 * `loadMap` publishes the restored grid *before* it validates the restored units,
 * so its Result can be a failure while the previous grid is already gone. The grid
 * is therefore the only thing that matters here: the pool slots must be hidden
 * (their meshes were released), the map size cache refreshed, every chunk marked
 * dirty and the path/selection state dropped. The caller decides what to report.
 */
function resyncAfterLoad(st) {
    hideAllRenderables(st);
    hideUnitRenderables(st);

    local size = hexmap.cellCountX();
    for (local index = 0; index < MAP_SIZES.len(); index++) {
        if (MAP_SIZES[index] == size) st.sizeIndex = index;
    }

    clearPathPreview(st);
    st.selectedId = 0;
    st.selectedTraveling = false;
    st.hasHover = false;
    st.hoverX = -1; st.hoverZ = -1;
    // The restored visibility is already rebuilt by `loadMap`; recomputing it from
    // the restored units only guards the case where the unit restore failed and the
    // counters were left behind. It runs before the rebuild so the fog meshes match.
    hexmap.resetVisibility();
    hexmap.markAllChunksDirty();
    rebuildAllChunks(st);
    hideUnusedRenderables(st);

    st.pivotX = (hexmap.cellPositionX(0, 0) + hexmap.cellPositionX(size - 1, hexmap.cellCountZ() - 1)) * 0.5;
    st.pivotZ = (hexmap.cellPositionZ(0, 0) + hexmap.cellPositionZ(size - 1, hexmap.cellCountZ() - 1)) * 0.5;
    clampPivot(st);
    applyCamera(st);
}

/** Reads the payload back and rebuilds the grid from it. */
function loadMapFromDisk(st) {
    local fs = prepareSaveFilesystem(st);
    if (fs == null) { st.statusText = "load failed: the app-data save directory is unavailable"; return; }
    local blob = fs.readText(st.savePath);
    if (blob.len() == 0) { st.statusText = "load failed: no save at " + st.savePath; return; }

    local loaded = hexmap.loadMap(gfx, blob);
    resyncAfterLoad(st);
    if (!loaded.ok) { st.statusText = "load failed: " + loaded.status.summary; return; }
    st.statusText = "loaded " + blob.len() + " bytes, " + hexmap.unitCount() + " unit(s)";
}

// --- HUD --------------------------------------------------------------------

/** Human-readable form of a boolean, so the HUD never prints a raw `true`/`false`. */
function yesNo(value) {
    return value ? "yes" : "no";
}

function modeName(mode) {
    if (mode == MODE_ELEVATION) return "Elevation";
    if (mode == MODE_WATER) return "Water";
    if (mode == MODE_TERRAIN) return "Terrain";
    if (mode == MODE_RIVER) return "River";
    return "Road";
}

function buildHud(st) {
    ui.beginBuild();
    ui.beginWindow("HexMap", "root");
    ui.text("EVEngine Hex Map", "title");
    ui.text("", "mode");
    ui.text("", "cell");
    ui.text("", "structure");
    ui.text("", "map");
    ui.text("", "stats");
    ui.text("", "fog");
    ui.text("", "unit");
    ui.text("", "path");
    ui.separator("sep");
    ui.text("[WASD]pan [Q/E]rotate [wheel]zoom", "help1");
    ui.text("[1-5]mode [/]brush radius [,/.]terrain", "help2");
    ui.text("[LMB]raise/paint/drag [RMB]lower/erase", "help3");
    ui.text("[N]new seed [F]map size [R]rebuild", "help4");
    ui.text("[tab]next unit [G]plan [space]go [esc]clear", "help5");
    ui.text("[T]add unit [Y]remove unit [J][K]explore", "help6");
    ui.text("[M]generated map [F5]save [F9]load", "help7");
    ui.end();
    ui.mountBuildAs("hud");
    ui.select("hud");
    ui.setHostOverlay(true);
    ui.setHostVisible(true);
    ui.setHostPos(12.0, 12.0, 0.0, 0.0);
    st.uiBuilt = true;
}

function refreshHud(st) {
    if (!st.uiBuilt) return;
    ui.select("hud");
    // The `ui.text(..., label)` labels in `buildHud` are the ids used here, so the
    // two lists must stay in the same order.
    local hover = st.hasHover ? ("(" + st.hoverX + ", " + st.hoverZ + ")") : "(none)";
    ui.setText("mode", "mode: " + modeName(st.mode) + "   brush: " + st.brushRadius +
                       "   terrain: " + TERRAIN_NAMES[st.terrainType]);
    if (st.hasHover) {
        local elevation = hexmap.elevation(st.hoverX, st.hoverZ);
        local water = hexmap.waterLevel(st.hoverX, st.hoverZ);
        local terrain = hexmap.terrainType(st.hoverX, st.hoverZ);
        local features = "";
        if (hexmap.hasRiver(st.hoverX, st.hoverZ)) features += " river";
        if (hexmap.hasRoad(st.hoverX, st.hoverZ)) features += " road";
        if (hexmap.isExplored(st.hoverX, st.hoverZ)) features += " explored"; else features += " unexplored";
        if (!hexmap.isExplorable(st.hoverX, st.hoverZ)) features += " not-explorable";
        if (hexmap.isCellVisible(st.hoverX, st.hoverZ)) features += " visible";
        ui.setText("cell", "cell " + hover + "  elev " + elevation + "  water " + water +
                           "  " + TERRAIN_NAMES[terrain] + features);
        ui.setText("structure", "structure walled=" + yesNo(hexmap.isWalled(st.hoverX, st.hoverZ)) +
                               "  urban " + hexmap.urbanLevel(st.hoverX, st.hoverZ) +
                               "  farm " + hexmap.farmLevel(st.hoverX, st.hoverZ) +
                               "  plant " + hexmap.plantLevel(st.hoverX, st.hoverZ) +
                               "  special " + hexmap.specialIndex(st.hoverX, st.hoverZ));
    } else {
        ui.setText("cell", "cell " + hover);
        ui.setText("structure", "structure (no cell under the cursor)");
    }
    ui.setText("map", "size " + MAP_SIZES[st.sizeIndex] + "  seed " + st.seed + "  " + st.statusText);
    ui.setText("stats", "surfaces rebuilt " + st.meshCount + "  dirty " + hexmap.dirtyChunkCount() +
                       "  unit id at hover " + (st.hasHover ? hexmap.unitIdAt(st.hoverX, st.hoverZ) : -1));
    ui.setText("fog", "fog: " + hexmap.visibleCellCount() + " of " +
                      (hexmap.cellCountX() * hexmap.cellCountZ()) + " cells visible" +
                      "  ([J]/[K] toggle explored/explorable on the hovered cell)");
    local count = hexmap.unitCount();
    if (count <= 0 || st.selectedId < 0) {
        ui.setText("unit", "unit none  (" + count + " in the map, [T] adds one)");
    } else {
        ui.setText("unit", "unit " + st.selectedId + " of " + count + "  cell (" +
                           st.selectedCellX + ", " + st.selectedCellZ + ")  " +
                           (st.selectedTraveling ? "travelling" : "idle"));
    }
    if (st.pathPreview.len() == 0) {
        ui.setText("path", "path none  ([G] plans from the selected unit to the hovered cell)");
    } else {
        ui.setText("path", "path " + st.pathPreview.len() + " cells  " + pathTurns(st) +
                           " turns  ([space] travel, [esc] clear)");
    }
}

// --- frame loop -------------------------------------------------------------

function handleKeys(st) {
    if (key_just_pressed("1")) st.mode = MODE_ELEVATION;
    if (key_just_pressed("2")) st.mode = MODE_WATER;
    if (key_just_pressed("3")) st.mode = MODE_TERRAIN;
    if (key_just_pressed("4")) st.mode = MODE_RIVER;
    if (key_just_pressed("5")) st.mode = MODE_ROAD;

    if (key_just_pressed("]")) st.brushRadius = st.brushRadius + 1 > 6 ? 6 : st.brushRadius + 1;
    if (key_just_pressed("[")) st.brushRadius = st.brushRadius - 1 < 0 ? 0 : st.brushRadius - 1;
    if (key_just_pressed(".")) st.terrainType = (st.terrainType + 1) % TERRAIN_NAMES.len();
    if (key_just_pressed(",")) st.terrainType = (st.terrainType + TERRAIN_NAMES.len() - 1) % TERRAIN_NAMES.len();

    if (key_just_pressed("r")) {
        st.seed = procgen.randomSeed();
        buildMap(st);
    }
    if (key_just_pressed("f")) {
        st.sizeIndex = (st.sizeIndex + 1) % MAP_SIZES.len();
        buildMap(st);
    }
    if (key_just_pressed("n")) {
        hexmap.markAllChunksDirty();
        rebuildAllChunks(st);
    }
    // `M` replaces the hand-authored map with a freshly generated one at the size
    // the `F` key selects. `R` stays what it always was: a new noise seed for the
    // hand-authored map, not a new map.
    if (key_just_pressed("m")) reseedProceduralMap(st);

    // --- units, fog and travel ---
    if (key_just_pressed("tab", "Tab")) {
        local count = hexmap.unitCount();
        if (count > 0) {
            st.selectedId = (st.selectedId + 1) % count;
            st.statusText = "selected unit " + st.selectedId + " of " + count;
        }
    }
    if (key_just_pressed("g")) planPath(st);
    if (key_just_pressed("space", "Space")) commitPath(st);
    if (key_just_pressed("escape", "Escape")) {
        if (st.pathPreview.len() > 0) st.statusText = "path cleared";
        clearPathPreview(st);
    }
    if (key_just_pressed("t")) addUnitAtHover(st);
    if (key_just_pressed("y")) removeSelectedUnit(st);
    if (key_just_pressed("j")) toggleHoverExplored(st);
    if (key_just_pressed("k")) toggleHoverExplorable(st);

    // --- persistence ---
    if (key_just_pressed("F5")) saveMapToDisk(st);
    if (key_just_pressed("F9")) loadMapFromDisk(st);
}

eve_init = function() {
    if (hexMap == null) hexMap = createHexState();
    local st = hexMap;
    st.camera = eve.Camera3D();
    st.camera.setFov(50.0);
    st.camera.setAmbient(0.34, 0.38, 0.44);
    st.camera.setActive(true);

    gfx.setBackgroundColor(0.075, 0.11, 0.17, 1.0);
    gfx.setDirectionalLight(-0.55, 0.78, 0.32, 1.55, 1.38, 1.12);

    buildMap(st);
    createUnitPool(st);
    // Hex cursor: a closed polyline redrawn whenever the hovered cell changes.
    local highlightResult = gfx.newPrimitivePolyline3D(
        [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0], true, 1.0, 0.82, 0.25, 1.0, 3.5);
    if (highlightResult.ok) {
        st.highlight = highlightResult.value;
        st.highlight.setDepthMode("ignore");
        st.highlight.setVisible(false);
    } else {
        print("hex map: hex cursor unavailable: " + highlightResult.status.summary + "\n");
    }
    // Planned-path polyline: the same recipe, open instead of closed, redrawn only
    // when the plan changed. Two points are the primitive's minimum.
    local pathResult = gfx.newPrimitivePolyline3D(
        [0.0, 0.0, 0.0, 0.0, 0.0, 0.0], false, 0.35, 0.95, 1.0, 1.0, 4.0);
    if (pathResult.ok) {
        st.pathLine = pathResult.value;
        st.pathLine.setDepthMode("ignore");
        st.pathLine.setVisible(false);
    } else {
        print("hex map: path preview unavailable: " + pathResult.status.summary + "\n");
    }
    buildHud(st);
    applyCamera(st);
    print("hex map: WASD pan | Q/E rotate | wheel zoom | 1-5 mode | LMB/RMB edit | R new seed\n");
    print("hex map: tab next unit | G plan | space go | esc clear | T add | Y remove | F5 save | F9 load\n");
    print("hex map: M generated map (uses the size selected by F)\n");
};

eve_update = function(dt) {
    local st = hexMap;
    if (st == null) return;
    st.elapsed += dt;
    st.frameCount += 1;
    gfx.setCloudShadows(0.24, 6.0, st.elapsed, 0.35, 0.38, 0.56, 0.62);

    handleKeys(st);
    updateCamera(st, dt);
    updateHover(st);
    updateBrush(st);
    // Units first: their motion is what makes the fog dirty, and `advanceUnits` is
    // the module's per-frame simulation step (it must run even with no units).
    hexmap.advanceUnits(dt);
    updateUnits(st);
    // Only the chunks the module flagged (visibility or edits) are rebuilt here.
    updateDirtyChunks(st);
    updatePathPreview(st);
    updateHighlight(st);
    refreshHud(st);

    // Readback is enabled by the first call, so retry until the frame lands.
    if (!st.screenshotSaved && st.frameCount > 90 && gfx.saveFramePng("hex-map.png"))
        st.screenshotSaved = true;
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    ui.beginFrameAndRender();
};
