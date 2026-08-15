// WebGPU demo: a rotating lit sphere rendered through the offscreen
// scene-color pass and composited to the swapchain.

local frameCount = 0;
local cam = null;
local ball = null;

eve_init = function() {
    gfx.setBackgroundColor(0.04, 0.05, 0.12, 1.0);
    cam = eve.Camera3D();
    cam.setEye(0.0, 0.0, 3.2);
    cam.setTarget(0.0, 0.0, 0.0);
    cam.setFov(45.0);
    cam.setActive(true);
    ball = eve.Renderable3D();
    ball.setMesh(gfx.newMeshSphere(48, 24));
    ball.setTint(0.85, 0.9, 1.0, 1.0);
    gfx.setDirectionalLight(-0.35, 0.7, 0.45, 1.0, 0.95, 0.9);
    print("webgpu main.nut init ok\n");
};

eve_update = function(dt) {
    if (ball != null)
        ball.setYaw(ball.getYaw() + dt * 0.4);
};

eve_render = function() {
    frameCount += 1;
    if ((frameCount % 60) == 0)
        print("eve_render frame " + frameCount + "\n");
    gfx.clear();
    gfx.render3D();
};
