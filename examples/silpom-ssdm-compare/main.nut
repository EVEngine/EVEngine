// Full SilPOM vs full SSDM — planar extruded brick cards.
//
// Left  = classic POM   (parallax only; geometric silhouette)
// Mid   = SilPOM        (steep POM + soft chart clip + horizon trim +
//                        height normals + self-shadow + FragDepth)
// Right = SSDM          (model-space heightfield raymarch through the slab +
//                        FragDepth from the geometric hit)
//
// Cards are extruded slabs (local Z = height axis) so SilPOM/SSDM can change
// the silhouette. Cylinders are the wrong domain for these algorithms.
//
// Controls:
//   O          toggle auto-orbit
//   A / D      yaw
//   W / S      pitch
//   [ / ]      relief scale
//   - / =      max ray-march layers
//   1 / 2 / 3  focus POM / SilPOM / SSDM

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
persist cmpScale = 0.08
persist cmpMinLayers = 16.0
persist cmpMaxLayers = 48.0
persist cmpFocus = 2
persist cmpStatus = "orbit off"
persist cmpSlabMesh = null
persist cmpThinMesh = null
persist cmpReady = false

function clampf(v, a, b) {
    if (v < a) return a;
    if (v > b) return b;
    return v;
}

function brickColor(u, v) {
    local bu = u * 6.0;
    local bv = v * 4.0;
    local row = floor(bv);
    local odd = (row.tointeger() % 2) == 1;
    local ou = odd ? (bu + 0.5) : bu;
    local fx = fabs(ou - floor(ou) - 0.5);
    local fy = fabs(bv - floor(bv) - 0.5);
    local mortar = (fx > 0.40 || fy > 0.36) ? 1.0 : 0.0;
    if (mortar > 0.5) return [0.52, 0.50, 0.46, 0.05];
    local h = 0.55 + 0.40 * (0.5 + 0.5 * sin(ou * 9.1) * cos(bv * 7.3));
    return [0.70 + 0.12 * h, 0.32 + 0.08 * h, 0.24 + 0.05 * h, h];
}

