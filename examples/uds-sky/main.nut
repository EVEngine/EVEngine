persist skyProfileFile = "sky-procedural.json"
persist skyPackDirectory = ""
persist sky = null
persist skyCamera = null

eve_init <- function() {
    if (skyPackDirectory != "") {
        assert(fs.mountExternalReadOnly(skyPackDirectory, "sky-pack"), "Cannot mount external sky pack");
    }
    local display = gfx.setSceneToneMapping("filmic");
    assert(display.ok, "Filmic scene presentation unavailable");
    local vignette = gfx.setScenePhotographicVignette(0.4);
    assert(vignette.ok, "Photographic vignette unavailable");
    local bloom = gfx.setBloomFilter("gaussianPyramid", 0.68, 6, 65472.0);
    assert(bloom.ok, "Photographic bloom unavailable");
    local prepared = daynight.prepareSky(gfx, fs.readText(skyProfileFile));
    assert(prepared.ok, "Independent sky preparation failed");
    print("Sky resource source: " + prepared.assetSource + "\n");
    sky = prepared.value;
    local attached = sky.attach();
    assert(attached.ok, "Independent sky attachment failed");
    skyCamera = eve.Camera3D();
    skyCamera.setEye(0.0, 2.0, 0.0);
    skyCamera.setTarget(0.965925826, 2.258819045, 0.0);
    // 75-degree horizontal FOV at 16:9, expressed as vertical FOV.
    skyCamera.setFov(46.6921257);
    skyCamera.setClipPlanes(0.1, 10000.0);
    skyCamera.setBloom(0.675, 0.0);
    skyCamera.setActive(true);
    gfx.setBackgroundColor(0.0, 0.0, 0.0, 1.0);
};

eve_update <- function(dt) {
    local advanced = sky.advanceSeconds(dt);
    assert(advanced.ok, "Independent sky update failed");
};

eve_render <- function() {
    gfx.clear();
    gfx.render3D();
};
