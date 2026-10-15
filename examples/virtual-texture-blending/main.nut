// Virtual-texture streaming (UE5 RVT-shaped): feedback → bounded decode → atomic publish.
// Layers are authored into VirtualTextureStreaming; residency follows a low-res feedback grid.

persist vtCamera = null
persist vtGround = null
persist vtMaterial = null
persist vtStreaming = null
persist vtAtlasPreview = null
persist vtTick = 0

function vtMakeLayer(r, g, b) {
    local image = eve.Image().newEmptyImageData(64, 64, "RGBA8");
    for (local y = 0; y < 64; ++y) {
        for (local x = 0; x < 64; ++x) {
            local detail = ((x / 8 + y / 8) % 2 == 0) ? 0.04 : -0.04;
            image.setPixel(x, y, r + detail, g + detail, b + detail, 1.0);
        }
    }
    return image;
}

function vtBuildStreaming() {
    // 128×64 virtual space, 64px tiles → 2×1 pages, capacity 2 physical pages + fallback.
    vtStreaming = gfx.newVirtualTextureStreaming(128, 64, 64, 2, 2, 2);
    local rock = vtMakeLayer(0.34, 0.30, 0.25);
    local moss = vtMakeLayer(0.16, 0.40, 0.18);
    local flatNormal = eve.Image().newEmptyImageData(64, 64, "RGBA8");
    for (local y = 0; y < 64; ++y)
        for (local x = 0; x < 64; ++x)
            flatNormal.setPixel(x, y, 0.5, 0.5, 1.0, 1.0);
    vtStreaming.addLayer(rock, flatNormal, 1.0, 1.0, 0.0);
    vtStreaming.addLayer(moss, flatNormal, 1.0, 1.0, 0.85);
    vtStreaming.initializePhysicalCache();

    vtMaterial = gfx.newMaterial();
    vtMaterial.setRoughness(0.78);
    vtMaterial.setMetallic(0.0);
    // Bootstrap GPU atlases (fallback only) then stream requested pages.
    local boot = vtStreaming.syncToMaterial(gfx, vtMaterial);
    if (!boot.ok) throw("vt bootstrap failed: " + boot.status.summary);
}

eve_init = function() {
    gfx.setBackgroundColor(0.035, 0.05, 0.075, 1.0);
    vtCamera = eve.Camera3D();
    vtCamera.setEye(0.0, 7.0, 9.0);
    vtCamera.setTarget(0.0, 0.0, 0.0);
    vtCamera.setFov(52.0);
    vtCamera.setAmbient(0.28, 0.30, 0.32);
    vtCamera.setActive(true);
    gfx.setDirectionalLight(-0.35, -1.0, -0.25, 1.2, 1.15, 1.05);

    local rc = gfx.getRenderControl();
    rc.disable("shadow");
    rc.disable("gbuffer");
    rc.disable("msaa");
    rc.disable("atmosphere");
    rc.compile();

    vtBuildStreaming();
    vtGround = eve.Renderable3D();
    vtGround.setMesh(gfx.newMeshCube(1.0));
    vtGround.setScale(11.0, 0.35, 7.0);
    vtGround.setMaterial(vtMaterial);
    vtGround.setReceiveShadow(true);
    print("Virtual texture streaming: feedback grid → decode budget → atomic publish\n");
};

eve_update = function(dt) {
    if (has_module("scene")) scene.updateTransformsAll();
    vtTick += 1;
    // Alternate which half of the surface is requested so eviction / residency is visible.
    if ((vtTick / 90) % 2 == 0) {
        vtStreaming.recordFeedback(0, 0, 0);
        vtStreaming.recordFeedback(1, 0, 0);
    } else {
        vtStreaming.recordFeedback(1, 0, 0);
    }
    local stats = vtStreaming.update();
    if (!stats.ok) throw("vt update failed: " + stats.status.summary);
    local sync = vtStreaming.syncToMaterial(gfx, vtMaterial);
    if (!sync.ok) throw("vt sync failed: " + sync.status.summary);
    if (vtTick % 90 == 1) {
        print("VT gen=" + vtStreaming.generation() +
              " resident=" + vtStreaming.residentPages() +
              " pages=" + vtStreaming.pageCountX() + "x" + vtStreaming.pageCountY() + "\n");
    }
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    // Status is printed occasionally from update; keep the frame free of font deps.
};
