// Procedural bushes crossing global height fog and two analytic local fog volumes.
// 1/2/3/4 select fixed camera checks; V cycles views; F toggles all fog.

persist bfCamera = null
persist bfFog = null
persist bfObjects = []
persist bfVolumes = []
persist bfView = 0
persist bfFogEnabled = true
persist bfTexture = null
persist bfElapsed = 0.0
persist bfCapturePending = false
persist bfCaptureName = "bush-fog-volumes.png"

bfViews <- [
    { ex = 10.5, ey = 5.6, ez = 13.5, tx = 0.0, ty = 1.0, tz = -3.0 },
    { ex = -12.0, ey = 4.2, ez = 8.0, tx = 0.5, ty = 1.0, tz = -4.0 },
    { ex = 8.0, ey = 3.0, ez = -16.0, tx = 0.0, ty = 0.9, tz = -3.5 },
    { ex = 0.0, ey = 10.5, ez = 10.0, tx = 0.0, ty = 0.0, tz = -4.0 }
];

function bfMaterial() {
    local mat = gfx.newMaterial();
    mat.setAlbedoTexture(bfTexture);
    mat.setTint(1.0, 1.0, 1.0, 1.0);
    mat.setRoughness(0.88);
    mat.setDoubleSided(true);
    // Foliage must write depth. Whole-bush alpha blending exposes every
    // overlapping leaf shell and produces a soft, doubled silhouette in fog.
    mat.setSurfaceMode("masked");
    mat.setAlphaCutoff(0.35);
    mat.setAlphaTechnique("coverage");
    return mat;
}

function bfBush(seed, x, z, scale) {
    local p = procgen.newParams();
    p.setSeed(seed);
    p.setString("style", "mound");
    p.setString("leafMode", "mixed");
    p.setFloat("leafDensity", 0.78);
    p.setFloat("height", 1.9);
    p.setFloat("width", 2.8);
    p.setInt("blobs", 13);
    p.setInt("rings", 5);
    p.setInt("radialSegments", 12);
    p.setFloat("leafSize", 0.34);
    p.setInt("twigs", 7);
    local object = eve.Renderable3D();
    object.setMesh(procgen.generateMesh("mesh.bush", p, gfx));
    object.setPosition(x, 0.0, z);
    object.setScale(scale, scale, scale);
    object.setYaw((seed % 11) * 0.31);
    object.setMaterial(bfMaterial());
    object.setCastShadow(true);
    object.setReceiveShadow(true);
    bfObjects.push(object);
}

function bfSolid(mesh, x, y, z, sx, sy, sz, r, g, b) {
    local object = eve.Renderable3D();
    object.setMesh(mesh);
    object.setPosition(x, y, z);
    object.setScale(sx, sy, sz);
    object.setTint(r, g, b, 1.0);
    object.setRoughness(0.92);
    object.setCastShadow(true);
    object.setReceiveShadow(true);
    bfObjects.push(object);
}

function bfMakeVolume(shape, x, y, z, sx, sy, sz, extinction, r, g, b) {
    local volume = eve.FogVolume();
    volume.setShape(shape);
    volume.setPosition(x, y, z);
    volume.setSize(sx, sy, sz);
    volume.setExtinction(extinction);
    volume.setAlbedo(r, g, b);
    volume.setAnisotropy(0.18);
    volume.setEdgeFalloff(0.42);
    bfVolumes.push(volume);
}

function bfRebuildFog() {
    local v = bfViews[bfView];
    bfFog.setCamera(v.ex, v.ey, v.ez, v.tx, v.ty, v.tz,
                    0.0, 1.0, 0.0, 48.0, 1.7777778, 0.1, 70.0);
    bfFog.configureFroxelGrid(96, 54, 40, 0.1, 70.0);
    bfFog.clearFroxelGrid();
    // A light global layer establishes atmospheric depth between bush rows.
    bfFog.injectFroxelHeightFog(0.010, 0.68, 0.78, 0.82, 0.0, 0.20, -1.0, 7.0);
    foreach (volume in bfVolumes) bfFog.injectFroxelLocalVolume(volume);
    bfFog.integrateFroxel(1.05, 1.00, 0.88, 1.0);
    bfFog.uploadFroxel(gfx);
}

