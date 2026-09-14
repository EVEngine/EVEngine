local runtimeStamp = null;
local runtimeSource = null;
local runtimeTarget = null;
local spawnProgress = null;
local taskQueue = null;

function checked(result, label) {
    if (!result.ok) throw "pcg-runtime-orchestration: " + label + " failed";
    return result;
}

eve_init = function() {
    local sourceResult = procgen.newHeightmap(3, 3);
    local targetResult = procgen.newHeightmap(5, 5);
    checked(sourceResult, "source heightmap");
    checked(targetResult, "target heightmap");
    runtimeSource = sourceResult.value;
    runtimeTarget = targetResult.value;
    for (local y = 0; y < 3; ++y) for (local x = 0; x < 3; ++x) runtimeSource.setHeight(x, y, 1.0);
    for (local y = 0; y < 5; ++y) for (local x = 0; x < 5; ++x) runtimeTarget.setHeight(x, y, 20.0);

    runtimeStamp = eve.PcgRuntimeStamper();
    checked(runtimeStamp.configure("Pcg\\Stamps\\Runtime", true, true), "configure");
    checked(runtimeStamp.loadStamp(runtimeSource, "Runtime"), "load");
    checked(runtimeStamp.execute(runtimeTarget, 0.0, 0.0, 1.0, 1.0), "stamp");
    if (runtimeTarget.height(2, 2) != 6.0 || runtimeStamp.getStatus() != 3) throw "stamp mismatch";

    spawnProgress = eve.PcgSpawnProgress();
    checked(spawnProgress.updateRule("Terrain", 10, 3, 4, 1), "progress rule");
    checked(spawnProgress.updateRuleFraction(0.5), "progress fraction");
    if (abs(spawnProgress.getProgress() - 0.35) > 0.0001) throw "progress mismatch";

    taskQueue = eve.PcgTaskQueue();
    local addResult = taskQueue.add(0.25);
    checked(addResult, "task add");
    checked(taskQueue.tick(0.25), "task tick");
    if (taskQueue.getReadyTaskId() != addResult.value) throw "task ID mismatch";
    checked(taskQueue.resolveReady(true), "task resolve");

    gfx.setBackgroundColor(0.025, 0.055, 0.09, 1.0);
    print("PCG_HOST_SYSTEM limitFrame="+("limitFrame" in system)+" type="+typeof system+"\n");
    print("PCG_RUNTIME_ORCHESTRATION_PASS stamp=6 progress=0.35 queue=0\n");
    eve.bootBench <- true;
};

eve_update = function(dt) {};
eve_render = function() {
    gfx.clear();
    gfx.drawSolidRect(64.0, 80.0, 512.0, 200.0, 0.12, 0.48, 0.28, 1.0);
    gfx.drawSolidRect(88.0, 105.0, 464.0, 150.0, 0.04, 0.15, 0.10, 1.0);
    gfx.drawSolidRect(88.0, 215.0, 162.0, 18.0, 0.25, 0.82, 0.42, 1.0);
};
