// Terrain reference: the level stores *how to obtain* terrain, not the samples.
//
// mode = "generated" recreates the heightmap deterministically from the sampler
// parameters below, so the level file stays small and diffable.
// mode = "asset" references an existing terrain asset on disk; the engine decodes
// it (EVTR chunk archive or the EVTRN heightfield payload of an `eve.terrain`
// asset) and hands back the same sampling surface.
//
// World mapping (shared by the render mesh and the collider):
//   worldX = originX + cellX * spacing
//   worldY = sample * heightScale
//   worldZ = originZ + cellY * spacing

function terrainDefaultReference() {
    return {
        mode = "generated",
        path = "assets/terrain/level.evtr",
        // "auto" lets the decoder pick EVTR or EVTRN from the file magic, which is
        // what a designer wants by default; name the encoding only to force it.
        format = "auto",
        seed = 20260910,
        width = 97, height = 97,
        spacing = 2.0,
        heightScale = 7.0,
        originX = -96.0, originZ = -96.0,
        sampler = {
            frequency = 0.041666667, octaves = 4, lacunarity = 2.0, gain = 0.5,
            ridge = 0.35, warp = 0.30, exponent = 1.6, continent = 0.55,
            island = 0.28, coast = 0.12, baseLevel = 0.35, amplitude = 0.50
        }
    };
}

// Squirrel `const` only accepts scalar literals, so the sampler alias map is a
// function-local table: it translates readable source names to TerrainSampler
// keys (`base` is a reserved word in Squirrel).
function terrainSamplerParams(reference) {
    local aliases = {
        frequency = "frequency", octaves = "octaves", lacunarity = "lacunarity",
        gain = "gain", ridge = "ridge", warp = "warp", exponent = "exponent",
        continent = "continent", island = "island", coast = "coast",
        baseLevel = "base", amplitude = "amplitude"
    };
    local created = procgen.newParams();
    if (!created.ok) return created;
    local params = created.value;
    params.setSize(reference.width, reference.height);
    params.setSeed(reference.seed);
    foreach (key, value in reference.sampler) {
        local engineKey = (key in aliases) ? aliases[key] : key;
        if (typeof value == "integer") params.setInt(engineKey, value);
        else params.setFloat(engineKey, value);
    }
    params.setInt("clamp", 1);
    params.setFloat("heightMin", 0.0);
    params.setFloat("heightMax", 1.0);
    return created;
}

function terrainSampleBounds(heightmap) {
    local minimum = null, maximum = null;
    for (local y = 0; y < heightmap.getHeight(); ++y) {
        for (local x = 0; x < heightmap.getWidth(); ++x) {
            local value = heightmap.height(x, y);
            if (minimum == null || value < minimum) minimum = value;
            if (maximum == null || value > maximum) maximum = value;
        }
    }
    if (minimum == null) { minimum = 0.0; maximum = 1.0; }
    return {minimum = minimum, maximum = maximum};
}

// Builds the sampling surface for `reference` and returns it, or null with
// level.status set. The caller owns attaching it to the scene and collider.
function terrainBuild(reference) {
    local heightmap = null, spacingX = reference.spacing, spacingZ = reference.spacing;
    local decodedMin = 0.0, decodedMax = 1.0;

    if (reference.mode == "asset") {
        local loaded = procgen.loadTerrainFile(reference.path, reference.format);
        if (!loaded.ok) { level.status = loaded.status.summary; return null; }
        heightmap = loaded.value;
        // EVTRN stores metres-per-cell; an EVTR archive stores none, so the level's
        // own spacing stays authoritative in that case.
        if (loaded.hasSpacing) {
            spacingX = loaded.spacingX;
            spacingZ = loaded.spacingZ;
        } else {
            spacingX = reference.spacing;
            spacingZ = reference.spacing;
        }
        decodedMin = loaded.minHeight;
        decodedMax = loaded.maxHeight;
    } else {
        local params = terrainSamplerParams(reference);
        if (!params.ok) { level.status = params.status.summary; return null; }
        local generated = procgen.generateHeightmap(params.value);
        if (!generated.ok) { level.status = generated.status.summary; return null; }
        heightmap = generated.value;
        local bounds = terrainSampleBounds(heightmap);
        decodedMin = bounds.minimum;
        decodedMax = bounds.maximum;
    }

    // EVTRN stores metres already; the procedural path stores normalized samples.
    local scale = reference.mode == "asset" ? 1.0 : reference.heightScale;
    local mesh = level.heightmapTargets.newSmoothMesh(heightmap, spacingX, scale);
    if (mesh == null) { level.status = "terrain mesh build failed"; return null; }

    local entity = eve.Renderable3D();
    entity.setMesh(mesh);
    entity.setPosition(reference.originX, 0.0, reference.originZ);
    entity.setTint(0.30, 0.38, 0.30, 1.0);
    entity.setRoughness(0.92);
    entity.setReceiveShadow(true);
    entity.setCastShadow(false);

    local terrain = {
        reference = reference,
        heightmap = heightmap,
        mesh = mesh,
        entity = entity,
        spacingX = spacingX,
        spacingZ = spacingZ,
        heightScale = scale,
        originX = reference.originX,
        originZ = reference.originZ,
        width = heightmap.getWidth(),
        height = heightmap.getHeight(),
        minSample = decodedMin,
        maxSample = decodedMax,
        collider = null,
        body = null
    };
    terrain.mesh = mesh;
    level.terrain = terrain;
    return terrain;
}

