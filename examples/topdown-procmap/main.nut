// Top-down procedural map showcase — matches the Bilibili demo's structure:
// TerrainChunk heightfield + splat blend, separate turquoise water, and
// DetailsRoot-style scatter (trees / rocks / grass) under an ARPG oblique camera.
// Reference: 俯视角程序化地图效果展示 (TerrainRoot / DetailsRoot / InstRoot / Ocean).

persist mapSeed = 20260917
persist mapCamera = null
persist mapFocusX = 12.0
persist mapFocusZ = 12.0
persist mapHeight = 14.0
persist mapTilt = 1.0 // ~57° from horizontal — high oblique top-down like the video
persist mapYaw = 0.55
persist mapAutoPan = true
persist mapTime = 0.0
persist mapFrame = 0
persist mapScreenshotSaved = false
persist mapReady = false
persist mapKeep = []
persist mapEntities = []
persist mapDecor = []
persist mapPrototypes = {}
persist mapGrass = null
persist mapHeightmap = null
persist mapLayers = null
persist mapCellSize = 0.12
persist mapHeightScale = 7.2
persist mapGrid = 193
persist mapChunk = 64
persist mapSeaLevel = 0.34

function retain(value) {
    mapKeep.append(value);
    return value;
}

function clearEntities(list) {
    foreach (ent in list) {
        if (ent != null) ent.setVisible(false);
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

function sampleSlope(heightmap, gx, gy) {
    local h = heightmap.sampleBilinear(gx.tofloat(), gy.tofloat());
    local hx = heightmap.sampleBilinear((gx + 1).tofloat(), gy.tofloat());
    local hy = heightmap.sampleBilinear(gx.tofloat(), (gy + 1).tofloat());
    local dx = (hx - h) * mapHeightScale / mapCellSize;
    local dy = (hy - h) * mapHeightScale / mapCellSize;
    return sqrt(dx * dx + dy * dy);
}

function makeSurface(recipe, seedOffset, size) {
    local paramsResult = procgen.newParams();
    if (!paramsResult.ok) return { albedo = null, normal = null };
    local p = retain(paramsResult.value);
    p.setSeed(mapSeed + seedOffset);
    p.setSize(size, size);
    p.setFloat("scale", 4.0);
    p.setInt("octaves", 5);
    p.setInt("seamless", 1);
    p.setInt("colors", 7);
    local albedo = null;
    local normal = null;
    local tr = procgen.generateTexture(recipe, p, gfx);
    if (tr.ok) albedo = retain(tr.value);
    local nr = procgen.generateNormalImage(recipe, p);
    if (nr.ok) normal = retain(gfx.newTexture(nr.value, true, true));
    return { albedo = albedo, normal = normal };
}

function makeDecorMaterial(albedo, normal, masked, cutoff) {
    local mat = retain(gfx.newMaterial());
    if (albedo != null) mat.setAlbedoTexture(albedo);
    if (normal != null) mat.setNormalTexture(normal);
    mat.setTint(1.0, 1.0, 1.0, 1.0);
    mat.setMetallic(0.02);
    mat.setRoughness(0.86);
    if (masked) {
        mat.setDoubleSided(true);
        mat.setSurfaceMode("masked");
        mat.setAlphaCutoff(cutoff);
        mat.setAlphaTechnique("coverage");
    }
    return mat;
}

function makePrototype(kind, seedOffset) {
    local paramsResult = procgen.newParams();
    if (!paramsResult.ok) return null;
    local p = retain(paramsResult.value);
    p.setSeed(mapSeed + seedOffset);
    local mesh = null;
    local material = null;
    if (kind == "tree") {
        p.setString("style", "realistic");
        p.setString("branchAlgorithm", "weberPenn");
        p.setString("leafMode", "clusters");
        p.setFloat("leafDensity", 0.86);
        p.setFloat("height", 4.8);
        p.setFloat("crownRadius", 1.75);
        p.setInt("branchLevels", 3);
        p.setInt("branchCount", 8);
        p.setFloat("clusterSize", 0.26);
        p.setFloat("clusterSeparation", 0.40);
        p.setFloat("clusterLeafScale", 0.78);
        p.setInt("clusterPlanes", 13);
        p.setInt("clusterLeaves", 32);
        p.setInt("clusterLimit", 120);
        local meshResult = procgen.generateMesh("mesh.tree", p, gfx);
        if (!meshResult.ok) return null;
        mesh = retain(meshResult.value);
        local surf = makeSurface("tex.tree_atlas", seedOffset + 100, 512);
        material = makeDecorMaterial(surf.albedo, null, true, 0.28);
    } else if (kind == "bush") {
        p.setString("style", "mound");
        p.setString("leafMode", "mixed");
        p.setFloat("height", 1.25);
        p.setFloat("width", 1.75);
        p.setInt("blobs", 14);
        p.setFloat("leafDensity", 0.88);
        p.setFloat("lobeScale", 0.62);
        p.setFloat("irregularity", 0.7);
        p.setInt("rings", 5);
        p.setInt("radialSegments", 10);
        p.setFloat("leafSize", 0.18);
        p.setInt("twigs", 6);
        p.setFloat("twigLength", 0.34);
        local meshResult = procgen.generateMesh("mesh.bush", p, gfx);
        if (!meshResult.ok) return null;
        mesh = retain(meshResult.value);
        local surf = makeSurface("tex.foliage", seedOffset + 120, 256);
        material = makeDecorMaterial(surf.albedo, surf.normal, true, 0.32);
    } else {
        // cliff / stone
        p.setInt("subdivisions", kind == "cliff" ? 3 : 2);
        p.setString("baseShape", kind == "cliff" ? "cliff" : "boulder");
        p.setFloat("variation", 0.55);
        p.setFloat("radius", kind == "cliff" ? 1.05 : 0.55);
        local meshResult = procgen.generateMesh("mesh.rock", p, gfx);
        if (!meshResult.ok) return null;
        mesh = retain(meshResult.value);
        local recipe = kind == "cliff" ? "tex.moss" : "tex.rock";
        local surf = makeSurface(recipe, seedOffset + 140, 256);
        material = makeDecorMaterial(surf.albedo, surf.normal, false, 0.5);
    }
    return { mesh = mesh, material = material };
}

function ensurePrototypes() {
    if (!("treeA" in mapPrototypes)) mapPrototypes.treeA <- makePrototype("tree", 11);
    if (!("treeB" in mapPrototypes)) mapPrototypes.treeB <- makePrototype("tree", 29);
    if (!("bush" in mapPrototypes)) mapPrototypes.bush <- makePrototype("bush", 47);
    if (!("stone" in mapPrototypes)) mapPrototypes.stone <- makePrototype("stone", 71);
    if (!("cliff" in mapPrototypes)) mapPrototypes.cliff <- makePrototype("cliff", 97);
}

function placeDecor(proto, wx, wy, wz, sx, sy, sz, yaw) {
    if (proto == null || proto.mesh == null) return;
    local ent = retain(eve.Renderable3D());
    ent.setMesh(proto.mesh);
    ent.setPosition(wx, wy, wz);
    ent.setScale(sx, sy, sz);
    ent.setYaw(yaw);
    if (proto.material != null) ent.setMaterial(proto.material);
    ent.setCastShadow(true);
    ent.setReceiveShadow(true);
    mapDecor.append(ent);
}

function buildGrassField(heightmap, layers) {
    local pointsResult = procgen.newPointSet();
    if (!pointsResult.ok) return;
    local points = retain(pointsResult.value);
    local step = 2;
    local count = 0;
    for (local gy = 6; gy < mapGrid - 6; gy += step) {
        for (local gx = 6; gx < mapGrid - 6; gx += step) {
            local biome = layers.getBiomeName(gx, gy);
            if (biome != "grassland" && biome != "forest" && biome != "rainforest" &&
                biome != "wetland" && biome != "taiga")
                continue;
            if (sampleSlope(heightmap, gx, gy) > 1.8) continue;
            local chance = hash01(gx, gy, 19);
            local dens = biome == "forest" || biome == "rainforest" ? 0.92 : 0.68;
            if (chance > dens) continue;
            local jitterX = (hash01(gx, gy, 23) - 0.5) * mapCellSize * 1.6;
            local jitterZ = (hash01(gx, gy, 29) - 0.5) * mapCellSize * 1.6;
            local hNorm = heightmap.sampleBilinear(gx.tofloat(), gy.tofloat());
            local wx = gx * mapCellSize + jitterX;
            local wy = hNorm * mapHeightScale;
            local wz = gy * mapCellSize + jitterZ;
            local i = points.add(wx, wy, wz);
            local bladeH = 0.35 + hash01(gx, gy, 31) * 0.55;
            local bladeW = 0.45 + hash01(gx, gy, 37) * 0.35;
            points.setScale(i, bladeW, bladeH, bladeW);
            // Mix grass green with occasional flower tint (Flower01-03 vibe).
            local flower = hash01(gx, gy, 41);
            if (flower > 0.92) points.setColor(i, 0.85, 0.25, 0.35, 1.0); // red
            else if (flower > 0.84) points.setColor(i, 0.35, 0.55, 0.95, 1.0); // blue
            else if (flower > 0.76) points.setColor(i, 0.95, 0.92, 0.75, 1.0); // white
            else points.setColor(i, 0.38 + flower * 0.15, 0.72 + flower * 0.12, 0.22, 1.0);
            points.setStringAttribute(i, "asset", "pcg:detail");
            count += 1;
            if (count >= 2800) break;
        }
        if (count >= 2800) break;
    }
    if (count == 0) return;
    points.assignPointIds(mapSeed.tostring());

    local imageApi = eve.Image();
    local albedo = retain(imageApi.newEmptyImageData(32, 32, "RGBA8"));
    local normal = retain(imageApi.newEmptyImageData(32, 32, "RGBA8"));
    local mask = retain(imageApi.newEmptyImageData(32, 32, "RGBA8"));
    for (local y = 0; y < 32; ++y) {
        for (local x = 0; x < 32; ++x) {
            local nx = (x - 15.5) / 15.5;
            local ny = y / 31.0;
            local blade = abs(nx) < (0.08 + 0.34 * ny);
            albedo.setPixel(x, y, 0.32 + 0.22 * ny, 0.70 + 0.18 * ny, 0.16, blade ? 1.0 : 0.0);
            normal.setPixel(x, y, 0.5, 0.5, 1.0, 1.0);
            mask.setPixel(x, y, 0.05, 0.82, 0.22, 0.72);
        }
    }
    mapGrass = retain(gfx.newGrassField());
    local foliage = eve.GrassFoliageSettings();
    foliage.baseR = 0.92; foliage.baseG = 1.0; foliage.baseB = 0.82;
    foliage.alphaCutoff = 0.08;
    foliage.normalStrength = 1.0;
    foliage.renderDistance = 55.0;
    foliage.fadeRange = 14.0;
    foliage.hardRenderDistance = 80.0;
    foliage.density = 1.0;
    foliage.snowMinimumHeight = 100.0;
    foliage.snowFadeDistance = 20.0;
    foliage.snowProgress = 0.0;
    local baked = eve.bakeTerrainGrassFoliage(
        mapGrass, points, "pcg:detail", 0.7, 1.15, albedo, normal, mask, foliage);
    if (!baked.ok) {
        print("TOPDOWN_PROCMAP_GRASS_SKIP " + baked.status.summary + "\n");
        mapGrass = null;
        return;
    }
    print("TOPDOWN_PROCMAP_GRASS points=" + count + "\n");
}

function scatterDecorations(heightmap, layers) {
    ensurePrototypes();
    local step = 3;
    local half = (mapGrid - 1) * mapCellSize * 0.5;
    for (local gy = 4; gy < mapGrid - 4; gy += step) {
        for (local gx = 4; gx < mapGrid - 4; gx += step) {
            local biome = layers.getBiomeName(gx, gy);
            if (biome == "ocean" || biome == "lake" || biome == "river") continue;
            local slope = sampleSlope(heightmap, gx, gy);
            local jitter = hash01(gx, gy, 3);
            local chance = hash01(gx, gy, 7);
            local hNorm = heightmap.sampleBilinear(gx.tofloat(), gy.tofloat());
            local wy = hNorm * mapHeightScale;
            local wx = gx * mapCellSize;
            local wz = gy * mapCellSize;
            local yaw = hash01(gx, gy, 13) * 6.2831853;

            // Cliffs hug steep slopes / coasts — like Cliff01 in the video hierarchy.
            if (slope > 2.2 && chance < 0.62 && biome != "beach") {
                local s = 0.85 + jitter * 0.9;
                placeDecor(mapPrototypes.cliff, wx, wy - 0.15, wz,
                           s * (0.8 + jitter * 0.5), s * (0.9 + jitter * 0.6), s, yaw);
                continue;
            }
            if (biome == "beach" && chance < 0.28) {
                local s = 0.4 + jitter * 0.5;
                placeDecor(mapPrototypes.stone, wx, wy, wz, s, s * 0.7, s, yaw);
                continue;
            }
            if ((biome == "forest" || biome == "rainforest" || biome == "taiga") && chance < 0.82) {
                local proto = (jitter < 0.5) ? mapPrototypes.treeA : mapPrototypes.treeB;
                local scale = 0.55 + jitter * 0.65;
                if (biome == "rainforest") scale *= 1.2;
                if (biome == "taiga") scale *= 0.85;
                placeDecor(proto, wx, wy, wz, scale, scale, scale, yaw);
            } else if ((biome == "grassland" || biome == "wetland") && chance < 0.36) {
                local scale = 0.65 + jitter * 0.55;
                placeDecor(mapPrototypes.bush, wx, wy, wz, scale, scale, scale, yaw);
            } else if ((biome == "desert" || biome == "alpine" || biome == "tundra") && chance < 0.38) {
                local scale = 0.5 + jitter * 0.75;
                placeDecor(mapPrototypes.stone, wx, wy, wz, scale, scale * 0.75, scale, yaw);
            } else if (biome == "forest" && chance < 0.95 && jitter > 0.62) {
                placeDecor(mapPrototypes.bush, wx, wy, wz, 0.55 + jitter * 0.4, 0.55 + jitter * 0.4,
                           0.55 + jitter * 0.4, yaw);
            }
        }
    }
    mapFocusX = half;
    mapFocusZ = half * 0.92;
    // Prefer a land/vegetation pocket for the opening shot (high seaLevel leaves
    // the geometric center as empty beach in many seeds).
    local bestScore = -1.0;
    for (local gy = 8; gy < mapGrid - 8; gy += 6) {
        for (local gx = 8; gx < mapGrid - 8; gx += 6) {
            local biome = layers.getBiomeName(gx, gy);
            local score = 0.0;
            if (biome == "forest" || biome == "rainforest") score = 3.0;
            else if (biome == "grassland" || biome == "taiga" || biome == "wetland") score = 2.0;
            else if (biome == "alpine" || biome == "tundra") score = 1.0;
            else continue;
            score += sampleSlope(heightmap, gx, gy) * 0.15;
            if (score > bestScore) {
                bestScore = score;
                mapFocusX = gx * mapCellSize;
                mapFocusZ = gy * mapCellSize;
            }
        }
    }
}

function rebuildWorld() {
    mapReady = false;
    clearEntities(mapEntities);
    clearEntities(mapDecor);
    mapGrass = null;
    mapPrototypes = {};

    local terrainShader = retain(procgen.createTerrainMaterialShader(gfx));
    local waterShader = retain(procgen.createTerrainWaterShader(gfx));

    local paramsResult = procgen.newParams();
    if (!paramsResult.ok) return;
    local params = retain(paramsResult.value);
    params.setSize(mapGrid, mapGrid);
    params.setSeed(mapSeed);
    // Coastal / cove-friendly continent: lower frequency, stronger coast band.
    params.setFloat("frequency", 1.0 / 88.0);
    params.setInt("octaves", 5);
    params.setFloat("gain", 0.44);
    params.setFloat("ridge", 0.38);
    params.setFloat("warp", 0.22);
    params.setFloat("exponent", 1.15);
    params.setFloat("continent", 1.0);
    params.setFloat("island", 0.35);
    params.setFloat("coast", 0.42);

    local heightmapResult = procgen.generateHeightmap(params);
    if (!heightmapResult.ok) return;
    local heightmap = retain(heightmapResult.value);
    procgen.erodeTerrainThermal(heightmap, 12, 0.011, 0.30);
    procgen.erodeTerrainHydraulic(heightmap, 12, 0.007, 0.12, 1.4, 0.08, 0.11);
    procgen.erodeTerrainFluvialScaled(heightmap, 14, 0.008, 0.09, 0.08, 7.0, 0.045, 1.5);
    // Higher sea level → turquoise coves and sandy beaches like the screenshots.
    local layers = retain(procgen.analyzeTerrainScaled(
        heightmap, 360.0, mapSeaLevel, 0.38, 1.5));
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
            entity.setTint(1.05, 1.02, 0.95, 1.0);
            entity.setMetallic(0.0);
            entity.setRoughness(0.88);
            entity.setPosition(originX * mapCellSize, 0.0, originY * mapCellSize);
            mapEntities.append(entity);

            local riverMesh = procgen.generateTerrainRiverMeshAdvanced(
                heightmap, layers, gfx, originX, originY, mapChunk, mapChunk,
                mapCellSize, mapHeightScale, 0.025, 0.14, 0.04, 0.0, 1.6);
            if (riverMesh != null) {
                retain(riverMesh);
                local river = retain(eve.Renderable3D());
                river.setMesh(riverMesh);
                river.setShader(waterShader);
                // Bright turquoise shallow water like the video Ocean.
                river.setTint(0.04, 0.62, 0.72, 1.0);
                river.setMetallic(0.05);
                river.setRoughness(0.22);
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
                lake.setTint(0.05, 0.68, 0.78, 1.0);
                lake.setMetallic(0.08);
                lake.setRoughness(0.14);
                lake.setPosition(originX * mapCellSize, 0.0, originY * mapCellSize);
                mapEntities.append(lake);
            }
        }
    }

    scatterDecorations(heightmap, layers);
    buildGrassField(heightmap, layers);
    mapReady = true;
    print("TOPDOWN_PROCMAP_READY seed=" + mapSeed +
          " decor=" + mapDecor.len() +
          " chunks=" + (chunks * chunks) +
          " sea=" + mapSeaLevel + "\n");
}

