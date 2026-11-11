// Canonical map/RPG session: real unit receipts -> HP damage -> death handoff -> finite waves.
local frameTick = 0;
local waveRpg = null;
local strikeClock = 0.0;
local observed = null;
local published = 0;
local completed = false;
local roundtripped = false;
local saveEpoch = "01900000-0000-7000-8000-000000000788";
function checked(result, label) {
    if (!result.ok) throw label + ": " + result.status.summary;
    return result.value;
}
function readAsset(path) {
    local handle = file(path, "r");
    if (handle == null) throw "Missing wave asset: " + path;
    local text = handle.read();
    handle.close();
    return text;
}
eve_init <- function() {
    waveRpg = eve.RPG();
    if (!fs.setIdentity("spawn-rpg-waves", false) || !fs.setupWriteDirectory())
        throw "Wave save directory setup failed";
    gfx.setBackgroundColor(0.05, 0.08, 0.12, 1.0);
    if (waveRpg.registerSkillsFromJson("[{\"id\":\"wolf.claw\",\"cooldown\":1.0,\"castTime\":0.0,\"costs\":[],\"grantedEffects\":[],\"tags\":[\"attack\"]}]") != 1)
        throw "Wave skill admission failed";
    checked(mapRpgSpawn.prepareRuntime("[" + readAsset("data/unit.json") + "," + readAsset("data/alpha.json") + "]",
        "[" + readAsset("data/template.json") + "]", readAsset("data/binding.json"),
        "01900000-0000-7000-8000-000000000778", 42, 6.0), "prepare wave runtime");
    checked(mapRpgSpawn.commitRuntime(), "publish wave runtime");
};
eve_update <- function(dt) {
    strikeClock += dt;
    if (!completed && observed != null && observed.subjects.len() != 0 && strikeClock >= 0.25) {
        strikeClock = 0.0;
        local applied = checked(mapRpgSpawn.damage(observed.subjects[0], 1000.0), "training strike");
        if (applied <= 0.0) throw "Training strike did not damage a living wave unit";
    }
    frameTick += 1;
    local nanoseconds = (dt * 1000000000.0).tointeger();
    if (nanoseconds <= 0) nanoseconds = 1;
    local report = checked(mapRpgSpawn.advanceRuntime(frameTick, nanoseconds), "wave frame");
    if (report.diagnostic_count != 0) throw "Wave frame contains source diagnostics";
    local snapshot = checked(mapRpgSpawn.captureRuntimeState(), "observe waves");
    if (snapshot.sources.len() != 1) throw "Expected one wave source";
    observed = snapshot.sources[0];
    if (report.units != 0) {
        local expected = published == 0 ? 3 : (published == 3 ? 2 : (published == 5 ? 1 : 0));
        if (report.units != expected) throw "Wave was skipped or replayed";
        published += report.units;
        print("spawn-rpg-waves: wave " + (observed.wave + 1) + " published " + report.units + " real RPG actors\n");
    }
    if (observed.total.tointeger() != published) throw "Wave receipt and core total mismatch";
    if (!roundtripped && published == 5) {
        local oldDelivery = checked(mapRpgSpawn.readRuntimeEvents(saveEpoch, 1), "freeze event prefix");
        checked(mapRpgSpawn.freezeRuntime(saveEpoch, 1), "freeze second wave");
        local saved = checked(mapRpgSpawn.sealRuntime(), "seal second wave");
        if (fs.writeTextAtomic("waves-mid.json", saved) != 1) throw "Wave save failed";
        checked(mapRpgSpawn.prepareRuntimeRestore(fs.readText("waves-mid.json"), saveEpoch), "read second wave");
        checked(mapRpgSpawn.commitRuntimeRestore(), "restore second wave");
        if (mapRpgSpawn.acknowledgeRuntimeEvents(oldDelivery.receipt).ok) throw "Old wave receipt accepted";
        local delivered = checked(mapRpgSpawn.readRuntimeEvents(saveEpoch, 2), "restored pending events");
        checked(mapRpgSpawn.acknowledgeRuntimeEvents(delivered.receipt), "confirm restored deaths");
        observed = checked(mapRpgSpawn.captureRuntimeState(), "restored wave state").sources[0];
        if (observed.wave != 1 || observed.alive != 2 || observed.total != "5") throw "Second wave restore mismatch";
        roundtripped = true;
        print("spawn-rpg-waves: second wave file restored with pending deaths, no wave replay\n");
    }
    if (!completed && observed.phase == "completed") {
        if (!roundtripped || published != 6 || observed.total != "6" || observed.alive != 0)
            throw "Finite wave completion ledger mismatch";
        local finalEvents = checked(mapRpgSpawn.readRuntimeEvents(saveEpoch, 3), "last deaths");
        checked(mapRpgSpawn.acknowledgeRuntimeEvents(finalEvents.receipt), "confirm last deaths");
        checked(mapRpgSpawn.freezeRuntime(saveEpoch, 3), "freeze completion");
        if (fs.writeTextAtomic("waves-complete.json", checked(mapRpgSpawn.sealRuntime(), "seal completion")) != 1)
            throw "Completion save failed";
        checked(mapRpgSpawn.prepareRuntimeRestore(fs.readText("waves-complete.json"), saveEpoch), "read completion");
        checked(mapRpgSpawn.commitRuntimeRestore(), "restore completion");
        observed = checked(mapRpgSpawn.captureRuntimeState(), "restored completion").sources[0];
        if (observed.phase != "completed" || observed.total != "6" || observed.alive != 0)
            throw "Restored completion mismatch";
        completed = true;
        print("spawn-rpg-waves: three waves completed, six real deaths reconciled\n");
        print("spawn-rpg-waves: completed file restored without restarting waves\n");
    }
};
eve_render <- function() {
    gfx.clear();
    gfx.drawSolidRect(8.0, 8.0, 624.0, 432.0, 0.10, 0.15, 0.22, 1.0);
    checked(mapRpgSpawn.draw(gfx, 18.0, 18.0, 0.9, 0.5, 0.2, 1.0), "draw actual wave units");
    if (observed != null) {
        for (local index = 0; index < 3; index += 1) {
            local done = completed || observed.wave > index;
            local active = !completed && observed.wave == index;
            gfx.drawSolidRect(16.0 + index * 204.0, 16.0, 196.0, 12.0,
                done ? 0.2 : (active ? 0.9 : 0.15), done ? 0.8 : 0.25, 0.2, 1.0);
        }
    }
};
eve_quit <- function() {
    checked(mapRpgSpawn.close("remove"), "close wave encounter");
};
