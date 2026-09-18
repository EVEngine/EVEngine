// Close-up gallery: procedural tree / bush / cliff / stone with generated
// bark-foliage atlas and rock PBR maps — aiming at the video's material density.

persist galleryKeep = []
persist galleryCamera = null
persist galleryYaw = 0.35
persist galleryTime = 0.0
persist galleryFrame = 0
persist gallerySaved = false
persist gallerySeed = 20260917

function retain(v) { galleryKeep.append(v); return v; }

function makeTex(recipe, seed, size) {
    local pr = procgen.newParams();
    if (!pr.ok) return null;
    local p = retain(pr.value);
    p.setSeed(seed);
    p.setSize(size, size);
    p.setFloat("scale", 4.2);
    p.setInt("octaves", 5);
    p.setInt("seamless", 1);
    p.setInt("colors", 7);
    local tr = procgen.generateTexture(recipe, p, gfx);
    if (!tr.ok) return null;
    return retain(tr.value);
}

function makeNormal(recipe, seed, size) {
    local pr = procgen.newParams();
    if (!pr.ok) return null;
    local p = retain(pr.value);
    p.setSeed(seed);
    p.setSize(size, size);
    p.setFloat("scale", 4.2);
    p.setInt("octaves", 5);
    p.setInt("seamless", 1);
    local nr = procgen.generateNormalImage(recipe, p);
    if (!nr.ok) return null;
    return retain(gfx.newTexture(nr.value, true, true));
}

function makeFoliageMaterial(albedo, normal, rough, cutoff) {
    local mat = retain(gfx.newMaterial());
    if (albedo != null) mat.setAlbedoTexture(albedo);
    if (normal != null) mat.setNormalTexture(normal);
    mat.setTint(1.0, 1.0, 1.0, 1.0);
    mat.setMetallic(0.02);
    mat.setRoughness(rough);
    mat.setDoubleSided(true);
    mat.setSurfaceMode("masked");
    mat.setAlphaCutoff(cutoff);
    mat.setAlphaTechnique("cutoff");
    return mat;
}

function makeOpaqueMaterial(albedo, normal, rough) {
    local mat = retain(gfx.newMaterial());
    if (albedo != null) mat.setAlbedoTexture(albedo);
    if (normal != null) mat.setNormalTexture(normal);
    mat.setTint(1.0, 1.0, 1.0, 1.0);
    mat.setMetallic(0.02);
    mat.setRoughness(rough);
    return mat;
}

function placeMesh(mesh, x, y, z, sx, sy, sz, material) {
    local ent = retain(eve.Renderable3D());
    ent.setMesh(mesh);
    ent.setPosition(x, y, z);
    ent.setScale(sx, sy, sz);
    if (material != null) ent.setMaterial(material);
    ent.setCastShadow(true);
    ent.setReceiveShadow(true);
    return ent;
}

function buildTree(seed, x) {
    local pr = procgen.newParams();
    if (!pr.ok) return;
    local p = retain(pr.value);
    p.setSeed(seed);
    p.setString("style", "realistic");
    p.setString("branchAlgorithm", "weberPenn");
    p.setString("leafMode", "clusters");
    p.setFloat("leafDensity", 0.88);
    p.setFloat("height", 5.4);
    p.setFloat("crownRadius", 2.05);
    p.setInt("branchLevels", 3);
    p.setInt("branchCount", 9);
    p.setFloat("clusterSize", 0.26);
    p.setFloat("clusterSeparation", 0.40);
    p.setFloat("clusterLeafScale", 0.78);
    p.setInt("clusterPlanes", 14);
    p.setInt("clusterLeaves", 36);
    p.setInt("clusterLimit", 140);
    local mr = procgen.generateMesh("mesh.tree", p, gfx);
    if (!mr.ok) return;
    local atlas = makeTex("tex.tree_atlas", seed + 3, 512);
    local mat = makeFoliageMaterial(atlas, null, 0.82, 0.32);
    placeMesh(retain(mr.value), x, -2.4, 0.0, 1.0, 1.0, 1.0, mat);
}

function buildBush(seed, x) {
    local pr = procgen.newParams();
    if (!pr.ok) return;
    local p = retain(pr.value);
    p.setSeed(seed);
    p.setString("style", "mound");
    p.setString("leafMode", "cards");
    p.setFloat("height", 1.65);
    p.setFloat("width", 2.35);
    p.setInt("blobs", 12);
    p.setFloat("leafDensity", 0.85);
    p.setFloat("lobeScale", 0.64);
    p.setFloat("irregularity", 0.72);
    p.setInt("rings", 5);
    p.setInt("radialSegments", 11);
    p.setFloat("leafSize", 0.62);
    p.setInt("twigs", 7);
    local mr = procgen.generateMesh("mesh.bush", p, gfx);
    if (!mr.ok) return;
    local foliage = makeTex("tex.foliage", seed + 5, 256);
    local normal = makeNormal("tex.foliage", seed + 5, 256);
    // Masked leaf cards: tex.foliage right half is transparent + ovate leaves.
    local mat = makeFoliageMaterial(foliage, normal, 0.88, 0.32);
    if (mat != null) mat.setTint(0.72, 0.92, 0.68, 1.0);
    placeMesh(retain(mr.value), x, -2.55, 0.4, 1.15, 1.15, 1.15, mat);
}

