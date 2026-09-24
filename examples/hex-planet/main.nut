// examples/hex-planet -- the hex terrain system wrapped onto a sphere, from orbit.
//
// This is the spherical counterpart of `examples/hex-terrain-3d`: the same module,
// the same `HexCellData` / `HexValues` / `HexFlags` records and the same terrain
// vertex encoding, but the grid is an icosahedral (Goldberg) topology reached through
// `hexmap`'s sphere bindings instead of the flat chunk grid.
//
// Everything geometric lives in C++ (`HexSphereTopology`, `HexSphereMap`,
// `HexSphereMesh`, `HexSphereGenerator`); this script owns the orbit camera, the
// input mapping, the two renderables and the HUD.

const DEG_TO_RAD = 0.017453292519943295;

// Topology levels: cells = 10 * 4^s + 2. Level 4 is 2562 cells and is the default;
// level 6 is 40962 and still generates and builds in well under a second.
const SUBDIVISION_MIN = 1;
const SUBDIVISION_MAX = 6;

const DEFAULT_SUBDIVISION = 4;
const DEFAULT_RADIUS      = 100.0;
const DEFAULT_SEED        = 20260918;
const DEFAULT_LAND        = 45;
const DEFAULT_WATER_LEVEL = 0;

// Framing: the globe has to fit the vertical field of view with room for the relief.
const FOV_DEGREES = 45.0;
const FIT_MARGIN  = 1.16;

const ORBIT_SPEED = 0.30;   // degrees of rotation per pixel dragged
const PITCH_MIN   = -78.0;  // never exactly +/-90: the up vector degenerates there
const PITCH_MAX   = 78.0;
const ZOOM_STEP   = 0.12;
const ZOOM_MIN    = 1.12;   // as a multiple of the framing distance
const ZOOM_MAX    = 4.50;
const SPIN_SPEED  = 4.0;    // degrees per second while auto-spin is on

persist planet = null;

function createState() {
    return {
        camera        = null,
        terrainShader = null,
        waterShader   = null,
        terrain       = null,
        water         = null,

        subdivision = DEFAULT_SUBDIVISION,
        radius      = DEFAULT_RADIUS,
        seed        = DEFAULT_SEED,
        landPercent = DEFAULT_LAND,
        waterLevel  = DEFAULT_WATER_LEVEL,

        yaw      = 35.0,
        pitch    = 38.0,
        zoom     = 1.0,
        spinning = false,

        dragging   = false,
        lastMouseX = 0,
        lastMouseY = 0,

        elapsed         = 0.0,
        frames          = 0,
        screenshotSaved = false,
        statusText      = "building...",
        uiBuilt         = false,
        triangles       = 0,
    };
}

// --- camera -----------------------------------------------------------------

function fitDistance(st) {
    return (st.radius * FIT_MARGIN) / sin(FOV_DEGREES * 0.5 * DEG_TO_RAD);
}

function applyCamera(st) {
    if (st.camera == null) return;
    local distance = fitDistance(st) * st.zoom;
    local yawRad = st.yaw * DEG_TO_RAD;
    local pitchRad = st.pitch * DEG_TO_RAD;
    local horizontal = cos(pitchRad) * distance;

    st.camera.setEye(horizontal * sin(yawRad), sin(pitchRad) * distance, horizontal * cos(yawRad));
    // The globe is centred on the origin, so the target is its centre and the relief
    // is symmetric around it.
    st.camera.setTarget(0.0, 0.0, 0.0);
    st.camera.setClipPlanes(distance * 0.02, distance * 3.0);
}

