// Virtual-texture material: author layers on the material; the engine streams residency on bind.

persist vtCamera = null
persist vtGround = null
persist vtMaterial = null

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

function vtBuildMaterial() {
    vtMaterial = gfx.newMaterial();
    // 128×64 virtual space, 64px tiles → 2×1 pages; capacity 2 + substitute slot; 2 decodes/frame.
    vtMaterial.enableVirtualTexture(128, 64, 64, 2, 2, 2);
    local rock = vtMakeLayer(0.34, 0.30, 0.25);
    local moss = vtMakeLayer(0.16, 0.40, 0.18);
    local flatNormal = eve.Image().newEmptyImageData(64, 64, "RGBA8");
    for (local y = 0; y < 64; ++y)
        for (local x = 0; x < 64; ++x)
            flatNormal.setPixel(x, y, 0.5, 0.5, 1.0, 1.0);
    vtMaterial.addVirtualTextureLayer(rock, flatNormal, 1.0, 1.0, 0.0);
    vtMaterial.addVirtualTextureLayer(moss, flatNormal, 1.0, 1.0, 0.85);
    vtMaterial.setRoughness(0.78);
    vtMaterial.setMetallic(0.0);
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

    vtBuildMaterial();
    vtGround = eve.Renderable3D();
    vtGround.setMesh(gfx.newMeshCube(1.0));
    vtGround.setScale(11.0, 0.35, 7.0);
    vtGround.setMaterial(vtMaterial);
    vtGround.setReceiveShadow(true);
    print("Virtual texture: layers authored on material; engine streams on bind\n");
};

eve_update = function(dt) {
    if (has_module("scene")) scene.updateTransformsAll();
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
};
