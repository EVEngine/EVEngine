// Loaded after examples/snow/main.nut by scripts/check_snow_example.py.
// Drive the real update path with deterministic input and simulation time.
snowTestInit <- eve_init;
snowTestUpdate <- eve_update;
snowTestFrame <- 0;
snowTestKey <- "";
snowTestButton <- 0;
snowTestRecoveryStart <- 0.0;
snowTestRecoveryTime <- 0.0;
snowTestInitialTriangles <- 0;
adaptiveTopologyAudit = true;
key_just_pressed = function(key) { return key == snowTestKey; };
mousePressed = function(button) { return button == snowTestButton; };
groundFromMouse = function(mx, my) { return [1.6, 1.6]; };

function snowRequire(condition, message) {
    if (!condition) throw "SNOW_FAIL: " + message;
}

function snowRequireSynced() {
    for (local z = 0; z < H; z++) {
        for (local x = 0; x < W; x++) {
            local expected = terrainHm.height(x, z) + sf.height(x, z) * SNOW_SCALE;
            snowRequire(abs(combinedHm.height(x, z) - expected) < 0.00001,
                        "display heightmap is stale");
        }
    }
    snowRequire(!sf.isDirty(), "texture projection is stale");
}

eve_init = function() {
    snowTestInit();
    snowTestInitialTriangles = adaptiveTriangleCount;
    snowRequire(adaptiveMinSpan == 1 && adaptiveMaxSpan > 1, "mesh is not adaptive");
    snowRequire(adaptiveTriangleCount < (W - 1) * (H - 1) * 2 / 2,
                "adaptive mesh did not reduce triangle count");
    print("SNOW_MESH initial: " + adaptiveVertexCount + " vertices, " + adaptiveTriangleCount + " triangles\n");
};

eve_update = function(ignoredDt) {
    try {
        snowTestFrame++;
        snowTestKey = "";
        snowTestButton = 0;
        if (snowTestFrame == 2) snowTestKey = "w";
        if (snowTestFrame == 28) snowTestKey = "w";
        if (snowTestFrame == 30) snowTestButton = 1;
        if (snowTestFrame == 32) snowTestButton = 2;
        if (snowTestFrame == 34) {
            snowRequire(sf.height(128, 128) < 0.2, "impact did not remove snow");
            snowTestRecoveryStart = sf.height(128, 128);
            snowTestKey = "s";
        }
        // Alternating frame sizes catch both dropped time and frame-rate coupling.
        local dt = snowTestFrame % 2 == 0 ? 0.01 : 0.04;
        if (snowTestFrame >= 34 && snowTestFrame < 74) snowTestRecoveryTime += dt;
        if (snowTestFrame == 74) snowTestKey = "s";
        if (snowTestFrame == 78 || snowTestFrame == 80) snowTestKey = "p";
        if (snowTestFrame == 84) snowTestKey = "r";
        snowTestUpdate(dt);
        if (snowTestFrame == 1) gfx.saveFramePng("snow-initial.png");
        if (snowTestFrame == 2) snowRequire(gfx.saveFramePng("snow-initial.png"), "initial capture failed");
        if (snowTestFrame >= 2) snowRequireSynced();
        if (snowTestFrame == 27)
            snowRequire(sf.height(170, 128) < 0.85, "walker did not stamp");
        if (snowTestFrame == 74)
            snowRequire(abs(sf.height(128, 128) - snowTestRecoveryStart -
                            snowTestRecoveryTime * 0.06) < 0.00001,
                        "snowfall lost simulation time");
        if (snowTestFrame == 84) {
            snowRequire(abs(sf.height(128, 128) - 0.85) < 0.00001, "reset failed");
            snowRequire(adaptiveTriangleCount < snowTestInitialTriangles, "reset did not coarsen mesh");
            print("SNOW_MESH reset: " + adaptiveVertexCount + " vertices, " + adaptiveTriangleCount + " triangles\n");
        }
        if (snowTestFrame == 76) gfx.saveFramePng("snow-interaction.png");
        if (snowTestFrame == 77)
            snowRequire(gfx.saveFramePng("snow-interaction.png"), "interaction capture failed");
        if (snowTestFrame == 87)
            snowRequire(gfx.saveFramePng("snow-reset.png"), "reset capture failed");
        if (snowTestFrame == 90) {
            print("SNOW_PASS: walker, footprint, impact, recovery, POM toggle, reset\n");
            eve.bootBench <- true;
        }
    } catch (error) {
        print("SNOW_FAIL: " + error + "\n");
        eve.bootBench <- true;
    }
};
