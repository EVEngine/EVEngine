// Code-first procedural pipeline. Edit this file while the example is running:
// eve_reload rebuilds a complete staging snapshot and atomically commits it.

if (!("forestSeed" in getroottable())) forestSeed <- 42;
if (!("forestPoints" in getroottable())) forestPoints <- null;
if (!("pipelineStatus" in getroottable())) pipelineStatus <- "not generated";
if (!("debugStage" in getroottable())) debugStage <- "trees";
if (!("debugPoints" in getroottable())) debugPoints <- null;
if (!("roadPoints" in getroottable())) roadPoints <- null;

gfx.setBackgroundColor(0.055, 0.075, 0.07, 1.0);

function selectDebugStage(name) {
    debugStage = name;
    debugPoints = procgen.getSystemDebugStage("forest", debugStage);
}

function rebuildForest() {
    local ctx = procgen.beginSystem("forest", forestSeed);
    if (ctx == null) {
        pipelineStatus = procgen.lastError();
        return;
    }

    try {
        local candidates = procgen.sampleGrid(28, 18, 28.0, ctx.seedFor("candidates"), 0.8);
        ctx.captureDebug("candidates", candidates);
        ctx.trace("sampleGrid", 0, candidates.getCount(), 0.0);

        local road = procgen.newPointSet();
        road.add(20.0, 0.0, 40.0);
        road.add(260.0, 0.0, 170.0);
        road.add(510.0, 0.0, 150.0);
        road.add(720.0, 0.0, 410.0);
        roadPoints = procgen.sampleSpline(road, 10.0, ctx.seedFor("road preview"), 0.0);
        local awayFromRoad = procgen.filterSplineDistance(candidates, road, 42.0, 100000.0);
        ctx.captureDebug("outside road", awayFromRoad);
        ctx.captureDebug("road", roadPoints);
        ctx.trace("exclude road", candidates.getCount(), awayFromRoad.getCount(), 0.0);

        local trees = procgen.selfPrune(awayFromRoad, 32.0);
        ctx.captureDebug("trees", trees);
        ctx.trace("self prune", awayFromRoad.getCount(), trees.getCount(), 0.0);
        if (!ctx.publish("trees", trees)) throw ctx.getError();

        if (!procgen.commitSystem(ctx)) throw procgen.lastError();
        forestPoints = procgen.getSystemOutput("forest", "trees");
        selectDebugStage(debugStage);
        pipelineStatus = procgen.getSystemDebugReport("forest");
        print("\n" + pipelineStatus + "\n");
    } catch (error) {
        ctx.fail(error.tostring());
        // A failed commit closes staging but leaves the last committed forest intact.
        procgen.commitSystem(ctx);
        forestPoints = procgen.getSystemOutput("forest", "trees");
        pipelineStatus = "rebuild failed, previous snapshot kept: " + error.tostring();
    }
}

eve_init <- function() {
    rebuildForest();
};

eve_reload <- function() {
    rebuildForest();
};

eve_update <- function(dt) {
    if (key_just_pressed("R")) {
        forestSeed += 1;
        rebuildForest();
    }
    if (key_just_pressed("1")) selectDebugStage("candidates");
    if (key_just_pressed("2")) selectDebugStage("outside road");
    if (key_just_pressed("3")) selectDebugStage("trees");
};

eve_render <- function() {
    gfx.clear();
    if (roadPoints != null) {
        for (local i = 0; i < roadPoints.getCount(); i += 1) {
            gfx.drawSolidRect(68.0 + roadPoints.getX(i), 68.0 + roadPoints.getZ(i), 5.0, 5.0,
                              0.22, 0.24, 0.21, 1.0);
        }
    }
    if (debugPoints != null) {
        for (local i = 0; i < debugPoints.getCount(); i += 1) {
            local x = 70.0 + debugPoints.getX(i);
            local y = 70.0 + debugPoints.getZ(i);
            gfx.drawSolidRect(x - 2.0, y - 2.0, 4.0, 4.0, 0.85, 0.64, 0.18, 0.8);
        }
    }
    if (forestPoints == null) return;
    for (local i = 0; i < forestPoints.getCount(); i += 1) {
        local x = 70.0 + forestPoints.getX(i);
        local y = 70.0 + forestPoints.getZ(i);
        local size = 5.0 + (forestPoints.getPointSeed(i) % 5);
        gfx.drawSolidRect(x - size * 0.5, y - size * 0.5, size, size,
                          0.18, 0.48, 0.25, 1.0);
    }
};