function updateCamera(st, dt) {
    if (st.spinning) st.yaw += SPIN_SPEED * dt;

    local x = mouse.getX();
    local y = mouse.getY();
    local down = mouse.isDown(1);
    if (down) {
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
    if (wheel != 0.0) st.zoom = clampf(st.zoom - wheel * ZOOM_STEP, ZOOM_MIN, ZOOM_MAX);

    applyCamera(st);
}

// --- shaders and renderables ------------------------------------------------

function createShaders(st) {
    if (st.terrainShader != null) return true;

    // The engine's built-in Mesh3D vertex stage already produces the varyings both
    // fragment stages consume, so only the fragment stages are shipped -- as SPIR-V,
    // which needs no runtime compiler.
    local terrain = gfx.loadMeshShaderSpv("", "shaders/hex_planet_terrain.frag.spv");
    if (!terrain.ok) {
        print("hex planet: terrain shader failed: " + terrain.status.summary + "\n");
        return false;
    }
    local water = gfx.loadMeshShaderSpv("", "shaders/hex_planet_water.frag.spv");
    if (!water.ok) {
        print("hex planet: water shader failed: " + water.status.summary + "\n");
        return false;
    }
    st.terrainShader = terrain.value;
    st.waterShader = water.value;
    return true;
}

function createRenderables(st) {
    if (!createShaders(st)) return;

    st.terrain = eve.Renderable3D();
    st.terrain.setShader(st.terrainShader);
    // The globe does not cast onto or receive from anything here: there is one
    // surface, and self-shadowing it only produces acne along every hex boundary.
    st.terrain.setCastShadow(false);
    st.terrain.setReceiveShadow(false);
    st.terrain.setVisible(false);

    st.water = eve.Renderable3D();
    st.water.setShader(st.waterShader);
    st.water.setCastShadow(false);
    st.water.setReceiveShadow(false);
    st.water.setVisible(false);
}

function bindMeshes(st) {
    if (st.terrain == null) return;

    local terrainMesh = hexmap.sphereTerrainMesh();
    if (terrainMesh != null) {
        st.terrain.setMesh(terrainMesh);
        st.terrain.setVisible(true);
    } else {
        st.terrain.setVisible(false);
    }

    local waterMesh = hexmap.sphereWaterMesh();
    if (waterMesh != null) {
        st.water.setMesh(waterMesh);
        st.water.setVisible(true);
    } else {
        // Nothing is flooded at this sea level; the renderable must not keep drawing
        // the previous ocean.
        st.water.setVisible(false);
    }
}

// --- world ------------------------------------------------------------------

function describePlanet(st) {
    return hexmap.sphereCellCount() + " cells, " + hexmap.spherePentagonCount() + " pentagons, " +
           hexmap.sphereSubdivision() + " subdivisions";
}

/** Reports what the generator actually produced, so a washed-out frame is measurable. */
function reportDistribution(st) {
    local counts = [0, 0, 0, 0, 0];
    local underwater = 0;
    local count = hexmap.sphereCellCount();
    for (local cell = 0; cell < count; cell++) {
        local terrain = hexmap.sphereTerrainType(cell);
        if (terrain >= 0 && terrain < 5) counts[terrain] = counts[terrain] + 1;
        if (hexmap.sphereIsUnderwater(cell)) underwater = underwater + 1;
    }
    print("hex planet: sand " + counts[0] + " grass " + counts[1] + " mud " + counts[2] + " stone " + counts[3] +
          " snow " + counts[4] + " | underwater " + underwater + " of " + count + "\n");
    print("hex planet: terrain mesh " + (hexmap.sphereTerrainMesh() != null) + ", water mesh " +
          (hexmap.sphereWaterMesh() != null) + "\n");
}

function buildPlanet(st) {
    local created = hexmap.newSphere(gfx, st.subdivision, st.radius, st.seed);
    if (!created.ok) {
        st.statusText = "sphere failed: " + created.status.summary;
        print("hex planet: " + st.statusText + "\n");
        return false;
    }

    local generated = hexmap.generateSphere(gfx, st.seed, st.landPercent, st.waterLevel);
    if (!generated.ok) {
        st.statusText = "generate failed: " + generated.status.summary;
        print("hex planet: " + st.statusText + "\n");
        return false;
    }

    st.triangles = 0;
    st.statusText = describePlanet(st);
    print("hex planet: " + describePlanet(st) + ", radius " + st.radius + ", seed " + st.seed + "\n");
    print("hex planet: " + st.landPercent + "% land, water line " + st.waterLevel + ", spacing " +
          hexmap.sphereCellSpacing() + "\n");
    reportDistribution(st);
    print("hex planet: LMB drag orbit | wheel zoom | space spin | R new seed | [ ] subdivision | - = land\n");
    return true;
}

function regenerate(st) {
    local generated = hexmap.generateSphere(gfx, st.seed, st.landPercent, st.waterLevel);
    if (!generated.ok) {
        st.statusText = "generate failed: " + generated.status.summary;
        return;
    }
    bindMeshes(st);
    st.statusText = describePlanet(st) + ", seed " + st.seed;
}

// --- input ------------------------------------------------------------------

function handleKeys(st) {
    if (key_just_pressed("r")) {
        st.seed += 1;
        regenerate(st);
        print("hex planet: seed " + st.seed + "\n");
    }
    if (key_just_pressed("space", "Space")) {
        st.spinning = !st.spinning;
        st.statusText = st.spinning ? "auto-spin on" : "auto-spin off";
    }
    if (key_just_pressed("escape", "Escape")) {
        st.spinning = false;
        st.yaw = 35.0;
        st.pitch = 38.0;
        st.zoom = 1.0;
        applyCamera(st);
    }
    if (key_just_pressed("F5")) {
        if (gfx.saveFramePng("hex-planet.png")) st.statusText = "saved hex-planet.png";
    }

    // Rebuilding a subdivision level replaces the whole topology, so it goes through
    // the full create path rather than the cheaper regenerate.
    local subdivision = st.subdivision;
    if (key_just_pressed("]")) subdivision = subdivision + 1;
    if (key_just_pressed("[")) subdivision = subdivision - 1;
    if (subdivision < SUBDIVISION_MIN) subdivision = SUBDIVISION_MIN;
    if (subdivision > SUBDIVISION_MAX) subdivision = SUBDIVISION_MAX;
    if (subdivision != st.subdivision) {
        st.subdivision = subdivision;
        buildPlanet(st);
        bindMeshes(st);
    }

    local land = st.landPercent;
    if (key_just_pressed("=")) land = land + 5;
    if (key_just_pressed("-")) land = land - 5;
    if (land < 5) land = 5;
    if (land > 95) land = 95;
    if (land != st.landPercent) {
        st.landPercent = land;
        regenerate(st);
        print("hex planet: " + land + "% land\n");
    }
}

// --- HUD --------------------------------------------------------------------

function buildHud(st) {
    // Widgets only exist inside a window, so the panel is built once and then updated
    // by label in `refreshHud`.
    ui.beginBuild();
    ui.beginWindow("HexPlanet", "root");
    ui.text("EVEngine Hex Planet", "title");
    ui.text("", "planet");
    ui.text("", "view");
    ui.separator("sep");
    ui.text("[LMB]orbit [wheel]zoom [space]spin [R]seed", "help1");
    ui.text("[[/]]subdivision [-/=]land [F5]shot [esc]reset", "help2");
    ui.end();
    ui.mountBuildAs("hud");
    ui.select("hud");
    ui.setHostOverlay(true);
    ui.setHostVisible(true);
    ui.setHostPos(12.0, 12.0, 0.0, 0.0);
    st.uiBuilt = true;
}

function refreshHud(st) {
    if (!st.uiBuilt) return;
    ui.select("hud");
    ui.setText("planet", "planet: " + st.statusText);
    ui.setText("view", "view: yaw " + st.yaw + "  pitch " + st.pitch + "  zoom " + st.zoom);
}

// --- entry points -----------------------------------------------------------

eve_init = function() {
    if (planet == null) planet = createState();
    local st = planet;

    st.camera = eve.Camera3D();
    st.camera.setFov(FOV_DEGREES);
    // The default view looks down on the northern cap (pitch 38) and the light sits well
    // off the view axis: a lower pitch foreshortens the ice into the very top edge, and a
    // light near the camera puts every visible pixel at N.L ~ 1, which flattens the globe
    // into a bright disc with no terminator to read the sphere's shape from.
    st.camera.setAmbient(0.17, 0.20, 0.27);
    st.camera.setActive(true);

    gfx.setBackgroundColor(0.012, 0.016, 0.030, 1.0);
    gfx.setDirectionalLight(-0.78, 0.42, 0.46, 1.45, 1.34, 1.16);
    gfx.setCloudShadows(0.22, 6.0, 0.0, 0.42, 0.40, 0.52, 0.62);

    createRenderables(st);
    buildPlanet(st);
    bindMeshes(st);
    buildHud(st);
    applyCamera(st);
};

eve_update = function(dt) {
    local st = planet;
    if (st == null) return;
    st.elapsed += dt;
    st.frames += 1;

    handleKeys(st);
    updateCamera(st, dt);
    refreshHud(st);

    // Readback is enabled by the first call, so retry until the frame lands. The
    // default pose is static, so the saved frame is reproducible run to run.
    if (!st.screenshotSaved && st.frames > 90 && gfx.saveFramePng("hex-planet.png"))
        st.screenshotSaved = true;
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    ui.beginFrameAndRender();
};
