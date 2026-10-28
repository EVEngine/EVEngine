// UE-PCG-style runtime biome: spatial composition -> point processing ->
// hierarchical cell scheduling -> reconciled scene instance batches.

persist biomeRuntime = null
persist biomeCells = {}
persist sourceX = 320.0
persist sourceZ = 220.0
persist biomeStatus = "not started"
persist roadSpatial = null
persist coarseBiome = null
persist detailBiome = null

gfx.setBackgroundColor(0.035, 0.055, 0.045, 1.0);

function cellKey(request) {
    return "L" + request.getLevel() + "/" + request.getX() + "/" + request.getZ();
}

function buildBiomeCell(request) {
    local domainResult = procgen.boxVolume(request.getMinX(), 0.0, request.getMinZ(),
                                           request.getMaxX(), 0.0, request.getMaxZ());
    if (!domainResult.ok) throw domainResult.status.summary;
    local domain = domainResult.value;
    local spacing = request.getLevel() == 0 ? 56.0 : 18.0;
    local rules = request.getLevel() == 0 ? coarseBiome : detailBiome;
    local attributed = rules.generate(domain, spacing, request.getSeed(), 0.72);
    if (attributed == null) throw rules.getError();
    local graphResult = procgen.newPointGraph();
    if (!graphResult.ok) throw graphResult.status.summary;
    local graph = graphResult.value;
    graph.addNode("biome", "input");
    graph.setNodePoints("biome", attributed);
    graph.addNode("prune", "self.prune");
    graph.setNodeFloat("prune", "radius", request.getLevel() == 0 ? 38.0 : 10.0);
    graph.connect("biome", "prune");
    local result = graph.execute("prune");
    if (result == null) throw graph.getError();
    return result;
}

function pumpRuntime() {
    biomeRuntime.updateSource(sourceX, sourceZ, 1.0, 0.15);
    biomeRuntime.beginFrame();
    for (local request = biomeRuntime.nextGenerate(); request != null;
         request = biomeRuntime.nextGenerate()) {
        try {
            local points = buildBiomeCell(request);
            local publishResult = procgen.publishCellInstances(
                "biome", request, points, "asset", "unknown");
            if (!publishResult.ok) throw publishResult.status.summary;
            if (!biomeRuntime.completeGeneration(request, points))
                throw "stale cell generation request";
            biomeCells[cellKey(request)] <- points;
        } catch (error) {
            biomeRuntime.failGeneration(request);
            print("pcg-biome generation failed: " + error + "\n");
        }
    }
    for (local request = biomeRuntime.nextCleanup(); request != null;
         request = biomeRuntime.nextCleanup()) {
        procgen.removeCellInstances("biome", request);
        local key = cellKey(request);
        if (key in biomeCells) delete biomeCells[key];
        biomeRuntime.completeCleanup(request);
    }
    biomeStatus = biomeRuntime.debugReport();
}

function resetBiome() {
    foreach (key, points in biomeCells) procgen.removeInstances("biome/" + key);
    biomeCells = {};
    local runtimeResult = procgen.newRuntimeGeneration(42017);
    if (!runtimeResult.ok) {
        biomeStatus = runtimeResult.status.summary;
        return;
    }
    biomeRuntime = runtimeResult.value;
    biomeRuntime.addLevel(256.0, 430.0, 1.35);
    biomeRuntime.addLevel(64.0, 150.0, 1.65);
    biomeRuntime.setDirectionWeight(0.3);
    biomeRuntime.setFrustumCulling(true, 75.0, 96.0);
    biomeRuntime.setMaxGenerating(3);
    biomeRuntime.setFrameTimeBudget(3.0);

    local roadResult = procgen.newPointSet();
    if (!roadResult.ok) {
        biomeStatus = roadResult.status.summary;
        return;
    }
    local road = roadResult.value;
    road.add(-100.0, 0.0, 80.0);
    road.add(260.0, 0.0, 210.0);
    road.add(680.0, 0.0, 260.0);
    road.add(1100.0, 0.0, 500.0);
    local roadSpatialResult = procgen.splineData(road, 30.0);
    if (!roadSpatialResult.ok) { biomeStatus = roadSpatialResult.status.summary; return; }
    roadSpatial = roadSpatialResult.value;

    local worldResult = procgen.boxVolume(-2048.0, 0.0, -2048.0, 2048.0, 0.0, 2048.0);
    if (!worldResult.ok) { biomeStatus = worldResult.status.summary; return; }
    local world = worldResult.value;
    local coarseResult = procgen.newBiomeRules();
    if (!coarseResult.ok) { biomeStatus = coarseResult.status.summary; return; }
    coarseBiome = coarseResult.value;
    coarseBiome.addLayer("forest", world, 10, 0.72);
    coarseBiome.addAsset("forest", "oak", 4.0, 0.8, 1.2, true);
    coarseBiome.addAsset("forest", "pine", 1.0, 0.9, 1.3, true);
    coarseBiome.addExclusion(roadSpatial);

    local detailResult = procgen.newBiomeRules();
    if (!detailResult.ok) { biomeStatus = detailResult.status.summary; return; }
    detailBiome = detailResult.value;
    detailBiome.addLayer("undergrowth", world, 10, 0.52);
    detailBiome.addAsset("undergrowth", "grass", 3.0, 0.7, 1.15, true);
    detailBiome.addAsset("undergrowth", "rock", 1.0, 0.65, 1.4, true);
    detailBiome.addExclusion(roadSpatial);
    pumpRuntime();
}

eve_init <- function() { resetBiome(); };
eve_reload <- function() { resetBiome(); };

eve_update <- function(dt) {
    local moved = false;
    if (key_just_pressed("A")) { sourceX -= 96.0; moved = true; }
    if (key_just_pressed("D")) { sourceX += 96.0; moved = true; }
    if (key_just_pressed("W")) { sourceZ -= 96.0; moved = true; }
    if (key_just_pressed("S")) { sourceZ += 96.0; moved = true; }
    if (moved || biomeRuntime.getPendingGenerateCount() > 0) pumpRuntime();
};

eve_render <- function() {
    gfx.clear();
    local viewX = 90.0;
    local viewZ = 80.0;
    local scale = 0.55;
    foreach (key, points in biomeCells) {
        for (local i = 0; i < points.getCount(); ++i) {
            local asset = points.getStringAttribute(i, "asset", "");
            local r = asset == "rock" ? 0.55 : (asset == "grass" ? 0.28 : 0.12);
            local g = asset == "rock" ? 0.52 : (asset == "grass" ? 0.62 : 0.42);
            local b = asset == "rock" ? 0.48 : (asset == "grass" ? 0.22 : 0.16);
            local size = asset == "oak" || asset == "pine" ? 7.0 : 3.0;
            gfx.drawSolidRect(viewX + points.getX(i) * scale,
                              viewZ + points.getZ(i) * scale, size, size, r, g, b, 1.0);
        }
    }
    gfx.drawSolidRect(viewX + sourceX * scale - 5.0, viewZ + sourceZ * scale - 5.0,
                      10.0, 10.0, 0.95, 0.72, 0.18, 1.0);
};
