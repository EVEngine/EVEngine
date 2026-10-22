// Native procgen vegetation; fixed seeds, shared mesh variants, terrain-rooted placement.
persist skyVegetationKeep = []
persist skyGrassShader = null
persist skyVegetationTime = 0.0

function vegetationKeep(value) { skyVegetationKeep.push(value); return value; }

function vegetationHash(x, z, seed) {
    local n = math.polarY(1.0, x * 127.1 + z * 311.7 + seed * 13.9) * 43758.5453;
    return n - floor(n);
}

function vegetationBankDistance(x, z) {
    local nearest = 10000.0;
    foreach (r in riverSamples) {
        local dx = x - r.x, dz = z - r.z;
        local d = sqrt(dx * dx + dz * dz) - r.width;
        if (d < nearest) nearest = d;
    }
    return nearest;
}

function vegetationSurfaceHeight(x, z, hm, cell) {
    // Use the same heightfield as the rendered ground, including its clipping.
    return hm.sampleBilinear((x + 18.0) / cell, (z + 18.0) / cell) * 8.0 - 1.5;
}

function vegetationTreeVariant(seed) {
    local p = vegetationKeep(riverRequire(procgen.newParams()));
    p.setSeed(seed);
    p.setString("style", "realistic");
    p.setString("branchAlgorithm", "weberPenn");
    p.setString("leafMode", "clusters");
    p.setFloat("height", 5.0 + (seed % 3) * 0.45);
    p.setFloat("crownRadius", 1.75 + (seed % 2) * 0.3);
    p.setFloat("trunkRadius", 0.17);
    p.setFloat("foliageStart", 0.30);
    p.setFloat("branchAngle", 67.0);
    p.setFloat("branchCurve", 0.20);
    p.setFloat("droop", 0.22);
    p.setInt("branchLevels", 3);
    p.setInt("branchCount", 9);
    p.setFloat("leafDensity", 0.95);
    p.setFloat("leafSize", 0.32);
    p.setFloat("clusterSize", 0.34);
    p.setFloat("clusterSeparation", 0.38);
    p.setFloat("clusterLeafScale", 1.05);
    p.setInt("clusterPlanes", 10);
    p.setInt("clusterLeaves", 28);
    p.setInt("clusterLimit", 100);
    return vegetationKeep(riverRequire(procgen.generateMesh("mesh.tree", p, gfx)));
}

function buildNativeTrees(hm, cell) {
    local p = vegetationKeep(riverRequire(procgen.newParams()));
    p.setSeed(84031);
    p.setSize(512, 512);
    p.setInt("colors", 12);
    local atlas = vegetationKeep(riverRequire(procgen.generateTexture("tex.tree_atlas", p, gfx)));
    local material = vegetationKeep(gfx.newMaterial());
    material.setAlbedoTexture(atlas);
    material.setTint(0.48, 0.62, 0.40, 1.0);
    material.setRoughness(0.88);
    material.setSurfaceMode("masked");
    material.setAlphaCutoff(0.32);
    material.setAlphaTechnique("cutoff");
    material.setDoubleSided(true);
    material.setCastShadow(true);
    material.setReceiveShadow(true);
    local variants = [];
    for (local i = 0; i < 5; ++i) variants.push(vegetationTreeVariant(4201 + i * 37));
    local count = 0;
    for (local row = 0; row < 5; ++row) for (local col = 0; col < 8; ++col) {
        local x = -15.5 + col * 4.4 + vegetationHash(col, row, 5) * 1.1;
        local z = -15.2 + row * 5.1 + vegetationHash(col, row, 9) * 1.3;
        if (vegetationBankDistance(x, z) < 3.0) continue;
        // Preserve the central falls silhouette and near-camera river corridor.
        if (z > -8.0 && z < -4.0 && abs(x) < 5.0) continue;
        local scale = 0.60 + vegetationHash(col, row, 11) * 0.32;
        local tree = addObject(variants[count % variants.len()], x,
            vegetationSurfaceHeight(x, z, hm, cell) - 0.04, z,
            scale, scale, scale, 1.0, 1.0, 1.0, 0.88);
        tree.setMaterial(material);
        tree.setYaw(vegetationHash(col, row, 13) * 360.0);
        tree.setCastShadow(true);
        ++count;
    }
    print("LIVING_SKY_PROCGEN_TREES count=" + count + " variants=5 atlas=tex.tree_atlas\n");
}

