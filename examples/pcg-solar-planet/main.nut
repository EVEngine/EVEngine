// examples/pcg-solar-planet
// SebLague Solar-System Episode 02 lab on EVEngine's PCG stack:
//   UV / Fibonacci / Cube / Icosphere topologies,
//   CPU height (fBm + ridged + warp + crater SDF),
//   triplanar detail + height/slope colouring, ocean shell.

dofile("planet_gen.nut");

const DEG_TO_RAD = 0.017453292519943295;
const FOV_DEGREES = 42.0;
const FIT_MARGIN = 1.22;
const ORBIT_SPEED = 0.28;
const PITCH_MIN = -78.0;
const PITCH_MAX = 78.0;
const ZOOM_STEP = 0.12;
const ZOOM_MIN = 0.55;
const ZOOM_MAX = 3.80;
const SPIN_SPEED = 8.0;

persist st = null;

function createState() {
    return {
        camera = null,
        terrain = null,
        ocean = null,
        terrainShader = null,
        oceanShader = null,
        terrainMesh = null,
        oceanMesh = null,

        topologyIndex = 2, // ico
        topologyNames = ["uv", "fibonacci", "cube", "ico"],
        topologyLabels = ["UV Sphere", "Fibonacci Sphere", "Cube Sphere", "Icosphere"],
        presetIndex = 0,
        presetNames = ["earth", "moon", "hyperion", "bare"],
        presetLabels = ["Earth-like", "Moon craters", "Hyperion", "Bare mesh"],
        resolution = 3,
        seed = 20261012,

        yaw = 28.0,
        pitch = 22.0,
        zoom = 1.0,
        spinning = true,
        dragging = false,
        lastMouseX = 0,
        lastMouseY = 0,

        status = "building...",
        triangleCount = 0,
        vertexCount = 0,
        uiReady = false,
        oceanLevel01 = 0.4,
        showOcean = true,
        minHeight = 1.0,
        maxHeight = 1.0
    };
}

function fitDistance(radius) {
    return (radius * FIT_MARGIN) / sin(FOV_DEGREES * 0.5 * DEG_TO_RAD);
}

function applyCamera() {
    if (st.camera == null) return;
    local distance = fitDistance(st.maxHeight) * st.zoom;
    local yawRad = st.yaw * DEG_TO_RAD;
    local pitchRad = st.pitch * DEG_TO_RAD;
    local horizontal = cos(pitchRad) * distance;
    st.camera.setEye(horizontal * sin(yawRad), sin(pitchRad) * distance, horizontal * cos(yawRad));
    st.camera.setTarget(0.0, 0.0, 0.0);
    st.camera.setClipPlanes(distance * 0.02, distance * 4.0);
}

function ensureShaders() {
    if (st.terrainShader != null) return true;
    local terrain = gfx.loadMeshShaderSpv("", "shaders/solar_planet_terrain.frag.spv");
    if (!terrain.ok) {
        st.status = "terrain shader: " + terrain.status.summary;
        print("pcg-solar-planet: " + st.status + "\n");
        return false;
    }
    local ocean = gfx.loadMeshShaderSpv("", "shaders/solar_planet_ocean.frag.spv");
    if (!ocean.ok) {
        st.status = "ocean shader: " + ocean.status.summary;
        print("pcg-solar-planet: " + st.status + "\n");
        return false;
    }
    st.terrainShader = terrain.value;
    st.oceanShader = ocean.value;
    return true;
}

function oceanRadius(cfg) {
    // Map normalized ocean level back into absolute radial height.
    return lerp(st.minHeight, st.maxHeight, cfg.oceanLevel01);
}

