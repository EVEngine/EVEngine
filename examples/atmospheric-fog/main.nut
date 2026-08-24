// Atmospheric froxel fog: a complete scene-depth -> volume-atlas -> composite example.
// 1/2/3 select thin, medium and dense fog. Space toggles the effect.

if (!("fogDemoCamera" in getroottable())) fogDemoCamera <- null;
if (!("fogDemoVolume" in getroottable())) fogDemoVolume <- null;
if (!("fogDemoObjects" in getroottable())) fogDemoObjects <- [];
if (!("fogDemoEnabled" in getroottable())) fogDemoEnabled <- true;
if (!("fogDemoPreset" in getroottable())) fogDemoPreset <- 2;
if (!("fogDemoSpaceDown" in getroottable())) fogDemoSpaceDown <- false;

function fogDemoObject(mesh, x, y, z, sx, sy, sz, r, g, b) {
    local object = eve.Renderable3D();
    object.setMesh(mesh);
    object.setPosition(x, y, z);
    object.setScale(sx, sy, sz);
    object.setTint(r, g, b, 1.0);
    object.setRoughness(0.72);
    object.setCastShadow(true);
    object.setReceiveShadow(true);
    fogDemoObjects.push(object);
    return object;
}

function rebuildFog() {
    // The grid is deliberately small: each XY cell stores 32 logarithmic depth slices.
    // uploadFroxel packs those slices into a 2D atlas understood by the Vulkan shader.
    fogDemoVolume.configureFroxelGrid(80, 45, 32, 0.1, 100.0);
    fogDemoVolume.clearFroxelGrid();

    local extinction = fogDemoPreset == 1 ? 0.012 :
        (fogDemoPreset == 2 ? 0.035 : 0.075);
    local falloff = fogDemoPreset == 3 ? 0.09 : 0.16;
    fogDemoVolume.injectFroxelHeightFog(
        extinction,       // extinction at base height
        0.72, 0.82, 0.98, // single-scattering albedo / fog tint
        0.0,              // base world height
        falloff,
        -2.0, 10.0);      // world-height range represented by grid rows

    // Incident sun + sky radiance. DayNight can drive these values in a game;
    // constants keep this example focused on the froxel API.
    fogDemoVolume.integrateFroxel(1.18, 1.05, 0.82, 1.0);
    fogDemoVolume.uploadFroxel(gfx);
}

eve_init = function() {
    gfx.setBackgroundColor(0.045, 0.075, 0.13, 1.0);

    fogDemoCamera = eve.Camera3D();
    fogDemoCamera.setEye(0.0, 5.0, 15.0);
    fogDemoCamera.setTarget(0.0, 1.4, -15.0);
    fogDemoCamera.setFov(55.0);
    fogDemoCamera.setAmbient(0.20, 0.25, 0.34);

    local cube = gfx.newMeshCube(1.0);
    fogDemoObject(cube, 0.0, -0.65, -18.0, 24.0, 0.5, 55.0, 0.16, 0.20, 0.15);
    for (local row = 0; row < 6; ++row) {
        local z = 2.0 - row * 8.0;
        fogDemoObject(cube, -4.2, 1.0, z, 1.5, 3.2, 1.5,
                      0.72, 0.26 + row * 0.045, 0.16);
        fogDemoObject(cube, 4.2, 1.0, z - 3.0, 1.5, 3.2, 1.5,
                      0.16, 0.36 + row * 0.05, 0.72);
    }

    local rc = gfx.getRenderControl();
    rc.enable("gbuffer");             // applyFroxel needs linear scene depth
    rc.enable("atmosphere");
    rc.enable("volumetricFog");
    rc.compile();

    fogDemoVolume = gfx.newVolumetric();
    fogDemoVolume.setMode("froxel");
    fogDemoVolume.setQuality("medium");
    fogDemoVolume.setCamera(0.0, 5.0, 15.0, 0.0, 1.4, -15.0,
                            0.0, 1.0, 0.0, 55.0, 1.7777778, 0.1, 100.0);
    rebuildFog();
    print("Atmospheric fog: 1 thin, 2 medium, 3 dense, Space toggle\n");
};

eve_update = function(dt) {
    for (local i = 1; i <= 3; ++i) {
        if (key_just_pressed(i.tostring())) {
            fogDemoPreset = i;
            rebuildFog();
        }
    }
    local down = keyboard.isDown("space");
    if (down && !fogDemoSpaceDown) fogDemoEnabled = !fogDemoEnabled;
    fogDemoSpaceDown = down;
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();

    // Composite after opaque 3D. The shader samples the same GBuffer linear depth,
    // so near columns receive less fog than distant columns and the sky uses far depth.
    if (fogDemoEnabled) {
        local depth = gfx.getRenderControl().getGBuffer().getDepthTexture();
        fogDemoVolume.applyFroxel(gfx, depth);
    }
};
