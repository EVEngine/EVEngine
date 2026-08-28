// Terrain editor demo: UI Viewport widget embedding a live 3D render target.
//
//   - heightmap (procgen::Heightmap) -> flat-shaded terrain mesh
//   - orbit camera driven by viewport drag / wheel
//   - raise/lower brush paints the heightmap, mesh updates in place
//
// Run: make run/win32-debug GAME=examples/terrain-editor

if (!("hm" in getroottable())) hm <- null;
if (!("terrainMesh" in getroottable())) terrainMesh <- null;
if (!("terrainEnt" in getroottable())) terrainEnt <- null;
if (!("cam" in getroottable())) cam <- null;
if (!("vpCanvas" in getroottable())) vpCanvas <- null;
if (!("yaw" in getroottable())) yaw <- 0.75;
if (!("pitch" in getroottable())) pitch <- 0.45;
if (!("dist" in getroottable())) dist <- 26.0;
if (!("tool" in getroottable())) tool <- "raise";
if (!("brushR" in getroottable())) brushR <- 4.0;
if (!("strength" in getroottable())) strength <- 0.02;
if (!("terrainLayers" in getroottable())) terrainLayers <- null;
if (!("analysisText" in getroottable())) analysisText <- "not analyzed";

const W = 64;
const H = 64;
const CELL = 0.5;      // world units per heightmap cell
const HSCALE = 3.2;    // world units per unit of height

function clampf(v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v); }

function regenTerrain() {
    local p = procgen.newParams();
    p.setSize(W, H);
    p.setSeed(20260819);
    p.setFloat("frequency", 1.0 / 22.0);
    p.setInt("octaves", 5);
    local generated = procgen.generateHeightmap(p);
    if (generated != null) {
        hm = generated;
    } else {
        if (hm == null) hm = procgen.newHeightmap(W, H);
        for (local y = 0; y < H; y++) {
            for (local x = 0; x < W; x++) {
                hm.setHeight(x, y, 0.5 + 0.22 * sin(x * 0.18) * cos(y * 0.18));
            }
        }
    }
    analyzeTerrainLayers();
    rebuildMesh();
}

function analyzeTerrainLayers() {
    if (hm == null) return;
    terrainLayers = procgen.analyzeTerrain(hm, 48.0, 0.25, 0.65);
    if (terrainLayers != null) {
        local cx = W / 2;
        local cy = H / 2;
        analysisText = "center biome=" + terrainLayers.getBiomeName(cx, cy) +
                       "  flow=" + terrainLayers.getFlowAccumulation(cx, cy);
    }
}

function rebuildMesh() {
    if (hm == null) return;
    if (terrainMesh == null) {
        terrainMesh = editor.newHeightmapMesh(hm, CELL, HSCALE);
        if (terrainMesh == null) return;
        terrainEnt = eve.Renderable3D();
        terrainEnt.setMesh(terrainMesh);
        terrainEnt.setTint(0.45, 0.62, 0.38, 1.0);
        terrainEnt.setMetallic(0.0);
        terrainEnt.setRoughness(0.9);
        terrainEnt.setPosition(0.0, 0.0, 0.0);
        terrainEnt.setVisible(true);
    } else {
        editor.updateHeightmapMesh(terrainMesh, gfx, hm, CELL, HSCALE);
    }
}

function setupCamera() {
    cam = eve.Camera3D();
    cam.setFov(50.0);
    cam.setAmbient(0.42, 0.45, 0.5);
    cam.setActive(true);
    gfx.setDirectionalLight(-0.45, 0.9, 0.35, 1.9, 1.6, 1.3);
}

eve_init = function() {
    ui.setTheme("dark");
    ui.setNavKeyboard(true);
    regenTerrain();
    setupCamera();

    ui.beginBuild();
    ui.beginWindow("Terrain Editor", "root");
    ui.beginRow("body", 8.0);
    ui.beginColumn("tools", 6.0);
    ui.setItemSize(190.0, 0.0);
    ui.text("Tool", "lbl_tool");
    ui.button("Raise (1)", "raise");
    ui.button("Lower (2)", "lower");
    ui.button("Regenerate (R)", "reset");
    ui.button("Thermal erosion", "thermal");
    ui.button("Hydraulic erosion", "hydraulic");
    ui.button("Analyze rivers/biomes", "analyze");
    ui.separator("sep");
    ui.text("Brush radius", "lbl_brush");
    ui.slider("Radius", brushR, 1.0, 12.0, "brush");
    ui.text("Drag: orbit   Wheel: zoom", "help");
    ui.text("", "status");
    ui.end();
    ui.viewport("vp", 0.0, 440.0);
    ui.setItemFlexGrow(1.0);
    ui.end();
    ui.end();
    ui.mountBuildAs("ed");
};

