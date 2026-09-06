// Encapsulated PointSet generator. The editor loads schema + generate();
// it does not parse this file itself.

forestModule <- {
    id = "forest"
    displayName = "Forest scatter"
    kind = "points"
    schema = [
        { key="seed", kind="int", min=1, max=9999, defaultValue=42, label="Seed" },
        { key="cols", kind="int", min=8, max=48, defaultValue=28, label="Columns" },
        { key="rows", kind="int", min=8, max=48, defaultValue=18, label="Rows" },
        { key="spacing", kind="float", min=8.0, max=64.0, step=1.0, defaultValue=28.0, label="Spacing" },
        { key="density", kind="float", min=0.1, max=1.0, step=0.05, defaultValue=0.8, label="Density" },
        { key="pruneRadius", kind="float", min=4.0, max=80.0, step=1.0, defaultValue=32.0, label="Prune radius" }
    ]
}

forestModule.generate <- function(params, ctx) {
    if (!ctx.beginTrace("sampleGrid", 0)) throw ctx.getError();
    local candidatesResult = procgen.sampleGrid(
        params.getInt("cols", 28), params.getInt("rows", 18),
        params.getFloat("spacing", 28.0), ctx.seedFor("candidates"),
        params.getFloat("density", 0.8));
    if (!candidatesResult.ok) throw candidatesResult.status.summary;
    local candidates = candidatesResult.value;
    if (!ctx.endTrace(candidates.getCount())) throw ctx.getError();
    if (!ctx.captureDebug("candidates", candidates)) throw ctx.getError();

    if (!ctx.beginTrace("self prune", candidates.getCount())) throw ctx.getError();
    local treesResult = procgen.selfPrune(candidates, params.getFloat("pruneRadius", 32.0));
    if (!treesResult.ok) throw treesResult.status.summary;
    local trees = treesResult.value;
    if (!ctx.endTrace(trees.getCount())) throw ctx.getError();
    if (!ctx.captureDebug("trees", trees)) throw ctx.getError();
    if (!ctx.publish("trees", trees)) throw ctx.getError();
}