function applyCamera() {
    if (mapCamera == null) return;
    local dist = mapHeight;
    local eyeX = mapFocusX + dist * cos(mapTilt) * sin(mapYaw);
    local eyeY = dist * sin(mapTilt) + 1.5;
    local eyeZ = mapFocusZ + dist * cos(mapTilt) * cos(mapYaw);
    mapCamera.setEye(eyeX, eyeY, eyeZ);
    mapCamera.setTarget(mapFocusX, 0.9, mapFocusZ);
    mapCamera.setUp(0.0, 1.0, 0.0);
}

function clampFocus() {
    local extent = (mapGrid - 1) * mapCellSize;
    mapFocusX = clampf(mapFocusX, 1.5, extent - 1.5);
    mapFocusZ = clampf(mapFocusZ, 1.5, extent - 1.5);
}

eve_init = function() {
    // Warm daylight sky like the screenshots (not cold gray).
    gfx.setBackgroundColor(0.42, 0.58, 0.72, 1.0);
    gfx.setDirectionalLight(-0.62, 0.78, 0.22, 2.45, 2.05, 1.35);

    mapCamera = eve.Camera3D();
    mapCamera.setFov(38.0);
    mapCamera.setAmbient(0.18, 0.20, 0.22);
    mapCamera.setClipPlanes(0.2, 180.0);
    mapCamera.setActive(true);

    rebuildWorld();
    applyCamera();
};

