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

    // 1) Hard noise-warped scarp for cliff extraction (Pegasus needs real steep faces).
    for (local z = 12; z < 116; ++z) {
        local front = 56.0 + math.noise2(z * 0.07, 1.7) * 6.0 + math.noise2(z * 0.03, 4.2) * 3.0;
        for (local x = 0; x < mapSize; ++x) {
            if (x + 0.0 < front) continue;
            local h = heightmap.height(x, z);
            heightmap.setHeight(x, z, h * 0.11);
        }
    }
    requireOk(eve.generateCliffFitnessMask(cliffMask, heightmap, cellSize, cellSize, heightScale, 0.36, 1.0),
        "cliffFitness");
    // Keep fitness only along the authored scarp — alpine ridges elsewhere must not
    // become beige cladding that swallows the whole view.
    for (local z = 0; z < mapSize; ++z) {
        local front = 56.0 + math.noise2(z * 0.07, 1.7) * 6.0 + math.noise2(z * 0.03, 4.2) * 3.0;
        for (local x = 0; x < mapSize; ++x) {
            if (z < 12 || z >= 116 || x < front - 3.0 || x > front + 2.0) {
                cliffMask.setHeight(x, z, 0.0);
            }
        }
    }

    local cliffSettings = retain(eve.CliffBandMeshSettings());
    cliffSettings.setOrigin(0.0, 0.0);
    cliffSettings.setSpacing(cellSize, cellSize);
    cliffSettings.heightScale = heightScale;
    cliffSettings.slopeMin = 0.58;  // ~55°+ faces only — skip mild ramps/shelves
    cliffSettings.minRelief = 1.4;
    cliffSettings.subdivisions = 3;
    cliffSettings.strataAmplitude = 0.65;
    cliffSettings.strataScale = 0.40;
    cliffSettings.strataVertical = 1.10;
    cliffSettings.ledgeHeight = 1.0;
    cliffSettings.outwardBias = 0.93;
    cliffSettings.claddingOffset = 1.8;  // sit clearly in front of the terrain skin
    cliffSettings.strataTilt = 0.04;
    cliffSettings.macroAmplitude = 0.70;
    cliffSettings.smoothIterations = 1;
    cliffSettings.uvScaleU = 0.10;
    cliffSettings.uvScaleV = 0.12;
    cliffSettings.setSeed(20261105);
    cliffSettings.maxTriangles = 500000;
    local cliffCpu = retain(requireOk(eve.generateCliffBandMesh(heightmap, cliffSettings, cliffMask), "cliffBand"));
    print("procgen-cliffs-mountains cliff band: verts=" + cliffCpu.getVertexCount() +
        " tris=" + (cliffCpu.getIndexCount() / 3) + "\n");
    if (cliffCpu.getVertexCount() <= 0) throw "procgen-cliffs-mountains: empty cliff band";

    // 2) Soften only the plateau lip AFTER extraction (do not rebuild the sheer face).
    for (local z = 12; z < 116; ++z) {
        local front = 56.0 + math.noise2(z * 0.07, 1.7) * 6.0 + math.noise2(z * 0.03, 4.2) * 3.0;
        local rimX = front - 5.0;
        local lowX = front - 0.5;  // stop before the scarp cell so cladding stays exposed
        local xi0 = rimX.tointeger();
        local xi1 = lowX.tointeger();
        if (xi1 <= xi0) continue;
        if (xi0 < 1) xi0 = 1;
        if (xi1 > mapSize - 2) xi1 = mapSize - 2;
        local rim = heightmap.height(xi0, z);
        local low = heightmap.height(xi1, z);
        for (local x = xi0; x <= xi1; ++x) {
            local t = (x - rimX) / (lowX - rimX);
            t = math.clamp(t, 0.0, 1.0);
            t = t * t * (3.0 - 2.0 * t);
            heightmap.setHeight(x, z, rim * (1.0 - t) + low * t);
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
    for (local i = 0; i < 20; ++i) {
        local rockParams = retain(requireOk(procgen.newParams(), "rockParams" + i));
        rockParams.setSeed(107 + i * 17);
        rockParams.setString("baseShape", shapes[i % 4]);
        rockParams.setFloat("scale", 1.4 + 0.30 * (i % 5));
        rockParams.setInt("subdivisions", 3);
        local rockMesh = retain(requireOk(procgen.generateMesh("mesh.rock", rockParams, gfx), "rockMesh" + i));
        local ent = retain(eve.Renderable3D());
        ent.setMesh(rockMesh);
        ent.setMaterial(propMat);
        local px = 27.5 + (i % 5) * 0.75 + math.noise2(i * 0.4, 0.7) * 0.9;
        local pz = 12.0 + i * 1.55 + math.noise2(1.3, i * 0.33) * 0.8;
        local py = heightmap.sampleBilinear(px / cellSize, pz / cellSize) * heightScale;
        ent.setPosition(px, py + 0.08, pz);
        ent.setYaw(i * 1.05);
        local sc = 0.45 + 0.24 * (i % 5);
        ent.setScale(sc * (0.8 + 0.3 * (i % 3)), sc * (0.55 + 0.35 * (i % 4)), sc);
        ++rockCount;
    }
    print("procgen-cliffs-mountains rock props: " + rockCount + "\n");

    camera = retain(eve.Camera3D());
    // Side-oblique view: scarp as a vertical band with terrain context.
    camera.setEye(48.0, 16.0, 55.0);
    camera.setTarget(24.0, 6.5, 28.0);
    camera.setUp(0.0, 1.0, 0.0);
    camera.setFov(46.0);
    camera.setAmbient(0.25, 0.29, 0.35);
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
