// Linear, tileable procedural structures demo (Phase C: mesh recipes).
// Builds six procgen mesh recipes — wooden fence, stone wall, bridge, Great Wall,
// hedge, cheval de frise — each repeated from a single tileable unit segment.
// Textures are CC0 images bundled under assets/ (see ATTRIBUTION.md).
// Run: make run/win32-debug GAME=examples/linear-structures

if (!("structures" in getroottable())) structures <- [];
if (!("active" in getroottable())) active <- 0;
if (!("segments" in getroottable())) segments <- 6;
if (!("segLength" in getroottable())) segLength <- 2.0;
if (!("autoSpin" in getroottable())) autoSpin <- false;
if (!("yaw" in getroottable())) yaw <- 0.0;
if (!("camYaw" in getroottable())) camYaw <- 0.0;
if (!("uiReady" in getroottable())) uiReady <- false;
if (!("rWasDown" in getroottable())) rWasDown <- false;

local STRUCT_RECIPES = [
    { id = "mesh.fence",       file = "assets/wood.jpg",  tex = "tex.soil",  tint = { r=1.0, g=0.92, b=0.78 } },
    { id = "mesh.stonewall",   file = "assets/stonewall.jpg", tex = "tex.stone", tint = { r=0.88, g=0.86, b=0.80 } },
    { id = "mesh.bridge",      file = "assets/brick.jpg", tex = "tex.stone", tint = { r=0.95, g=0.88, b=0.74 } },
    { id = "mesh.greatwall",   file = "assets/brick.jpg", tex = "tex.marble", tint = { r=0.92, g=0.85, b=0.70 } },
    { id = "mesh.hedge",       file = "assets/hedge.jpg", tex = "tex.soil",  tint = { r=0.62, g=0.86, b=0.45 } },
    { id = "mesh.chevaldefrise", file = "assets/wood.jpg", tex = "tex.soil", tint = { r=0.94, g=0.78, b=0.55 } },
];

function loadStructureTexture(spec, p) {
    local path = spec.file;
    if (path != null && path.len() > 0) {
        try {
            return gfx.newTextureFromFileRepeated(path, true, true);
        } catch (err) {
            // Fall back to a procedural texture if the CC0 file is missing.
        }
    }
    local tp = procgen.newParams();
    tp.setSeed(7);
    tp.setSize(128, 128);
    tp.setFloat("scale", 4.0);
    tp.setInt("octaves", 4);
    tp.setInt("colors", 6);
    tp.setInt("pixelSize", 2);
    tp.setInt("seamless", 1);
    return procgen.generateTexture(spec.tex, tp, gfx);
}

function buildOne(spec, x, y, z) {
    local p = procgen.newParams();
    p.setInt("segments", segments);
    p.setFloat("segLength", segLength);
    p.setFloat("uvRepeat", 2.0);

    local mesh = procgen.generateMesh(spec.id, p, gfx);
    if (mesh == null) {
        ui.select("lab");
        ui.setText("status", "FAIL " + spec.id + ": " + procgen.lastError());
        return null;
    }
    local tex = loadStructureTexture(spec, p);

    local ent = eve.Renderable3D();
    ent.setMesh(mesh);
    ent.setTexture(tex);
    ent.setTint(spec.tint.r, spec.tint.g, spec.tint.b, 1.0);
    ent.setRoughness(0.85);
    ent.setMetallic(0.03);
    ent.setCastShadow(true);
    ent.setReceiveShadow(true);
    ent.setPosition(x, y, z);
    return ent;
}

