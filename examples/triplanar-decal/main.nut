// Triplanar Decal demo — local-space YZ/XZ/XY projection so side faces
// keep undistorted UVs (UE5-style "三维投射贴花").
//
// Controls:
//   T        toggle planar / triplanar
//   [ / ]    blend sharpness
//   O        toggle auto-orbit
//   A/D      yaw
//   W/S      pitch
//   R        reset view

persist tdCamera = null
persist tdFloor = null
persist tdWall = null
persist tdPillar = null
persist tdDecal = null
persist tdTex = null
persist tdId = 0
persist tdMode = "triplanar"
persist tdSharp = 4.0
persist tdYaw = 0.55
persist tdPitch = 0.28
persist tdOrbit = true
persist tdTime = 0.0
persist tdStatus = "triplanar"
persist tdFrame = 0
persist tdShotSaved = false
persist tdCapturePath = ""
persist tdKeyEdge = {}

function clampf(v, a, b) {
    if (v < a) return a;
    if (v > b) return b;
    return v;
}

function hasKeyboard() {
    return "keyboard" in getroottable() && keyboard != null;
}

function tdKeyDown(name) {
    return hasKeyboard() && keyboard.isDown(name);
}

function tdKeyJust(name) {
    local down = tdKeyDown(name);
    local was = (name in tdKeyEdge) ? tdKeyEdge[name] : false;
    tdKeyEdge[name] <- down;
    return down && !was;
}

function updateCamera() {
    local dist = 5.2;
    local cx = dist * cos(tdPitch) * sin(tdYaw);
    local cy = 1.1 + dist * sin(tdPitch);
    local cz = dist * cos(tdPitch) * cos(tdYaw);
    tdCamera.setEye(cx, cy, cz);
    tdCamera.setTarget(0.0, 0.55, 0.0);
}

function applyProjection() {
    if (tdId <= 0) return;
    tdDecal.setProjection(tdId, tdMode, tdSharp);
    tdStatus = tdMode + "  sharp=" + tdSharp;
}

function spawnDecal() {
    // Project downward (+Y) onto the floor/wall corner so planar mode
    // either culls or stretches the wall, while triplanar wraps cleanly.
    tdId = tdDecal.project(
        0.0, 0.05, 0.0,
        0.0, 1.0, 0.0,
        tdTex,
        "demo",
        2.8,
        2.4,
        false, 0,
        0.0, 0.0, 0.0);
    applyProjection();
}

function makeGridTex() {
    // Procedural high-contrast grid so stretch is obvious without file I/O.
    local img = eve.Image().newEmptyImageData(128, 128, "RGBA8");
    local cells = 8;
    local cell = 128 / cells;
    for (local y = 0; y < 128; y += 1) {
        for (local x = 0; x < 128; x += 1) {
            local cx = x / cell;
            local cy = y / cell;
            local dark = ((cx + cy) % 2) == 0;
            local r = dark ? 0.90 : 0.95;
            local g = dark ? 0.28 : 0.82;
            local b = dark ? 0.12 : 0.18;
            // Cyan cross for axis orientation.
            if (x >= 60 && x <= 67) { r = 0.15; g = 0.85; b = 1.0; }
            if (y >= 60 && y <= 67) { r = 0.15; g = 0.85; b = 1.0; }
            img.setPixel(x, y, r, g, b, 0.95);
        }
    }
    local tex = gfx.newTexture(img, false, false);
    gfx.setTextureSampler(tex, "linear", "none", 1.0, 0.0);
    return tex;
}