function buildRock(seed, shape, x, scale) {
    local pr = procgen.newParams();
    if (!pr.ok) return;
    local p = retain(pr.value);
    p.setSeed(seed);
    p.setString("baseShape", shape);
    p.setInt("subdivisions", 3);
    p.setFloat("radius", 0.85);
    p.setFloat("variation", 0.55);
    local mr = procgen.generateMesh("mesh.rock", p, gfx);
    if (!mr.ok) return;
    local recipe = shape == "cliff" ? "tex.moss" : "tex.rock";
    local albedo = makeTex(recipe, seed + 7, 256);
    local normal = makeNormal(recipe, seed + 7, 256);
    local mat = makeOpaqueMaterial(albedo, normal, 0.86);
    placeMesh(retain(mr.value), x, -2.35, -0.2, scale, scale, scale, mat);
}

function buildFlower(seed, x, z, scale, tintR, tintG, tintB) {
    local pr = procgen.newParams();
    if (!pr.ok) return;
    local p = retain(pr.value);
    p.setSeed(seed);
    p.setFloat("height", 0.78);
    p.setFloat("petalLength", 0.32);
    p.setFloat("petalWidth", 0.18);
    p.setInt("petals", 6 + (seed % 2));
    p.setFloat("openAngle", 68.0 + (seed % 8));
    local mr = procgen.generateMesh("mesh.flower", p, gfx);
    if (!mr.ok) {
        print("PROCGEN_NATURE_QUALITY_FLOWER_FAIL seed=" + seed + "\n");
        return;
    }
    local albedo = makeTex("tex.flower", seed + 9, 128);
    local mat = makeFoliageMaterial(albedo, null, 0.72, 0.28);
    if (mat != null) mat.setTint(tintR, tintG, tintB, 1.0);
    placeMesh(retain(mr.value), x, -2.75, z, scale, scale, scale, mat);
    print("PROCGEN_NATURE_QUALITY_FLOWER seed=" + seed + " at " + x + "," + z + "\n");
}

function buildGround() {
    local ground = retain(eve.Renderable3D());
    ground.setMesh(gfx.newMeshCube(1.0));
    ground.setScale(14.0, 0.18, 6.0);
    ground.setPosition(0.0, -2.85, 0.0);
    local soil = makeTex("tex.soil", gallerySeed + 1, 128);
    local normal = makeNormal("tex.soil", gallerySeed + 1, 128);
    local mat = makeOpaqueMaterial(soil, normal, 0.92);
    if (mat != null) {
        mat.setTint(0.85, 0.95, 0.55, 1.0);
        ground.setMaterial(mat);
    }
    ground.setReceiveShadow(true);
}

eve_init = function() {
    gfx.setBackgroundColor(0.38, 0.55, 0.72, 1.0);
    // Strong warm key light + cooler fill ambient — closer to the video's
    // late-afternoon contrast than a flat studio look.
    gfx.setDirectionalLight(-0.72, 0.68, 0.18, 2.55, 2.05, 1.35);

    galleryCamera = eve.Camera3D();
    galleryCamera.setFov(34.0);
    galleryCamera.setAmbient(0.14, 0.16, 0.20);
    galleryCamera.setClipPlanes(0.1, 80.0);
    galleryCamera.setActive(true);

    buildGround();
    buildTree(gallerySeed, -4.2);
    buildBush(gallerySeed + 11, -1.4);
    buildRock(gallerySeed + 23, "cliff", 1.5, 1.55);
    buildRock(gallerySeed + 41, "boulder", 4.4, 1.25);
    // Sparse flower accents in front of the bush / rock (Flower01-03 vibe).
    buildFlower(gallerySeed + 61, -2.55, 1.45, 1.45, 1.15, 0.28, 0.38);
    buildFlower(gallerySeed + 67, -0.85, 1.65, 1.30, 0.38, 0.48, 1.15);
    buildFlower(gallerySeed + 71, -1.75, 1.95, 1.20, 1.05, 1.02, 0.95);
    buildFlower(gallerySeed + 79, 0.35, 1.35, 1.35, 1.15, 0.95, 0.22);
    buildFlower(gallerySeed + 83, -3.15, 1.15, 1.15, 1.05, 0.45, 0.72);
    buildFlower(gallerySeed + 89, 0.95, 0.85, 1.10, 0.55, 0.75, 1.10);

    print("PROCGEN_NATURE_QUALITY_READY seed=" + gallerySeed + "\n");
};

eve_update = function(dt) {
    galleryTime += dt;
    galleryFrame += 1;
    galleryYaw += dt * 0.18;
    gfx.setCloudShadows(0.16, 4.5, galleryTime * 0.1, 0.25, 0.35, 0.45, 0.55);

    local eyeX = 0.2 + 9.5 * sin(galleryYaw);
    local eyeZ = 9.5 * cos(galleryYaw);
    galleryCamera.setEye(eyeX, 2.8, eyeZ);
    galleryCamera.setTarget(0.2, -1.4, 0.0);

    if (key_just_pressed("r") || key_just_pressed("R")) {
        gallerySeed = procgen.randomSeed();
        // Soft reset: hide old entities by rebuilding from a fresh retain list is
        // expensive; advance seed and rely on rerun for a clean gallery.
        print("PROCGEN_NATURE_QUALITY_SEED " + gallerySeed + " (restart example to rebuild)\n");
    }
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    if (!gallerySaved && galleryFrame > 36 && galleryTime > 2.2) {
        if (gfx.saveFramePng("procgen-nature-quality.png")) {
            gallerySaved = true;
            print("PROCGEN_NATURE_QUALITY_SCREENSHOT procgen-nature-quality.png\n");
        }
    }
};
