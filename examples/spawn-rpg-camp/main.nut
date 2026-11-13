// Game path: versioned assets -> canonical isolated map transaction -> real RPG actors -> graphics.
local frameTick = 0;
local campRpg = null;
local announced = false;
local elapsed = 0.0;
local struck = false;
local replaced = false;
local fallenSubject = null;
local saveEpoch = "01900000-0000-7000-8000-000000000778";
function checked(result, label) {
    if (!result.ok) throw label + ": " + result.status.summary;
    return result.value;
}
function readAsset(path) {
    local handle = file(path, "r");
    if (handle == null) throw "Missing camp asset: " + path;
    local text = handle.read();
    handle.close();
    return text;
}
eve_init <- function() {
    campRpg = eve.RPG();
    if (!fs.setIdentity("spawn-rpg-camp", false) || !fs.setupWriteDirectory())
        throw "Camp save directory setup failed";
    gfx.setBackgroundColor(0.05, 0.1, 0.08, 1.0);
    if (campRpg.registerSkillsFromJson("[{\"id\":\"wolf.claw\",\"cooldown\":1.0,\"castTime\":0.0,\"costs\":[],\"grantedEffects\":[],\"tags\":[\"attack\"]}]") != 1)
        throw "Camp skill admission failed";
    checked(mapRpgSpawn.prepareRuntime("[" + readAsset("data/unit.json") + "]",
        "[" + readAsset("data/template.json") + "]", readAsset("data/binding.json"),
        "01900000-0000-7000-8000-000000000777", 42, 6.0), "prepare runtime");
    checked(mapRpgSpawn.commitRuntime(), "publish runtime");
};
eve_update <- function(dt) {
    elapsed += dt;
    if (announced && !struck && elapsed >= 1.0) {
        local previous = checked(mapRpgSpawn.captureRuntimeState(), "observe before input");
        if (previous.sources.len() != 1 || previous.sources[0].subjects.len() == 0)
            throw "Camp training target missing";
        fallenSubject = previous.sources[0].subjects[0];
        local applied = checked(mapRpgSpawn.damage(fallenSubject, 1000.0), "camp training strike");
        if (applied <= 0.0) throw "Camp strike did not damage a living actor";
        struck = true;
    }
    frameTick += 1;
    local nanoseconds = (dt * 1000000000.0).tointeger();
    if (nanoseconds <= 0) nanoseconds = 1;
    local report = checked(mapRpgSpawn.advanceRuntime(frameTick, nanoseconds), "camp frame");
    if (report.diagnostic_count != 0) throw "Camp frame contains source diagnostics";
    if (!announced && report.units == 4) {
        print("spawn-rpg-camp: four real RPG actors published\n");
        announced = true;
        checked(mapRpgSpawn.freezeRuntime(saveEpoch, 1), "freeze whole runtime");
        local saved = checked(mapRpgSpawn.sealRuntime(), "seal whole runtime");
        if (fs.writeTextAtomic("spawn-rpg-camp-save.json", saved) != 1) throw "Camp save failed";
        checked(mapRpgSpawn.prepareRuntimeRestore(fs.readText("spawn-rpg-camp-save.json"), saveEpoch), "read camp save");
        checked(mapRpgSpawn.commitRuntimeRestore(), "replace whole runtime");
        print("spawn-rpg-camp: whole runtime file restored\n");
    }
    local snapshot = checked(mapRpgSpawn.captureRuntimeState(), "observe camp");
    if (snapshot.sources.len() != 1) throw "Expected one camp source";
    local source = snapshot.sources[0];
    if (struck && !replaced && source.alive == 4) {
        local oldBodyPresent = false;
        foreach (subject in source.subjects) if (subject == fallenSubject) oldBodyPresent = true;
        if (!oldBodyPresent) {
            replaced = true;
            print("spawn-rpg-camp: replacement published after real death\n");
        }
    }
};
eve_render <- function() {
    gfx.clear();
    gfx.drawSolidRect(8.0, 8.0, 624.0, 432.0, 0.12, 0.22, 0.15, 1.0);
    // Marker size is a presentation choice; occupancy radius remains in the asset/domain setup.
    checked(mapRpgSpawn.draw(gfx, 18.0, 18.0, 0.9, 0.5, 0.2, 1.0), "draw camp");
};
eve_quit <- function() {
    checked(mapRpgSpawn.close("remove"), "close camp");
};
