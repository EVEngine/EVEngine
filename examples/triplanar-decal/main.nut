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

function clampf(v, a, b) {
    if (v < a) return a;
    if (v > b) return b;
    return v;
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
        0.0, 0.02, 0.0,   // position (slightly above floor)
        0.0, 1.0, 0.0,    // forward = +Y
        tdTex,
        "demo",
        2.4,              // size (XY)
        1.6,              // depth (along forward) — tall enough to cover the wall
        false, 0,
        0.0, 0.0, 0.0);
    applyProjection();
}

eve_init = function() {
    gfx.setBackgroundColor(0.10, 0.12, 0.15, 1.0);

    // Optional capture_settings.nut (used by scripts/capture_root.nut):
    //   td_force_mode <- "planar" / "triplanar"
    //   td_capture_path <- "out.png"
    if ("td_force_mode" in getroottable()) {
        tdMode = td_force_mode;
        tdOrbit = false;
        tdYaw = 0.85;
        tdPitch = 0.32;
    }
    if ("td_capture_path" in getroottable()) tdCapturePath = td_capture_path;

    if (tdTex == null) {
        tdTex = gfx.newTextureFromFile("assets/grid_decal.png");
        gfx.setTextureSampler(tdTex, "linear", "none", 1.0, 0.0);
    }

    if (tdFloor == null) {
        tdFloor = eve.Renderable3D();
        tdFloor.setMesh(gfx.newMeshCube(1.0));
        tdFloor.setPosition(0.0, -0.05, 0.0);
        tdFloor.setScale(4.0, 0.1, 4.0);
        tdFloor.setTint(0.55, 0.56, 0.58, 1.0);
        tdFloor.setRoughness(0.92);
        tdFloor.setCastShadow(false);
    }
    if (tdWall == null) {
        tdWall = eve.Renderable3D();
        tdWall.setMesh(gfx.newMeshCube(1.0));
        tdWall.setPosition(0.0, 1.0, -1.55);
        tdWall.setScale(4.0, 2.0, 0.12);
        tdWall.setTint(0.62, 0.63, 0.66, 1.0);
        tdWall.setRoughness(0.9);
        tdWall.setCastShadow(false);
    }
    if (tdPillar == null) {
        // Extra side face near the corner to show YZ-plane sampling.
        tdPillar = eve.Renderable3D();
        tdPillar.setMesh(gfx.newMeshCube(1.0));
        tdPillar.setPosition(-1.55, 1.0, 0.0);
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
        tdDecal.clearAll();
        spawnDecal();
    }

    gfx.setDirectionalLight(-0.45, 0.85, 0.35, 1.25, 1.15, 1.05);
    updateCamera();
    print("Triplanar Decal: T mode | [/] sharpness | O orbit | A/D yaw | W/S pitch | R reset\n");
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

    if (key_just_pressed("t") || key_just_pressed("T")) {
        if (tdMode == "triplanar") tdMode = "planar";
        else tdMode = "triplanar";
        applyProjection();
        print("[triplanar-decal] " + tdStatus + "\n");
    }
    if (key_just_pressed("[")) {
        tdSharp = clampf(tdSharp - 1.0, 1.0, 32.0);
        applyProjection();
        print("[triplanar-decal] " + tdStatus + "\n");
    }
    if (key_just_pressed("]")) {
        tdSharp = clampf(tdSharp + 1.0, 1.0, 32.0);
        applyProjection();
        print("[triplanar-decal] " + tdStatus + "\n");
    }
    if (key_just_pressed("o") || key_just_pressed("O")) {
        tdOrbit = !tdOrbit;
    }
    if (key_just_pressed("r") || key_just_pressed("R")) {
        tdYaw = 0.55;
        tdPitch = 0.28;
        tdOrbit = true;
    }
    if (key_pressed("a") || key_pressed("A")) tdYaw -= dt * 1.1;
    if (key_pressed("d") || key_pressed("D")) tdYaw += dt * 1.1;
    if (key_pressed("w") || key_pressed("W")) tdPitch = clampf(tdPitch + dt * 0.8, -0.2, 1.2);
    if (key_pressed("s") || key_pressed("S")) tdPitch = clampf(tdPitch - dt * 0.8, -0.2, 1.2);

    if (tdOrbit) tdYaw += dt * 0.25;
    updateCamera();
};
