// Terrain editor demo: UI Viewport widget embedding a live 3D render target.
//
//   - heightmap (procgen::Heightmap) -> flat-shaded terrain mesh
//   - orbit camera driven by viewport drag / wheel
//   - raise/lower brush paints the heightmap, mesh updates in place
//
// Run: make run/win32-debug GAME=examples/terrain-editor

persist hm = null
persist terrainMesh = null
persist terrainEnt = null
persist cam = null
persist vpCanvas = null
persist yaw = 0.75
persist pitch = 0.45
persist dist = 26.0
persist tool = "raise"
persist brushR = 4.0
persist strength = 0.06
persist orbitDragging = false
persist orbitMouseX = 0.0
persist orbitMouseY = 0.0
persist editStatus = "ready"
persist meshDirty = false
persist meshCooldown = 0.0

const W = 64;
const H = 64;
const CELL = 0.5;      // world units per heightmap cell
const HSCALE = 3.2;    // world units per unit of height

function regenTerrain() {
    local paramsResult = procgen.newParams();
    if (!paramsResult.ok) return;
    local p = paramsResult.value;
    p.setSize(W, H);
    p.setSeed(20260819);
    p.setFloat("frequency", 1.0 / 22.0);
    p.setInt("octaves", 5);
    local generatedResult = procgen.generateHeightmap(p);
    if (generatedResult.ok) {
        hm = generatedResult.value;
    } else {
        if (hm == null) {
            local fallbackResult = procgen.newHeightmap(W, H);
            if (!fallbackResult.ok) return;
            hm = fallbackResult.value;
        }
        for (local y = 0; y < H; y++) {
            for (local x = 0; x < W; x++) {
                hm.setHeight(x, y, 0.5 + 0.22 * sin(x * 0.18) * cos(y * 0.18));
            }
        }
    }
    rebuildMesh();
}

function rebuildMesh() {
    if (hm == null) return;
    if (terrainMesh == null) {
        terrainMesh = editor.newHeightmapMeshSmooth(hm, CELL, HSCALE);
        if (terrainMesh == null) return;
        terrainEnt = eve.Renderable3D();
        terrainEnt.setMesh(terrainMesh);
        terrainEnt.setTint(0.28, 0.40, 0.22, 1.0);
        terrainEnt.setMetallic(0.0);
        terrainEnt.setRoughness(0.9);
        terrainEnt.setPosition(0.0, 0.0, 0.0);
        terrainEnt.setVisible(true);
    } else {
        editor.updateHeightmapMeshSmooth(terrainMesh, gfx, hm, CELL, HSCALE);
    }
}

function setupCamera() {
    cam = eve.Camera3D();
    cam.setFov(50.0);
    cam.setAmbient(0.12, 0.15, 0.18);
    cam.setActive(true);
    gfx.setBackgroundColor(0.10, 0.14, 0.19, 1.0);
    gfx.setDirectionalLight(-0.45, 0.9, 0.35, 0.95, 0.98, 1.0);
}

function surfaceHeight(wx, wz) {
    local gx = clampf((wx / CELL).tofloat(), 0.0, (W - 1).tofloat()).tointeger();
    local gz = clampf((wz / CELL).tofloat(), 0.0, (H - 1).tofloat()).tointeger();
    return hm.height(gx, gz) * HSCALE;
}