function rebuild() {
    foreach (s in structures) if (s != null) s.setVisible(false);

    local total = 0.0;
    local widths = [];
    foreach (spec in STRUCT_RECIPES) {
        // Rough footprint: length = segments * segLength. Depth used for spacing in Z.
        widths.push(segments * segLength);
        total += segments * segLength;
    }
    local gap = 3.0;
    total += gap * (STRUCT_RECIPES.len() - 1);
    local startX = -total * 0.5;

    structures = [];
    local cx = startX;
    for (local i = 0; i < STRUCT_RECIPES.len(); i++) {
        local spec = STRUCT_RECIPES[i];
        local w = widths[i];
        // Center the structure at the midpoint of its slot.
        local px = cx + w * 0.5;
        structures.push(buildOne(spec, px, 0.0, 0.0));
        cx += w + gap;
    }

    if (uiReady) {
        ui.select("lab");
        ui.setText("status", STRUCT_RECIPES[active].id + "  /  segments=" + segments);
    }
}

function setupCamera() {
    camera = eve.Camera3D();
    camera.setEye(0.0, 6.5, 16.0);
    camera.setTarget(0.0, 1.2, 0.0);
    camera.setFov(50.0);
    camera.setAmbient(0.30, 0.30, 0.28);
    gfx.setDirectionalLight(-0.5, 0.9, 0.4, 1.9, 1.6, 1.3);
}

function buildPanel() {
    ui.setTheme("dark");
    ui.setNavKeyboard(true);
    ui.beginBuild();
    ui.beginWindow("LINEAR STRUCTURES", "root");
    ui.text("TILEABLE PROCEDURAL PROPS", "eyebrow");
    ui.text(STRUCT_RECIPES[active].id, "sample");
    ui.slider("Segments", segments, 1.0, 24.0, "segments");
    ui.slider("Unit length", segLength, 0.5, 6.0, "segLength");
    ui.button("Next structure", "next");
    ui.button("Rebuild", "rebuild");
    ui.text("Cycle structures with Tab • +/- adjust segments", "hint");
    ui.text("Ready.", "status");
    ui.end();
    ui.mountBuildAs("lab");
    ui.select("lab");
    ui.setHostOverlay(true);
    ui.setHostPos(900.0, 30.0, 340.0, 420.0);
    uiReady = true;
}

function activeId() { return STRUCT_RECIPES[active].id; }

eve_init = function() {
    gfx.setBackgroundColor(0.05, 0.06, 0.07, 1.0);
    setupCamera();
    if (!uiReady) buildPanel();
    rebuild();
};

eve_update = function(dt) {
    if (autoSpin) {
        camYaw += dt * 6.0;
        local ex = math.polarX(16.0, camYaw);
        local ez = math.polarY(16.0, camYaw);
        camera.setEye(ex, 6.5, ez);
        camera.setTarget(0.0, 1.2, 0.0);
    }

    local changed = ui.consumeChange();
    while (changed != "") {
        ui.select("lab");
        if (changed == "lab/segments") { segments = ui.getValue("segments").tointeger(); rebuild(); }
        else if (changed == "lab/segLength") { segLength = ui.getValue("segLength"); rebuild(); }
        changed = ui.consumeChange();
    }

    local clicked = ui.consumeClick();
    while (clicked != "") {
        if (clicked == "lab/next") { active = (active + 1) % STRUCT_RECIPES.len(); rebuild(); }
        else if (clicked == "lab/rebuild") { rebuild(); }
        clicked = ui.consumeClick();
    }

    local tabDown = keyboard.isDown("tab");
    local tabPressed = tabDown && !("tabWas" in getroottable() ? tabWas : false);
    tabWas <- tabDown;
    if (tabPressed) { active = (active + 1) % STRUCT_RECIPES.len(); rebuild(); }

    local plus = keyboard.isDown("plus") || keyboard.isDown("equals");
    local wasPlus = ("plusWas" in getroottable() ? plusWas : false);
    plusWas <- plus;
    if (plus && !wasPlus) { segments = segments + 1; rebuild(); }

    local minus = keyboard.isDown("minus");
    local wasMinus = ("minusWas" in getroottable() ? minusWas : false);
    minusWas <- minus;
    if (minus && !wasMinus) { if (segments > 1) { segments = segments - 1; rebuild(); } }
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    ui.beginFrameAndRender();
};
