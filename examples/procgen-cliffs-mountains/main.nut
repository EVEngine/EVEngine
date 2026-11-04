// Alpine mountain recipe + cliff-band mesh + rock scatter demo.

local retained = [];
local camera = null;
local terrainShader = null;
local rockShader = null;
local elapsed = 0.0;
local renderedFrames = 0;
local saved = false;
local cellSize = 0.35;
local heightScale = 18.0;
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
    gfx.setBackgroundColor(0.05, 0.07, 0.10, 1.0);
    gfx.setDirectionalLight(-0.42, 0.88, 0.28, 1.85, 1.70, 1.40);
    terrainShader = retain(procgen.createTerrainMaterialShader(gfx));
    rockShader = retain(procgen.createTerrainMaterialShader(gfx));

    local settings = retain(eve.MountainRecipeSettings());
    settings.setPreset(0); // AlpineRidge
    settings.setSize(mapSize, mapSize);
    settings.setSeed(20261105);
    settings.spacingX = cellSize;
    settings.spacingZ = cellSize;
    settings.heightScale = heightScale;
    settings.frequencyOverride = 1.0 / 42.0;
    settings.ridgeOverride = 0.65;
    settings.cliffSlopeMin = 0.22;
    settings.thermalIterations = 12;
    settings.hydraulicIterations = 8;
    settings.fluvialIterations = 6;

    local heightmap = retain(requireOk(procgen.newHeightmap(mapSize, mapSize), "heightmap"));
    local slopeMask = retain(requireOk(procgen.newHeightmap(mapSize, mapSize), "slope"));
    local cliffMask = retain(requireOk(procgen.newHeightmap(mapSize, mapSize), "cliff"));
    requireOk(eve.bakeMountainRecipe(heightmap, settings, slopeMask, cliffMask), "bakeMountain");

    local cliffSettings = retain(eve.CliffBandMeshSettings());
    cliffSettings.setOrigin(0.0, 0.0);
    cliffSettings.setSpacing(cellSize, cellSize);
    cliffSettings.heightScale = heightScale;
    cliffSettings.slopeMin = 0.22;
    cliffSettings.subdivisions = 1;
    cliffSettings.strataAmplitude = 0.18;
    cliffSettings.ledgeHeight = 0.45;
    cliffSettings.setSeed(20261105);
    cliffSettings.maxTriangles = 200000;
    local cliffCpu = retain(requireOk(eve.generateCliffBandMesh(heightmap, cliffSettings, cliffMask), "cliffBand"));
    if (cliffCpu.getVertexCount() <= 0) throw "procgen-cliffs-mountains: cliff band produced no vertices";
    local cliffGpu = retain(requireOk(procgen.uploadMesh(cliffCpu, gfx), "cliffUpload"));
    local cliffEntity = retain(eve.Renderable3D());
    cliffEntity.setMesh(cliffGpu);
    cliffEntity.setShader(rockShader);
    cliffEntity.setTint(0.52, 0.48, 0.44, 1.0);
    cliffEntity.setMetallic(0.04);
    cliffEntity.setRoughness(0.88);
    cliffEntity.setPosition(0.0, 0.0, 0.0);

    local layers = retain(procgen.analyzeTerrain(heightmap, 360.0, 0.18, 0.40));
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
            entity.setTint(1.0, 1.0, 1.0, 1.0);
            entity.setMetallic(0.0);
            entity.setRoughness(0.92);
            entity.setPosition(ox * cellSize, 0.0, oy * cellSize);
        }
    }

    // Dress steep bands with a handful of procedural cliff rocks.
    local worldWidth = 128.0 * cellSize;
    local worldDepth = 128.0 * cellSize;
    local placement = retain(eve.TerrainObjectPlacementSettings());
    requireOk(eve.configureCliffRockScatterDefaults(placement, 0.0, 0.0, worldWidth, worldDepth,
        heightScale, 20261105, 14.0, 0.55, "mesh.rock:cliff"), "cliffScatter");
    local points = retain(requireOk(procgen.newPointSet(), "points"));
    local exportResult = placement.exportPoints(points, cliffMask, heightmap);
    if (exportResult.ok && exportResult.value > 0) {
        local rockParams = retain(requireOk(procgen.newParams(), "rockParams"));
        rockParams.setSeed(91);
        rockParams.setString("baseShape", "cliff");
        rockParams.setFloat("scale", 1.8);
        rockParams.setInt("subdivisions", 2);
        local rockMesh = retain(requireOk(procgen.generateMesh("mesh.rock", rockParams, gfx), "rockMesh"));
        local count = points.getCount();
        local limit = count > 12 ? 12 : count;
        for (local i = 0; i < limit; ++i) {
            local ent = retain(eve.Renderable3D());
            ent.setMesh(rockMesh);
            ent.setShader(rockShader);
            ent.setTint(0.45, 0.42, 0.38, 1.0);
            ent.setRoughness(0.9);
            ent.setPosition(points.getX(i), points.getY(i), points.getZ(i));
            local sc = 0.7 + 0.08 * (i % 5);
            ent.setScale(sc, sc, sc);
        }
    }

    camera = retain(eve.Camera3D());
    camera.setEye(38.0, 26.0, 48.0);
    camera.setTarget(18.0, 6.0, 18.0);
    camera.setUp(0.0, 1.0, 0.0);
    camera.setFov(48.0);
    camera.setAmbient(0.26, 0.32, 0.40);
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
