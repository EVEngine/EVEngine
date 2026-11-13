// Arbor Lab: bounded broadleaf presets, geometric petals and transferred normals.
persist treeSeed = 31415
persist plantPreset = 0
persist treeAlgorithm = "weberPenn"
persist leafMode = "clusters"
persist leafDensity = 0.85
persist leafScale = 1.0
persist crownScale = 1.0
persist plantResources = []
persist treeObject = null
persist treeCamera = null
persist treeYaw = 0.0
persist rotatePlant = false
persist inspectTrunk = false
persist trunkDetail = true
persist barkNormals = true
persist groundResources = []

function checked(result) {
    if (!result.ok) throw result.status.summary;
    return result.value;
}

function rebuildTree() {
    local bush = plantPreset >= 2;
    local height = bush ? (plantPreset == 2 ? 1.35 : 1.8) : (plantPreset == 0 ? 6.2 : 7.0);
    local radius = bush ? 1.2 : (plantPreset == 0 ? 2.3 : 1.65);
    local p = checked(procgen.newParams());
    p.setSeed(treeSeed);
    p.setString("leafMode", leafMode);
    p.setFloat("leafDensity", leafDensity);
    p.setFloat("height", height);
    p.setFloat("leafSize", (bush ? 0.065 : 0.16) * leafScale);
    if (bush) {
        p.setString("style", plantPreset == 2 ? "mound" : "sphere");
        p.setFloat("width", radius * 2.0 * crownScale);
        p.setInt("twigs", 9);
        p.setInt("branchCount", 5);
        p.setFloat("branchingStart", 0.12);
        p.setFloat("foliageStart", 0.10);
        p.setFloat("canopyNormalBlend", 0.55);
        p.setFloat("twigRadius", height * 0.010);
        p.setFloat("stemSpread", 0.24);
        p.setInt("branchLevels", 3);
        p.setInt("branchSegments", 16);
        p.setInt("radialSegments", 10);
        p.setInt("blobs", 80);
        p.setFloat("lobeScale", 1.05);
        p.setInt("clusterPlanes", 12);
        p.setInt("clusterLeaves", 40);
    } else {
        p.setString("style", "realistic");
        p.setString("branchAlgorithm", treeAlgorithm);
        p.setFloat("crownRadius", radius * crownScale);
        p.setFloat("trunkRadius", height * 0.032);
        p.setFloat("trunkCurve", 0.14);
        p.setFloat("rootFlare", 0.85);
        p.setFloat("rootFlareHeight", 0.12);
        p.setFloat("trunkTipRatio", 0.10);
        p.setInt("radialSegments", 24);
        p.setInt("surfaceSegments", 96);
        p.setFloat("trunkNoise", trunkDetail ? 0.28 : 0.0);
        p.setFloat("trunkNoiseScale", 3.0);
        p.setInt("trunkKnots", trunkDetail ? 5 : 0);
        p.setFloat("trunkKnotStrength", 0.55);
        p.setFloat("foliageStart", plantPreset == 0 ? 0.30 : 0.22);
        p.setInt("branchLevels", 3);
        p.setInt("branchCount", 10);
        p.setFloat("branchAngle", plantPreset == 0 ? 58.0 : 38.0);
        p.setFloat("clusterSize", 0.27);
        p.setFloat("clusterSeparation", 0.32);
        p.setFloat("clusterLeafScale", 1.0);
        p.setInt("clusterPlanes", 12);
        p.setInt("clusterLeaves", 96);
        p.setInt("clusterLimit", 160);
    }
    // Prepare all resources before publishing, so generation failure leaves the old plant intact.
    local cpu = checked(procgen.buildMesh(bush ? "mesh.bush" : "mesh.tree", p));
    local lo = [1e9, 1e9, 1e9];
    local hi = [-1e9, -1e9, -1e9];
    for (local i = 0; i < cpu.getVertexCount(); i += 1) {
        local xyz = [cpu.getPositionX(i), cpu.getPositionY(i), cpu.getPositionZ(i)];
        for (local axis = 0; axis < 3; axis += 1) {
            if (xyz[axis] < lo[axis]) lo[axis] = xyz[axis];
            if (xyz[axis] > hi[axis]) hi[axis] = xyz[axis];
        }
    }
    local mesh = checked(procgen.uploadMesh(cpu, gfx));
    local textureParams = checked(procgen.newParams());
    textureParams.setSeed(treeSeed + 17);
    textureParams.setSize(1024, 1024);
    textureParams.setInt("colors", 24);
    textureParams.setString("leafLayout", leafMode == "clusters" ? "single" : "cards");
    local texture = checked(procgen.generateTexture(bush ? "tex.foliage" : "tex.tree_atlas", textureParams, gfx));
    textureParams.setString("map", "normal");
    textureParams.setFloat("barkNormalStrength", 0.65);
    local surfaceImage = checked(procgen.generateImage(bush ? "tex.foliage" : "tex.tree_atlas", textureParams));
    local surface = gfx.newTexture(surfaceImage, true, true);
    local material = gfx.newMaterial();
    material.setAlbedoTexture(texture);
    if (barkNormals) material.setNormalTexture(surface);
    material.setTint(1.0, 1.0, 1.0, 1.0);
    material.setRoughness(0.88);
    material.setMetallic(0.0);
    // Rectangular cards use alpha cutoff; opaque bark shares the atlas.
    material.setSurfaceMode((leafMode == "cards" || leafMode == "clusters") ? "masked" : "opaque");
    material.setAlphaCutoff(0.4);
    if (treeObject == null) {
        treeObject = eve.Renderable3D();
        treeObject.setCastShadow(true);
        treeObject.setReceiveShadow(true);
    }
    treeObject.setMesh(mesh);
    treeObject.setMaterial(material);
    treeObject.setPosition(0.0, 0.0, 0.0);
    plantResources = [mesh, texture, surfaceImage, surface, material];
    // Fit the generated bounds, including branches and leaves beyond nominal height.
    local cx = (lo[0] + hi[0]) * 0.5;
    local cy = (lo[1] + hi[1]) * 0.5;
    local cz = (lo[2] + hi[2]) * 0.5;
    local dx = hi[0] - lo[0], dy = hi[1] - lo[1], dz = hi[2] - lo[2];
    local distance = sqrt(dx * dx + dy * dy + dz * dz) * 0.5 / sin(18.0 * 0.01745329252) * 1.08;
    treeCamera.setEye(cx + distance * 0.55, cy + distance * 0.25, cz + distance * 0.80);
    treeCamera.setTarget(cx, cy, cz);
    if (inspectTrunk) {
        treeCamera.setTarget(0.0, height * 0.24, 0.0);
        treeCamera.setEye(height * (bush ? 0.95 : 0.45), height * (bush ? 0.65 : 0.36),
                          height * (bush ? 1.4 : 0.66));
    }
    print("ARBOR preset=" + plantPreset + " seed=" + treeSeed + " mode=" + leafMode +
          " density=" + leafDensity + " leafScale=" + leafScale + " crownScale=" + crownScale + "\n");
}

