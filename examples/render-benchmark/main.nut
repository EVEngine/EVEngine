// Fixed cube grid. One shared mesh, one material style, fixed camera.
local cubes = [];
local cubeMesh = null;
local cubeMaterial = null;
local camera = null;
local samples = [];
local elapsed = 0.0;
local reported = false;
local renderTotal = 0.0;
local renderCount = 0;

eve_init = function() {
    if ("benchmarkDisable" in config) {
        local rc = gfx.getRenderControl();
        foreach (feature in config.benchmarkDisable) rc.disable(feature);
        rc.compile();
    }
    gfx.setBackgroundColor(0.08, 0.10, 0.14, 1.0);
    gfx.setDirectionalLight(-0.4, -1.0, -0.3, 1.0, 1.0, 1.0);
    cubeMesh = gfx.newMeshCube(1.0);
    cubeMaterial = gfx.newMaterial();
    cubeMaterial.setTint(0.65, 0.72, 0.82, 1.0);
    cubeMaterial.setMetallic(0.0);
    cubeMaterial.setRoughness(0.8);
    local grid = config.benchmarkGrid;
    for (local z = 0; z < grid; ++z) {
        for (local x = 0; x < grid; ++x) {
            local cube = eve.Renderable3D();
            cube.setMesh(cubeMesh);
            cube.setMaterial(cubeMaterial);
            cube.setPosition((x - (grid - 1) * 0.5) * 1.25, 0.0,
                             (z - (grid - 1) * 0.5) * 1.25);
            cube.setScale(0.8, 0.8, 0.8);
            cubes.append(cube);
        }
    }
    camera = eve.Camera3D();
    camera.setEye(0.0, grid * 1.0, grid * 1.25);
    camera.setTarget(0.0, 0.0, 0.0);
    camera.setFov(55.0);
    camera.setActive(true);
};

eve_update = function(dt) {
    elapsed += dt;
    if (elapsed >= 5.0 && elapsed < 35.0)
        samples.append(dt * 1000.0);
    if (elapsed >= 35.0 && !reported) {
        reported = true;
        local total = 0.0;
        local over33 = 0;
        local over50 = 0;
        foreach (sample in samples) {
            total += sample;
            if (sample > 33.333) ++over33;
            if (sample > 50.0) ++over50;
        }
        samples.sort(function(a, b) { return a < b ? -1 : (a > b ? 1 : 0); });
        local n = samples.len();
        local median = samples[ceil(n * 0.5).tointeger() - 1];
        local p99 = samples[ceil(n * 0.99).tointeger() - 1];
        print("BENCH_SUMMARY," + n + "," + total + "," + median + "," + p99 + "," + samples[n - 1] + "," + over33 + "," + over50 + "\n");
        print("BENCH_RENDER_CPU_MS," + (renderTotal / renderCount) + "\n");
        print("BENCH_DONE\n");
    }
};

eve_render = function() {
    gfx.clear();
    local start = timer.getTime();
    gfx.render3D();
    if (elapsed >= 5.0 && elapsed < 35.0) {
        renderTotal += (timer.getTime() - start) * 1000.0;
        ++renderCount;
    }
};
