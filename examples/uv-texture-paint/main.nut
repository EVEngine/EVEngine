// Click the quad to map the Box3D triangle hit back to model UV and paint it.
persist model = null;
persist renderable = null;
persist pixels = null;
persist texture = null;
persist world = null;
persist camera = null;
persist previousLeft = false;

function paintHit() {
    camera.screenToRay(mouse.getX(), mouse.getY(), gfx.getWidth().tofloat(), gfx.getHeight().tofloat());
    local ox = camera.getScreenRayOriginX(), oy = camera.getScreenRayOriginY(), oz = camera.getScreenRayOriginZ();
    local dx = camera.getScreenRayDirX(), dy = camera.getScreenRayDirY(), dz = camera.getScreenRayDirZ();
    world.rayCast(ox, oy, oz, ox + dx * 100.0, oy + dy * 100.0, oz + dz * 100.0);
    if (!world.hasRayHit()) return;

    // The renderable and collider are identity-transformed, so hit world space is model local space.
    local uv = model.mapSurfacePointToUv(0, world.getRayHitTriangleIndex(),
        world.getRayHitX(), world.getRayHitY(), world.getRayHitZ(), 0);
    pixels.paintCircleUv(uv.getU(), uv.getV(), 14.0, 1.0, 0.12, 0.04, 1.0, false, false);
    gfx.updateTextureFromImageData(texture, pixels);
    print("paint: triangle=" + uv.getTriangleIndex() + " uv=(" + uv.getU() + ", " + uv.getV() + ")\n");
}

eve_init = function() {
    gfx.setBackgroundColor(0.04, 0.06, 0.09, 1.0);
    camera = eve.Camera3D();
    camera.setEye(0.0, 0.0, 5.0);
    camera.setTarget(0.0, 0.0, 0.0);
    camera.setUp(0.0, 1.0, 0.0);
    camera.setActive(true);

    model = model3d.newModelDataFromFile("paintable.obj");
    renderable = model3d.createRenderable(gfx, model, 0);
    pixels = image.newEmptyImageData(512, 256, "RGBA8");
    for (local y = 0; y < 256; ++y)
        for (local x = 0; x < 512; ++x)
            pixels.setPixel(x, y, 0.12 + 0.18 * x / 511.0, 0.28, 0.55, 1.0);
    texture = gfx.newTexture(pixels, false, false);
    renderable.setTexture(texture);

    world = physics.newWorld3D(0.0, 0.0, 0.0, false);
    local body = world.newBody("static", 0.0, 0.0, 0.0);
    local vertices = [], indices = [];
    for (local vertex = 0; vertex < model.getVertexCount(0); ++vertex)
        for (local component = 0; component < 3; ++component)
            vertices.push(model.getVertexPosition(0, vertex, component));
    for (local triangle = 0; triangle < model.getFaceCount(0); ++triangle)
        for (local corner = 0; corner < 3; ++corner)
            indices.push(model.getFaceVertexIndex(0, triangle, corner));
    body.newTriangleMeshShape(vertices, indices);
    print("UV Texture Paint: click the quad to paint its texture.\n");
};

eve_update = function(dt) {
    local left = mouse.isDown(1);
    if (left && !previousLeft) paintHit();
    previousLeft = left;
    world.update(dt);
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
};
