// Top-down procedural map showcase — heightmap continents, biome splat,
// rivers/lakes, and scattered vegetation under a pan/zoom top-down camera.
// Inspired by common Unity "俯视角程序化地图" demos: noise terrain + biomes
// + decorations, viewed from above with free pan.

persist mapSeed = 20260917
persist mapCamera = null
persist mapCtrl = null
persist mapFocusX = 14.0
persist mapFocusZ = 14.0
persist mapHeight = 28.0
persist mapTilt = 0.82 // radians from horizontal; ~47° look-down for depth
persist mapYaw = 0.35
persist mapAutoPan = true
persist mapIdle = 0.0
persist mapTime = 0.0
persist mapFrame = 0
persist mapScreenshotSaved = false
persist mapStatus = "boot"
persist mapReady = false
persist mapKeep = []
persist mapEntities = []
persist mapDecor = []
persist mapPrototypes = {}
persist mapHeightmap = null
persist mapLayers = null
persist mapCellSize = 0.11
persist mapHeightScale = 6.8
persist mapGrid = 193
persist mapChunk = 64

function retain(value) {
    mapKeep.append(value);
    return value;
}

function clearEntities(list) {
    foreach (ent in list) {
        if (ent != null) {
            ent.setVisible(false);
            ent.destroy();
        }
    }
    list.clear();
}

function hash01(x, y, salt) {
    local n = (x * 374761393) + (y * 668265263) + (salt * 1274126177) + mapSeed;
    n = (n ^ (n >> 13)) * 1274126177;
    n = n ^ (n >> 16);
    if (n < 0) n = -n;
    return (n % 10000).tofloat() / 10000.0;
}

function makePrototype(kind, seedOffset) {
    local paramsResult = procgen.newParams();
    if (!paramsResult.ok) return null;
    local p = retain(paramsResult.value);
    p.setSeed(mapSeed + seedOffset);
    if (kind == "tree") {
        p.setString("style", "lowpoly");
        p.setString("branchAlgorithm", "weberPenn");
        p.setString("leafMode", "clusters");
        p.setFloat("leafDensity", 0.55);
        p.setFloat("height", 3.4);
        p.setFloat("crownRadius", 1.15);
        p.setInt("branchLevels", 2);
        p.setInt("branchCount", 5);
        p.setFloat("clusterSize", 0.28);
        p.setFloat("clusterSeparation", 0.55);
        p.setFloat("clusterLeafScale", 0.8);
        p.setInt("clusterPlanes", 8);
        p.setInt("clusterLeaves", 18);
        p.setInt("clusterLimit", 48);
        local meshResult = procgen.generateMesh("mesh.tree", p, gfx);
        if (!meshResult.ok) return null;
        return retain(meshResult.value);
    }
    if (kind == "bush") {
        p.setString("style", "mound");
        p.setString("leafMode", "mixed");
        p.setFloat("height", 0.85);
        p.setFloat("width", 1.35);
        p.setInt("blobs", 8);
        p.setFloat("leafDensity", 0.7);
        p.setFloat("lobeScale", 0.55);
        p.setFloat("irregularity", 0.55);
        p.setInt("rings", 4);
        p.setInt("radialSegments", 8);
        p.setFloat("leafSize", 0.18);
        p.setInt("twigs", 3);
        p.setFloat("twigLength", 0.28);
        local meshResult = procgen.generateMesh("mesh.bush", p, gfx);
        if (!meshResult.ok) return null;
        return retain(meshResult.value);
    }
    // rock
    p.setInt("subdivisions", 1);
    p.setString("baseShape", "boulder");
    p.setFloat("variation", 0.35);
    p.setFloat("radius", 0.42);
    p.setFloat("flattening", 0.28);
    p.setFloat("angularity", 0.32);
    p.setFloat("erosion", 0.12);
    p.setFloat("scale", 2.0);
    p.setInt("octaves", 3);
    local meshResult = procgen.generateMesh("mesh.rock", p, gfx);
    if (!meshResult.ok) return null;
    return retain(meshResult.value);
}

