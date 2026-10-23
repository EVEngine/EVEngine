persist  keep            = [];
persist  camera          = null;
persist  frame           = 0;
persist  glassBall       = null;
persist  opaqueCanvas    = null;
persist  depthCanvas     = null;
persist  depthMaterial   = null;
persist  opaqueObjects   = [];
persist  opaqueMaterials = [];
persist  displayShader   = null;
persist  finalCanvas     = null;
persist  glassMaterial   = null;
function retain(v) {
    keep.append(v);
    return v;
}
eve_init = function() {
    local gpu = eve.OS();
    retain(gpu);
    print("LOOKDEV_GPU " + gpu.getGpuName() + " " + gpu.getGpuVendor() + " " + gpu.getGpuDeviceType() + "\n");
    if (gpu.getGpuVendor() != "NVIDIA" || gpu.getGpuDeviceType() != "discrete")
        throw "Refusing non-NVIDIA hardware for lookdev";
    gfx.setBackgroundColor(0.045, 0.045, 0.045, 1.0);
    camera = retain(eve.Camera3D());
    camera.setFov(36.0);
    camera.setEye(0.0, 2.4, 23.0);
    camera.setTarget(0.0, 0.4, 0.0);
    camera.setUp(0.0, 1.0, 0.0);
    camera.setAmbient(0.06, 0.06, 0.06);
    camera.setActive(true);
    local roomBounds   = [ -9.0, -8.0, -6.0, 9.0, 8.0, 26.0 ];
    local keyDirection = [ -0.65, 0.70, 0.35 ];
    local rimDirection = [ 0.95, 0.25, -0.15 ];
    gfx.setDirectionalLight(keyDirection[0], keyDirection[1], keyDirection[2], 1.2, 1.2, 1.2);
    local probe = retain(gfx.newReflectionProbeCapture());
    probe.configure(0.0, 0.0, 0.0, 256, 0.1, 60.0);
    probe.setCaptureMask(1);
    local pedestalShaderResult = gfx.loadMeshShaderSpv("", "shaders/studio-pedestal.frag.spv");
    if (!pedestalShaderResult.ok) throw pedestalShaderResult.status.summary;
    local pedestalMaterial = retain(gfx.newMaterial());
    pedestalMaterial.setShader(retain(pedestalShaderResult.value));
    pedestalMaterial.setShadingModel("custom");
    local glassPedestalMaterial = retain(gfx.newMaterial());
    glassPedestalMaterial.setShader(retain(pedestalShaderResult.value));
    glassPedestalMaterial.setShadingModel("custom");
    glassPedestalMaterial.setTint(1.0, 1.0, 1.0, 0.15);
    local roomShaderResult = gfx.loadMeshShaderSpv("", "shaders/studio-room.frag.spv");
    if (!roomShaderResult.ok) throw roomShaderResult.status.summary;
    local roomMaterial = retain(gfx.newMaterial());
    local roomShader   = retain(roomShaderResult.value);
    local roomControls = [
        "minX", "minY", "minZ", "maxX", "maxY", "maxZ", "keyX", "keyY", "keyZ", "keyWidth", "keyHeight", "keyRadiance",
        "rimX", "rimY", "rimZ", "rimWidth", "rimHeight", "rimRadiance"
    ];
    local roomValues = [];
    foreach (v in roomBounds) roomValues.append(v);
    foreach (v in keyDirection) roomValues.append(v);
    roomValues.extend([ 0.27, 0.55, 8.0 ]);
    foreach (v in rimDirection) roomValues.append(v);
    roomValues.extend([ 0.04, 0.45, 2.0 ]);
    for (local i = 0; i < roomControls.len(); i++) {
        roomShader.declareFloat(roomControls[i]);
        roomShader.sendFloat(roomControls[i], roomValues[i]);
    }
    roomMaterial.setShader(roomShader);
    roomMaterial.setShadingModel("custom");
    local cx = (roomBounds[0] + roomBounds[3]) * 0.5, cy = (roomBounds[1] + roomBounds[4]) * 0.5,
          cz = (roomBounds[2] + roomBounds[5]) * 0.5;
    local sx = roomBounds[3] - roomBounds[0], sy = roomBounds[4] - roomBounds[1], sz = roomBounds[5] - roomBounds[2];
    local walls = [[roomBounds [0], cy, cz, 0.1, sy, sz], [roomBounds [3], cy, cz, 0.1, sy, sz],
                   [cx, roomBounds [1], cz, sx, 0.1, sz], [cx, roomBounds [4], cz, sx, 0.1, sz],
                   [cx, cy, roomBounds [2], sx, sy, 0.1], [cx, cy, roomBounds [5], sx, sy, 0.1]];
    foreach (w in walls) {
        local wall = retain(eve.Renderable3D());
        wall.setMesh(retain(gfx.newMeshCube(1.0)));
        wall.setPosition(w[0], w[1], w[2]);
        wall.setScale(w[3], w[4], w[5]);
        wall.setMaterial(roomMaterial);
        wall.setReflectionCaptureMask(1);
        opaqueObjects.append(wall);
        opaqueMaterials.append(roomMaterial);
    }
    probe.requestCapture();
    local guard = 0;
    while (!probe.isCaptureComplete() && guard < 24) {
        probe.update(6);
        guard++;
    }
    if (!probe.isCaptureComplete() || !probe.stageCapturedFaces() || !probe.filterAndPublish(64))
        throw "Room reflection capture failed";
    camera.setEnvMap(probe.getActiveCubemap());
    camera.setEnvIntensity(1.0);
    local names = [
        "wood", "metal", "rubber", "cloth", "plastic", "steel", "glass", "paper", "aluminum", "cast_iron", "cement",
        "paint", "marble"
    ];
    for (local i = 0; i < names.len(); i++) {
        local x     = (i % 4 - 1.5) * 2.7;
        local y     = 4.65 - (i / 4) * 3.0;
        local model = retain(model3d.newModelDataFromFile("assets/" + names[i] + ".gltf"));
        if (model == null) throw "Material glTF load failed";
        local ball = retain(model3d.createRenderable(gfx, model, 0));
        ball.setPosition(x, y, 0.0);
        ball.setReflectionCaptureMask(2);
        ball.setCastShadow(true);
        ball.setReceiveShadow(true);
        if (i == 6)
            glassBall = ball;
        else {
            opaqueObjects.append(ball);
            opaqueMaterials.append(ball.getMaterial());
        }
        local pedestalData = retain(model3d.newModelDataFromFile("assets/pedestal.gltf"));
        local pedestal     = retain(model3d.createRenderable(gfx, pedestalData, 0));
        pedestal.setScale(1.1, 0.20, 1.1);
        pedestal.setPosition(x, y - 1.15, 0.0);
        pedestal.setMaterial(i == 6 ? glassPedestalMaterial : pedestalMaterial);
        pedestal.setReflectionCaptureMask(2);
        pedestal.setCastShadow(true);
        pedestal.setReceiveShadow(true);
        opaqueObjects.append(pedestal);
        opaqueMaterials.append(pedestal.getMaterial());
    }
    finalCanvas       = retain(gfx.newCanvas(config.width, config.height));
    opaqueCanvas      = retain(gfx.newCanvas(config.width, config.height));
    depthCanvas       = retain(gfx.newCanvas(config.width, config.height));
    local depthResult = gfx.loadMeshShaderSpv("", "shaders/scene-depth.frag.spv");
    if (!depthResult.ok) throw depthResult.status.summary;
    depthMaterial = retain(gfx.newMaterial());
    depthMaterial.setShader(retain(depthResult.value));
    depthMaterial.setShadingModel("custom");
    displayShader           = retain(gfx.newShaderFromSpvFile("shaders/display-linear.frag.spv"));
    local glassShaderResult = gfx.loadMeshShaderSpv("", "shaders/solid-sphere-glass.frag.spv");
    if (!glassShaderResult.ok) throw glassShaderResult.status.summary;
    local glassShader = retain(glassShaderResult.value);
    glassShader.declareFloat("ior");
    glassShader.declareFloat("depth");
    glassShader.declareFloat("density");
    glassShader.sendFloat("ior", 1.5);
    glassShader.sendFloat("depth", 20.0);
    glassShader.sendFloat("density", 0.35);
    local boundNames = [ "roomMinX", "roomMinY", "roomMinZ", "roomMaxX", "roomMaxY", "roomMaxZ" ];
    for (local i = 0; i < 6; i++) {
        glassShader.declareFloat(boundNames[i]);
        glassShader.sendFloat(boundNames[i], roomBounds[i]);
    }
    glassMaterial = retain(gfx.newMaterial());
    glassMaterial.setShader(glassShader);
    glassMaterial.setShadingModel("custom");
    glassMaterial.setAlbedoTexture(opaqueCanvas.getTexture());
    glassMaterial.setNormalTexture(depthCanvas.getTexture());
    glassMaterial.setTint(1.0, 1.0, 1.0, 1.0);
    glassBall.setMaterial(glassMaterial);
    print("MATERIAL_LOOKDEV_READY\n");
};
eve_update = function(dt) { frame++; };
eve_render = function() {
    glassBall.setVisible(false);
    for (local i = 0; i < opaqueObjects.len(); i++) opaqueObjects[i].setMaterial(depthMaterial);
    gfx.setBackgroundColor(0.0, 0.0, 0.0, 0.0);
    gfx.renderScene3DToCanvas(depthCanvas, camera);
    for (local i = 0; i < opaqueObjects.len(); i++) opaqueObjects[i].setMaterial(opaqueMaterials[i]);
    gfx.setBackgroundColor(0.045, 0.045, 0.045, 1.0);
    gfx.renderScene3DToCanvas(opaqueCanvas, camera);
    glassBall.setVisible(true);
    gfx.renderScene3DToCanvas(finalCanvas, camera);
    gfx.clear();
    gfx.setShader(displayShader);
    gfx.drawCanvas(finalCanvas, 0.0, 0.0, config.width.tofloat(), config.height.tofloat());
    gfx.setShader(null);
};
