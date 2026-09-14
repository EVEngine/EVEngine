// SilPOM vs SSDM — three brick *corners* (two faces at 90°) under a grazing camera.
//
// A dihedral crease is where the techniques diverge most:
//   Left  = classic POM   — flat geometric crease; relief stays inside each face
//   Mid   = SilPOM        — POM + discard when UV leaves the chart → gaps along the crease
//   Right = SSDM-style    — POM shading + FragDepth pull → crease stays filled
//
// Controls:
//   O          toggle auto-orbit
//   A / D      yaw
//   W / S      pitch
//   [ / ]      relief scale
//   - / =      max ray-march layers
//   1 / 2 / 3  focus camera on POM / SilPOM / SSDM corner

persist cmpCam = null
persist cmpPom = null
persist cmpSil = null
persist cmpSsdm = null
persist cmpGround = null
persist cmpAlbedo = null
persist cmpHeight = null
persist cmpYaw = 0.55
persist cmpPitch = 0.22
persist cmpOrbit = false
persist cmpScale = 0.12
persist cmpMinLayers = 12.0
persist cmpMaxLayers = 40.0
persist cmpFocus = 2
persist cmpStatus = "orbit off"
persist cmpCornerMesh = null

function clampf(v, a, b) {
    if (v < a) return a;
    if (v > b) return b;
    return v;
}

function brickColor(u, v) {
    local bu = u * 6.0;
    local bv = v * 3.0;
    local row = floor(bv);
    local odd = (row.tointeger() % 2) == 1;
    local ou = odd ? (bu + 0.5) : bu;
    local fx = fabs(ou - floor(ou) - 0.5);
    local fy = fabs(bv - floor(bv) - 0.5);
    local mortar = (fx > 0.42 || fy > 0.38) ? 1.0 : 0.0;
    if (mortar > 0.5) return [0.55, 0.52, 0.48, 0.08];
    local h = 0.55 + 0.35 * (0.5 + 0.5 * sin(ou * 9.1) * cos(bv * 7.3));
    return [0.62 + 0.15 * h, 0.28 + 0.08 * h, 0.22 + 0.05 * h, h];
}

function buildTextures() {
    local size = 128;
    local albedo = eve.Image().newEmptyImageData(size, size, "RGBA8");
    local height = eve.Image().newEmptyImageData(size, size, "RGBA8");
    for (local y = 0; y < size; y += 1) {
        for (local x = 0; x < size; x += 1) {
            local u = (x + 0.5) / size.tofloat();
            local v = (y + 0.5) / size.tofloat();
            local c = brickColor(u, v);
            albedo.setPixel(x, y, c[0], c[1], c[2], 1.0);
            height.setPixel(x, y, c[3], c[3], c[3], 1.0);
        }
    }
    cmpAlbedo = gfx.newTexture(albedo, true, true);
    cmpHeight = gfx.newTexture(height, true, true);
}

// L-shaped dihedral: face +Z (xy) and face +X (zy), crease along +Y through the origin.
// Each face is its own UV chart [0,1]^2 so SilPOM can clip at the crease (u=0 on both).
function makeCornerMesh() {
    local pos = [
        // A: +Z  (x in [0,1], y in [-1,1], z = 0)
        0.0, -1.0, 0.0,
        1.0, -1.0, 0.0,
        1.0,  1.0, 0.0,
        0.0,  1.0, 0.0,
        // B: +X  (z in [0,1], y in [-1,1], x = 0)
        0.0, -1.0, 0.0,
        0.0,  1.0, 0.0,
        0.0,  1.0, 1.0,
        0.0, -1.0, 1.0
    ];
    local nrm = [
        0.0, 0.0, 1.0,
        0.0, 0.0, 1.0,
        0.0, 0.0, 1.0,
        0.0, 0.0, 1.0,
        1.0, 0.0, 0.0,
        1.0, 0.0, 0.0,
        1.0, 0.0, 0.0,
        1.0, 0.0, 0.0
    ];
    local uv = [
        // A: u=x, v=(y+1)/2
        0.0, 0.0,
        1.0, 0.0,
        1.0, 1.0,
        0.0, 1.0,
        // B: u=z, v=(y+1)/2 — crease edge is u=0 on both charts
        0.0, 0.0,
        0.0, 1.0,
        1.0, 1.0,
        1.0, 0.0
    ];
    local idx = [
        0, 1, 2, 0, 2, 3,
        4, 5, 6, 4, 6, 7
    ];
    return gfx.newMeshFromArrays(pos, nrm, uv, 8, idx, 12);
}

