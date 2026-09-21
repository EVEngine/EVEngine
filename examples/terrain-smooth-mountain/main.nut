// Four real terrain meshes: two authored-height stamps and two baked-mesh stamps.
local retained = [];
local camera = null;
local shader = null;

function retain(value) {
    if (value == null) throw "terrain-smooth-mountain: missing resource";
    retained.append(value);
    return value;
}
function requireValue(result) {
    if (!result.ok) throw "terrain-smooth-mountain: operation failed";
    return retain(result.value);
}
function checked(result) {
    if (!result.ok) throw "terrain-smooth-mountain: operation failed";
    return result.value;
}
function raster(width, height) {
    return requireValue(procgen.newHeightmap(width, height));
}
function baseTerrain() {
    local map = raster(65, 65);
    for (local z = 0; z < 65; ++z) for (local x = 0; x < 65; ++x) {
        local h = 2.0 + 3.0 * math.noise2(x * 0.055, z * 0.055);
        map.setHeight(x, z, h);
    }
    return map;
}
function showTerrain(map, offsetX, offsetZ, smooth) {
    local chunk = retain(procgen.buildTerrainChunk(map, null, 0, 0, 64, 64, 0, 0.5, 1.0, 0.0));
    local mesh = retain(procgen.generateTerrainChunkMesh(chunk, gfx));
    local image = retain(procgen.generateTerrainSplatMap(chunk));
    local texture = retain(gfx.newTexture(image, false, false));
    local entity = retain(eve.Renderable3D());
    entity.setMesh(mesh);
    entity.setTexture(texture);
    entity.setShader(shader);
    if (smooth) entity.setTint(0.72, 1.0, 0.76, 1.0);
    else entity.setTint(1.0, 0.80, 0.55, 1.0);
    entity.setRoughness(0.92);
    entity.setPosition(offsetX, 0.0, offsetZ);
}
function placePair(stamp, coverage, one, amplitude, baseHeight, offsetZ) {
    for (local panel = 0; panel < 2; ++panel) {
        local terrain = baseTerrain();
        local settings = retain(eve.TerrainStampSettings());
        settings.setGrid(0.0, 0.0, 0.5, 0.5);
        settings.setCenter(16.0, 16.0);
        settings.setSize(28.0, 28.0);
        settings.setRotation(0.18);
        settings.amplitude = amplitude;
        settings.baseHeight = baseHeight;
        settings.smoothWidth = 4.0;
        settings.edgeFade = 3.0;
        local operation = panel == 0 ? 0 : 6;
        local changed = checked(terrain.applyStamp(stamp, settings, operation, one, coverage));
        if (changed <= 0) throw "terrain-smooth-mountain: stamp changed no samples";
        showTerrain(terrain, panel * 40.0, offsetZ, panel == 1);
    }
}
eve_init = function() {
    gfx.setBackgroundColor(0.045, 0.065, 0.09, 1.0);
    gfx.setDirectionalLight(-0.45, 0.9, 0.35, 1.8, 1.65, 1.35);
    shader = retain(procgen.createTerrainMaterialShader(gfx));
    local one = raster(1, 1);
    one.setHeight(0, 0, 1.0);

    // Author a mountain as a height stamp, then use identical terrain for both operations.
    local authored = raster(65, 65);
    for (local z = 0; z < 65; ++z) for (local x = 0; x < 65; ++x) {
        local dx = (x - 32) * 0.5, dz = (z - 32) * 0.5;
        authored.setHeight(x, z, 14.0 - 0.07 * (dx * dx + dz * dz));
    }
    placePair(authored, one, one, 1.0, 0.0, 40.0);

    // Standalone CPU model source. Replace this MeshBuild with your imported model data.
    local params = requireValue(procgen.newParams());
    params.setSeed(17);
    params.setInt("resolution", 24);
    params.setString("field", "rock");
    local cpuMesh = requireValue(procgen.buildMesh("mesh.marchingcubes", params));
    local builder = retain(eve.TerrainMeshStampBuilder());
    checked(builder.setSource(cpuMesh));
    local baked = raster(65, 65), coverage = raster(65, 65);
    local hits = checked(builder.bake(baked, coverage, -0.5, -0.5, 1.0, 1.0, 0.07));
    if (hits <= 0) throw "terrain-smooth-mountain: model bake has no surface hits";
    placePair(baked, coverage, one, 45.0, -2.0, 0.0);

    camera = retain(eve.Camera3D());
    camera.setEye(93.0, 67.0, 112.0);
    camera.setTarget(36.0, 3.5, 36.0);
    camera.setUp(0.0, 1.0, 0.0);
    camera.setFov(48.0);
    camera.setAmbient(0.28, 0.34, 0.42);
    camera.setActive(true);
    print("SMOOTH_MOUNTAIN_READY panels=4 meshSamples=" + hits + "\n");
};
eve_update = function(dt) {};
eve_render = function() {
    gfx.clear();
    gfx.render3D();
};
