// Project-composed procgen script editor. Native code owns Params, undo and
// preview copies. This presenter loads generators/forest.nut and runs generate.

persist pcgUi = {
    workspace = null, editor = null,
    status = "Loading forest generator...",
    undoWas = false, redoWas = false,
    frame = 0, screenshotSaved = false,
};

function requireResult(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return result.value;
}

function loadForestModule() {
    dofile("generators/forest.nut");
    requireResult(pcgUi.editor.loadModule("game:/generators/forest.nut", forestModule.id,
                                          forestModule.displayName, forestModule.kind, forestModule.schema),
                  "Load forest module");
}

function fillParams(params) {
    for (local i = 0; i < pcgUi.editor.getParamCount(); ++i) {
        local key = pcgUi.editor.getParamKey(i);
        local kind = pcgUi.editor.getParamKind(i);
        if (kind == "int") params.setInt(key, pcgUi.editor.getInt(key));
        else if (kind == "float") params.setFloat(key, pcgUi.editor.getFloat(key));
        else if (kind == "bool") params.setBool(key, pcgUi.editor.getBool(key));
        else params.setString(key, pcgUi.editor.getString(key));
    }
    params.setSeed(pcgUi.editor.getInt("seed"));
}

function rebuild() {
    if (!pcgUi.editor.isDirty()) return;
    local expected = pcgUi.editor.getRevision();
    local paramsResult = procgen.newParams();
    if (!paramsResult.ok) {
        pcgUi.editor.failPreview(paramsResult.status.summary, expected);
        pcgUi.status = paramsResult.status.summary;
        return;
    }
    local params = paramsResult.value;
    fillParams(params);
    local contextResult = procgen.beginSystem(pcgUi.editor.getModuleId(), params.getSeed());
    if (!contextResult.ok) {
        pcgUi.editor.failPreview(contextResult.status.summary, expected);
        pcgUi.status = contextResult.status.summary;
        return;
    }
    local ctx = contextResult.value;
    try {
        forestModule.generate(params, ctx);
        local commitResult = procgen.commitSystem(ctx);
        if (!commitResult.ok) throw commitResult.status.summary;
        local outputResult = procgen.getSystemOutput(pcgUi.editor.getModuleId(), "trees");
        if (!outputResult.ok) throw outputResult.status.summary;
        local stageCount = procgen.getSystemDebugStageCount(pcgUi.editor.getModuleId());
        for (local i = 0; i < stageCount; ++i) {
            local name = procgen.getSystemDebugStageName(pcgUi.editor.getModuleId(), i);
            local stage = procgen.getSystemDebugStage(pcgUi.editor.getModuleId(), name);
            if (stage.ok) pcgUi.editor.publishStage(stage.value, name);
        }
        requireResult(pcgUi.editor.publishPreview(outputResult.value, "trees", expected), "Publish preview");
        pcgUi.status = "Generated " + pcgUi.editor.getPointCount() + " points";
    } catch (error) {
        ctx.fail(error.tostring());
        procgen.commitSystem(ctx);
        pcgUi.editor.failPreview(error.tostring(), expected);
        pcgUi.status = "rebuild failed, previous preview kept: " + error.tostring();
    }
}

function buildWorkspace() {
    pcgUi.workspace = editor.newWorkspace("preview.procgen", "Forest Generator");
    pcgUi.editor = requireResult(eve.ProcgenEditorModule().create("asset.preview.forest"),
                                 "Create procgen script editor");
    requireResult(pcgUi.editor.configureWorkspace(pcgUi.workspace), "Compose workspace");
    loadForestModule();
    pcgUi.workspace.setRegionSize("left", 220.0);
    pcgUi.workspace.setRegionSize("right", 280.0);
    pcgUi.workspace.setRegionSize("bottom", 140.0);
    pcgUi.workspace.layout(config.width.tofloat(), config.height.tofloat());
}

function panelModules() {
    ui.text("Modules", "modules-title");
    ui.listItem(pcgUi.editor.getDisplayName(), "module-forest");
    ui.text("", "module-selection");
}

function panelPreview() {
    ui.text("Commit sliders to rebuild. Gold dots are the selected stage.", "preview-help");
    ui.text("", "preview-status");
    ui.viewport("procgen-world", pcgUi.workspace.getRegionW("center") - 20.0,
                pcgUi.workspace.getRegionH("center") - 72.0);
}

