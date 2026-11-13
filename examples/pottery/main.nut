// See docs/usr/modules/procgen/pottery.md for curve controls and material recipes.
persist potteryResources = []
persist potteryCamera = null
persist potteryTime = 0.0
function keepPottery(value) { potteryResources.append(value); return value; }
function potteryValue(result) {
    if (!result.ok) throw result.status.summary;
    return keepPottery(result.value);
}
persist potteryBodyMaps = null
persist potteryShaders = []
persist potteryDebug = 0
persist potteryOrbit = true

function potteryMaps(recipe, seed) {
    local params = potteryValue(procgen.newParams());
    params.setSize(512, 512);
    params.setSeed(seed);
    params.setInt("colors", 64);
    local defaults = procgen.applyPbrRecipeDefaults(recipe, params);
    if (!defaults.ok) throw defaults.status.summary;
    return potteryValue(procgen.generatePbrMaterial(recipe, params));
}
function potteryPasteTile(atlas, source, tile) {
    local size = source.getWidth();
    local x = tile * (size + 2);
    atlas.paste(source, x + 1, 1, 0, 0, size, size);
    atlas.paste(source, x, 1, size - 1, 0, 1, size);
    atlas.paste(source, x + size + 1, 1, 0, 0, 1, size);
    atlas.paste(source, x + 1, 0, 0, size - 1, size, 1);
    atlas.paste(source, x + 1, size + 1, 0, 0, size, 1);
    atlas.paste(source, x, 0, size - 1, size - 1, 1, 1);
    atlas.paste(source, x + size + 1, 0, 0, size - 1, 1, 1);
    atlas.paste(source, x, size + 1, size - 1, 0, 1, 1);
    atlas.paste(source, x + size + 1, size + 1, 0, 0, 1, 1);
}
function potteryAtlas(bodyImage, glaze) {
    local size = bodyImage.getWidth();
    local atlas = keepPottery(eve.Image().newEmptyImageData((size + 2) * 2, size + 2, "RGBA8"));
    potteryPasteTile(atlas, bodyImage, 0);
    potteryPasteTile(atlas, glaze, 1);
    local texture = keepPottery(gfx.newTexture(atlas, false, false));
    gfx.setTextureSampler(texture, "linear", "none", 1.0, 0.0);
    return texture;
}
function potteryMaterial(recipe, seed, minimumY, maximumY, baseEnd) {
    if (potteryBodyMaps == null) potteryBodyMaps = potteryMaps("pbr.terracotta", 20261111);
    local glaze = potteryMaps(recipe, seed);
    local mat = keepPottery(gfx.newMaterial());
    mat.setAlbedoTexture(potteryAtlas(potteryBodyMaps.getAlbedo(), glaze.getAlbedo()));
    mat.setNormalTexture(potteryAtlas(potteryBodyMaps.getNormal(), glaze.getNormal()));
    // This custom fragment program uses the existing height slot for a scalar roughness atlas.
    mat.setHeightTexture(potteryAtlas(potteryBodyMaps.getRoughness(), glaze.getRoughness()));
    local shader = potteryValue(gfx.loadMeshShaderSpv(
        "shaders/pottery_height.vert.spv", "shaders/pottery_height.frag.spv"));
    local names = ["minimumY", "heightRange", "baseEnd", "blendWidth", "rimStart",
                   "noiseAmount", "noiseScale", "seed", "rimDarkening", "debugView", "rimStrength"];
    local glazed = recipe != "pbr.terracotta" && recipe != "pbr.stoneware";
    local values = [minimumY, maximumY - minimumY, baseEnd, 0.07, 0.91,
                    0.09, 5.0, (seed % 10007).tofloat(), 0.24, 0.0, glazed ? 1.0 : 0.25];
    for (local i = 0; i < names.len(); ++i) {
        shader.declareFloat(names[i]);
        shader.sendFloat(names[i], values[i]);
    }
    potteryShaders.append(shader);
    mat.setShader(shader);
    mat.setMetallic(0.0);
    return mat;
}
function potteryObject(shape, finish, x, z, custom, seed, baseEnd) {
    local params = potteryValue(procgen.newParams());
    params.setString("shape", shape);
    params.setString("material", finish);
    if (custom) {
        params.setFloat("vesselHeight", 1.4);
        params.setFloat("radius0", 0.4);
        params.setFloat("radius1", 1.25);
        params.setFloat("radius2", 0.35);
        params.setFloat("radius3", 0.55);
    }
    local build = potteryValue(procgen.buildMesh("mesh.vessel", params));
    local minimumY = build.getPositionY(0), maximumY = minimumY;
    local vertexCount = build.getVertexCount();
    for (local i = 1; i < vertexCount; ++i) {
        local y = build.getPositionY(i);
        if (y < minimumY) minimumY = y;
        if (y > maximumY) maximumY = y;
    }
    local mesh = potteryValue(procgen.uploadMesh(build, gfx));
    local object = keepPottery(eve.Renderable3D());
    object.setMesh(mesh);
    object.setMaterial(potteryMaterial("pbr." + finish, seed, minimumY, maximumY, baseEnd));
    object.setPosition(x, 0.0, z);
    object.setCastShadow(true);
    object.setReceiveShadow(true);
}
eve_init = function() {
    gfx.setBackgroundColor(0.12, 0.14, 0.17, 1.0);
    gfx.setDirectionalLight(-0.5, 0.8, 0.4, 1.2, 1.16, 1.1);
    potteryCamera = eve.Camera3D();
    potteryCamera.setFov(38.0);
    potteryCamera.setClipPlanes(0.1, 60.0);
    potteryCamera.setAmbient(0.12, 0.14, 0.16);
    potteryCamera.setActive(true);
    local plinth = keepPottery(eve.Renderable3D());
    plinth.setMesh(keepPottery(gfx.newMeshCube(1.0)));
    plinth.setScale(8.5, 0.1, 5.8);
    plinth.setPosition(0.0, -0.05, 0.0);
    local plinthMaterial = keepPottery(gfx.newMaterial());
    plinthMaterial.setTint(0.22, 0.24, 0.27, 1.0);
    plinthMaterial.setMetallic(0.0);
    plinthMaterial.setRoughness(0.85);
    plinth.setMaterial(plinthMaterial);
    plinth.setReceiveShadow(true);
    potteryObject("planter", "terracotta", -2.7, -1.4, false, 20261113, 0.2);
    potteryObject("jar", "glazed_ceramic", 0.0, -1.4, false, 20261114, 0.24);
    potteryObject("cylinder", "stoneware", 2.7, -1.4, true, 20261115, 0.3);
    potteryObject("bowl", "porcelain", -2.7, 1.3, false, 20261116, 0.05);
    potteryObject("plate", "celadon", 0.0, 1.3, false, 20261117, 0.1);
    potteryObject("saucer", "glazed_ceramic", 2.7, 1.3, false, 20261118, 0.16);
    print("POTTERY_READY six hollow vessels, five seeded PBR finishes, local-height glaze blending\n");
};
eve_update = function(dt) {
    if (key_just_pressed("space")) potteryOrbit = !potteryOrbit;
    if (key_just_pressed("h", "H")) {
        potteryDebug = (potteryDebug + 1) % 3;
        foreach (shader in potteryShaders) shader.sendFloat("debugView", potteryDebug.tofloat());
        print("POTTERY_HEIGHT_VIEW " + potteryDebug + "\n");
    }
    if (potteryOrbit) potteryTime += dt;
    potteryCamera.setEye(8.5 * sin(potteryTime * 0.08), 7.0, 11.0 * cos(potteryTime * 0.08));
    potteryCamera.setTarget(0.0, 0.4, 0.0);
};
eve_render = function() { gfx.clear(); gfx.render3D(); };