function buildTextures() {
    // 128² keeps Squirrel setPixel init reasonable on this VM.
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

// Unit slab: XY in [-1,1], Z in [0,1] (front at Z=1). UV from XY.
// newMeshFromArrays requires FLOAT position/normal/uv arrays (not integers).
function makeSlabMesh() {
    local pos = [
        // front z=1
        -1.0, -1.0, 1.0,  1.0, -1.0, 1.0,  1.0, 1.0, 1.0,  -1.0, 1.0, 1.0,
        // back z=0
        -1.0, -1.0, 0.0, -1.0, 1.0, 0.0,  1.0, 1.0, 0.0,   1.0,-1.0, 0.0,
        // -X
        -1.0, -1.0, 0.0, -1.0,-1.0, 1.0, -1.0, 1.0, 1.0,  -1.0, 1.0, 0.0,
        // +X
         1.0, -1.0, 0.0,  1.0, 1.0, 0.0,  1.0, 1.0, 1.0,   1.0,-1.0, 1.0,
        // -Y
        -1.0, -1.0, 0.0,  1.0,-1.0, 0.0,  1.0,-1.0, 1.0,  -1.0,-1.0, 1.0,
        // +Y
        -1.0,  1.0, 0.0, -1.0, 1.0, 1.0,  1.0, 1.0, 1.0,   1.0, 1.0, 0.0
    ];
    local nrm = [
        0.0,0.0,1.0, 0.0,0.0,1.0, 0.0,0.0,1.0, 0.0,0.0,1.0,
        0.0,0.0,-1.0, 0.0,0.0,-1.0, 0.0,0.0,-1.0, 0.0,0.0,-1.0,
        -1.0,0.0,0.0, -1.0,0.0,0.0, -1.0,0.0,0.0, -1.0,0.0,0.0,
        1.0,0.0,0.0, 1.0,0.0,0.0, 1.0,0.0,0.0, 1.0,0.0,0.0,
        0.0,-1.0,0.0, 0.0,-1.0,0.0, 0.0,-1.0,0.0, 0.0,-1.0,0.0,
        0.0,1.0,0.0, 0.0,1.0,0.0, 0.0,1.0,0.0, 0.0,1.0,0.0
    ];
    local uv = [];
    for (local i = 0; i < 24; i += 1) {
        local x = pos[i * 3];
        local y = pos[i * 3 + 1];
        uv.push(x * 0.5 + 0.5);
        uv.push(y * 0.5 + 0.5);
    }
    local idx = [];
    for (local f = 0; f < 6; f += 1) {
        local b = f * 4;
        idx.push(b); idx.push(b + 1); idx.push(b + 2);
        idx.push(b); idx.push(b + 2); idx.push(b + 3);
    }
    return gfx.newMeshFromArrays(pos, nrm, uv, 24, idx, 36);
}

function makeThinMesh() {
    local pos = [
        -1.0, -1.0, 1.0,
         1.0, -1.0, 1.0,
         1.0,  1.0, 1.0,
        -1.0,  1.0, 1.0
    ];
    local nrm = [0.0,0.0,1.0, 0.0,0.0,1.0, 0.0,0.0,1.0, 0.0,0.0,1.0];
    local uv = [0.0,0.0, 1.0,0.0, 1.0,1.0, 0.0,1.0];
    local idx = [0, 1, 2, 0, 2, 3];
    return gfx.newMeshFromArrays(pos, nrm, uv, 4, idx, 6);
}

function makeCard(mode, x) {
    local shader = gfx.newMeshShader(fs.readText("shaders/compare.frag"));
    shader.declareFloat("mode");
    shader.declareFloat("scale");
    shader.declareFloat("minLayers");
    shader.declareFloat("maxLayers");
    shader.declareFloat("feather");
    shader.declareFloat("horizon");
    shader.sendFloat("mode", mode.tofloat());
    shader.sendFloat("scale", cmpScale);
    shader.sendFloat("minLayers", cmpMinLayers);
    shader.sendFloat("maxLayers", cmpMaxLayers);
    shader.sendFloat("feather", 0.02);
    shader.sendFloat("horizon", mode == 1 ? 0.45 : 0.0);

    local ent = eve.Renderable3D();
    ent.setMesh(mode == 0 ? cmpThinMesh : cmpSlabMesh);
    ent.setShader(shader);
    ent.setTexture(cmpAlbedo);
    ent.setHeightTexture(cmpHeight);
    ent.setTint(1.0, 1.0, 1.0, 1.0);
    ent.setCastShadow(false);
    ent.setReceiveShadow(false);
    local thick = (mode == 0) ? 0.02 : clampf(cmpScale * 1.25, 0.04, 0.20);
    ent.setPosition(x, 1.0, 0.0);
    ent.setScale(1.0, 1.0, thick);
    ent.setYaw(-0.15);
    return { ent = ent, shader = shader, mode = mode.tofloat(), x = x };
}

function syncPanel(panel) {
    if (panel == null || panel.shader == null) return;
    panel.shader.sendFloat("mode", panel.mode);
    panel.shader.sendFloat("scale", cmpScale);
    panel.shader.sendFloat("minLayers", cmpMinLayers);
    panel.shader.sendFloat("maxLayers", cmpMaxLayers);
    panel.shader.sendFloat("feather", 0.02);
    panel.shader.sendFloat("horizon", panel.mode > 0.5 && panel.mode < 1.5 ? 0.45 : 0.0);
    if (panel.mode > 0.5) {
        local thick = clampf(cmpScale * 1.25, 0.04, 0.20);
        panel.ent.setScale(1.0, 1.0, thick);
    }
}

function updateCamera() {
    if (cmpCam == null) return;
    local spacing = 2.8;
    local focusX = (cmpFocus - 2).tofloat() * spacing;
    local dist = 4.6;
    local cx = focusX + dist * cos(cmpPitch) * sin(cmpYaw);
    local cy = 1.0 + dist * sin(cmpPitch);
    local cz = dist * cos(cmpPitch) * cos(cmpYaw);
    cmpCam.setEye(cx, cy, cz);
    cmpCam.setTarget(focusX, 1.0, 0.0);
}

eve_init = function() {
    cmpReady = false;
    gfx.setBackgroundColor(0.10, 0.12, 0.16, 1.0);
    if (cmpAlbedo == null) buildTextures();
    if (cmpSlabMesh == null) cmpSlabMesh = makeSlabMesh();
    if (cmpThinMesh == null) cmpThinMesh = makeThinMesh();
    if (cmpPom == null) cmpPom = makeCard(0, -2.8);
    if (cmpSil == null) cmpSil = makeCard(1, 0.0);
    if (cmpSsdm == null) cmpSsdm = makeCard(2, 2.8);
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
        cmpCam.setAmbient(0.30, 0.32, 0.36);
        cmpCam.setActive(true);
    }
    gfx.setDirectionalLight(-0.25, 0.55, 0.75, 1.40, 1.28, 1.15);

    cmpPom.ent.setMesh(cmpThinMesh);
    cmpSil.ent.setMesh(cmpSlabMesh);
    cmpSsdm.ent.setMesh(cmpSlabMesh);
    cmpPom.ent.setPosition(-2.8, 1.0, 0.0);
    cmpSil.ent.setPosition(0.0, 1.0, 0.0);
    cmpSsdm.ent.setPosition(2.8, 1.0, 0.0);
    cmpPom.ent.setScale(1.0, 1.0, 0.02);
    local thick = clampf(cmpScale * 1.25, 0.04, 0.20);
    cmpSil.ent.setScale(1.0, 1.0, thick);
    cmpSsdm.ent.setScale(1.0, 1.0, thick);
    cmpPom.ent.setYaw(-0.15);
    cmpSil.ent.setYaw(-0.15);
    cmpSsdm.ent.setYaw(-0.15);

    syncPanel(cmpPom); syncPanel(cmpSil); syncPanel(cmpSsdm);
    updateCamera();
    cmpReady = true;
    print("Full SilPOM vs SSDM: O orbit | A/D yaw | W/S pitch | [/] scale | 1/2/3 focus\n");
    print("Left=POM  Mid=SilPOM (soft clip+horizon)  Right=SSDM (geometric height march)\n");
};

eve_asset_reload <- function(path) {
    if (!cmpReady) return;
    if (path.find("compare.frag") == null) return;
    local src = fs.readText("shaders/compare.frag");
    foreach (p in [cmpPom, cmpSil, cmpSsdm]) {
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
    if (!cmpReady) return;
    if (cmpOrbit) cmpYaw += dt * 0.18;
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
        cmpScale = clampf(cmpScale - 0.01, 0.02, 0.16);
        cmpStatus = "scale " + cmpScale;
    }
    if (key_just_pressed("]")) {
        cmpScale = clampf(cmpScale + 0.01, 0.02, 0.16);
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
    if (cmpReady) gfx.render3D();
};
