// Alpine mountain + Pegasus/Ubisoft cliff extract (steep HF patch → strata → blur).

local retained = [];
local camera = null;
local terrainShader = null;
local elapsed = 0.0;
local renderedFrames = 0;
local saved = false;
local cellSize = 0.40;
local heightScale = 26.0;
local mapSize = 129;

function retain(value) {
    if (value == null) throw "procgen-cliffs-mountains: missing resource";
    retained.append(value);
    return value;
}

function requireOk(result, label) {
    if (!result.ok) throw "procgen-cliffs-mountains: " + label + " failed";
    return result.value;
}

function makeCliffMaterial() {
    local params = retain(requireOk(procgen.newParams(), "cliffPbrParams"));
    requireOk(params.setSeed(20261105), "cliffPbr seed");
    requireOk(params.setSize(512, 512), "cliffPbr size");
    params.setFloat("scale", 2.5);
    params.setFloat("normalStrength", 5.2);
    params.setFloat("aoStrength", 1.8);
    params.setFloat("heightStrength", 1.35);
    params.setFloat("roughnessLow", 0.70);
    params.setFloat("roughnessHigh", 0.96);
    local maps = requireOk(procgen.generatePbrMaterial("pbr.cliff.strata", params), "cliffPbr");
    local albedo = retain(gfx.newTexture(maps.getAlbedo(), true, true));
    local normal = retain(gfx.newTexture(maps.getNormal(), true, true));
    local height = retain(gfx.newTexture(maps.getHeight(), true, true));
    maps.destroy();
    local m = retain(gfx.newMaterial());
    m.setAlbedoTexture(albedo);
    m.setNormalTexture(normal);
    m.setHeightTexture(height);
    m.setTint(0.94, 0.88, 0.78, 1.0);
    m.setMetallic(0.02);
    m.setRoughness(0.86);
    m.setParallax(0.016, 8.0, 20.0);
    m.setReceiveLight(true);
    return m;
}

function makeRockPropMaterial() {
    local params = retain(requireOk(procgen.newParams(), "rockPbrParams"));
    requireOk(params.setSeed(107), "rockPbr seed");
    requireOk(params.setSize(256, 256), "rockPbr size");
    params.setFloat("scale", 2.8);
    params.setFloat("normalStrength", 3.2);
    params.setFloat("aoStrength", 1.6);
    local maps = requireOk(procgen.generatePbrMaterial("pbr.rock", params), "rockPbr");
    local albedo = retain(gfx.newTexture(maps.getAlbedo(), true, true));
    local normal = retain(gfx.newTexture(maps.getNormal(), true, true));
    maps.destroy();
    local m = retain(gfx.newMaterial());
    m.setAlbedoTexture(albedo);
    m.setNormalTexture(normal);
    m.setTint(0.90, 0.84, 0.74, 1.0);
    m.setMetallic(0.03);
    m.setRoughness(0.92);
    m.setReceiveLight(true);
    return m;
}