eve_update = function(dt) {
    mapTime += dt;
    mapFrame += 1;
    // Soft dappled cloud shadow — similar to the video's filtered sunlight.
    gfx.setCloudShadows(0.18, 5.5, mapTime * 0.12, 0.28, 0.4, 0.48, 0.6);
    if (mapGrass != null) mapGrass.update(dt);

    local moved = false;
    local speed = mapHeight * 0.6;
    if (has_module("keyboard")) {
        if (keyboard.isDown("W") || keyboard.isDown("Up")) { mapFocusZ -= speed * dt; moved = true; }
        if (keyboard.isDown("S") || keyboard.isDown("Down")) { mapFocusZ += speed * dt; moved = true; }
        if (keyboard.isDown("A") || keyboard.isDown("Left")) { mapFocusX -= speed * dt; moved = true; }
        if (keyboard.isDown("D") || keyboard.isDown("Right")) { mapFocusX += speed * dt; moved = true; }
        if (keyboard.isDown("Q")) { mapYaw -= dt * 0.55; moved = true; }
        if (keyboard.isDown("E")) { mapYaw += dt * 0.55; moved = true; }
    }
    if (has_module("mouse")) {
        local wheel = mouse.getWheelY();
        if (wheel != 0) {
            mapHeight = clampf(mapHeight - wheel * 1.5, 8.0, 42.0);
            moved = true;
        }
    }
    if (key_just_pressed("r") || key_just_pressed("R")) {
        mapSeed = procgen.randomSeed();
        rebuildWorld();
        moved = true;
    }
    if (key_just_pressed("space") || key_just_pressed("Space")) mapAutoPan = !mapAutoPan;

    if (moved) mapAutoPan = false;
    if (mapAutoPan && mapReady) {
        mapFocusX += cos(mapTime * 0.11) * dt * 1.4;
        mapFocusZ += sin(mapTime * 0.09) * dt * 1.1;
        mapYaw += dt * 0.03;
    }
    clampFocus();
    applyCamera();
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    if (mapGrass != null) mapGrass.draw();
    gfx.drawSolidRect(16.0, 16.0, 400.0, 40.0, 0.05, 0.08, 0.1, 0.72);

    if (!mapScreenshotSaved && mapReady && mapFrame > 28 && mapTime > 2.0) {
        if (gfx.saveFramePng("topdown-procmap.png")) {
            mapScreenshotSaved = true;
            print("TOPDOWN_PROCMAP_SCREENSHOT topdown-procmap.png\n");
        }
    }
};
