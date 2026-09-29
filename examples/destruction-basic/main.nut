// ============================================================================
// Destruction basic — two welded boxes broken by a Strain field.
//
// Run: make run/linux-debug GAME=examples/destruction-basic
// ============================================================================

persist physics = null
persist destruction = null
persist world3 = null
persist instance = null
persist asset = null
persist tick = 0
persist brokenLogged = false
persist prevKeys = {}

function edgePressed(name) {
    local down = keyboard.isDown(name);
    local key = "k_" + name;
    local was = (key in prevKeys) ? prevKeys[key] : false;
    prevKeys[key] <- down;
    return down && !was;
}

function resetScene() {
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
    destruction.registerGeometryCollectionSchema();
    world3 = physics.newWorld3D(0.0, -9.8, 0.0, true);
    local ground = world3.newBody("static", 0.0, -0.5, 0.0);
    ground.newBoxShape(20.0, 1.0, 20.0, 0.0, 0.4, 0.0);
    asset = destruction.newWeldedBoxesFixture(1.0);
    instance = destruction.createInstance(world3, asset, 0.0, 0.0, 0.0);
    tick = 0;
    brokenLogged = false;
    // Weaken the weld; the next step detaches both bones.
    instance.applyStrainField(0.0, 1.0, 0.0, 3.0, 1.5);
    print("destruction-basic: welded boxes ready; strain applied\n");
}

function eve_init() {
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
}

function eve_render() {
    gfx.clear(0.12, 0.14, 0.18, 1.0);
}