function terrainFromScreen(mx, my) {
    cam.screenToRay(mx, my, gfx.getWidth().tofloat(), gfx.getHeight().tofloat());
    local ox = cam.getScreenRayOriginX();
    local oy = cam.getScreenRayOriginY();
    local oz = cam.getScreenRayOriginZ();
    local dx = cam.getScreenRayDirX();
    local dy = cam.getScreenRayDirY();
    local dz = cam.getScreenRayDirZ();
    if (dy >= -0.0001) return null;

    // Start at the middle of the height range, then converge onto the sampled
    // height field. This keeps the brush under the cursor on hills and valleys.
    local t = (HSCALE * 0.5 - oy) / dy;
    if (t < 0.0) return null;
    local wx = ox + dx * t;
    local wz = oz + dz * t;
    for (local i = 0; i < 3; i++) {
        t = (surfaceHeight(wx, wz) - oy) / dy;
        wx = ox + dx * t;
        wz = oz + dz * t;
    }
    if (wx < 0.0 || wz < 0.0 || wx > (W - 1) * CELL || wz > (H - 1) * CELL)
        return null;
    return [wx / CELL, wz / CELL];
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
    ui.separator("sep");
    ui.text("Brush radius", "lbl_brush");
    ui.slider("Radius", brushR, 1.0, 12.0, "brush");
    ui.text("Scene: LMB sculpt", "help");
    ui.text("Scene: RMB orbit", "orbit_help");
    ui.text("", "status");
    ui.end();
    ui.end();
    ui.end();
    ui.mountBuildAs("ed");
    ui.setHostPos(12.0, 12.0, 0.0, 0.0);
    ui.setHostSize(230.0, 300.0);
};

eve_update = function(dt) {
    // DevTools and other overlays may select their own host between frames.
    ui.select("ed");
    // Right drag orbits; left drag is reserved for sculpting.
    local orbitDown = mouse.isDown(2);
    if (orbitDown && !ui.wantCaptureMouse()) {
        local mx = mouse.getX();
        local my = mouse.getY();
        if (orbitDragging) {
            yaw -= (mx - orbitMouseX) * 0.008;
            pitch += (my - orbitMouseY) * 0.008;
            pitch = clampf(pitch, 0.05, 1.45);
        }
        orbitMouseX = mx;
        orbitMouseY = my;
        orbitDragging = true;
    } else {
        orbitDragging = false;
    }
    local cx = (W - 1) * CELL * 0.5;
    local cz = (H - 1) * CELL * 0.5;
    cam.setEye(cx + dist * cos(pitch) * sin(yaw),
               dist * sin(pitch) + 1.2,
               cz + dist * cos(pitch) * cos(yaw));
    cam.setTarget(cx, 0.8, cz);

    // Brush painting (hold left button over the 3D scene, outside the toolbar).
    if (mouse.isDown(1) && !ui.wantCaptureMouse()) {
        local mx = mouse.getX();
        local my = mouse.getY();
        if (cam != null && (tool == "raise" || tool == "lower")) {
            local hit = terrainFromScreen(mx, my);
            if (hit != null) {
                local cellX = hit[0];
                local cellZ = hit[1];
                local dir = tool == "raise" ? 1.0 : -1.0;
                local centerX = cellX.tointeger();
                local centerZ = cellZ.tointeger();
                local before = hm.height(centerX, centerZ);
                local changed = editor.applyHeightmapBrush(
                    hm, cellX, cellZ, brushR, dir * strength * clampf(dt * 60.0, 0.0, 2.0));
                if (changed > 0) {
                    meshDirty = true;
                    editStatus = tool + " (" + centerX + "," + centerZ + ") " + before +
                                 " -> " + hm.height(centerX, centerZ) + "  cells=" + changed;
                }
            }
        }
    }

    // Height edits remain responsive while expensive mesh generation/upload is
    // capped at 30 Hz. The latest accumulated heightmap is uploaded each flush.
    meshCooldown -= dt;
    if (meshDirty && meshCooldown <= 0.0) {
        rebuildMesh();
        meshDirty = false;
        meshCooldown = 1.0 / 30.0;
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
        }
        c = ui.consumeClick();
    }
    local ch = ui.consumeChange();
    while (ch != "") {
        if (ch == "ed/brush") brushR = ui.getValue("brush");
        ch = ui.consumeChange();
    }
    ui.setText("status", editStatus + "  radius=" + brushR);
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    ui.select("ed");
    ui.beginFrameAndRender();
};