function terrainDispose() {
    if (level.terrain == null) return;
    if (level.terrain.entity != null) level.terrain.entity.setVisible(false);
    level.terrain = null;
}

/**
 * @brief Rebuild from `reference`, keeping the current terrain if it fails.
 *
 * `terrainBuild` only publishes its result once every step succeeded, so a bad
 * path or unsupported encoding leaves the previous terrain sampling surface in
 * place instead of tearing the level down.
 */
function terrainRebuild(reference) {
    local previous = level.terrain;
    local built = terrainBuild(reference);
    if (built == null) return null;
    if (previous != null && previous != built && previous.entity != null) previous.entity.setVisible(false);
    return built;
}

/** @brief Rebuild from the level's own reference, then re-frame the view. */
function levelRebuildTerrain() {
    local reference = level.terrain != null ? level.terrain.reference : terrainDefaultReference();
    local previous = level.terrain;
    if (terrainRebuild(reference) == null) {
        level.status = "terrain rebuild failed: " + level.status;
        level.terrain = previous;
        return;
    }
    whiteboxSyncTransforms();
    spawnSyncTransforms();
    levelFrameTerrain();
    levelRefreshPanels();
    level.status = "terrain: " + level.terrain.reference.mode + " " +
                   level.terrain.width + "x" + level.terrain.height + " @ " + level.terrain.spacingX + "m";
}

function terrainWorldExtent(terrain) {
    local lastX = (terrain.width - 1) * terrain.spacingX;
    local lastZ = (terrain.height - 1) * terrain.spacingZ;
    return {minX = terrain.originX, minZ = terrain.originZ,
            maxX = terrain.originX + lastX, maxZ = terrain.originZ + lastZ};
}

function terrainInside(terrain, worldX, worldZ) {
    local extent = terrainWorldExtent(terrain);
    return worldX >= extent.minX && worldX <= extent.maxX &&
           worldZ >= extent.minZ && worldZ <= extent.maxZ;
}

/** @brief Terrain surface height in metres, or null outside the terrain extent. */
function terrainHeightAt(worldX, worldZ) {
    local terrain = level.terrain;
    if (terrain == null) return null;
    if (!terrainInside(terrain, worldX, worldZ)) return null;
    local cellX = (worldX - terrain.originX) / terrain.spacingX;
    local cellZ = (worldZ - terrain.originZ) / terrain.spacingZ;
    cellX = levelClamp(cellX, 0.0, terrain.width - 1.001);
    cellZ = levelClamp(cellZ, 0.0, terrain.height - 1.001);
    return terrain.heightmap.sampleBilinear(cellX, cellZ) * terrain.heightScale;
}

/** @brief Terrain surface height clamped into the extent, for snapping. */
function terrainSnapHeight(worldX, worldZ) {
    local height = terrainHeightAt(worldX, worldZ);
    if (height != null) return height;
    local terrain = level.terrain;
    if (terrain == null) return 0.0;
    local extent = terrainWorldExtent(terrain);
    return terrainHeightAt(levelClamp(worldX, extent.minX, extent.maxX),
                           levelClamp(worldZ, extent.minZ, extent.maxZ));
}

// The collider mirrors the render mesh exactly: same origin, same spacing, same
// height scale. Box3D height fields span X/Z with heights along Y, so a static
// body placed at the terrain origin is already ground-oriented.
function terrainBuildCollider(world) {
    local terrain = level.terrain;
    if (terrain == null || world == null) return null;
    local heights = [];
    heights.resize(terrain.width * terrain.height);
    local index = 0;
    for (local y = 0; y < terrain.height; ++y) {
        for (local x = 0; x < terrain.width; ++x) {
            heights[index] = terrain.heightmap.height(x, y) * terrain.heightScale;
            index += 1;
        }
    }
    local body = world.newBody("static", terrain.originX, 0.0, terrain.originZ);
    body.newHeightFieldShapeFull(terrain.width, terrain.height,
        terrain.spacingX, terrain.spacingZ, heights,
        terrain.minSample * terrain.heightScale - 0.01,
        terrain.maxSample * terrain.heightScale + 0.01, false);
    terrain.body = body;
    return body;
}
