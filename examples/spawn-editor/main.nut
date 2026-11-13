// Formal authoring path: actual RPG definitions, canonical map assets and owned native editor application.
local target = null;
local editorRpg = null;
local closing = false;
local lastInputError = "";
function checked(result, label) {
    if (!result.ok) throw label + ": " + result.status.summary;
    return result.value;
}
function readAsset(path) {
    local handle = file(path, "r");
    if (handle == null) throw "Missing editor asset: " + path;
    local text = handle.read();
    handle.close();
    return text;
}
eve_init <- function() {
    gfx.setBackgroundColor(0.04, 0.05, 0.07, 1.0);
    if ("dev" in eve) {
        eve.dev.ai.setVisible(false);
        eve.dev.console.setVisible(false);
        eve.dev.stats.setVisible(false);
    }
    if (!fs.setIdentity("evengine-spawn-editor", false))
        throw "Editor save directory configuration failed";
    if (!fs.createDirectory("spawn-editor"))
        throw "Editor export directory creation failed";
    editorRpg = eve.RPG();
    if (editorRpg.registerSkillsFromJson(readAsset("data/skills.json")) != 1)
        throw "Editor skill content admission failed";
    checked(rpgSpawnEditor.publishUnit(readAsset("data/unit.json")), "publish editor unit");
    checked(rpgSpawnEditor.publishTemplate(readAsset("data/template.json")), "publish editor template");
    checked(rpgSpawnEditor.open(win, fs, readAsset("data/binding.json"),
        "{\"schema\":\"eve.map.object-contract\",\"version\":1,\"types\":[]}",
        ".", "spawn-editor-forest", "content://spawn-editor/forest.json", "spawn-editor/forest.spawns.json"), "open editor");
    checked(rpgSpawnEditor.startPreview(fs, "01900000-0000-7000-8000-000000000032", 42, 6.0), "start isolated preview");
    checked(rpgSpawnEditor.stepPreview(0), "initial preview step");
    target = gfx.newCanvas(480, 480);
    if (target == null) throw "Editor canvas allocation failed";
    print("spawn-editor: canonical RPG content and private map editor opened\n");
};
eve_update <- function(dt) {
    if (closing) {
        // Keep the requested shutdown pending until the runtime accepts quit (including startup guards).
        platform_event.pushData("quit", "");
        return;
    }
    local result = rpgSpawnEditor.update(win, ui);
    if (!result.ok) {
        // Native input/close errors remain visible in the session panel; keep the editor and its drafts alive.
        local message = result.status.summary;
        if (message != lastInputError) print("spawn-editor input: " + message + "\n");
        lastInputError = message;
        return;
    }
    lastInputError = "";
    if (result.value.closed) {
        closing = true;
        platform_event.pushData("quit", "");
        return;
    }
    // UI requests are executed here, outside component builds and the platform event observer.
    local imported = result.value.import_action;
    local importResult = null;
    if (imported == "prepare")
        importResult = rpgSpawnEditor.prepareTiledImport(fs, result.value.import_path,
            result.value.import_guid, result.value.import_prototype);
    else if (imported == "commit") importResult = rpgSpawnEditor.commitTiledImport();
    else if (imported == "cancel") importResult = rpgSpawnEditor.cancelTiledImport();
    if (importResult != null && !importResult.ok)
        print("spawn-editor import: " + importResult.status.summary + "\n");
    local action = result.value.preview_action;
    local previewResult = null;
    if (action == "start")
        previewResult = rpgSpawnEditor.startPreview(fs, "01900000-0000-7000-8000-000000000032", 42, 6.0);
    else if (action == "step") previewResult = rpgSpawnEditor.stepPreview(1000000000);
    else if (action == "stop") previewResult = rpgSpawnEditor.stopPreview();
    else if (action == "event") previewResult = rpgSpawnEditor.triggerPreviewEvent(result.value.preview_event);
    else if (action == "death") previewResult = rpgSpawnEditor.defeatPreviewUnit(result.value.preview_subject);
    else if (action == "players") previewResult = rpgSpawnEditor.applyPreviewPlayers(result.value.preview_players);
    if (previewResult != null && !previewResult.ok)
        print("spawn-editor preview: " + previewResult.status.summary + "\n");
};
eve_render <- function() {
    gfx.clear();
    if (!closing) checked(rpgSpawnEditor.render(gfx, target, ui), "render editor");
    else ui.beginFrameAndRender();
};
eve_quit <- function() {
    checked(rpgSpawnEditor.shutdown(win, "require_clean"), "close editor");
};