function rebuildPlanet() {
    if (!ensureShaders()) return;

    local topology = st.topologyNames[st.topologyIndex];
    local preset = st.presetNames[st.presetIndex];
    local cfg = heightConfigForPreset(preset, st.seed);

    local mesh = buildTopology(topology, st.resolution);
    applyHeightField(mesh, cfg);
    st.minHeight = mesh.minHeight;
    st.maxHeight = mesh.maxHeight;
    st.oceanLevel01 = cfg.oceanLevel01;
    st.showOcean = cfg.showOcean;
    st.vertexCount = mesh.dirs.len();
    st.triangleCount = mesh.idx.len() / 3;

    st.terrainMesh = gfx.newMeshFromArrays(mesh.pos, mesh.nrm, mesh.uv,
                                           mesh.dirs.len(), mesh.idx, mesh.idx.len());
    if (st.terrain == null) st.terrain = eve.Renderable3D();
    st.terrain.setMesh(st.terrainMesh);
    st.terrain.setShader(st.terrainShader);
    st.terrain.setTint(st.oceanLevel01, 1.0, 1.0, 1.0);
    st.terrain.setCastShadow(false);
    st.terrain.setReceiveShadow(false);
    st.terrain.setPosition(0.0, 0.0, 0.0);

    if (st.showOcean) {
        local oceanTopo = buildUvSphere(48, 24);
        local radius = oceanRadius(cfg);
        for (local i = 0; i < oceanTopo.dirs.len(); ++i) {
            local d = oceanTopo.dirs[i];
            oceanTopo.pos[i * 3] = d[0] * radius;
            oceanTopo.pos[i * 3 + 1] = d[1] * radius;
            oceanTopo.pos[i * 3 + 2] = d[2] * radius;
            oceanTopo.nrm[i * 3] = d[0];
            oceanTopo.nrm[i * 3 + 1] = d[1];
            oceanTopo.nrm[i * 3 + 2] = d[2];
        }
        st.oceanMesh = gfx.newMeshFromArrays(oceanTopo.pos, oceanTopo.nrm, oceanTopo.uv,
                                             oceanTopo.dirs.len(), oceanTopo.idx, oceanTopo.idx.len());
        if (st.ocean == null) st.ocean = eve.Renderable3D();
        st.ocean.setMesh(st.oceanMesh);
        st.ocean.setShader(st.oceanShader);
        st.ocean.setTint(1.0, 1.0, 1.0, 1.0);
        st.ocean.setCastShadow(false);
        st.ocean.setReceiveShadow(false);
        st.ocean.setVisible(true);
    } else if (st.ocean != null) {
        st.ocean.setVisible(false);
    }

    st.status = st.topologyLabels[st.topologyIndex] + " / " + st.presetLabels[st.presetIndex]
        + "  ·  " + st.triangleCount + " tris  ·  seed " + st.seed;
    if (st.uiReady) {
        ui.select("lab");
        ui.setText("status", st.status);
        ui.setText("sample", "SEED " + st.seed);
        ui.setText("topoLabel", st.topologyLabels[st.topologyIndex]);
        ui.setText("presetLabel", st.presetLabels[st.presetIndex]);
    }
    applyCamera();
    print("SOLAR_PLANET_READY topology=" + topology + " preset=" + preset
          + " tris=" + st.triangleCount + " verts=" + st.vertexCount + "\n");
}

function buildPanel() {
    ui.setTheme("dark");
    ui.setNavKeyboard(true);
    ui.beginBuild();
    ui.beginWindow("PCG / SOLAR PLANET", "root");
    ui.text("EPISODE 02 LAB", "eyebrow");
    ui.text("SEED " + st.seed, "sample");
    ui.text(st.topologyLabels[st.topologyIndex], "topoLabel");
    ui.text(st.presetLabels[st.presetIndex], "presetLabel");
    ui.slider("Resolution", st.resolution.tofloat(), 1.0, 5.0, "resolution");
    ui.beginRow("actions", 8.0);
    ui.button("Topology", "nextTopo");
    ui.button("Preset", "nextPreset");
    ui.end();
    ui.beginRow("actions2", 8.0);
    ui.button("New seed", "randomize");
    ui.button("Rebuild", "rebuild");
    ui.end();
    ui.button("Pause spin", "spin");
    ui.text("LMB orbit · wheel zoom · R seed · 1-4 topology · Q/E preset", "hint");
    ui.text("Generating…", "status");
    ui.end();
    ui.mountBuildAs("lab");
    ui.select("lab");
    ui.setHostOverlay(true);
    ui.setHostPos(930.0, 28.0, 320.0, 520.0);
    st.uiReady = true;
}