eve_init = function() {
    gfx.setBackgroundColor(0.56, 0.70, 0.88, 1.0);
    gfx.setDirectionalLight(-0.88, 0.40, 0.18, 2.6, 2.2, 1.6);
    terrainShader = retain(procgen.createTerrainMaterialShader(gfx));
    local cliffMat = makeCliffMaterial();
    local propMat = makeRockPropMaterial();

    local settings = retain(eve.MountainRecipeSettings());
    settings.setPreset(0);
    settings.setSize(mapSize, mapSize);
    settings.setSeed(20261105);
    settings.spacingX = cellSize;
    settings.spacingZ = cellSize;
    settings.heightScale = heightScale;
    settings.frequencyOverride = 1.0 / 34.0;
    settings.ridgeOverride = 0.74;
    settings.warpOverride = 0.30;
    settings.amplitudeOverride = 1.08;
    settings.cliffSlopeMin = 0.42;
    settings.thermalIterations = 4;
    settings.hydraulicIterations = 3;
    settings.fluvialIterations = 2;

    local heightmap = retain(requireOk(procgen.newHeightmap(mapSize, mapSize), "heightmap"));
    local slopeMask = retain(requireOk(procgen.newHeightmap(mapSize, mapSize), "slope"));
    local cliffMask = retain(requireOk(procgen.newHeightmap(mapSize, mapSize), "cliff"));
    requireOk(eve.bakeMountainRecipe(heightmap, settings, slopeMask, cliffMask), "bakeMountain");

    // 1) Multi-cell steep scarp (Pegasus needs real face area, not a 1-cell knife edge).
    //    faceWidth cells of steep drop → extracted mesh has depth for strata shelves.
    local faceWidth = 7.0;
    for (local z = 12; z < 116; ++z) {
        local front = 56.0 + math.noise2(z * 0.07, 1.7) * 6.0 + math.noise2(z * 0.03, 4.2) * 3.0;
        // Local face thickness varies so the silhouette breaks into buttresses.
        local width = faceWidth + math.noise2(z * 0.11, 8.3) * 2.2;
        for (local x = 0; x < mapSize; ++x) {
            local xf = x + 0.0;
            if (xf < front - width) continue;
            local plateau = heightmap.height(x, z);
            local valley = plateau * 0.10;
            if (xf >= front) {
                heightmap.setHeight(x, z, valley);
                continue;
            }
            // Steep ease: stay near-vertical mid-face, soften only at lip/toe.
            local t = (xf - (front - width)) / width;
            t = math.clamp(t, 0.0, 1.0);
            // Bias toward a sheer mid-face (pow > 1 keeps upper cells high longer).
            local sheer = pow(t, 0.55);
            // Small lateral noise so the face is not a perfect plane.
            local n = math.noise2(x * 0.35, z * 0.22) * 0.06;
            sheer = math.clamp(sheer + n, 0.0, 1.0);
            heightmap.setHeight(x, z, plateau * (1.0 - sheer) + valley * sheer);
        }
    }
    requireOk(eve.generateCliffFitnessMask(cliffMask, heightmap, cellSize, cellSize, heightScale, 0.36, 1.0),
        "cliffFitness");
    // Scope fitness to the authored scarp band only (skip alpine ridges elsewhere).
    for (local z = 0; z < mapSize; ++z) {
        local front = 56.0 + math.noise2(z * 0.07, 1.7) * 6.0 + math.noise2(z * 0.03, 4.2) * 3.0;
        local width = faceWidth + math.noise2(z * 0.11, 8.3) * 2.2;
        for (local x = 0; x < mapSize; ++x) {
            if (z < 12 || z >= 116 || x < front - width - 1.0 || x > front + 1.5) {
                cliffMask.setHeight(x, z, 0.0);
            }
        }
    }

    local cliffSettings = retain(eve.CliffBandMeshSettings());
    cliffSettings.setOrigin(0.0, 0.0);
    cliffSettings.setSpacing(cellSize, cellSize);
    cliffSettings.heightScale = heightScale;
    cliffSettings.slopeMin = 0.42;  // keep the thick face; still skip soft plateaus
    cliffSettings.minRelief = 0.85;
    cliffSettings.subdivisions = 3;
    cliffSettings.strataAmplitude = 0.28;  // subtle bed banding — keep the wall continuous
    cliffSettings.strataScale = 0.38;
    cliffSettings.strataVertical = 0.95;
    cliffSettings.ledgeHeight = 1.80;
    cliffSettings.outwardBias = 0.90;
    cliffSettings.claddingOffset = 0.85;
    // No hollow back-face: an open shell reads as venetian-blind wafers from 3/4 views.
    // Volume comes from the multi-cell steep scarp + cladding sitting on the buried ramp.
    cliffSettings.shellThickness = 0.0;
    cliffSettings.strataTilt = 0.03;
    cliffSettings.macroAmplitude = 0.55;  // Worley rock blocks for fractured face detail
    cliffSettings.smoothIterations = 2;
    cliffSettings.uvScaleU = 0.07;
    cliffSettings.uvScaleV = 0.10;
    cliffSettings.setSeed(20261105);
    cliffSettings.maxTriangles = 500000;
    local cliffCpu = retain(requireOk(eve.generateCliffBandMesh(heightmap, cliffSettings, cliffMask), "cliffBand"));
    print("procgen-cliffs-mountains cliff band: verts=" + cliffCpu.getVertexCount() +
        " tris=" + (cliffCpu.getIndexCount() / 3) + "\n");
    if (cliffCpu.getVertexCount() <= 0) throw "procgen-cliffs-mountains: empty cliff band";
    // Debug AABB (skip non-finite verts — a few bad samples must not break framing).
    local minX = 1e9; local maxX = -1e9;
    local minY = 1e9; local maxY = -1e9;
    local minZ = 1e9; local maxZ = -1e9;
    local bad = 0;
    local vc = cliffCpu.getVertexCount();
    for (local i = 0; i < vc; ++i) {
        local x = cliffCpu.getPositionX(i);
        local y = cliffCpu.getPositionY(i);
        local z = cliffCpu.getPositionZ(i);
        if (!(x == x) || !(y == y) || !(z == z) || x < -1e5 || x > 1e5 || z < -1e5 || z > 1e5) {
            if (bad < 3) {
                print("procgen-cliffs-mountains bad vert " + i + ": (" + x + "," + y + "," + z + ")\n");
            }
            ++bad;
            continue;
        }
        if (x < minX) minX = x; if (x > maxX) maxX = x;
        if (y < minY) minY = y; if (y > maxY) maxY = y;
        if (z < minZ) minZ = z; if (z > maxZ) maxZ = z;
    }
    print("procgen-cliffs-mountains cliff aabb: (" + minX + "," + minY + "," + minZ +
        ")..(" + maxX + "," + maxY + "," + maxZ + ")\n");
    local midX = (minX + maxX) * 0.5;
    local midY = (minY + maxY) * 0.5;
    local midZ = (minZ + maxZ) * 0.5;
    local faceDepth = maxX - minX;
    if (!(faceDepth > 0.05) || midX < -1e4 || midX > 1e4 || minY > maxY) {
        midX = 22.0; midY = 10.0; midZ = 26.0; maxX = 25.0; minZ = 10.0; maxZ = 40.0;
    }

    // 2) After extraction, bury the terrain's sheer face under a soft ramp so the
    //    green heightfield skin does not double the beige cliff cladding (wafer pair).
    //    Cliff mesh already captured the hard scarp; terrain only needs a support ramp.
    for (local z = 12; z < 116; ++z) {
        local front = 56.0 + math.noise2(z * 0.07, 1.7) * 6.0 + math.noise2(z * 0.03, 4.2) * 3.0;
        local width = faceWidth + math.noise2(z * 0.11, 8.3) * 2.2;
        local rimX = front - width - 2.5;
        local toeX = front + 3.5;
        local xi0 = rimX.tointeger();
        local xi1 = toeX.tointeger();
        if (xi1 <= xi0) continue;
        if (xi0 < 1) xi0 = 1;
        if (xi1 > mapSize - 2) xi1 = mapSize - 2;
        local rim = heightmap.height(xi0, z);
        local toe = heightmap.height(xi1, z);
        for (local x = xi0; x <= xi1; ++x) {
            local t = (x - rimX) / (toeX - rimX);
            t = math.clamp(t, 0.0, 1.0);
            // Smoothstep ramp — shallow enough that terrain no longer reads as a wall.
            t = t * t * (3.0 - 2.0 * t);
            heightmap.setHeight(x, z, rim * (1.0 - t) + toe * t);
        }
    }

    local layers = retain(procgen.analyzeTerrain(heightmap, 360.0, 0.14, 0.36));
    local chunkCells = 64;
    for (local cy = 0; cy < 2; ++cy) {
        for (local cx = 0; cx < 2; ++cx) {
            local ox = cx * chunkCells;
            local oy = cy * chunkCells;
            local chunk = retain(procgen.buildTerrainChunk(heightmap, layers, ox, oy,
                chunkCells, chunkCells, 0, cellSize, heightScale, 0.0));
            local mesh = retain(procgen.generateTerrainChunkMesh(chunk, gfx));
            local albedo = retain(procgen.generateTerrainSplatMap(chunk));
            local texture = retain(gfx.newTexture(albedo, false, false));
            local entity = retain(eve.Renderable3D());
            entity.setMesh(mesh);
            entity.setTexture(texture);
            entity.setShader(terrainShader);
            entity.setTint(0.80, 0.86, 0.74, 1.0);
            entity.setRoughness(0.96);
            entity.setPosition(ox * cellSize, 0.0, oy * cellSize);
        }
    }

    local cliffGpu = retain(requireOk(procgen.uploadMesh(cliffCpu, gfx), "cliffUpload"));
    local cliffEntity = retain(eve.Renderable3D());
    cliffEntity.setMesh(cliffGpu);
    cliffEntity.setMaterial(cliffMat);

    local shapes = ["boulder", "shard", "cliff", "block"];
    local rockCount = 0;
    for (local i = 0; i < 16; ++i) {
        local rockParams = retain(requireOk(procgen.newParams(), "rockParams" + i));
        rockParams.setSeed(107 + i * 17);
        rockParams.setString("baseShape", shapes[i % 4]);
        rockParams.setFloat("scale", 1.2 + 0.25 * (i % 5));
        rockParams.setInt("subdivisions", 3);
        local rockMesh = retain(requireOk(procgen.generateMesh("mesh.rock", rockParams, gfx), "rockMesh" + i));
        local ent = retain(eve.Renderable3D());
        ent.setMesh(rockMesh);
        ent.setMaterial(propMat);
        // Valley-floor talus only — never mid-face (avoids floating props).
        local pz = 14.0 + i * 1.85 + math.noise2(1.3, i * 0.33) * 0.9;
        local frontCell = 56.0 + math.noise2((pz / cellSize) * 0.07, 1.7) * 6.0 +
            math.noise2((pz / cellSize) * 0.03, 4.2) * 3.0;
        local px = (frontCell + 2.5 + (i % 4) * 1.1 + math.noise2(i * 0.4, 0.7) * 0.8) * cellSize;
        local py = heightmap.sampleBilinear(px / cellSize, pz / cellSize) * heightScale;
        ent.setPosition(px, py + 0.05, pz);
        ent.setYaw(i * 1.05);
        local sc = 0.38 + 0.20 * (i % 5);
        ent.setScale(sc * (0.8 + 0.3 * (i % 3)), sc * (0.55 + 0.35 * (i % 4)), sc);
        ++rockCount;
    }
    print("procgen-cliffs-mountains rock props: " + rockCount + "\n");

    camera = retain(eve.Camera3D());
    // Landscape 3/4 on the front skin: enough Z offset for depth, not so much that
    // the wall collapses into a wafer silhouette.
    camera.setEye(maxX + 12.0, midY + 3.5, midZ + (maxZ - minZ) * 0.22);
    camera.setTarget(maxX - 1.0, midY * 0.75, midZ - (maxZ - minZ) * 0.05);
    camera.setUp(0.0, 1.0, 0.0);
    camera.setFov(50.0);
    camera.setAmbient(0.34, 0.36, 0.40);
    camera.setActive(true);
};

eve_update = function(dt) {
    elapsed += dt;
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    ++renderedFrames;
    if (!saved && elapsed > 1.5 && renderedFrames >= 8) {
        if (gfx.saveFramePng("procgen-cliffs-mountains.png")) {
            print("procgen-cliffs-mountains frame saved: procgen-cliffs-mountains.png\n");
            saved = true;
        }
    }
};
