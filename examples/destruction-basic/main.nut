// ============================================================================
// Destruction basic — two welded boxes broken by a Strain field.
//
// Run: make run/linux-debug GAME=examples/destruction-basic
// ============================================================================

persist physics = null
persist destruction = null
persist destructionFx = null
persist world3 = null
persist instance = null
persist renderer = null
persist asset = null
persist tick = 0
persist brokenLogged = false
persist sleepLogged = false
persist prevKeys = {}

function edgePressed(name) {
    local down = keyboard.isDown(name);
    local key = "k_" + name;
    local was = (key in prevKeys) ? prevKeys[key] : false;
    prevKeys[key] <- down;
    return down && !was;
}

function resetScene() {
    if (renderer != null) {
        renderer.setInstance(null);
        renderer = null;
    }
    if (instance != null) {
        instance.releaseBodies();
        instance = null;
    }
    if (world3 != null) {
        world3.destroy();
        world3 = null;
    }
    if (physics == null) physics = eve.Physics();
    if (destruction == null) destruction = eve.Destruction();
    if (destructionFx == null && has_module("destructionFx")) {
        destructionFx = eve.DestructionFx();
    }
    destruction.registerGeometryCollectionSchema();
    world3 = physics.newWorld3D(0.0, -9.8, 0.0, true);
    local ground = world3.newBody("static", 0.0, -0.5, 0.0);
    ground.newBoxShape(20.0, 1.0, 20.0, 0.0, 0.4, 0.0);
    asset = destruction.newWeldedBoxesFixture(1.0);
    instance = destruction.createInstance(world3, asset, 0.0, 0.0, 0.0);
    if (destructionFx != null) {
        renderer = destructionFx.createRenderer(instance);
        renderer.setExteriorColor(0.62, 0.58, 0.52, 1.0);
        renderer.setInteriorColor(0.78, 0.42, 0.28, 1.0);
        renderer.setSleepColor(0.45, 0.45, 0.48, 1.0);
    }
    tick = 0;
    brokenLogged = false;
    sleepLogged = false;
    // Weaken the weld; the next step detaches both bones.
    instance.applyStrainField(0.0, 1.0, 0.0, 3.0, 1.5);
    print("destruction-basic: welded boxes ready; strain applied\n");
}

function eve_init() {
    gfx.setBackgroundColor(0.08, 0.09, 0.11, 1.0);
    camera = eve.Camera3D();
    camera.setEye(5.5, 3.8, 6.5);
    camera.setTarget(0.0, 1.0, 0.0);
    camera.setUp(0.0, 1.0, 0.0);
    camera.setFov(50.0);
    camera.setAmbient(0.28, 0.30, 0.34);
    camera.setActive(true);
    gfx.setDirectionalLight(-0.4, -1.0, -0.3, 1.2, 1.1, 1.0);
    resetScene();
}

function eve_update(dt) {
    if (edgePressed("r")) resetScene();
    if (world3 == null || instance == null) return;
    world3.update(dt);
    tick += 1;
    instance.step(tick, dt);
    if (!brokenLogged && instance.edgeBroken(0)) {
        brokenLogged = true;
        print("destruction-basic: edge broken, bones detached=" +
              instance.boneState(0) + "," + instance.boneState(1) +
              " events=" + instance.detachEventCount() + "\n");
    }
    // After fragments settle, request sleep so the renderer can batch them.
    if (brokenLogged && !sleepLogged && tick > 90) {
        instance.applySleepField(0.0, 1.0, 0.0, 5.0);
        sleepLogged = true;
        print("destruction-basic: sleep field applied\n");
    }
}

function eve_render() {
    gfx.clear();
    gfx.render3D();
    if (renderer != null) {
        renderer.draw(gfx);
    }
}