function makeCorner(mode, x) {
    // One shader instance per corner so push-constant `mode` stays distinct.
    local shader = gfx.newMeshShader(fs.readText("shaders/compare.frag"));
    shader.declareFloat("mode");
    shader.declareFloat("scale");
    shader.declareFloat("minLayers");
    shader.declareFloat("maxLayers");
    shader.declareFloat("padding");
    shader.sendFloat("mode", mode.tofloat());
    shader.sendFloat("scale", cmpScale);
    shader.sendFloat("minLayers", cmpMinLayers);
    shader.sendFloat("maxLayers", cmpMaxLayers);
    shader.sendFloat("padding", 0.0);

    if (cmpCornerMesh == null) cmpCornerMesh = makeCornerMesh();

    local ent = eve.Renderable3D();
    ent.setMesh(cmpCornerMesh);
    ent.setShader(shader);
    ent.setTexture(cmpAlbedo);
    ent.setHeightTexture(cmpHeight);
    ent.setTint(1.0, 1.0, 1.0, 1.0);
    ent.setCastShadow(false);
    ent.setReceiveShadow(false);
    // Yaw so the camera looks *into* the crook (both faces visible).
    ent.setPosition(x, 1.08, 0.0);
    ent.setYaw(-0.55);
    return { ent = ent, shader = shader, mode = mode.tofloat(), x = x };
}

function syncPanel(panel) {
    panel.shader.sendFloat("mode", panel.mode);
    panel.shader.sendFloat("scale", cmpScale);
    panel.shader.sendFloat("minLayers", cmpMinLayers);
    panel.shader.sendFloat("maxLayers", cmpMaxLayers);
    panel.shader.sendFloat("padding", 0.0);
}

function updateCamera() {
    local spacing = 3.0;
    local focusX = (cmpFocus - 2).tofloat() * spacing;
    local dist = 5.6;
    local cx = focusX + dist * cos(cmpPitch) * sin(cmpYaw);
    local cy = 1.05 + dist * sin(cmpPitch);
    local cz = dist * cos(cmpPitch) * cos(cmpYaw);
    cmpCam.setEye(cx, cy, cz);
    cmpCam.setTarget(focusX, 1.05, 0.35);
}

