// Explicit prepared-frame updates reuse one GPU batch; legacy camera/depth configuration is unchanged.
local api = eve.MultiscaleView();
local checked = function(result) {
    if (!result.ok) throw result.status.summary;
    return result.value;
};
local origin = { cell = ["0", "0", "0"], fraction = [0, 0, 0] };
local nearSphere = {
    center = { cell = ["0", "0", "10"], fraction = [0, 0, 0] },
    radiusMantissa = 2, radiusExponent = 900, color = [0.8, 0.1, 0.1]
};
local farSphere = {
    center = { cell = ["0", "0", "1000000000000000000"], fraction = [0, 0, 0] },
    radiusMantissa = 2.0e17, radiusExponent = 900, color = [0.1, 0.1, 0.8]
};
local description = {
    camera = origin,
    view = { right = [1,0,0], up = [0,1,0], forward = [0,0,1],
             tanHalfVerticalFov = 0.6, aspect = 1920.0/1080.0, cellExponent = 900 },
    spheres = [nearSphere, farSphere]
};
local occluded = checked(api.prepareSphereFrame(description, 2));
local batch = checked(api.prepareSphereBatch(gfx, occluded));
description.spheres = [farSphere];
local revealed = checked(api.prepareSphereFrame(description, 2));
local elapsed = 0.0;
local showNear = true;
eve_update = function(dt) {
    elapsed += dt;
    if (elapsed >= 4.0) {
        elapsed -= 4.0;
        showNear = !showNear;
        local updated = batch.update(showNear ? occluded : revealed);
        if (!updated.ok) throw updated.status.summary;
        print(showNear ? "multiscale: near sphere occludes far sphere\n" : "multiscale: distant sphere revealed\n");
    }
};
eve_render = function() {
    local result = batch.draw();
    if (!result.ok) throw result.status.summary;
};
eve_quit = function() { batch = null; occluded = null; revealed = null; };
print("multiscale: two equal-angular-size spheres, toggling every four seconds\n");
