// Direct-to-swapchain 3D terrain preview and visual-audit capture.

local terrainMeshes = [];
local terrainSplatImages = [];
local terrainSplatTextures = [];
local terrainEntities = [];
local riverMeshes = [];
local riverEntities = [];
local lakeMeshes = [];
local lakeEntities = [];
local terrainMaterialShader = null;
local waterMaterialShader = null;
local camera = null;
local elapsed = 0.0;
local renderedFrames = 0;
local saved = false;

eve_init = function() {
    gfx.setBackgroundColor(0.055, 0.08, 0.12, 1.0);
    gfx.setDirectionalLight(-0.45, 0.9, 0.35, 1.8, 1.65, 1.35);
    terrainMaterialShader = procgen.createTerrainMaterialShader(gfx);
    waterMaterialShader = procgen.createTerrainWaterShader(gfx);

    local paramsResult = procgen.newParams();
    if (!paramsResult.ok) return;
    local params = paramsResult.value;
    params.setSize(257, 257);
    params.setSeed(20260826);
    params.setFloat("frequency", 1.0 / 124.0);
    params.setInt("octaves", 4);
    params.setFloat("gain", 0.40);
    params.setFloat("ridge", 0.45);
    params.setFloat("warp", 0.18);
    params.setFloat("exponent", 1.25);
    params.setFloat("continent", 1.0);
    params.setFloat("island", 0.0);
    params.setFloat("coast", 0.35);
    local heightmapResult = procgen.generateHeightmap(params);
    if (!heightmapResult.ok) return;
    local heightmap = heightmapResult.value;
    procgen.erodeTerrainThermal(heightmap, 30, 0.0055, 0.30);
    procgen.erodeTerrainHydraulic(heightmap, 20, 0.007, 0.11, 1.4, 0.08, 0.11);
    procgen.erodeTerrainFluvialAdvanced(heightmap, 24, 0.0050, 0.11, 0.17, 8.0, 0.025);
    local layers = procgen.analyzeTerrain(heightmap, 360.0, 0.22, 0.42);

    for (local chunkY = 0; chunkY < 2; ++chunkY) {
        for (local chunkX = 0; chunkX < 2; ++chunkX) {
            local originX = chunkX * 128;
            local originY = chunkY * 128;
            local chunk = procgen.buildTerrainChunk(heightmap, layers, originX, originY,
                                                     128, 128, 0, 0.175, 16.0, 0.0);
            local mesh = procgen.generateTerrainChunkMesh(chunk, gfx);
            local albedoImage = procgen.generateTerrainSplatMap(chunk);
            local albedoTexture = gfx.newTexture(albedoImage, false, false);
            local entity = eve.Renderable3D();
            entity.setMesh(mesh);
            entity.setTexture(albedoTexture);
            entity.setShader(terrainMaterialShader);
            entity.setTint(1.0, 1.0, 1.0, 1.0);
            entity.setMetallic(0.0);
            entity.setRoughness(0.92);
            entity.setPosition(originX * 0.175, 0.0, originY * 0.175);
            terrainMeshes.append(mesh);
            terrainSplatImages.append(albedoImage);
            terrainSplatTextures.append(albedoTexture);
            terrainEntities.append(entity);

            local riverMesh = procgen.generateTerrainRiverMesh(heightmap, layers, gfx,
                originX, originY, 128, 128, 0.175, 16.0, 0.055, 0.34, 0.045);
            if (riverMesh != null) {
                local riverEntity = eve.Renderable3D();
                riverEntity.setMesh(riverMesh);
                riverEntity.setShader(waterMaterialShader);
                riverEntity.setTint(0.035, 0.30, 0.52, 1.0);
                riverEntity.setMetallic(0.05);
                riverEntity.setRoughness(0.20);
                riverEntity.setPosition(originX * 0.175, 0.0, originY * 0.175);
                riverMeshes.append(riverMesh);
                riverEntities.append(riverEntity);
            }
            local lakeMesh = procgen.generateTerrainLakeMesh(heightmap, layers, gfx,
                originX, originY, 128, 128, 0.175, 16.0, 0.004, 0.05);
            if (lakeMesh != null) {
                local lakeEntity = eve.Renderable3D();
                lakeEntity.setMesh(lakeMesh);
                lakeEntity.setShader(waterMaterialShader);
                lakeEntity.setTint(0.025, 0.22, 0.38, 1.0);
                lakeEntity.setMetallic(0.08);
                lakeEntity.setRoughness(0.12);
                lakeEntity.setPosition(originX * 0.175, 0.0, originY * 0.175);
                lakeMeshes.append(lakeMesh);
                lakeEntities.append(lakeEntity);
            }
        }
    }

    camera = eve.Camera3D();
    camera.setEye(62.0, 31.0, 64.0);
    camera.setTarget(22.4, 4.0, 22.4);
    camera.setUp(0.0, 1.0, 0.0);
    camera.setFov(48.0);
    camera.setAmbient(0.28, 0.34, 0.42);
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
        if (gfx.saveFramePng("/private/tmp/evengine-terrain-preview.png")) {
            print("terrain preview frame saved: /private/tmp/evengine-terrain-preview.png\n");
            saved = true;
        }
    }
};