function ensurePrototypes() {
    if (!("treeA" in mapPrototypes)) mapPrototypes.treeA <- makePrototype("tree", 11);
    if (!("treeB" in mapPrototypes)) mapPrototypes.treeB <- makePrototype("tree", 29);
    if (!("bush" in mapPrototypes)) mapPrototypes.bush <- makePrototype("bush", 47);
    if (!("rock" in mapPrototypes)) mapPrototypes.rock <- makePrototype("rock", 71);
}

function placeDecor(kind, mesh, wx, wy, wz, scale, yawDeg, tintR, tintG, tintB) {
    if (mesh == null) return;
    local ent = retain(eve.Renderable3D());
    ent.setMesh(mesh);
    ent.setPosition(wx, wy, wz);
    ent.setScale(scale, scale, scale);
    ent.setYaw(yawDeg);
    ent.setTint(tintR, tintG, tintB, 1.0);
    ent.setRoughness(0.88);
    ent.setCastShadow(true);
    mapDecor.append(ent);
}

function scatterDecorations(heightmap, layers) {
    ensurePrototypes();
    local step = 5;
    local half = (mapGrid - 1) * mapCellSize * 0.5;
    for (local gy = 4; gy < mapGrid - 4; gy += step) {
        for (local gx = 4; gx < mapGrid - 4; gx += step) {
            local biome = layers.getBiomeName(gx, gy);
            if (biome == "ocean" || biome == "lake" || biome == "river" || biome == "beach")
                continue;
            local jitter = hash01(gx, gy, 3);
            local chance = hash01(gx, gy, 7);
            local hNorm = heightmap.sampleBilinear(gx.tofloat(), gy.tofloat());
            local wy = hNorm * mapHeightScale;
            local wx = gx * mapCellSize;
            local wz = gy * mapCellSize;
            local yaw = hash01(gx, gy, 13) * 6.2831853;

            if ((biome == "forest" || biome == "rainforest" || biome == "taiga") && chance < 0.72) {
                local mesh = (jitter < 0.5) ? mapPrototypes.treeA : mapPrototypes.treeB;
                local scale = 0.55 + jitter * 0.55;
                if (biome == "rainforest") scale *= 1.15;
                if (biome == "taiga") scale *= 0.85;
                local tintG = biome == "taiga" ? 0.42 : 0.55;
                placeDecor("tree", mesh, wx, wy, wz, scale, yaw, 0.28, tintG, 0.22);
            } else if ((biome == "grassland" || biome == "wetland") && chance < 0.35) {
                local scale = 0.7 + jitter * 0.5;
                placeDecor("bush", mapPrototypes.bush, wx, wy, wz, scale, yaw, 0.34, 0.58, 0.24);
            } else if ((biome == "desert" || biome == "alpine" || biome == "tundra") && chance < 0.28) {
                local scale = 0.55 + jitter * 0.7;
                placeDecor("rock", mapPrototypes.rock, wx, wy, wz, scale, yaw, 0.55, 0.52, 0.48);
            } else if (biome == "forest" && chance < 0.88 && jitter > 0.7) {
                placeDecor("bush", mapPrototypes.bush, wx, wy, wz, 0.6 + jitter * 0.4, yaw,
                           0.30, 0.52, 0.22);
            }
        }
    }
    mapFocusX = half;
    mapFocusZ = half;
}