eve_init = function() {
    gfx.setBackgroundColor(0.10, 0.12, 0.15, 1.0);

    if ("td_force_mode" in getroottable()) {
        tdMode = td_force_mode;
        tdOrbit = false;
        tdYaw = 0.85;
        tdPitch = 0.32;
    }
    if ("td_capture_path" in getroottable()) tdCapturePath = td_capture_path;

    if (tdTex == null) tdTex = makeGridTex();

    if (tdFloor == null) {
        tdFloor = eve.Renderable3D();
        tdFloor.setMesh(gfx.newMeshCube(1.0));
        tdFloor.setPosition(0.0, -0.05, 0.0);
        tdFloor.setScale(3.5, 0.1, 3.5);
        tdFloor.setTint(0.55, 0.56, 0.58, 1.0);
        tdFloor.setRoughness(0.92);
        tdFloor.setCastShadow(false);
    }
    if (tdWall == null) {
        tdWall = eve.Renderable3D();
        tdWall.setMesh(gfx.newMeshCube(1.0));
        // Keep the wall inside the decal volume (size ~2.8 → ±1.4 on XZ).
        tdWall.setPosition(0.0, 1.0, -1.05);
        tdWall.setScale(3.5, 2.0, 0.12);
        tdWall.setTint(0.62, 0.63, 0.66, 1.0);
        tdWall.setRoughness(0.9);
        tdWall.setCastShadow(false);
    }
    if (tdPillar == null) {
        tdPillar = eve.Renderable3D();
        tdPillar.setMesh(gfx.newMeshCube(1.0));
        tdPillar.setPosition(-1.05, 1.0, 0.0);
        tdPillar.setScale(0.12, 2.0, 2.2);
        tdPillar.setTint(0.58, 0.60, 0.63, 1.0);
        tdPillar.setRoughness(0.9);
        tdPillar.setCastShadow(false);
    }

    if (tdCamera == null) {
        tdCamera = eve.Camera3D();
        tdCamera.setFov(48.0);
        tdCamera.setAmbient(0.28, 0.30, 0.34);
        tdCamera.setActive(true);
    }

    if (tdDecal == null) {
        tdDecal = eve.Decal();
        tdDecal.setEnabled(gfx, true);
        gfx.getRenderControl().compile();
        tdDecal.clearAll();
        spawnDecal();
    }

    gfx.setDirectionalLight(-0.45, 0.85, 0.35, 1.25, 1.15, 1.05);
    updateCamera();
    print("Triplanar Decal: T mode | [/] sharpness | O orbit | A/D yaw | W/S pitch | R reset\n");
    print("[triplanar-decal] mode=" + tdMode + " id=" + tdId + " count=" + tdDecal.count() + "\n");
};

eve_update = function(dt) {
    tdTime += dt;
    tdFrame += 1;
    tdDecal.update(dt);

    if (tdCapturePath != "" && !tdShotSaved && tdFrame > 24) {
        if (gfx.saveFramePng(tdCapturePath)) {
            tdShotSaved = true;
            print("[triplanar-decal] saved " + tdCapturePath + " (" + tdMode + ")\n");
        }
    }

    if (tdKeyJust("t") || tdKeyJust("T")) {
        if (tdMode == "triplanar") tdMode = "planar";
        else tdMode = "triplanar";
        applyProjection();
        print("[triplanar-decal] " + tdStatus + "\n");
    }
    if (tdKeyJust("[")) {
        tdSharp = clampf(tdSharp - 1.0, 1.0, 32.0);
        applyProjection();
        print("[triplanar-decal] " + tdStatus + "\n");
    }
    if (tdKeyJust("]")) {
        tdSharp = clampf(tdSharp + 1.0, 1.0, 32.0);
        applyProjection();
        print("[triplanar-decal] " + tdStatus + "\n");
    }
    if (tdKeyJust("o") || tdKeyJust("O")) {
        tdOrbit = !tdOrbit;
    }
    if (tdKeyJust("r") || tdKeyJust("R")) {
        tdYaw = 0.55;
        tdPitch = 0.28;
        tdOrbit = true;
    }
    if (tdKeyDown("a") || tdKeyDown("A")) tdYaw -= dt * 1.1;
    if (tdKeyDown("d") || tdKeyDown("D")) tdYaw += dt * 1.1;
    if (tdKeyDown("w") || tdKeyDown("W")) tdPitch = clampf(tdPitch + dt * 0.8, -0.2, 1.2);
    if (tdKeyDown("s") || tdKeyDown("S")) tdPitch = clampf(tdPitch - dt * 0.8, -0.2, 1.2);

    if (tdOrbit) tdYaw += dt * 0.25;
    updateCamera();
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
};
