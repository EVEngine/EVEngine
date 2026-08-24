// Code-first procedural pipeline. Edit this file while the example is running:
// eve_reload rebuilds a complete staging snapshot and atomically commits it.

if (!("forestSeed" in getroottable())) forestSeed <- 42;
if (!("forestPoints" in getroottable())) forestPoints <- null;
if (!("pipelineStatus" in getroottable())) pipelineStatus <- "not generated";
if (!("debugStage" in getroottable())) debugStage <- "trees";
if (!("debugPoints" in getroottable())) debugPoints <- null;

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

        // This circular exclusion stands in for a future road/spline exclusion input.
        local awayFromPlaza = procgen.excludeRadius(candidates, 380.0, 230.0, 105.0);
        ctx.captureDebug("outside plaza", awayFromPlaza);
        ctx.trace("exclude plaza", candidates.getCount(), awayFromPlaza.getCount(), 0.0);

        local trees = procgen.selfPrune(awayFromPlaza, 32.0);
        ctx.captureDebug("trees", trees);
        ctx.trace("self prune", awayFromPlaza.getCount(), trees.getCount(), 0.0);
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
    if (key_just_pressed("2")) selectDebugStage("outside plaza");
    if (key_just_pressed("3")) selectDebugStage("trees");
};

eve_render <- function() {
    gfx.clear();
    gfx.drawSolidRect(380.0 - 105.0, 230.0 - 105.0, 210.0, 210.0, 0.12, 0.15, 0.14, 1.0);
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
