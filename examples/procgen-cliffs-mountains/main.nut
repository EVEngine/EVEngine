// Alpine mountain recipe + continuous cliff-band rock strips + rock scatter demo.

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

function makeStrataTexture(size) {
    local image = eve.Image();
    local pixels = retain(image.newEmptyImageData(size, size, "RGBA8"));
    for (local y = 0; y < size; ++y) {
        // Strong horizontal beds (V = world height on cliff mesh).
        local band = ((y / 10) % 2 == 0) ? 0.18 : -0.08;
        local bed = math.noise2(0.2, y * 0.09) * 0.35 + band;
        for (local x = 0; x < size; ++x) {
            local n = math.noise2(x * 0.04, y * 0.11) * 0.12;
            local grit = math.noise2(x * 0.35, y * 0.35) * 0.08;
            local shade = 0.50 + bed + n + grit;
            if (shade < 0.22) shade = 0.22;
            if (shade > 0.90) shade = 0.90;
            pixels.setPixel(x, y, shade * 0.95 + 0.05, shade * 0.68 + 0.04, shade * 0.42 + 0.02, 1.0);
        }
    }
    return retain(gfx.newTexture(pixels, true, true));
}

function makeRockMaterial(r, g, b, roughness, texture) {
    local m = retain(gfx.newMaterial());
    if (texture != null) m.setAlbedoTexture(texture);
    m.setTint(r, g, b, 1.0);
    m.setMetallic(0.04);
    m.setRoughness(roughness);
    return m;
}

eve_init = function() {
    gfx.setBackgroundColor(0.56, 0.70, 0.88, 1.0);
    gfx.setDirectionalLight(-0.95, 0.32, 0.10, 2.7, 2.2, 1.55);
    terrainShader = retain(procgen.createTerrainMaterialShader(gfx));
    local strataTex = makeStrataTexture(256);
    local cliffMat = makeRockMaterial(0.95, 0.72, 0.48, 0.82, strataTex);
    local propMat = makeRockMaterial(0.70, 0.48, 0.30, 0.90, strataTex);

    local settings = retain(eve.MountainRecipeSettings());
    settings.setPreset(0);
    settings.setSize(mapSize, mapSize);
    settings.setSeed(20261105);
    settings.spacingX = cellSize;
    settings.spacingZ = cellSize;
    settings.heightScale = heightScale;
    settings.frequencyOverride = 1.0 / 36.0;
    settings.ridgeOverride = 0.72;
    settings.warpOverride = 0.28;
    settings.amplitudeOverride = 1.05;
    settings.cliffSlopeMin = 0.48;
    settings.thermalIterations = 3;
    settings.hydraulicIterations = 2;
    settings.fluvialIterations = 2;

    local heightmap = retain(requireOk(procgen.newHeightmap(mapSize, mapSize), "heightmap"));
    local slopeMask = retain(requireOk(procgen.newHeightmap(mapSize, mapSize), "slope"));
    local cliffMask = retain(requireOk(procgen.newHeightmap(mapSize, mapSize), "cliff"));
    requireOk(eve.bakeMountainRecipe(heightmap, settings, slopeMask, cliffMask), "bakeMountain");

    // Hard basin for cliff extraction.
    for (local z = 16; z < 114; ++z) {
        for (local x = 58; x < 122; ++x) {
            heightmap.setHeight(x, z, heightmap.height(x, z) * 0.12);
        }
    }
    requireOk(eve.generateCliffFitnessMask(cliffMask, heightmap, cellSize, cellSize, heightScale, 0.42, 1.0),
        "cliffFitness");

    local cliffSettings = retain(eve.CliffBandMeshSettings());
    cliffSettings.setOrigin(0.0, 0.0);
    cliffSettings.setSpacing(cellSize, cellSize);
    cliffSettings.heightScale = heightScale;
    cliffSettings.slopeMin = 0.42;
    cliffSettings.minRelief = 1.6;
    cliffSettings.subdivisions = 4;
    cliffSettings.strataAmplitude = 0.85;
    cliffSettings.strataScale = 0.95;
    cliffSettings.strataVertical = 3.2;
    // Texture carries bedding; geometric ledges read as cardboard stairs under soft light.
    cliffSettings.ledgeHeight = 0.0;
    cliffSettings.outwardBias = 0.9;
    cliffSettings.setSeed(20261105);
    cliffSettings.maxTriangles = 400000;
    local cliffCpu = retain(requireOk(eve.generateCliffBandMesh(heightmap, cliffSettings, cliffMask), "cliffBand"));
    print("procgen-cliffs-mountains cliff band: verts=" + cliffCpu.getVertexCount() +
        " tris=" + (cliffCpu.getIndexCount() / 3) + "\n");
    if (cliffCpu.getVertexCount() <= 0) throw "procgen-cliffs-mountains: empty cliff band";

    // Widen the terrain ramp so splat no longer owns a sheer face under the cladding.
    for (local z = 16; z < 114; ++z) {
        local rim = heightmap.height(48, z);
        local low = heightmap.height(58, z);
        for (local x = 49; x < 58; ++x) {
            local t = (x - 48) * 1.0 / 10.0;
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
            entity.setTint(0.84, 0.88, 0.80, 1.0);
            entity.setRoughness(0.96);
            entity.setPosition(ox * cellSize, 0.0, oy * cellSize);
        }
    }

    local cliffGpu = retain(requireOk(procgen.uploadMesh(cliffCpu, gfx), "cliffUpload"));
    local cliffEntity = retain(eve.Renderable3D());
    cliffEntity.setMesh(cliffGpu);
    cliffEntity.setMaterial(cliffMat);

    // Toe rocks sit on the basin floor along the scarp.
    local rockParams = retain(requireOk(procgen.newParams(), "rockParams"));
    rockParams.setSeed(107);
    local shapes = ["boulder", "shard", "cliff", "block"];
    local rockCount = 0;
    for (local i = 0; i < 18; ++i) {
        local rockParams = retain(requireOk(procgen.newParams(), "rockParams" + i));
        rockParams.setSeed(107 + i * 17);
        rockParams.setString("baseShape", shapes[i % 4]);
        rockParams.setFloat("scale", 1.6 + 0.35 * (i % 5));
        rockParams.setInt("subdivisions", 3);
        local rockMesh = retain(requireOk(procgen.generateMesh("mesh.rock", rockParams, gfx), "rockMesh" + i));
        local ent = retain(eve.Renderable3D());
        ent.setMesh(rockMesh);
        ent.setMaterial(propMat);
        local pz = 13.5 + i * 1.95 + ((i % 3) * 0.45);
        local cellZ = pz / cellSize;
        local py = heightmap.sampleBilinear(59.0, cellZ) * heightScale;
        ent.setPosition(24.6 + 0.9 * ((i * 5) % 4) * 0.35, py + 0.15, pz);
        ent.setYaw(i * 1.15);
        local sc = 0.50 + 0.28 * (i % 5);
        ent.setScale(sc * (0.75 + 0.35 * (i % 3)), sc * (0.55 + 0.4 * (i % 4)), sc);
        ++rockCount;
    }
    print("procgen-cliffs-mountains rock props: " + rockCount + "\n");

    camera = retain(eve.Camera3D());
    camera.setEye(42.0, 12.0, 35.0);
    camera.setTarget(24.5, 7.0, 26.0);
    camera.setUp(0.0, 1.0, 0.0);
    camera.setFov(44.0);
    camera.setAmbient(0.26, 0.30, 0.36);
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
