// SilPOM vs SSDM comparison — three planar brick cards under a grazing camera.
//
// Left  = classic POM   (depth inside the face; silhouette stays flat)
// Mid   = SilPOM        (POM + discard when displaced UV leaves the mesh chart)
// Right = SSDM-style    (view-space slab march + FragDepth silhouette)
//
// Controls:
//   O          toggle auto-orbit
//   A / D      yaw
//   W / S      pitch
//   [ / ]      relief scale
//   - / =      max ray-march layers
//   1 / 2 / 3  focus camera on POM / SilPOM / SSDM panel

persist cmpCam = null
persist cmpPom = null
persist cmpSil = null
persist cmpSsdm = null
persist cmpGround = null
persist cmpAlbedo = null
persist cmpHeight = null
persist cmpYaw = 0.15
persist cmpPitch = 0.18
persist cmpOrbit = false
persist cmpScale = 0.12
persist cmpMinLayers = 12.0
persist cmpMaxLayers = 40.0
persist cmpFocus = 2
persist cmpStatus = "orbit off"

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

function makeCardMesh() {
    local pos = [
        -1.0, -1.0, 0.0,
         1.0, -1.0, 0.0,
         1.0,  1.0, 0.0,
        -1.0,  1.0, 0.0
    ];
    local nrm = [
        0.0, 0.0, 1.0,
        0.0, 0.0, 1.0,
        0.0, 0.0, 1.0,
        0.0, 0.0, 1.0
    ];
    local uv = [
        0.0, 0.0,
        1.0, 0.0,
        1.0, 1.0,
        0.0, 1.0
    ];
    local idx = [0, 1, 2, 0, 2, 3];
    return gfx.newMeshFromArrays(pos, nrm, uv, 4, idx, 6);
}

function makePanel(mode, x) {
    // One shader instance per panel so push-constant `mode` stays distinct.
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

    local ent = eve.Renderable3D();
    ent.setMesh(makeCardMesh());
    ent.setShader(shader);
    ent.setTexture(cmpAlbedo);
    ent.setHeightTexture(cmpHeight);
    ent.setTint(1.0, 1.0, 1.0, 1.0);
    ent.setCastShadow(false);
    ent.setReceiveShadow(false);
    ent.setPosition(x, 1.08, 0.15);
    return { ent = ent, shader = shader, mode = mode.tofloat() };
}

function syncPanel(panel) {
    panel.shader.sendFloat("mode", panel.mode);
    panel.shader.sendFloat("scale", cmpScale);
    panel.shader.sendFloat("minLayers", cmpMinLayers);
    panel.shader.sendFloat("maxLayers", cmpMaxLayers);
    panel.shader.sendFloat("padding", 0.0);
}

function updateCamera() {
    local focusX = (cmpFocus - 2).tofloat() * 2.4;
    local dist = 5.2;
    local cx = focusX + dist * cos(cmpPitch) * sin(cmpYaw);
    local cy = 1.08 + dist * sin(cmpPitch);
    local cz = dist * cos(cmpPitch) * cos(cmpYaw);
    cmpCam.setEye(cx, cy, cz);
    cmpCam.setTarget(focusX, 1.08, 0.0);
}

eve_init = function() {
    gfx.setBackgroundColor(0.10, 0.12, 0.16, 1.0);
    if (cmpAlbedo == null) buildTextures();
    if (cmpPom == null) cmpPom = makePanel(0, -2.4);
    if (cmpSil == null) cmpSil = makePanel(1, 0.0);
    if (cmpSsdm == null) cmpSsdm = makePanel(2, 2.4);
    if (cmpGround == null) {
        cmpGround = eve.Renderable3D();
        cmpGround.setMesh(gfx.newMeshCube(1.0));
        cmpGround.setPosition(0.0, -0.08, 0.0);
        cmpGround.setScale(10.0, 0.16, 4.0);
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
    gfx.setDirectionalLight(-0.55, 0.35, 0.75, 1.45, 1.30, 1.15);
    // Always re-apply layout so persist doesn't freeze an old contact height.
    cmpPom.ent.setPosition(-2.4, 1.08, 0.15);
    cmpSil.ent.setPosition(0.0, 1.08, 0.15);
    cmpSsdm.ent.setPosition(2.4, 1.08, 0.15);
    cmpGround.setPosition(0.0, -0.08, 0.0);
    cmpGround.setScale(10.0, 0.16, 4.0);
    syncPanel(cmpPom); syncPanel(cmpSil); syncPanel(cmpSsdm);
    updateCamera();
    print("SilPOM vs SSDM: O orbit | A/D yaw | W/S pitch | [/] scale | 1/2/3 focus\n");
    print("Left=POM (flat edge)  Mid=SilPOM (UV clip)  Right=SSDM (view-space + depth)\n");
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