function panelInspector() {
    ui.text("Generator Inspector", "inspector-title");
    ui.text("", "revision");
    ui.separator("inspector-sep-1");
    for (local i = 0; i < pcgUi.editor.getParamCount(); ++i) {
        local key = pcgUi.editor.getParamKey(i);
        local kind = pcgUi.editor.getParamKind(i);
        local label = pcgUi.editor.getParamLabel(i);
        if (kind == "bool") ui.checkbox(label, pcgUi.editor.getBool(key), key);
        else if (kind == "choice") {
            local choices = "";
            for (local c = 0; c < pcgUi.editor.getParamChoiceCount(i); ++c) {
                if (c > 0) choices += ",";
                choices += pcgUi.editor.getParamChoice(i, c);
            }
            ui.combo(label, choices, 0, key);
        } else if (kind == "string") ui.inputText(label, pcgUi.editor.getString(key), key);
        else ui.slider(label, kind == "int" ? pcgUi.editor.getInt(key).tofloat() : pcgUi.editor.getFloat(key),
                       pcgUi.editor.getParamMinimum(i), pcgUi.editor.getParamMaximum(i), key);
    }
    ui.separator("inspector-sep-2");
    ui.checkbox("Live", pcgUi.editor.isLive(), "live");
    ui.beginRow("inspector-history", 8.0); ui.button("Undo", "undo"); ui.button("Redo", "redo"); ui.end();
}

function panelStages() {
    ui.text("Debug Stages", "stages-title");
    for (local i = 0; i < pcgUi.editor.getStageCount(); ++i)
        ui.listItem(pcgUi.editor.getStageName(i), "stage-" + pcgUi.editor.getStageName(i));
    ui.text("", "stage-selection");
}

panelBuilders <- {
    ["procgen.modules"]=panelModules,
    ["procgen.preview"]=panelPreview,
    ["procgen.inspector"]=panelInspector,
    ["procgen.stages"]=panelStages
};

function mountPanels() {
    for (local i = 0; i < pcgUi.workspace.getPanelCount(); ++i) {
        local id = pcgUi.workspace.getPanelId(i);
        if (!(id in panelBuilders)) continue;
        ui.beginBuild(); ui.beginWindow(pcgUi.workspace.getPanelTitle(i), "root");
        panelBuilders[id](); ui.end(); ui.mountBuildAs(id); ui.select(id);
        local region = pcgUi.workspace.getPanelRegion(i);
        ui.setHostPos(pcgUi.workspace.getRegionX(region), pcgUi.workspace.getRegionY(region), 0.0, 0.0);
        ui.setHostSize(pcgUi.workspace.getRegionW(region), pcgUi.workspace.getRegionH(region));
        ui.setHostOverlay(false);
    }
}

function applyHistory(command) {
    local result = command == "undo" ? pcgUi.editor.undo() : pcgUi.editor.redo();
    pcgUi.status = result.ok ? command + " applied" : command + ": " + result.status.summary;
}

function eventParts(path) {
    local slash = path.find("/");
    return slash == null ? ["", path] : [path.slice(0, slash), path.slice(slash + 1)];
}

function handleUiEvents() {
    local click = ui.consumeClick();
    while (click != "") {
        local event = eventParts(click); local host = event[0]; local id = event[1];
        if (id == "undo" || id == "redo") applyHistory(id);
        else if (host == "procgen.stages" && id.find("stage-") == 0) {
            local result = pcgUi.editor.selectStage(id.slice(6));
            pcgUi.status = result.ok ? "Stage " + pcgUi.editor.getSelectedStage() : result.status.summary;
        }
        click = ui.consumeClick();
    }

    local changed = ui.consumeChange();
    while (changed != "") {
        local event = eventParts(changed); local host = event[0]; local id = event[1];
        if (host == "procgen.inspector") {
            if (id == "live") {
                pcgUi.editor.setLive(ui.getValue("live") != 0);
            } else {
                local kind = "";
                for (local i = 0; i < pcgUi.editor.getParamCount(); ++i)
                    if (pcgUi.editor.getParamKey(i) == id) kind = pcgUi.editor.getParamKind(i);
                local result = { ok = true, status = { summary = "" } };
                if (kind == "int") result = pcgUi.editor.setInt(id, ui.getValue(id).tointeger());
                else if (kind == "float") result = pcgUi.editor.setFloat(id, ui.getValue(id));
                else if (kind == "bool") result = pcgUi.editor.setBool(id, ui.getValue(id) != 0);
                else if (kind == "choice" || kind == "string") result = pcgUi.editor.setString(id, ui.getText(id));
                pcgUi.status = result.ok ? id + " committed · revision " + pcgUi.editor.getRevision()
                                         : result.status.summary;
            }
        }
        changed = ui.consumeChange();
    }
}