function eve_init() {
    treeCamera = eve.Camera3D();
    treeCamera.setUp(0.0, 1.0, 0.0);
    treeCamera.setFov(36.0);
    treeCamera.setAmbient(0.08, 0.10, 0.12);
    treeCamera.setAutoExposure(false, -8.0, 8.0);
    treeCamera.setExposure(-0.7);
    treeCamera.setActive(true);
    // mesh3d uses the vector toward the light: positive Y lights the crown from above.
    gfx.setDirectionalLight(-0.45, 1.0, 0.55, 1.2, 1.1, 0.95);
    gfx.setBackgroundColor(0.16, 0.20, 0.23, 1.0);
    local groundMesh = gfx.newMeshCube(1.0);
    local ground = eve.Renderable3D();
    ground.setMesh(groundMesh);
    ground.setPosition(0.0, -0.04, 0.0);
    ground.setScale(10.0, 0.08, 10.0);
    ground.setTint(0.16, 0.19, 0.12, 1.0);
    ground.setRoughness(0.95);
    ground.setReceiveShadow(true);
    groundResources = [groundMesh, ground];
    rebuildTree();
    print("ARBOR: 1-4 presets; R seed; A branches; L leaves; [/] density; -/= leaf size; ,/. crown; Space rotate; B inspect trunk; N geometry detail; M bark normals\n");
}

function eve_update(dt) {
    if (rotatePlant) treeYaw += dt * 7.0;
    if (treeObject != null) treeObject.setYaw(treeYaw);
    local changed = false;
    for (local i = 1; i <= 4; i += 1) {
        if (key_just_pressed(i.tostring())) {
            plantPreset = i - 1;
            leafMode = "clusters";
            leafScale = 1.0;
            crownScale = 1.0;
            leafDensity = 0.85;
            changed = true;
        }
    }
    if (key_just_pressed("r")) { treeSeed += 1; changed = true; }
    if (key_just_pressed("a")) {
        treeAlgorithm = treeAlgorithm == "weberPenn" ? "spaceColonization" : "weberPenn";
        changed = true;
    }
    if (key_just_pressed("l")) {
        leafMode = leafMode == "clusters" ? "cards" : leafMode == "cards" ? "none" : "clusters";
        changed = true;
    }
    if (key_just_pressed("[")) { leafDensity = clampf(leafDensity - 0.05, 0.45, 1.0); changed = true; }
    if (key_just_pressed("]")) { leafDensity = clampf(leafDensity + 0.05, 0.45, 1.0); changed = true; }
    if (key_just_pressed("-")) { leafScale = clampf(leafScale - 0.1, 0.7, 1.4); changed = true; }
    if (key_just_pressed("=")) { leafScale = clampf(leafScale + 0.1, 0.7, 1.4); changed = true; }
    if (key_just_pressed(",")) { crownScale = clampf(crownScale - 0.1, 0.8, 1.2); changed = true; }
    if (key_just_pressed(".")) { crownScale = clampf(crownScale + 0.1, 0.8, 1.2); changed = true; }
    if (key_just_pressed("space")) rotatePlant = !rotatePlant;
    if (key_just_pressed("m")) { barkNormals = !barkNormals; changed = true; }
    if (key_just_pressed("n")) { trunkDetail = !trunkDetail; changed = true; }
    if (key_just_pressed("b")) { inspectTrunk = !inspectTrunk; changed = true; }
    if (changed) rebuildTree();
}

function eve_render() {
    gfx.clear();
    gfx.render3D();
}
