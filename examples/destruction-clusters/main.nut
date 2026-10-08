// ============================================================================
// Destruction clusters — anchored 2×3 pillar with budgets + snapshot.
//
// Run: make run/linux-debug GAME=examples/destruction-clusters
// ============================================================================

persist physics = null
persist destruction = null
persist destructionFx = null
persist world3 = null
persist instance = null
persist renderer = null
persist asset = null
persist tick = 0
persist snapshotJson = ""
persist lastPending = -1
persist lastClusterBreaks = -1
persist prevKeys = {}

function edgePressed(name) {
    local down = keyboard.isDown(name);
    local key = "k_" + name;
    local was = (key in prevKeys) ? prevKeys[key] : false;
    prevKeys[key] <- down;
    return down && !was;
}

function countBroken() {
    local n = 0;
    local edges = instance.edgeCount();
    for (local i = 0; i < edges; i++) {
        if (instance.isEdgeBroken(i)) n++;
    }
    return n;
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

    // Two cook clusters, bottom bones anchored, weaker inter-cluster welds.
    asset = destruction.newClusterPillarFixture(0.8);
    instance = destruction.createInstance(world3, asset, 0.0, 0.0, 0.0);
    instance.setStepBudget(1, 1); // at most one edge break + one sleep per step

    if (destructionFx != null) {
        renderer = destructionFx.createRenderer(instance);
        renderer.setExteriorColor(0.55, 0.58, 0.62, 1.0);
        renderer.setInteriorColor(0.85, 0.40, 0.28, 1.0);
        renderer.setSleepColor(0.40, 0.42, 0.46, 1.0);
    }

    tick = 0;
    lastPending = -1;
    lastClusterBreaks = -1;
    // Broad strain: weak inter-cluster edges break first under the budget.
    instance.applyStrainField(0.0, 1.5, 0.0, 4.0, 1.2);
    print("destruction-clusters: pillar ready bones=" + instance.boneCount() +
          " edges=" + instance.edgeCount() +
          " c0=" + instance.boneClusterId(0) +
          " c1=" + instance.boneClusterId(3) + "\n");
}

function eve_init() {
    gfx.setBackgroundColor(0.07, 0.08, 0.10, 1.0);
    camera = eve.Camera3D();
    camera.setEye(6.5, 4.2, 7.5);
    camera.setTarget(0.0, 1.4, 0.0);
    camera.setUp(0.0, 1.0, 0.0);
    camera.setFov(48.0);
    camera.setAmbient(0.26, 0.28, 0.32);
    camera.setActive(true);
    gfx.setDirectionalLight(-0.35, -1.0, -0.25, 1.15, 1.08, 1.0);
    resetScene();
    print("keys: R reset | S snapshot | L restore | I impulse | Z sleep\n");
}

function eve_update(dt) {
    if (edgePressed("r")) resetScene();
    if (world3 == null || instance == null) return;

    if (edgePressed("s")) {
        snapshotJson = instance.captureSnapshotJson();
        print("destruction-clusters: snapshot bytes=" + snapshotJson.len() +
              " broken=" + countBroken() + "\n");
    }
    if (edgePressed("l")) {
        if (snapshotJson.len() == 0) {
            print("destruction-clusters: no snapshot yet (press S)\n");
        } else {
            instance.restoreSnapshotJson(snapshotJson);
            print("destruction-clusters: restored snapshot broken=" + countBroken() +
                  " pending=" + instance.pendingEdgeBreakCount() + "\n");
        }
    }
    if (edgePressed("i")) {
        // Only Detached bones accept impulse; anchors/Attached stay put.
        instance.applyImpulseField(0.0, 1.5, 0.0, 4.0, 4.0, 0.6, 0.2, 0.0);
        print("destruction-clusters: impulse applied\n");
    }
    if (edgePressed("z")) {
        instance.applySleepField(0.0, 1.0, 0.0, 6.0);
        print("destruction-clusters: sleep field revision=" +
              instance.sleepBatchRevision() + "\n");
    }

    world3.update(dt);
    tick += 1;
    instance.step(tick, dt);

    local pending = instance.pendingEdgeBreakCount();
    local clusters = instance.clusterBreakEventCount();
    if (pending != lastPending || clusters != lastClusterBreaks) {
        lastPending = pending;
        lastClusterBreaks = clusters;
        print("destruction-clusters: tick=" + tick +
              " broken=" + countBroken() +
              " pending=" + pending +
              " clusterBreaks=" + clusters +
              " detach=" + instance.detachEventCount() + "\n");
    }
}

function eve_render() {
    gfx.clear();
    gfx.render3D();
    if (renderer != null) {
        renderer.draw(gfx);
    }
}