function bfSetView(index) {
    bfView = index;
    local v = bfViews[bfView];
    bfCamera.setEye(v.ex, v.ey, v.ez);
    bfCamera.setTarget(v.tx, v.ty, v.tz);
    bfRebuildFog();
    print("Bush fog camera " + (bfView + 1) + "/4\n");
}

eve_init = function() {
    gfx.setBackgroundColor(0.055, 0.085, 0.105, 1.0);
    gfx.setDirectionalLight(-0.45, -1.0, -0.30, 1.25, 1.17, 1.02);
    bfTexture = gfx.newTextureFromFile("assets/bush_atlas.png");

    bfCamera = eve.Camera3D();
    bfCamera.setUp(0.0, 1.0, 0.0);
    bfCamera.setFov(48.0);
    bfCamera.setAmbient(0.27, 0.33, 0.31);
    bfCamera.setActive(true);

    local cube = gfx.newMeshCube(1.0);
    bfSolid(cube, 0.0, -0.30, -4.0, 16.0, 0.5, 20.0, 0.16, 0.20, 0.17);
    bfSolid(cube, -5.8, 1.0, -7.5, 0.55, 2.6, 0.55, 0.48, 0.31, 0.20);
    bfSolid(cube, 5.6, 1.4, -10.5, 0.65, 3.4, 0.65, 0.35, 0.43, 0.47);

    bfBush(20260821, -4.7, 1.0, 0.95);
    bfBush(20260822, -1.8, -1.8, 1.12);
    bfBush(20260823, 1.3, -4.5, 1.00);
    bfBush(20260824, 4.1, -7.0, 1.18);
    bfBush(20260825, -3.0, -10.5, 1.20);
    bfBush(20260826, 2.2, -13.0, 1.28);

    // Warm spherical mist crosses the near transparent bush; cool cylinder
    // crosses the middle/far row so their silhouettes reveal local density.
    bfMakeVolume("sphere", -1.2, 1.1, -2.4, 5.8, 3.8, 5.8,
                 0.085, 0.92, 0.76, 0.58);
    bfMakeVolume("cylinder", 3.0, 1.3, -8.7, 5.2, 4.5, 5.2,
                 0.070, 0.56, 0.76, 0.92);

    local rc = gfx.getRenderControl();
    rc.enable("gbuffer");
    rc.enable("atmosphere");
    rc.enable("volumetricFog");
    rc.compile();
    bfFog = gfx.newVolumetric();
    bfFog.setMode("froxel");
    bfFog.setQuality("medium");
    bfSetView(0);
    print("Bush fog volumes: 1-4 camera, V cycle, F fog toggle\n");
};

eve_update = function(dt) {
    bfElapsed += dt;
    for (local i = 0; i < 4; ++i)
        if (key_just_pressed((i + 1).tostring())) bfSetView(i);
    if (key_just_pressed("v") || key_just_pressed("V")) bfSetView((bfView + 1) % 4);
    if (key_just_pressed("f") || key_just_pressed("F")) bfFogEnabled = !bfFogEnabled;
    if (key_just_pressed("p") || key_just_pressed("P")) {
        bfCaptureName = "bush-fog-view-" + (bfView + 1) + ".png";
        bfCapturePending = true;
    }
};

eve_render = function() {
    // saveFramePng enables readback on its first call and succeeds on a later
    // presented frame, so keep retrying after P until the view is saved.
    if (bfCapturePending && bfElapsed > 1.5 &&
        gfx.saveFramePng(bfCaptureName)) {
        bfCapturePending = false;
        print("Saved " + bfCaptureName + "\n");
    }
    gfx.clear();
    gfx.render3D();
    if (bfFogEnabled) {
        local depth = gfx.getRenderControl().getGBuffer().getDepthTexture();
        bfFog.applyFroxel(gfx, depth);
    }
};
