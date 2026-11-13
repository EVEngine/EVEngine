// Formal template authoring: canonical RPG content and existing document/form/save/export owners.
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
    checked(rpgSpawnEditor.openTemplate(win, fs, readAsset("data/template.json"),
        ".", "spawn-editor-wolves-template", "content://spawn-editor/wolves-template.json",
        "spawn-editor/wolves.template.json"), "open template editor");
    print("spawn-template-editor: canonical RPG content and private template editor opened\n");
};
eve_update <- function(dt) {
    if (closing) return;
    local result = rpgSpawnEditor.update(win, ui);
    if (!result.ok) {
        // Native input/close errors remain visible in the session panel; keep the editor and its drafts alive.
        local message = result.status.summary;
        if (message != lastInputError) print("spawn-template-editor input: " + message + "\n");
        lastInputError = message;
        return;
    }
    lastInputError = "";
    if (result.value.closed) {
        closing = true;
        platform_event.pushData("quit", "");
    }
};
eve_render <- function() {
    gfx.clear();
    if (!closing) checked(rpgSpawnEditor.render(gfx, null, ui), "render editor");
    else ui.beginFrameAndRender();
};
eve_quit <- function() {
    checked(rpgSpawnEditor.shutdown(win, "require_clean"), "close editor");
};