eve_init = function() {
    gfx.setBackgroundColor(0.10, 0.12, 0.16, 1.0);
    if (cmpAlbedo == null) buildTextures();
    if (cmpCornerMesh == null) cmpCornerMesh = makeCornerMesh();
    if (cmpPom == null) cmpPom = makeCorner(0, -3.0);
    if (cmpSil == null) cmpSil = makeCorner(1, 0.0);
    if (cmpSsdm == null) cmpSsdm = makeCorner(2, 3.0);
    if (cmpGround == null) {
        cmpGround = eve.Renderable3D();
        cmpGround.setMesh(gfx.newMeshCube(1.0));
        cmpGround.setPosition(0.0, -0.08, 0.2);
        cmpGround.setScale(12.0, 0.16, 5.0);
        cmpGround.setTint(0.16, 0.17, 0.20, 1.0);
        cmpGround.setRoughness(0.95);
        cmpGround.setCastShadow(false);
    }
    if (cmpCam == null) {
        cmpCam = eve.Camera3D();
        cmpCam.setFov(40.0);
        cmpCam.setAmbient(0.28, 0.30, 0.34);
        cmpCam.setActive(true);
    }
    gfx.setDirectionalLight(-0.35, 0.45, 0.70, 1.45, 1.30, 1.15);

    // Always re-apply mesh/layout so persist cannot freeze the old flat cards.
    cmpPom.ent.setMesh(cmpCornerMesh);
    cmpSil.ent.setMesh(cmpCornerMesh);
    cmpSsdm.ent.setMesh(cmpCornerMesh);
    cmpPom.ent.setPosition(-3.0, 1.08, 0.0);
    cmpSil.ent.setPosition(0.0, 1.08, 0.0);
    cmpSsdm.ent.setPosition(3.0, 1.08, 0.0);
    cmpPom.ent.setYaw(-0.55);
    cmpSil.ent.setYaw(-0.55);
    cmpSsdm.ent.setYaw(-0.55);
    cmpGround.setPosition(0.0, -0.08, 0.2);
    cmpGround.setScale(12.0, 0.16, 5.0);

    syncPanel(cmpPom); syncPanel(cmpSil); syncPanel(cmpSsdm);
    updateCamera();
    print("SilPOM vs SSDM corners: O orbit | A/D yaw | W/S pitch | [/] scale | 1/2/3 focus\n");
    print("Left=POM  Mid=SilPOM (crease gaps)  Right=SSDM (filled crease + depth)\n");
};

eve_asset_reload <- function(path) {
    if (path.find("compare.frag") == null) return;
    local src = fs.readText("shaders/compare.frag");
    local panels = [cmpPom, cmpSil, cmpSsdm];
    foreach (p in panels) {
        local result = gfx.replaceShaderFromGlsl(p.shader, "", src);
        if (!result.ok) {
            cmpStatus = "compile failed";
            print("[silpom-ssdm] compile failed\n");
            return;
        }
        syncPanel(p);
    }
    cmpStatus = "shader reloaded";
    print("[silpom-ssdm] shader reloaded\n");
};

eve_update = function(dt) {
    if (cmpOrbit) cmpYaw += dt * 0.20;

    if (key_just_pressed("o") || key_just_pressed("O")) {
        cmpOrbit = !cmpOrbit;
        cmpStatus = cmpOrbit ? "orbit on" : "orbit off";
    }
    if (key_just_pressed("a") || key_just_pressed("A")) cmpYaw -= 0.12;
    if (key_just_pressed("d") || key_just_pressed("D")) cmpYaw += 0.12;
    if (key_just_pressed("w") || key_just_pressed("W"))
        cmpPitch = clampf(cmpPitch + 0.05, 0.05, 0.55);
    if (key_just_pressed("s") || key_just_pressed("S"))
        cmpPitch = clampf(cmpPitch - 0.05, 0.05, 0.55);
    if (key_just_pressed("[")) {
        cmpScale = clampf(cmpScale - 0.01, 0.01, 0.18);
        cmpStatus = "scale " + cmpScale;
    }
    if (key_just_pressed("]")) {
        cmpScale = clampf(cmpScale + 0.01, 0.01, 0.18);
        cmpStatus = "scale " + cmpScale;
    }
    if (key_just_pressed("-") || key_just_pressed("_")) {
        cmpMaxLayers = clampf(cmpMaxLayers - 4.0, 8.0, 64.0);
        cmpStatus = "layers " + cmpMaxLayers;
    }
    if (key_just_pressed("=") || key_just_pressed("+")) {
        cmpMaxLayers = clampf(cmpMaxLayers + 4.0, 8.0, 64.0);
        cmpStatus = "layers " + cmpMaxLayers;
    }
    if (key_just_pressed("1")) { cmpFocus = 1; cmpStatus = "focus POM"; }
    if (key_just_pressed("2")) { cmpFocus = 2; cmpStatus = "focus SilPOM"; }
    if (key_just_pressed("3")) { cmpFocus = 3; cmpStatus = "focus SSDM"; }

    syncPanel(cmpPom); syncPanel(cmpSil); syncPanel(cmpSsdm);
    updateCamera();
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
};
