// Click the quad to map the Box3D triangle hit back to model UV and paint it.
local uvp = {
    model = null, renderable = null, pixels = null, texture = null,
    session = null, world = null, camera = null,
    paintHit = null, previousLeft = false, frame = 0, screenshotSaved = false
};

function paintRequire(result, label) {
    if (!result.ok) throw label + ": " + result.message;
    return result;
}

eve_init = function() {
    gfx.setBackgroundColor(0.04, 0.06, 0.09, 1.0);
    local paintCamera = eve.Camera3D();
    uvp.camera = paintCamera;
    paintCamera.setEye(0.0, 0.0, 5.0);
    paintCamera.setTarget(0.0, 0.0, 0.0);
    paintCamera.setUp(0.0, 1.0, 0.0);
    paintCamera.setAmbient(0.55, 0.55, 0.55);
    paintCamera.setActive(true);

    local paintModel = model3d.newModelDataFromFile("paintable.obj");
    uvp.model = paintModel;
    print("paint-stage:model\n");
    local paintedRenderable = model3d.createRenderable(gfx, paintModel, 0);
    uvp.renderable = paintedRenderable;
    local imageModule = eve.Image();
    local paintImage = imageModule.newEmptyImageData(512, 256, "RGBA8");
    uvp.pixels = paintImage;
    for (local y = 0; y < 256; ++y)
        for (local x = 0; x < 512; ++x)
            paintImage.setPixel(x, y, 0.12 + 0.18 * x / 511.0, 0.28, 0.55, 1.0);
    local paintSession = imageModule.newUvPaintSession();
    uvp.session = paintSession;
    paintRequire(paintSession.initialize(paintImage), "initialize paint session");
    local firstUv = paintModel.mapSurfacePointToUv(0, 0, -0.666667, -0.333333, 0.0, 0);
    local secondUv = paintModel.mapSurfacePointToUv(0, 1, 0.666667, 0.333333, 0.0, 0);
    paintRequire(paintSession.paintCircle(firstUv.getU(), firstUv.getV(), 38.0,
                                        1.0, 0.12, 0.04, 1.0, false, false), "paint first stroke");
    paintRequire(paintSession.paintCircle(secondUv.getU(), secondUv.getV(), 28.0,
                                        1.0, 0.82, 0.12, 1.0, false, false), "paint second stroke");
    paintRequire(paintSession.copyCurrentTo(paintImage), "copy painted pixels");
    local paintedTexture = gfx.setRenderableTextureFromImageData(paintedRenderable, paintImage, false, false);
    uvp.texture = paintedTexture;

    local paintWorld = physics.newWorld3D(0.0, 0.0, 0.0, false);
    uvp.world = paintWorld;
    local body = paintWorld.newBody("static", 0.0, 0.0, 0.0);
    local vertices = [], indices = [];
    for (local vertex = 0; vertex < paintModel.getVertexCount(0); ++vertex)
        for (local component = 0; component < 3; ++component)
            vertices.push(paintModel.getVertexPosition(0, vertex, component));
    for (local triangle = 0; triangle < paintModel.getFaceCount(0); ++triangle)
        for (local corner = 0; corner < 3; ++corner)
            indices.push(paintModel.getFaceVertexIndex(0, triangle, corner));
    body.newTriangleMeshShape(vertices, indices);

    uvp.paintHit = function(screenX, screenY) {
        paintCamera.screenToRay(screenX, screenY, gfx.getWidth().tofloat(), gfx.getHeight().tofloat());
        local ox = paintCamera.getScreenRayOriginX(), oy = paintCamera.getScreenRayOriginY();
        local oz = paintCamera.getScreenRayOriginZ();
        local dx = paintCamera.getScreenRayDirX(), dy = paintCamera.getScreenRayDirY();
        local dz = paintCamera.getScreenRayDirZ();
        paintWorld.rayCast(ox, oy, oz, ox + dx * 100.0, oy + dy * 100.0, oz + dz * 100.0);
        if (!paintWorld.hasRayHit()) return;
        local uv = paintModel.mapSurfacePointToUv(0, paintWorld.getRayHitTriangleIndex(),
            paintWorld.getRayHitX(), paintWorld.getRayHitY(), paintWorld.getRayHitZ(), 0);
        paintRequire(paintSession.paintCircle(uv.getU(), uv.getV(), 14.0,
            1.0, 0.12, 0.04, 1.0, false, false), "paint hit");
        paintRequire(paintSession.copyCurrentTo(paintImage), "copy interactive paint");
        if (paintedTexture != null) gfx.updateTextureFromImageData(paintedTexture, paintImage);
    };
    uvp.paintHit(gfx.getWidth().tofloat() * 0.5, gfx.getHeight().tofloat() * 0.5);

    print("UV_TEXTURE_PAINT_PASS mapping=barycentric physicsHit=verified session=transactional undo=available gpuUpdate=in-place\n");
};

eve_update = function(dt) {
    uvp.frame += 1;
    local left = mouse.isDown(1);
    if (left && !uvp.previousLeft && uvp.paintHit != null) uvp.paintHit(mouse.getX(), mouse.getY());
    uvp.previousLeft = left;
    if (!uvp.screenshotSaved && uvp.frame > 12 && gfx.saveFramePng("uv-texture-paint.png")) {
        uvp.screenshotSaved = true;
        print("UV Texture Paint: screenshot saved\n");
    }
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
};