eve_update = function(dt) {
    // Orbit camera from viewport drag / wheel.
    if (ui.viewportActive("vp")) {
        yaw -= ui.viewportDragDX("vp") * 0.008;
        pitch += ui.viewportDragDY("vp") * 0.008;
        pitch = clampf(pitch, 0.05, 1.45);
    }
    local wheel = ui.viewportWheel("vp");
    if (wheel != 0.0) {
        dist *= (1.0 - wheel * 0.12);
        dist = clampf(dist, 4.0, 90.0);
    }
    local cx = (W - 1) * CELL * 0.5;
    local cz = (H - 1) * CELL * 0.5;
    cam.setEye(cx + dist * cos(pitch) * sin(yaw),
               dist * sin(pitch) + 1.2,
               cz + dist * cos(pitch) * cos(yaw));
    cam.setTarget(cx, 0.8, cz);

    // Brush painting (hold left button over the viewport).
    if (ui.viewportActive("vp") && ui.viewportHovered("vp")) {
        local mx = ui.viewportMouseX("vp");
        local my = ui.viewportMouseY("vp");
        local canvas = ui.viewportCanvas("vp");
        if (canvas != null && cam != null && (tool == "raise" || tool == "lower")) {
            cam.screenToRay(mx, my, canvas.getWidth(), canvas.getHeight());
            local ox = cam.getScreenRayOriginX();
            local oy = cam.getScreenRayOriginY();
            local oz = cam.getScreenRayOriginZ();
            local dx = cam.getScreenRayDirX();
            local dy = cam.getScreenRayDirY();
            local dz = cam.getScreenRayDirZ();
            if (dy < -0.0001) {
                local t = -oy / dy;
                local wx = ox + dx * t;
                local wz = oz + dz * t;
                local cellX = wx / CELL;
                local cellZ = wz / CELL;
                local dir = tool == "raise" ? 1.0 : -1.0;
                local r = brushR.tointeger();
                for (local yy = -r; yy <= r; yy++) {
                    for (local xx = -r; xx <= r; xx++) {
                        local gx = cellX.tointeger() + xx;
                        local gy = cellZ.tointeger() + yy;
                        if (gx < 0 || gx >= W || gy < 0 || gy >= H) continue;
                        local d = sqrt(xx * xx + yy * yy).tofloat();
                        if (d > r) continue;
                        local fall = 1.0 - d / (r + 0.5);
                        local cur = hm.height(gx, gy);
                        hm.setHeight(gx, gy, clampf(cur + dir * strength * fall, 0.0, 1.0));
                    }
                }
                rebuildMesh();
            }
        }
    }

    // Toolbar interactions.
    local c = ui.consumeClick();
    while (c != "") {
        if (c == "ed/raise") {
            tool = "raise";
        } else if (c == "ed/lower") {
            tool = "lower";
        } else if (c == "ed/reset") {
            regenTerrain();
        } else if (c == "ed/thermal") {
            if (procgen.erodeTerrainThermal(hm, 20, 0.018, 0.32)) {
                analyzeTerrainLayers();
                rebuildMesh();
            }
        } else if (c == "ed/hydraulic") {
            if (procgen.erodeTerrainHydraulic(hm, 40, 0.01, 0.08, 2.0, 0.16, 0.12)) {
                analyzeTerrainLayers();
                rebuildMesh();
            }
        } else if (c == "ed/analyze") {
            analyzeTerrainLayers();
        }
        c = ui.consumeClick();
    }
    local ch = ui.consumeChange();
    while (ch != "") {
        if (ch == "ed/brush") brushR = ui.getValue("brush");
        ch = ui.consumeChange();
    }
    ui.setText("status", "tool=" + tool + "  radius=" + brushR + "\n" + analysisText);
};

eve_render = function() {
    gfx.clear();
    // Render the 3D scene into the viewport widget's offscreen canvas.
    vpCanvas = ui.viewportCanvas("vp");
    if (vpCanvas != null && cam != null) {
        gfx.renderScene3DToCanvas(vpCanvas, cam);
    }
    ui.beginFrameAndRender();
};