// A blade has four folded cross-sections and one sharp tip: 13 vertices, 14 triangles.
function appendGrassBlade(pos, nrm, uv, indices, x, y, z, height, width, angle, lean, tint) {
    local vertexBase = pos.len() / 3;
    local ax = math.polarX(1.0, angle), az = math.polarY(1.0, angle);
    local lx = -az, lz = ax;
    for (local ring = 0; ring < 4; ++ring) {
        local t = ring / 4.0;
        local bend = height * lean * t * t;
        local halfWidth = width * (1.0 - t) * (1.0 - t * 0.35);
        for (local edge = -1; edge <= 1; ++edge) {
            local crease = edge == 0 ? halfWidth * 0.32 : 0.0;
            pos.extend([x + lx * (bend + crease) + ax * halfWidth * edge,
                y + height * (t - 0.16 * t * t),
                z + lz * (bend + crease) + az * halfWidth * edge]);
            local nx = lx + ax * edge * 0.36, ny = 0.18 + lean * t, nz = lz + az * edge * 0.36;
            local length = sqrt(nx * nx + ny * ny + nz * nz);
            nrm.extend([nx / length, ny / length, nz / length]);
            uv.extend([tint, t]);
        }
    }
    pos.extend([x + lx * height * lean, y + height * 0.84, z + lz * height * lean]);
    nrm.extend([lx * 0.7, 0.714, lz * 0.7]);
    uv.extend([tint, 1.0]);
    for (local ring = 0; ring < 3; ++ring) for (local side = 0; side < 2; ++side) {
        local a = vertexBase + ring * 3 + side, b = a + 1, c = a + 3;
        indices.extend([a, b, c, b, c + 1, c]);
    }
    indices.extend([vertexBase + 9, vertexBase + 10, vertexBase + 12, vertexBase + 10, vertexBase + 11, vertexBase + 12]);
}

function buildNativeGrass(hm, cell) {
    local points = vegetationKeep(riverRequire(procgen.newPointSet()));
    // Jittered roots plus broad and small patches avoid a lawn-like lattice.
    for (local iz = 0; iz < 90; ++iz) for (local ix = 0; ix < 90; ++ix) {
        local x = -17.2 + (ix + vegetationHash(ix, iz, 31)) * 0.38;
        local z = -17.2 + (iz + vegetationHash(ix, iz, 37)) * 0.38;
        if (vegetationBankDistance(x, z) < 0.65) continue;
        local y = vegetationSurfaceHeight(x, z, hm, cell);
        local dx = vegetationSurfaceHeight(x + 0.15, z, hm, cell) - y;
        local dz = vegetationSurfaceHeight(x, z + 0.15, hm, cell) - y;
        if (sqrt(dx * dx + dz * dz) > 0.13) continue;
        local broad = 0.5 + 0.5 * math.polarY(1.0, x * 0.43 + math.polarX(1.0, z * 0.37) * 2.0);
        local fine = 0.5 + 0.5 * math.polarY(1.0, x * 1.7 - z * 1.13);
        if (vegetationHash(ix, iz, 41) > 0.15 + broad * 0.58 + fine * 0.16) continue;
        local i = points.add(x, y - 0.025, z);
        local h = 0.27 + vegetationHash(ix, iz, 43) * 0.43;
        points.setScale(i, 1.0, h, 1.0);
    }
    points.assignPointIds("1739");
    local pos = [], normals = [], uv = [], indices = [];
    local bladeCount = 0;
    for (local i = 0; i < points.getCount(); ++i) {
        local x = points.getX(i), z = points.getZ(i);
        local count = 9 + (vegetationHash(i, 0, 79) * 7.0).tointeger();
        for (local blade = 0; blade < count; ++blade) {
            local angle = vegetationHash(i, blade, 83) * 6.2831853;
            local radius = sqrt(vegetationHash(i, blade, 89)) * 0.21;
            local bx = x + math.polarX(radius, angle), bz = z + math.polarY(radius, angle);
            local y = vegetationSurfaceHeight(bx, bz, hm, cell) - 0.025;
            local h = points.getScaleY(i) * (0.65 + vegetationHash(i, blade, 97) * 0.7);
            local width = 0.014 + vegetationHash(i, blade, 101) * 0.017;
            appendGrassBlade(pos, normals, uv, indices, bx, y, bz, h, width,
                angle, 0.25 + vegetationHash(i, blade, 103) * 0.65,
                vegetationHash(i, blade, 107));
            ++bladeCount;
        }
    }
    local mesh = vegetationKeep(gfx.newMeshFromArrays(pos, normals, uv, pos.len() / 3, indices, indices.len()));
    skyGrassShader = vegetationKeep(gfx.newMeshShaderVF(
        fs.readText("shaders/grass_blades.vert"), fs.readText("shaders/grass_blades.frag")));
    skyGrassShader.declareFloat("time");
    skyGrassShader.declareFloat("wind");
    skyGrassShader.sendFloat("time", 0.0);
    skyGrassShader.sendFloat("wind", 0.025);
    local material = vegetationKeep(gfx.newMaterial());
    material.setShader(skyGrassShader);
    material.setDoubleSided(true);
    material.setSurfaceMode("transparent");
    material.setDepthWrite(true);
    material.setRoughness(0.95);
    local bladeObject = eve.Renderable3D();
    bladeObject.setMesh(mesh);
    skyProps.push(bladeObject);
    bladeObject.setMaterial(material);
    print("LIVING_SKY_GEOMETRY_GRASS roots=" + points.getCount() + " blades=" + bladeCount +
        " vertices=" + pos.len() / 3 + " triangles=" + indices.len() / 3 + " billboard=false\n");
}

function updateNativeVegetation(dt) {
    skyVegetationTime += dt;
    skyGrassShader.sendFloat("time", skyVegetationTime);
    skyGrassShader.sendFloat("wind", 0.012 + skyPresets[skyWeather].wind * 0.004);
}
