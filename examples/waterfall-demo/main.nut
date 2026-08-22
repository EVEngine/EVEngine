// Waterfall demo — flowing-water sheet shader.
// A vertical waterfall plane with downward flow streaks, turbulence and
// foam at the top lip + bottom splash pool. Press F to dump the current
// frame to /tmp/waterfall_demo.png.

if (!("fall" in getroottable())) fall <- null;
if (!("fallEnt" in getroottable())) fallEnt <- null;
if (!("fallCam" in getroottable())) fallCam <- null;
if (!("fallTime" in getroottable())) fallTime <- 0.0;
if (!("fallSaved" in getroottable())) fallSaved <- false;

eve_init = function() {
    gfx.setBackgroundColor(0.03, 0.05, 0.08, 1.0);
    gfx.setDirectionalLight(0.3, -1.0, 0.2, 1.1, 1.0, 0.9);

    if (fallCam == null) {
        fallCam = eve.Camera3D();
        fallCam.setEye(0.0, 0.5, 11.0);
        fallCam.setTarget(0.0, 0.0, 0.0);
        fallCam.setUp(0.0, 1.0, 0.0);
        fallCam.setFov(52.0);
        fallCam.setAmbient(0.30, 0.40, 0.50);
        fallCam.setActive(true);
    }

    if (fall == null) {
        fall = gfx.newWaterfall();
        fall.createSheet(8.0, 14.0, 24, 36);
        fall.setFlowSpeed(1.5);
        fall.setTurbulence(0.75);
        fall.setStreakCount(5);
        fall.setStreakScale(6.0);
        fall.setTopFoam(0.07);
        fall.setBottomFoam(0.14);
        fall.setFoamAmount(0.9);
        fall.setWaterColor(0.06, 0.24, 0.32);
        fall.setReflectionIntensity(0.5);
        fall.setSunIntensity(0.8);

        fallEnt = eve.Renderable3D();
        fallEnt.setMesh(fall.getMesh());
        fallEnt.setShader(fall.getShader());
        fallEnt.setPosition(0.0, 1.5, 0.0);
    }
};

eve_update = function(dt) {
    fallTime += dt;
    if (fall != null) fall.update(dt);
    // 手动保存当前帧（头部注释约定）。
    if (keyboard.isDown("F") || keyboard.isDown("f")) {
        if (gfx.saveFramePng("/tmp/waterfall_demo.png"))
            print("waterfall frame saved: /tmp/waterfall_demo.png\n");
    }
};

eve_render = function() {
    // saveFramePng reads the frame presented at the end of the previous tick,
    // so wait a couple of seconds for the swapchain to have a presented image.
    if (!fallSaved && fallTime > 1.5) {
        if (gfx.saveFramePng("/tmp/waterfall_demo.png")) {
            print("waterfall frame saved: /tmp/waterfall_demo.png\n");
        }
        fallSaved = true;
    }
    gfx.clear();
    gfx.render3D();
};
