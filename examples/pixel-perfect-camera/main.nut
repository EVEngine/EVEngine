// Perfect-pixel orthographic camera demo.
// Left: Camera3D setPixelOrthographic top-down grid of unit cubes.
// Right: Camera2D applyPixelPerfect draws a sharp 16×9 virtual HUD (4× on 1280×720).
// Keys: A/D nudge camera by 0.3 world units; S snap to pixel grid; R reset.

persist cam3 = null
persist ctrl = null
persist cam2 = null
persist tiles = []
persist hudReady = false
persist nudgeX = 0.0
persist status = "pixel ortho ready"
persist pulse = 0.0
persist keyLeftWas = false
persist keyRightWas = false
persist keySnapWas = false
persist keyResetWas = false

local REF_W = 320
local REF_H = 180
local PPU = 16.0
local GRID = 8

function makeCube(r, g, b) {
    local ent = eve.Renderable3D();
    ent.setMesh(gfx.newMeshCube(1.0));
    ent.setTint(r, g, b, 1.0);
    ent.setMetallic(0.05);
    ent.setRoughness(0.85);
    ent.setCastShadow(false);
    ent.setReceiveShadow(false);
    return ent;
}

function rebuildGrid() {
    tiles = [];
    for (local z = 0; z < GRID; z += 1) {
        for (local x = 0; x < GRID; x += 1) {
            local checker = ((x + z) % 2) == 0;
            local r = checker ? 0.92 : 0.18;
            local g = checker ? 0.55 : 0.62;
            local b = checker ? 0.22 : 0.88;
            if (x == 3 && z == 3) { r = 0.15; g = 0.95; b = 0.45; }
            if (x == 4 && z == 3) { r = 0.95; g = 0.25; b = 0.35; }
            local cube = makeCube(r, g, b);
            cube.setScale(0.92, 0.35, 0.92);
            cube.setPosition(x.tofloat() - GRID.tofloat() * 0.5 + 0.5,
                             0.175,
                             z.tofloat() - GRID.tofloat() * 0.5 + 0.5);
            tiles.push(cube);
        }
    }
}

function applyPixelOrtho() {
    local viewW = gfx.getWidth();
    local viewH = gfx.getHeight();
    local result = cam3.setPixelOrthographic(PPU, viewW, viewH, REF_W, REF_H);
    if (!result.ok) throw result.status.describe();
    local hud = cam2.setPixelConfig(1.0, REF_W, REF_H);
    if (!hud.ok) throw hud.status.describe();
    local applied = cam2.applyPixelPerfect(viewW, viewH);
    if (!applied.ok) throw applied.status.describe();
    status = "scale=" + cam3.getPixelScale() + " orthoH=" + cam3.getOrthoHeight() +
             " zoom2d=" + cam2.getZoom();
}

function placeCamera() {
    local cx = nudgeX;
    ctrl.setTarget(cx, 0.0, 0.0);
    ctrl.setRadius(14.0);
    ctrl.setOrthoHeight(cam3.getOrthoHeight());
    ctrl.snap();
    cam3.snapToPixelGrid(gfx.getWidth(), gfx.getHeight());
}

function drawPixelHud() {
    local viewW = gfx.getWidth().tofloat();
    local viewH = gfx.getHeight().tofloat();
    local z = cam2.getZoom();
    // Virtual 320×180 HUD: one pixel art panel centered in the reference frame.
    local panelX = 220.0;
    local panelY = 24.0;
    local panelW = 84.0;
    local panelH = 48.0;
    local sx = cam2.worldToScreenX(panelX, panelY, viewW, viewH);
    local sy = cam2.worldToScreenY(panelX, panelY, viewW, viewH);
    gfx.drawSolidRect(sx, sy, panelW * z, panelH * z, 0.08, 0.09, 0.12, 0.92);
    // 1-world-unit (=1 virtual pixel) border tick marks — must stay sharp at integer zoom.
    for (local i = 0; i < 16; i += 1) {
        local tx = panelX + 4.0 + i.tofloat() * 4.0;
        local ty = panelY + 8.0;
        local px = cam2.worldToScreenX(tx, ty, viewW, viewH);
        local py = cam2.worldToScreenY(tx, ty, viewW, viewH);
        local on = (i % 2) == 0;
        gfx.drawSolidRect(px, py, 3.0 * z, 3.0 * z,
                          on ? 0.95 : 0.2, on ? 0.85 : 0.75, on ? 0.2 : 0.95, 1.0);
    }
    // Crosshair at virtual origin of the HUD panel to prove pixel snap.
    local cx = cam2.worldToScreenX(panelX + 42.0, panelY + 28.0, viewW, viewH);
    local cy = cam2.worldToScreenY(panelX + 42.0, panelY + 28.0, viewW, viewH);
    gfx.drawSolidRect(cx - z, cy - 6.0 * z, 2.0 * z, 12.0 * z, 1.0, 1.0, 1.0, 1.0);
    gfx.drawSolidRect(cx - 6.0 * z, cy - z, 12.0 * z, 2.0 * z, 1.0, 1.0, 1.0, 1.0);
}

eve_init <- function() {
    gfx.setBackgroundColor(0.06, 0.07, 0.10, 1.0);
    gfx.setDirectionalLight(-0.2, 1.0, 0.15, 1.4, 1.35, 1.25);

    cam3 = eve.Camera3D();
    cam3.setAmbient(0.28, 0.30, 0.34);
    cam3.setActive(true);

    ctrl = eve.CameraController();
    ctrl.setCamera(cam3);
    ctrl.setMode("ortho");
    ctrl.setSmooth(0.0);

    cam2 = eve.Camera2D();
    cam2.setPosition(160.0, 90.0);
    cam2.setPixelSnap(true);

    if (tiles.len() == 0) rebuildGrid();
    applyPixelOrtho();
    placeCamera();
    hudReady = true;
    print("PIXEL_PERFECT_CAMERA_READY " + status + "\n");
};

eve_update <- function(dt) {
    pulse += dt;
    local left = keyboard.isDown("a") || keyboard.isDown("left");
    local right = keyboard.isDown("d") || keyboard.isDown("right");
    local snap = keyboard.isDown("s");
    local reset = keyboard.isDown("r");
    if (left && !keyLeftWas) {
        nudgeX -= 0.3;
        placeCamera();
        status = "nudged left x=" + cam3.getEyeX();
    }
    if (right && !keyRightWas) {
        nudgeX += 0.3;
        placeCamera();
        status = "nudged right x=" + cam3.getEyeX();
    }
    if (snap && !keySnapWas) {
        cam3.snapToPixelGrid(gfx.getWidth(), gfx.getHeight());
        status = "manual snap x=" + cam3.getEyeX();
    }
    if (reset && !keyResetWas) {
        nudgeX = 0.0;
        applyPixelOrtho();
        placeCamera();
        status = "reset scale=" + cam3.getPixelScale();
    }
    keyLeftWas = left;
    keyRightWas = right;
    keySnapWas = snap;
    keyResetWas = reset;
    ctrl.update(dt);
    // Controller rewrite of eye/target runs after snap(); re-align to the pixel grid.
    cam3.snapToPixelGrid(gfx.getWidth(), gfx.getHeight());
};

eve_render <- function() {
    gfx.clear();
    gfx.render3D();
    if (hudReady) drawPixelHud();
};

eve_reload <- function() {
    tiles = [];
    eve_init();
};