function updateKeyboardShortcuts() {
    local control = keyboard.isDown("lctrl") || keyboard.isDown("rctrl") || keyboard.isDown("ctrl");
    local undo = control && (keyboard.isDown("z") || keyboard.isDown("Z"));
    local redo = control && (keyboard.isDown("y") || keyboard.isDown("Y"));
    if (undo && !pcgUi.undoWas) applyHistory("undo");
    if (redo && !pcgUi.redoWas) applyHistory("redo");
    pcgUi.undoWas = undo; pcgUi.redoWas = redo;
}

function updateLabels() {
    ui.select("procgen.preview");
    ui.setText("preview-status", format("%d points  stage %s  rev %d",
        pcgUi.editor.getPointCount(), pcgUi.editor.getSelectedStage(), pcgUi.editor.getRevision()));
    ui.select("procgen.inspector");
    ui.setText("revision", "Revision " + pcgUi.editor.getRevision() +
               (pcgUi.editor.getPreviewFailureSummary() == "" ? "" : "  " + pcgUi.editor.getPreviewFailureSummary()));
    for (local i = 0; i < pcgUi.editor.getParamCount(); ++i) {
        local key = pcgUi.editor.getParamKey(i);
        local kind = pcgUi.editor.getParamKind(i);
        if (kind == "int") ui.setValue(key, pcgUi.editor.getInt(key).tofloat());
        else if (kind == "float") ui.setValue(key, pcgUi.editor.getFloat(key));
        else if (kind == "bool") ui.setValue(key, pcgUi.editor.getBool(key) ? 1.0 : 0.0);
    }
    ui.setValue("live", pcgUi.editor.isLive() ? 1.0 : 0.0);
    ui.setEnabled("undo", pcgUi.editor.canUndo());
    ui.setEnabled("redo", pcgUi.editor.canRedo());
    ui.select("procgen.modules");
    ui.setText("module-selection", "Active: " + pcgUi.editor.getModuleId());
    ui.select("procgen.stages");
    ui.setText("stage-selection", "Active: " + pcgUi.editor.getSelectedStage());
}

function drawPreview() {
    ui.select("procgen.preview");
    local canvas = ui.viewportCanvas("procgen-world");
    if (canvas == null) return;
    gfx.setCanvas(canvas); gfx.clear();
    local width = pcgUi.workspace.getRegionW("center") - 20.0;
    local height = pcgUi.workspace.getRegionH("center") - 72.0;
    gfx.drawSolidRect(0.0, 0.0, width, height, 0.055, 0.075, 0.07, 1.0);
    local count = pcgUi.editor.getPointCount();
    local maxX = 1.0;
    local maxZ = 1.0;
    for (local i = 0; i < count; ++i) {
        if (pcgUi.editor.getPointX(i) > maxX) maxX = pcgUi.editor.getPointX(i);
        if (pcgUi.editor.getPointZ(i) > maxZ) maxZ = pcgUi.editor.getPointZ(i);
    }
    local scale = (width - 48.0) / maxX;
    local scaleZ = (height - 48.0) / maxZ;
    if (scaleZ < scale) scale = scaleZ;
    for (local i = 0; i < count; ++i) {
        local x = 24.0 + pcgUi.editor.getPointX(i) * scale;
        local y = height - 24.0 - pcgUi.editor.getPointZ(i) * scale;
        local size = 4.0 + (pcgUi.editor.getPointSeed(i) % 4);
        gfx.drawSolidRect(x - size * 0.5, y - size * 0.5, size, size, 0.85, 0.64, 0.18, 1.0);
    }
    gfx.setCanvas(null);
}

eve_init = function() {
    gfx.setBackgroundColor(0.09, 0.11, 0.14, 1.0);
    buildWorkspace(); ui.setTheme("dark"); ui.setNavKeyboard(true); mountPanels(); rebuild();
    print("procgen-script-editor: points=" + pcgUi.editor.getPointCount() + "\n");
};

eve_reload <- function() {
    loadForestModule();
    mountPanels();
    rebuild();
};

eve_update = function(dt) {
    handleUiEvents(); updateKeyboardShortcuts(); rebuild(); updateLabels();
};

eve_render = function() {
    gfx.clear();
    drawPreview();
    ui.beginFrameAndRender();
    pcgUi.frame += 1;
    if (!pcgUi.screenshotSaved && pcgUi.frame > 90 && gfx.saveFramePng("procgen-script-editor.png")) {
        pcgUi.screenshotSaved = true;
        print("procgen-script-editor: saved procgen-script-editor.png\n");
    }
};
