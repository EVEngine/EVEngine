// Alpine mountain recipe + vertical cliff-band walls + rock scatter demo.

local retained = [];
local camera = null;
local terrainShader = null;
local rockShader = null;
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

eve_init = function() {
    gfx.setBackgroundColor(0.52, 0.68, 0.86, 1.0);
    gfx.setDirectionalLight(-0.62, 0.55, 0.40, 2.2, 1.9, 1.5);
    terrainShader = retain(procgen.createTerrainMaterialShader(gfx));
    rockShader = retain(procgen.createTerrainMaterialShader(gfx));

    local settings = retain(eve.MountainRecipeSettings());
    settings.setPreset(0); // AlpineRidge
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

    // Carve a midfield basin so the cliff-band extractor has a long vertical scarp at x≈58.
    for (local z = 20; z < 110; ++z) {
        for (local x = 58; x < 120; ++x) {
            local h = heightmap.height(x, z);
            heightmap.setHeight(x, z, h * 0.15);
        }
    }
    requireOk(eve.generateCliffFitnessMask(cliffMask, heightmap, cellSize, cellSize, heightScale, 0.48, 1.0),
        "cliffFitness");

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
            entity.setTint(0.90, 0.93, 0.86, 1.0);
            entity.setMetallic(0.0);
            entity.setRoughness(0.94);
            entity.setPosition(ox * cellSize, 0.0, oy * cellSize);
        }
    }

    local cliffSettings = retain(eve.CliffBandMeshSettings());
    cliffSettings.setOrigin(0.0, 0.0);
    cliffSettings.setSpacing(cellSize, cellSize);
    cliffSettings.heightScale = heightScale;
    cliffSettings.slopeMin = 0.48;
    cliffSettings.minRelief = 2.0;
    cliffSettings.subdivisions = 3;
    cliffSettings.strataAmplitude = 0.55;
    cliffSettings.strataScale = 0.95;
    cliffSettings.strataVertical = 3.4;
    cliffSettings.ledgeHeight = 0.55;
    cliffSettings.outwardBias = 0.95;
    cliffSettings.setSeed(20261105);
    cliffSettings.maxTriangles = 250000;
    local cliffCpu = retain(requireOk(eve.generateCliffBandMesh(heightmap, cliffSettings, cliffMask), "cliffBand"));
    local cliffVerts = cliffCpu.getVertexCount();
    local cliffTris = cliffCpu.getIndexCount() / 3;
    print("procgen-cliffs-mountains cliff band: verts=" + cliffVerts + " tris=" + cliffTris + "\n");
    if (cliffVerts <= 0 || cliffTris <= 0)
        throw "procgen-cliffs-mountains: cliff band produced no geometry";
    local cliffGpu = retain(requireOk(procgen.uploadMesh(cliffCpu, gfx), "cliffUpload"));
    local cliffEntity = retain(eve.Renderable3D());
    cliffEntity.setMesh(cliffGpu);
    cliffEntity.setShader(rockShader);
    cliffEntity.setTint(0.72, 0.38, 0.16, 1.0);
    cliffEntity.setMetallic(0.01);
    cliffEntity.setRoughness(0.98);
    cliffEntity.setPosition(0.0, 0.0, 0.0);

    local worldWidth = 128.0 * cellSize;
    local worldDepth = 128.0 * cellSize;
    local placement = retain(eve.TerrainObjectPlacementSettings());
    requireOk(eve.configureCliffRockScatterDefaults(placement, 0.0, 0.0, worldWidth, worldDepth,
        heightScale, 20261105, 9.0, 0.50, "mesh.rock:cliff"), "cliffScatter");
    local points = retain(requireOk(procgen.newPointSet(), "points"));
    local exportResult = placement.exportPoints(points, cliffMask, heightmap);
    if (exportResult.ok && exportResult.value > 0) {
        local rockParams = retain(requireOk(procgen.newParams(), "rockParams"));
        rockParams.setSeed(91);
        rockParams.setString("baseShape", "cliff");
        rockParams.setFloat("scale", 2.2);
        rockParams.setInt("subdivisions", 2);
        local rockMesh = retain(requireOk(procgen.generateMesh("mesh.rock", rockParams, gfx), "rockMesh"));
        local count = points.getCount();
        local limit = count > 16 ? 16 : count;
        for (local i = 0; i < limit; ++i) {
            local ent = retain(eve.Renderable3D());
            ent.setMesh(rockMesh);
            ent.setShader(rockShader);
            ent.setTint(0.36, 0.26, 0.18, 1.0);
            ent.setRoughness(0.95);
            ent.setPosition(points.getX(i), points.getY(i), points.getZ(i));
            local sc = 0.85 + 0.10 * (i % 5);
            ent.setScale(sc, sc, sc);
        }
        print("procgen-cliffs-mountains rock props: " + limit + "\n");
    }

    camera = retain(eve.Camera3D());
    // Basin is world x>23.2; scarp face at x≈23.2. Slight oblique so ledges read.
    camera.setEye(41.0, 13.0, 30.0);
    camera.setTarget(23.0, 6.5, 26.0);
    camera.setUp(0.0, 1.0, 0.0);
    camera.setFov(48.0);
    camera.setAmbient(0.36, 0.40, 0.46);
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