function rebuildWorld() {
    mapReady = false;
    mapStatus = "generating seed " + mapSeed;
    clearEntities(mapEntities);
    clearEntities(mapDecor);
    // Drop previous retain list except prototypes (rebuilt cheaply if missing).
    mapKeep = [];
    mapPrototypes = {};

    local terrainShader = retain(procgen.createTerrainMaterialShader(gfx));
    local waterShader = retain(procgen.createTerrainWaterShader(gfx));

    local paramsResult = procgen.newParams();
    if (!paramsResult.ok) {
        mapStatus = "params failed: " + paramsResult.status.summary;
        return;
    }
    local params = retain(paramsResult.value);
    params.setSize(mapGrid, mapGrid);
    params.setSeed(mapSeed);
    params.setFloat("frequency", 1.0 / 96.0);
    params.setInt("octaves", 5);
    params.setFloat("gain", 0.42);
    params.setFloat("ridge", 0.40);
    params.setFloat("warp", 0.20);
    params.setFloat("exponent", 1.20);
    params.setFloat("continent", 1.0);
    params.setFloat("island", 0.0);
    params.setFloat("coast", 0.32);

    local heightmapResult = procgen.generateHeightmap(params);
    if (!heightmapResult.ok) {
        mapStatus = "heightmap failed: " + heightmapResult.status.summary;
        return;
    }
    local heightmap = retain(heightmapResult.value);
    procgen.erodeTerrainThermal(heightmap, 14, 0.010, 0.28);
    procgen.erodeTerrainHydraulic(heightmap, 14, 0.007, 0.11, 1.4, 0.08, 0.11);
    procgen.erodeTerrainFluvialScaled(heightmap, 16, 0.008, 0.085, 0.08, 8.0, 0.050, 1.5);
    local layers = retain(procgen.analyzeTerrainScaled(heightmap, 380.0, 0.22, 0.42, 1.5));
    mapHeightmap = heightmap;
    mapLayers = layers;

    local chunks = (mapGrid - 1) / mapChunk;
    for (local cy = 0; cy < chunks; ++cy) {
        for (local cx = 0; cx < chunks; ++cx) {
            local originX = cx * mapChunk;
            local originY = cy * mapChunk;
            local chunk = retain(procgen.buildTerrainChunk(
                heightmap, layers, originX, originY, mapChunk, mapChunk, 0,
                mapCellSize, mapHeightScale, 0.0));
            local mesh = retain(procgen.generateTerrainChunkMesh(chunk, gfx));
            local splat = retain(procgen.generateTerrainSplatMap(chunk));
            local splatTex = retain(gfx.newTexture(splat, false, false));
            local entity = retain(eve.Renderable3D());
            entity.setMesh(mesh);
            entity.setTexture(splatTex);
            entity.setShader(terrainShader);
            entity.setTint(1.0, 1.0, 1.0, 1.0);
            entity.setMetallic(0.0);
            entity.setRoughness(0.9);
            entity.setPosition(originX * mapCellSize, 0.0, originY * mapCellSize);
            mapEntities.append(entity);

            local riverMesh = procgen.generateTerrainRiverMeshAdvanced(
                heightmap, layers, gfx, originX, originY, mapChunk, mapChunk,
                mapCellSize, mapHeightScale, 0.025, 0.15, 0.045, 0.0, 1.6);
            if (riverMesh != null) {
                retain(riverMesh);
                local river = retain(eve.Renderable3D());
                river.setMesh(riverMesh);
                river.setShader(waterShader);
                river.setTint(0.018, 0.16, 0.22, 1.0);
                river.setMetallic(0.0);
                river.setRoughness(0.34);
                river.setPosition(originX * mapCellSize, 0.0, originY * mapCellSize);
                mapEntities.append(river);
            }
            local lakeMesh = procgen.generateTerrainLakeMesh(
                heightmap, layers, gfx, originX, originY, mapChunk, mapChunk,
                mapCellSize, mapHeightScale, 0.004, 0.040);
            if (lakeMesh != null) {
                retain(lakeMesh);
                local lake = retain(eve.Renderable3D());
                lake.setMesh(lakeMesh);
                lake.setShader(waterShader);
                lake.setTint(0.025, 0.22, 0.38, 1.0);
                lake.setMetallic(0.08);
                lake.setRoughness(0.12);
                lake.setPosition(originX * mapCellSize, 0.0, originY * mapCellSize);
                mapEntities.append(lake);
            }
        }
    }

    scatterDecorations(heightmap, layers);
    mapReady = true;
    mapStatus = "seed " + mapSeed + "  |  WASD pan  wheel zoom  R reseed  Space auto-pan";
    print("TOPDOWN_PROCMAP_READY seed=" + mapSeed + " decor=" + mapDecor.len() +
          " chunks=" + (chunks * chunks) + "\n");
}

