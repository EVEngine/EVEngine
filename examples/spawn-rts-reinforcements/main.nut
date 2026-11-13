// Canonical private Map + RTS Table + Crowd + ActionRuntime; script only supplies frames and presentation.
local tick = 0;
local observed = null;
local published = 0;
local verified = false;
local restoredMoving = false;
local saveEpoch = "01900000-0000-7000-8000-000000000799";
local faction = "01900000-0000-7000-8000-000000000889";
function checked(result, label) {
    if (!result.ok) throw label + ": " + result.status.summary;
    return result.value;
}
function readAsset(path) {
    local handle = file(path, "r");
    if (handle == null) throw "Missing reinforcement asset: " + path;
    local text = handle.read();
    handle.close();
    return text;
}
eve_init <- function() {
    gfx.setBackgroundColor(0.04, 0.07, 0.10, 1.0);
    if (!fs.setIdentity("spawn-rts-reinforcements", false) || !fs.setupWriteDirectory())
        throw "Reinforcement save directory setup failed";
    checked(mapRtsSpawn.prepare(readAsset("data/content.json"), "[" + readAsset("data/template.json") + "]",
        readAsset("data/binding.json"), "01900000-0000-7000-8000-000000000779", faction, 42, 16.0, 8.0),
        "prepare private map and RTS");
    checked(mapRtsSpawn.commit(), "publish ready RTS runtime");
};
eve_update <- function(dt) {
    local nanoseconds = (dt * 1000000000.0).tointeger();
    if (nanoseconds <= 0) return;
    tick += 1;
    local report = checked(mapRtsSpawn.advance(tick, nanoseconds), "actual RTS and spawn frame");
    if (report.diagnostic_count != 0) throw "Reinforcement source diagnostics";
    published += report.units;
    if (published > 6) throw "Restored reinforcements replayed";
    observed = checked(mapRtsSpawn.captureState(), "actual RTS observations");
    if (observed.sources.len() != 1) throw "Expected one reinforcement source";
    local source = observed.sources[0];
    if (source.alive != observed.units.len() || source.total != published)
        throw "RTS and spawn source membership mismatch";
    local allMoved = observed.units.len() == 6;
    foreach (unit in observed.units) {
        if (unit.faction != faction || unit.health <= 0.0) throw "Invalid real reinforcement";
        if (unit.x <= 4.0) allMoved = false;
    }
    if (!restoredMoving && published >= 3 && observed.units[0].x > 2.0) {
        local before = observed;
        checked(mapRtsSpawn.freezeRuntime(saveEpoch, 1), "freeze moving reinforcements");
        if (fs.writeTextAtomic("reinforcements-moving.json", checked(mapRtsSpawn.sealRuntime(), "seal moving units")) != 1)
            throw "Moving reinforcement save failed";
        checked(mapRtsSpawn.prepareRuntimeRestore(fs.readText("reinforcements-moving.json"), saveEpoch), "read moving units");
        checked(mapRtsSpawn.commitRuntimeRestore(), "replace complete moving RTS owner");
        observed = checked(mapRtsSpawn.captureState(), "restored moving units");
        if (observed.tick != before.tick || observed.units.len() != before.units.len() || observed.crowd_agents != before.crowd_agents)
            throw "Moving RTS frame/agent restore mismatch";
        foreach (unit in before.units) {
            local found = false;
            foreach (restored in observed.units) if (restored.subject == unit.subject) {
                found = true;
                if (restored.x != unit.x || restored.y != unit.y || restored.order != unit.order || restored.faction != unit.faction)
                    throw "Moving RTS body/order restore mismatch";
            }
            if (!found) throw "Moving RTS subject lost";
        }
        restoredMoving = true;
        print("spawn-rts-reinforcements: moving units file restored with Crowd and attack-move orders\n");
    }
    if (!verified && restoredMoving && published == 6 && source.total == 6 && allMoved) {
        checked(mapRtsSpawn.freezeRuntime(saveEpoch, 2), "freeze all six reinforcements");
        if (fs.writeTextAtomic("reinforcements-six.json", checked(mapRtsSpawn.sealRuntime(), "seal all six units")) != 1)
            throw "Six-unit reinforcement save failed";
        checked(mapRtsSpawn.prepareRuntimeRestore(fs.readText("reinforcements-six.json"), saveEpoch), "read all six units");
        checked(mapRtsSpawn.commitRuntimeRestore(), "restore all six units");
        observed = checked(mapRtsSpawn.captureState(), "all six restored");
        if (observed.units.len() != 6 || observed.sources[0].total != 6 || observed.crowd_agents != 6)
            throw "Six-unit owner restore mismatch";
        verified = true;
        print("spawn-rts-reinforcements: six real RTS units admitted and moved toward attack-move target\n");
        print("spawn-rts-reinforcements: six-unit file restored without duplicate reinforcements\n");
    }
};
eve_render <- function() {
    gfx.clear();
    // Decorative grid matches the example's immutable 20 by 20, one-unit map asset.
    gfx.drawSolidRect(28.0, 44.0, 480.0, 480.0, 0.09, 0.14, 0.19, 1.0);
    for (local cell = 0; cell <= 20; cell += 1) {
        gfx.drawSolidRect(28.0 + cell * 24.0, 44.0, 1.0, 480.0, 0.15, 0.21, 0.25, 1.0);
        gfx.drawSolidRect(28.0, 44.0 + cell * 24.0, 480.0, 1.0, 0.15, 0.21, 0.25, 1.0);
    }
    gfx.drawSolidRect(70.0, 230.0, 12.0, 12.0, 0.2, 0.7, 0.35, 1.0);
    gfx.drawSolidRect(406.0, 230.0, 12.0, 12.0, 0.85, 0.3, 0.2, 1.0);
    if (observed != null) {
        foreach (unit in observed.units)
            gfx.drawSolidRect(28.0 + unit.x * 24.0 - 7.0, 44.0 + unit.y * 24.0 - 7.0,
                14.0, 14.0, 0.25, 0.7, 1.0, 1.0);
        for (local index = 0; index < 6; index += 1)
            gfx.drawSolidRect(28.0 + index * 80.0, 16.0, 72.0, 12.0,
                index < published ? 0.2 : 0.15, index < published ? 0.75 : 0.2, 0.3, 1.0);
    }
    if (verified) gfx.drawSolidRect(28.0, 544.0, 480.0, 8.0, 0.2, 0.8, 0.35, 1.0);
};
eve_quit <- function() {
    checked(mapRtsSpawn.close(), "explicit RTS source cleanup");
};
