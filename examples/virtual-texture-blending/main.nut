// Virtual-texture material blending: two resident pages plus a low-resolution fallback.
// The page gutters repeat the neighbouring edge color, demonstrating seam-free filtering.

persist vtCamera = null
persist vtGround = null
persist vtMaterial = null
persist vtResources = []

function vtFillSlot(image, slot, slotSize, border, pageIndex) {
    for (local y = 0; y < slotSize; ++y) {
        for (local x = 0; x < slotSize; ++x) {
            local localX = x - border;
            if (localX < 0) localX = 0;
            if (localX >= slotSize - border * 2) localX = slotSize - border * 2 - 1;
            local globalU = (pageIndex * (slotSize - border * 2) + localX).tofloat() /
                            ((slotSize - border * 2) * 2 - 1).tofloat();
            local blend = globalU * globalU * (3.0 - 2.0 * globalU);
            local detail = ((localX / 8 + y / 8) % 2 == 0) ? 0.035 : -0.035;
            local rockR = 0.34 + detail; local rockG = 0.30 + detail; local rockB = 0.25 + detail;
            local mossR = 0.16 + detail; local mossG = 0.40 + detail; local mossB = 0.18 + detail;
            image.setPixel(slot * slotSize + x, y,
                rockR * (1.0 - blend) + mossR * blend,
                rockG * (1.0 - blend) + mossG * blend,
                rockB * (1.0 - blend) + mossB * blend, 1.0);
        }
    }
}

function vtBuildMaterial() {
    local tile = 64;
    local border = 2;
    local slotSize = tile + border * 2;
    local atlas = eve.Image().newEmptyImageData(slotSize * 3, slotSize, "RGBA8");
    // Slot zero is the always-resident fallback; slots one and two are physical pages.
    vtFillSlot(atlas, 0, slotSize, border, 0);
    vtFillSlot(atlas, 1, slotSize, border, 0);
    vtFillSlot(atlas, 2, slotSize, border, 1);

    local normal = eve.Image().newEmptyImageData(slotSize * 3, slotSize, "RGBA8");
    for (local y = 0; y < slotSize; ++y)
        for (local x = 0; x < slotSize * 3; ++x)
            normal.setPixel(x, y, 0.5, 0.5, 1.0, 1.0);

    local pageTable = eve.Image().newEmptyImageData(2, 1, "RGBA8");
    pageTable.setPixel(0, 0, 0.5, 0.5, 1.0, 1.0);       // physical slot 1 / 3
    pageTable.setPixel(1, 0, 0.833333, 0.5, 1.0, 1.0);  // physical slot 2 / 3

    local atlasTexture = gfx.newTexture(atlas, false, false);
    local normalTexture = gfx.newTexture(normal, false, false);
    local tableTexture = gfx.newTexture(pageTable, false, false);
    vtResources = [atlas, normal, pageTable, atlasTexture, normalTexture, tableTexture];

    vtMaterial = gfx.newMaterial();
    vtMaterial.setVirtualTexture(atlasTexture, normalTexture, tableTexture,
                                 2, 1, 3, 1, border.tofloat() / slotSize.tofloat());
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
    print("Virtual texture: 2 pages, 2px gutters, fallback slot, derivative-safe sampling\n");
};

eve_update = function(dt) {
    if (has_module("scene")) scene.updateTransformsAll();
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    // Resident-atlas inset: fallback + both bordered physical pages. This also
    // remains visible in readback paths that currently omit the 3D scene color.
    gfx.drawSolidRect(42.0, 542.0, 842.0, 148.0, 0.015, 0.02, 0.03, 0.94);
    gfx.drawTexturedRect(vtResources[3], 50.0, 550.0, 826.0, 132.0,
                         1.0, 1.0, 1.0, 1.0);
};