function applyCamera() {
    if (mapCamera == null) return;
    local lookY = 1.2;
    // Slightly oblique top-down: high above, looking toward focus with a soft yaw.
    local dist = mapHeight;
    local eyeX = mapFocusX + dist * cos(mapTilt) * sin(mapYaw);
    local eyeY = mapFocusZ * 0.0 + dist * sin(mapTilt) + 2.0;
    local eyeZ = mapFocusZ + dist * cos(mapTilt) * cos(mapYaw);
    mapCamera.setEye(eyeX, eyeY, eyeZ);
    mapCamera.setTarget(mapFocusX, lookY, mapFocusZ);
    mapCamera.setUp(0.0, 1.0, 0.0);
}

function clampFocus() {
    local extent = (mapGrid - 1) * mapCellSize;
    mapFocusX = clampf(mapFocusX, 1.0, extent - 1.0);
    mapFocusZ = clampf(mapFocusZ, 1.0, extent - 1.0);
}

eve_init = function() {
    gfx.setBackgroundColor(0.05, 0.08, 0.12, 1.0);
    gfx.setDirectionalLight(-0.48, 0.86, 0.28, 1.70, 1.52, 1.25);

    mapCamera = eve.Camera3D();
    mapCamera.setFov(42.0);
    mapCamera.setAmbient(0.28, 0.33, 0.40);
    mapCamera.setClipPlanes(0.2, 220.0);
    mapCamera.setActive(true);

    // CameraController stays available as a secondary topdown rig reference;
    // the showcase drives an oblique "map camera" directly for cinematic pan.
    mapCtrl = eve.CameraController();
    mapCtrl.setCamera(mapCamera);
    mapCtrl.setMode("topdown");
    mapCtrl.setRadius(mapHeight);
    mapCtrl.setSmooth(8.0);

    rebuildWorld();
    applyCamera();
};

eve_update = function(dt) {
    mapTime += dt;
    mapFrame += 1;
    gfx.setCloudShadows(0.16, 6.5, mapTime * 0.15, 0.32, 0.36, 0.52, 0.55);

    local moved = false;
    local speed = mapHeight * 0.55;
    if (has_module("keyboard")) {
        if (keyboard.isDown("W") || keyboard.isDown("Up")) {
            mapFocusZ -= speed * dt; moved = true;
        }
        if (keyboard.isDown("S") || keyboard.isDown("Down")) {
            mapFocusZ += speed * dt; moved = true;
        }
        if (keyboard.isDown("A") || keyboard.isDown("Left")) {
            mapFocusX -= speed * dt; moved = true;
        }
        if (keyboard.isDown("D") || keyboard.isDown("Right")) {
            mapFocusX += speed * dt; moved = true;
        }
        if (keyboard.isDown("Q")) { mapYaw -= dt * 0.55; moved = true; }
        if (keyboard.isDown("E")) { mapYaw += dt * 0.55; moved = true; }
    }
    if (has_module("mouse")) {
        local wheel = mouse.getWheelY();
        if (wheel != 0) {
            mapHeight = clampf(mapHeight - wheel * 1.8, 10.0, 55.0);
            moved = true;
        }
    }
    if (key_just_pressed("r") || key_just_pressed("R")) {
        mapSeed = procgen.randomSeed();
        rebuildWorld();
        moved = true;
    }
    if (key_just_pressed("space") || key_just_pressed("Space")) {
        mapAutoPan = !mapAutoPan;
        mapStatus = mapAutoPan
            ? ("seed " + mapSeed + "  |  auto-pan ON")
            : ("seed " + mapSeed + "  |  auto-pan OFF");
    }

    if (moved) {
        mapIdle = 0.0;
        mapAutoPan = false;
    } else {
        mapIdle += dt;
    }
    if (mapAutoPan && mapReady) {
        mapFocusX += cos(mapTime * 0.12) * dt * 1.6;
        mapFocusZ += sin(mapTime * 0.09) * dt * 1.3;
        mapYaw += dt * 0.035;
    }

    clampFocus();
    applyCamera();
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();

    gfx.drawSolidRect(16.0, 16.0, 420.0, 48.0, 0.06, 0.09, 0.13, 0.78);

    if (!mapScreenshotSaved && mapReady && mapFrame > 24 && mapTime > 1.8) {
        if (gfx.saveFramePng("topdown-procmap.png")) {
            mapScreenshotSaved = true;
            print("TOPDOWN_PROCMAP_SCREENSHOT topdown-procmap.png\n");
        }
    }
};