function nextTopology(delta) {
    local n = st.topologyNames.len();
    st.topologyIndex = (st.topologyIndex + delta + n) % n;
    rebuildPlanet();
}

function nextPreset(delta) {
    local n = st.presetNames.len();
    st.presetIndex = (st.presetIndex + delta + n) % n;
    rebuildPlanet();
}

function randomizeSeed() {
    st.seed = (st.seed * 1664525 + 1013904223) & 0x7fffffff;
    rebuildPlanet();
}

eve_init = function() {
    st = createState();
    gfx.setBackgroundColor(0.02, 0.025, 0.04, 1.0);
    gfx.setDirectionalLight(-0.55, 0.65, 0.40, 2.1, 1.85, 1.45);
    st.camera = eve.Camera3D();
    st.camera.setFov(FOV_DEGREES);
    st.camera.setAmbient(0.18, 0.20, 0.24);
    st.camera.setActive(true);
    buildPanel();
    rebuildPlanet();
};

eve_update = function(dt) {
    if (st.spinning) st.yaw += SPIN_SPEED * dt;

    local x = mouse.getX();
    local y = mouse.getY();
    local down = mouse.isDown(1);
    // Ignore drags that start over the side panel.
    local overUi = x > 900.0;
    if (down && !overUi) {
        if (st.dragging) {
            st.yaw -= (x - st.lastMouseX) * ORBIT_SPEED;
            st.pitch = clampf(st.pitch + (y - st.lastMouseY) * ORBIT_SPEED, PITCH_MIN, PITCH_MAX);
        }
        st.dragging = true;
    } else {
        st.dragging = false;
    }
    st.lastMouseX = x;
    st.lastMouseY = y;

    local wheel = mouse.getWheelY();
    if (wheel != 0.0 && !overUi) st.zoom = clampf(st.zoom - wheel * ZOOM_STEP, ZOOM_MIN, ZOOM_MAX);
    applyCamera();

    if (key_just_pressed("r") || key_just_pressed("R")) randomizeSeed();
    if (key_just_pressed("space") || key_just_pressed("Space")) {
        st.spinning = !st.spinning;
        if (st.uiReady) {
            ui.select("lab");
            ui.setText("spin", st.spinning ? "Pause spin" : "Resume spin");
        }
    }
    if (key_just_pressed("1")) { st.topologyIndex = 0; rebuildPlanet(); }
    if (key_just_pressed("2")) { st.topologyIndex = 1; rebuildPlanet(); }
    if (key_just_pressed("3")) { st.topologyIndex = 2; rebuildPlanet(); }
    if (key_just_pressed("4")) { st.topologyIndex = 3; rebuildPlanet(); }
    if (key_just_pressed("q") || key_just_pressed("Q")) nextPreset(-1);
    if (key_just_pressed("e") || key_just_pressed("E")) nextPreset(1);
    if (key_just_pressed("[") ) { st.resolution = max(1, st.resolution - 1); rebuildPlanet(); }
    if (key_just_pressed("]") ) { st.resolution = min(5, st.resolution + 1); rebuildPlanet(); }

    local clicked = ui.consumeClick();
    while (clicked != "") {
        if (clicked == "lab/randomize") randomizeSeed();
        else if (clicked == "lab/rebuild") rebuildPlanet();
        else if (clicked == "lab/nextTopo") nextTopology(1);
        else if (clicked == "lab/nextPreset") nextPreset(1);
        else if (clicked == "lab/spin") {
            st.spinning = !st.spinning;
            ui.select("lab");
            ui.setText("spin", st.spinning ? "Pause spin" : "Resume spin");
        }
        clicked = ui.consumeClick();
    }

    local changed = ui.consumeChange();
    while (changed != "") {
        if (changed == "lab/resolution") {
            ui.select("lab");
            st.resolution = max(1, min(5, ui.getValue("resolution").tointeger()));
            rebuildPlanet();
        }
        changed = ui.consumeChange();
    }
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    ui.beginFrameAndRender();
};
