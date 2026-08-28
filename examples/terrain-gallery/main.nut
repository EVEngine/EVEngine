// Three-seed terrain morphology gallery under identical generation and lighting.

local keepAlive = [];
local terrainShader = null;
local waterShader = null;
local elapsed = 0.0;
local renderedFrames = 0;
local saved = false;
local detailSaved = false;
local terrainOnlySaved = false;
local hydrologySaved = false;
local camera = null;
local waterEntities = [];

function retain(value) {
    keepAlive.append(value);
    return value;
}

function buildTerrain(seed, worldOffsetX) {
    local heightScale = 7.5;
    local cellSize = 0.0875;
    local params = procgen.newParams();
    params.setSize(257, 257);
    params.setSeed(seed);
    params.setFloat("frequency", 1.0 / 124.0);
    params.setInt("octaves", 4);
    params.setFloat("gain", 0.40);
    params.setFloat("ridge", 0.45);
    params.setFloat("warp", 0.18);
    params.setFloat("exponent", 1.25);
    params.setFloat("continent", 1.0);
    params.setFloat("island", 0.0);
    params.setFloat("coast", 0.35);
    local heightmap = retain(procgen.generateHeightmap(params));
    procgen.erodeTerrainThermal(heightmap, 16, 0.011, 0.28);
    procgen.erodeTerrainHydraulic(heightmap, 18, 0.007, 0.11, 1.4, 0.08, 0.11);
    // A relatively sparse display threshold keeps minor D8 drainage lines out
    // of the material layer, while a wider bank setting lets mature trunks
    // shape readable V-valleys and alluvial floors.
    procgen.erodeTerrainFluvialScaled(heightmap, 22, 0.008, 0.085, 0.08, 10.0, 0.060, 2.0);
    local layers = retain(procgen.analyzeTerrainScaled(heightmap, 420.0, 0.22, 0.42, 2.0));

    for (local chunkY = 0; chunkY < 4; ++chunkY) {
        for (local chunkX = 0; chunkX < 4; ++chunkX) {
            local originX = chunkX * 64;
            local originY = chunkY * 64;
            local chunk = retain(procgen.buildTerrainChunk(heightmap, layers, originX, originY,
                                                            64, 64, 0, cellSize, heightScale, 0.0));
            local mesh = retain(procgen.generateTerrainChunkMesh(chunk, gfx));
            local splatImage = retain(procgen.generateTerrainSplatMap(chunk));
            local splatTexture = retain(gfx.newTexture(splatImage, false, false));
            local entity = retain(eve.Renderable3D());
            entity.setMesh(mesh);
            entity.setTexture(splatTexture);
            entity.setShader(terrainShader);
            entity.setTint(1.0, 1.0, 1.0, 1.0);
            entity.setMetallic(0.0);
            entity.setRoughness(0.9);
            entity.setPosition(worldOffsetX + originX * cellSize, 0.0, originY * cellSize);

            local riverMesh = procgen.generateTerrainRiverMeshAdvanced(heightmap, layers, gfx,
                originX, originY, 64, 64, cellSize, heightScale, 0.025, 0.15, 0.045,
                0.0, 1.6);
            if (riverMesh != null) {
                retain(riverMesh);
                local river = retain(eve.Renderable3D());
                river.setMesh(riverMesh);
                river.setShader(waterShader);
                river.setTint(0.018, 0.16, 0.22, 1.0);
                river.setMetallic(0.0);
                river.setRoughness(0.34);
                river.setPosition(worldOffsetX + originX * cellSize, 0.0, originY * cellSize);
                waterEntities.append(river);
            }
            local lakeMesh = procgen.generateTerrainLakeMesh(heightmap, layers, gfx,
                originX, originY, 64, 64, cellSize, heightScale, 0.004, 0.040);
            if (lakeMesh != null) {
                retain(lakeMesh);
                local lake = retain(eve.Renderable3D());
                lake.setMesh(lakeMesh);
                lake.setShader(waterShader);
                lake.setTint(0.025, 0.22, 0.38, 1.0);
                lake.setMetallic(0.08);
                lake.setRoughness(0.12);
                lake.setPosition(worldOffsetX + originX * cellSize, 0.0, originY * cellSize);
                waterEntities.append(lake);
            }
        }
    }
}

eve_init = function() {
    gfx.setBackgroundColor(0.04, 0.06, 0.09, 1.0);
    gfx.setDirectionalLight(-0.42, 0.88, 0.32, 1.75, 1.58, 1.30);
    terrainShader = retain(procgen.createTerrainMaterialShader(gfx));
    waterShader = retain(procgen.createTerrainWaterShader(gfx));
    buildTerrain(17, 0.0);
    buildTerrain(1031, 25.0);
    buildTerrain(8191, 50.0);

    camera = retain(eve.Camera3D());
    camera.setEye(36.0, 34.0, 58.0);
    camera.setTarget(36.0, 3.2, 10.5);
    camera.setUp(0.0, 1.0, 0.0);
    camera.setFov(52.0);
    camera.setAmbient(0.25, 0.30, 0.38);
    camera.setActive(true);
};

eve_update = function(dt) { elapsed += dt; };

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    ++renderedFrames;
    if (!saved && elapsed > 1.5 && renderedFrames >= 8) {
        if (gfx.saveFramePng("/private/tmp/evengine-terrain-gallery.png")) {
            print("terrain gallery saved: /private/tmp/evengine-terrain-gallery.png\n");
            saved = true;
            camera.setEye(36.2, 12.5, 29.0);
            camera.setTarget(36.2, 1.4, 11.0);
            camera.setFov(34.0);
            renderedFrames = 0;
        }
    }
    if (saved && !detailSaved && elapsed > 2.3 && renderedFrames >= 8) {
        if (gfx.saveFramePng("/private/tmp/evengine-terrain-gallery-detail.png")) {
            print("terrain detail saved: /private/tmp/evengine-terrain-gallery-detail.png\n");
            detailSaved = true;
            foreach (water in waterEntities) water.setVisible(false);
            renderedFrames = 0;
        }
    }
    if (detailSaved && !terrainOnlySaved && elapsed > 3.0 && renderedFrames >= 8) {
        if (gfx.saveFramePng("/private/tmp/evengine-terrain-gallery-geometry.png")) {
            print("terrain geometry saved: /private/tmp/evengine-terrain-gallery-geometry.png\n");
            terrainOnlySaved = true;
            foreach (water in waterEntities) water.setVisible(true);
            // Near-orthographic audit view: a tiny target offset avoids a
            // look-at/up-vector singularity while retaining a map-like view.
            camera.setEye(36.2, 34.0, 11.12);
            camera.setTarget(36.2, 0.0, 11.0);
            camera.setFov(37.0);
            renderedFrames = 0;
        }
    }
    if (terrainOnlySaved && !hydrologySaved && elapsed > 3.7 && renderedFrames >= 8) {
        if (gfx.saveFramePng("/private/tmp/evengine-terrain-gallery-hydrology.png")) {
            print("terrain hydrology saved: /private/tmp/evengine-terrain-gallery-hydrology.png\n");
            hydrologySaved = true;
        }
    }
};
