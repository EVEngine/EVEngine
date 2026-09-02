// Persistent 3D primitive example.
// Shows depth-tested lines, screen-space dashes, a wire sphere and an AABB.

persist primitiveCamera = null
persist primitiveObjects = []
persist animatedRoute = null
persist primitiveTime = 0.0

function requirePrimitive(result, label) {
    if (!result.ok) {
        throw label + ": " + result.status.summary;
    }
    primitiveObjects.push(result.value);
    return result.value;
}

function requireUpdate(result, label) {
    if (!result.ok) {
        throw label + ": " + result.status.summary;
    }
}

function buildPrimitiveScene() {
    // X/Y/Z axes. Ignore depth so the origin remains readable through other shapes.
    local xAxis = requirePrimitive(
        gfx.newPrimitiveLine3D(0.0, 0.0, 0.0, 4.0, 0.0, 0.0, 1.0, 0.2, 0.2, 1.0, 4.0), "x axis");
    local yAxis = requirePrimitive(
        gfx.newPrimitiveLine3D(0.0, 0.0, 0.0, 0.0, 4.0, 0.0, 0.2, 1.0, 0.3, 1.0, 4.0), "y axis");
    local zAxis = requirePrimitive(
        gfx.newPrimitiveLine3D(0.0, 0.0, 0.0, 0.0, 0.0, -4.0, 0.25, 0.55, 1.0, 1.0, 4.0), "z axis");
    requireUpdate(xAxis.setDepthMode("ignore"), "x axis depth");
    requireUpdate(yAxis.setDepthMode("ignore"), "y axis depth");
    requireUpdate(zAxis.setDepthMode("ignore"), "z axis depth");

    // A screen-space dashed route. Its dash phase is animated in eve_update().
    animatedRoute = requirePrimitive(
        gfx.newPrimitiveLine3D(-4.0, 0.35, -1.0, 4.0, 2.4, -5.0, 1.0, 0.72, 0.12, 1.0, 5.0),
        "dashed route");
    requireUpdate(animatedRoute.setDepthMode("test"), "route depth");
    requireUpdate(animatedRoute.setDash(18.0, 10.0, 0.0, "screen"), "route dash");

    local sphere = requirePrimitive(
        gfx.newPrimitiveSphere3D(-2.2, 1.4, -4.5, 1.35, 0.25, 0.85, 1.0, 0.95, 3.0),
        "wire sphere");
    requireUpdate(sphere.setDepthMode("test"), "sphere depth");

    local bounds = requirePrimitive(
        gfx.newPrimitiveAabb3D(0.7, 0.1, -6.0, 3.3, 2.7, -3.4,
                               1.0, 0.45, 0.12, 0.9, 3.0),
        "wire aabb");
    requireUpdate(bounds.setDepthMode("test"), "aabb depth");
    requireUpdate(bounds.setDash(10.0, 6.0, 0.0, "world"), "aabb dash");
}

eve_init = function() {
    gfx.setBackgroundColor(0.025, 0.035, 0.06, 1.0);
    gfx.setDirectionalLight(-0.4, 0.9, 0.3, 1.0, 0.95, 0.85);

    primitiveCamera = eve.Camera3D();
    primitiveCamera.setEye(8.5, 6.0, 11.5);
    primitiveCamera.setTarget(0.0, 1.2, -3.0);
    primitiveCamera.setFov(52.0);
    primitiveCamera.setAmbient(0.24, 0.27, 0.34);

    if (primitiveObjects.len() == 0) buildPrimitiveScene();
};

eve_update = function(dt) {
    primitiveTime += dt;
    if (animatedRoute != null && !animatedRoute.isStale()) {
        requireUpdate(animatedRoute.setDash(18.0, 10.0, primitiveTime * 28.0, "screen"),
                      "animate route dash");
    }
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
};
