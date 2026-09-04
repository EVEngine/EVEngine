// Project-composed avatar editor. Native code owns the document, composite
// preview, transactions and undo/redo.

persist avatarUi = {
    workspace = null, avatar = null, mouseDown = false,
    status = "Loading preview face...",
    undoWas = false, redoWas = false,
    frame = 0, screenshotSaved = false,
};

function requireResult(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return result.value;
}

function buildWorkspace() {
    avatarUi.workspace = editor.newWorkspace("preview.avatar", "Preview Face Editor");
    avatarUi.avatar = requireResult(eve.AvatarEditorModule().create("asset.preview.face"),
                                    "Create avatar document editor");
    requireResult(avatarUi.avatar.configureWorkspace(avatarUi.workspace), "Compose workspace");
    avatarUi.status = "Seeded two-layer face";
    avatarUi.workspace.setRegionSize("left", 220.0);
    avatarUi.workspace.setRegionSize("right", 280.0);
    avatarUi.workspace.setRegionSize("bottom", 160.0);
    avatarUi.workspace.layout(config.width.tofloat(), config.height.tofloat());
}

function panelLayers() {
    ui.text("Layers", "layers-title");
    for (local i = 0; i < avatarUi.avatar.getLayerCount(); ++i)
        ui.listItem(avatarUi.avatar.getLayerName(i), "layer-" + avatarUi.avatar.getLayerId(i));
    ui.text("", "layer-selection");
    ui.separator("layers-sep");
    ui.beginRow("layer-actions", 8.0);
    ui.button("Add layer", "add-layer");
    ui.button("Delete layer", "delete-layer");
    ui.end();
}

function panelPreview() {
    ui.text("Click a rectangle to select its layer.", "preview-help");
    ui.text("", "preview-status");
    ui.viewport("avatar-pose", avatarUi.workspace.getRegionW("center") - 20.0,
                avatarUi.workspace.getRegionH("center") - 72.0);
}

function panelInspector() {
    ui.text("Avatar Inspector", "inspector-title");
    ui.text("", "selection");
    ui.text("", "revision");
    ui.separator("inspector-sep-1");
    ui.checkbox("Visible", true, "visible");
    ui.slider("Z", 0.0, -8.0, 8.0, "z");
    ui.beginRow("inspector-history", 8.0); ui.button("Undo", "undo"); ui.button("Redo", "redo"); ui.end();
    ui.textWrapped("Deleting a layer or parameter still referenced by an expression is rejected.",
                   245.0, "inspector-help");
}

function panelParameters() {
    ui.text("Parameters", "parameters-title");
    for (local i = 0; i < avatarUi.avatar.getParameterCount(); ++i)
        ui.slider(avatarUi.avatar.getParameterName(i), avatarUi.avatar.getParameterValue(i),
                  avatarUi.avatar.getParameterMinimum(i), avatarUi.avatar.getParameterMaximum(i),
                  "param-" + avatarUi.avatar.getParameterId(i));
    ui.beginRow("parameter-actions", 8.0);
    ui.button("Add parameter", "add-parameter");
    ui.button("Delete parameter", "delete-parameter");
    ui.end();
}

function panelExpressions() {
    ui.text("Expressions", "expressions-title");
    for (local i = 0; i < avatarUi.avatar.getExpressionCount(); ++i) {
        local label = avatarUi.avatar.getExpressionName(i);
        local count = avatarUi.avatar.getExpressionChannelCount(i);
        if (count > 0) label += " -> " + avatarUi.avatar.getExpressionChannelName(i, 0);
        ui.listItem(label, "expr-" + avatarUi.avatar.getExpressionId(i));
    }
    ui.beginRow("expression-actions", 8.0);
    ui.button("Add expression", "add-expression");
    ui.button("Delete expression", "delete-expression");
    ui.end();
}

panelBuilders <- {
    ["avatar.layers"]=panelLayers,
    ["avatar.preview"]=panelPreview,
    ["avatar.inspector"]=panelInspector,
    ["avatar.parameters"]=panelParameters,
    ["avatar.expressions"]=panelExpressions
};

function mountPanels() {
    for (local i = 0; i < avatarUi.workspace.getPanelCount(); ++i) {
        local id = avatarUi.workspace.getPanelId(i);
        if (!(id in panelBuilders)) continue;
        ui.beginBuild(); ui.beginWindow(avatarUi.workspace.getPanelTitle(i), "root");
        panelBuilders[id](); ui.end(); ui.mountBuildAs(id); ui.select(id);
        local region = avatarUi.workspace.getPanelRegion(i);
        local x = avatarUi.workspace.getRegionX(region);
        local y = avatarUi.workspace.getRegionY(region);
        local w = avatarUi.workspace.getRegionW(region);
        local h = avatarUi.workspace.getRegionH(region);
        if (id == "avatar.inspector") h = h * 0.58;
        if (id == "avatar.expressions") { y = y + h * 0.58; h = h * 0.42; }
        ui.setHostPos(x, y, 0.0, 0.0);
        ui.setHostSize(w, h);
        ui.setHostOverlay(false);
    }
}

function applyHistory(command) {
    local result = command == "undo" ? avatarUi.avatar.undo() : avatarUi.avatar.redo();
    avatarUi.status = result.ok ? command + " applied" : command + ": " + result.status.summary;
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
        else if (id == "add-layer") {
            local result = avatarUi.avatar.createLayer("brow", "brow");
            avatarUi.status = result.ok ? "Created brow" : result.status.summary;
        } else if (id == "delete-layer") {
            local result = avatarUi.avatar.deleteSelectedLayer();
            avatarUi.status = result.ok ? "Deleted layer" : result.status.summary;
        } else if (id == "add-parameter") {
            local result = avatarUi.avatar.createParameter("blink", "blink");
            avatarUi.status = result.ok ? "Created blink" : result.status.summary;
        } else if (id == "delete-parameter") {
            local result = avatarUi.avatar.deleteSelectedParameter();
            avatarUi.status = result.ok ? "Deleted parameter" : result.status.summary;
        } else if (id == "add-expression") {
            local result = avatarUi.avatar.createExpression("neutral", "neutral");
            avatarUi.status = result.ok ? "Created expression" : result.status.summary;
        } else if (id == "delete-expression") {
            local result = avatarUi.avatar.deleteSelectedExpression();
            avatarUi.status = result.ok ? "Deleted expression" : result.status.summary;
        } else if (host == "avatar.layers" && id.find("layer-") == 0) {
            requireResult(avatarUi.avatar.selectLayer(id.slice(6)), "Select layer");
            avatarUi.status = "Selected layer " + avatarUi.avatar.getSelectedId();
        } else if (host == "avatar.expressions" && id.find("expr-") == 0) {
            requireResult(avatarUi.avatar.selectExpression(id.slice(5)), "Select expression");
            avatarUi.status = "Selected expression " + avatarUi.avatar.getSelectedId();
        }
        click = ui.consumeClick();
    }

    local changed = ui.consumeChange();
    while (changed != "") {
        local event = eventParts(changed); local host = event[0]; local id = event[1];
        if (host == "avatar.inspector") {
            ui.select("avatar.inspector");
            local result = { ok = true, status = { summary = "" } };
            if (id == "visible") result = avatarUi.avatar.setLayerVisible(ui.getChecked("visible"));
            else if (id == "z") result = avatarUi.avatar.setLayerZ(ui.getValue("z").tointeger());
            avatarUi.status = result.ok ? id + " committed · revision " + avatarUi.avatar.getRevision()
                                        : result.status.summary;
        } else if (host == "avatar.parameters" && id.find("param-") == 0) {
            requireResult(avatarUi.avatar.selectParameter(id.slice(6)), "Select parameter");
            local result = avatarUi.avatar.setParameterValue(ui.getValue(id));
            avatarUi.status = result.ok ? "smile preview · revision " + avatarUi.avatar.getRevision()
                                        : result.status.summary;
        }
        changed = ui.consumeChange();
    }
}

function updatePreviewPointer() {
    ui.select("avatar.preview");
    local hovered = ui.viewportHovered("avatar-pose");
    local down = hovered && mouse.isDown(1);
    if (down && !avatarUi.mouseDown) {
        local hit = avatarUi.avatar.pointerDown(ui.viewportMouseX("avatar-pose"), ui.viewportMouseY("avatar-pose"));
        avatarUi.status = hit.ok ? "Preview " + avatarUi.avatar.getSelectedId() : hit.status.summary;
    }
    avatarUi.mouseDown = down;
}

function updateKeyboardShortcuts() {
    local control = keyboard.isDown("lctrl") || keyboard.isDown("rctrl") || keyboard.isDown("ctrl");
    local undo = control && (keyboard.isDown("z") || keyboard.isDown("Z"));
    local redo = control && (keyboard.isDown("y") || keyboard.isDown("Y"));
    if (undo && !avatarUi.undoWas) applyHistory("undo");
    if (redo && !avatarUi.redoWas) applyHistory("redo");
    avatarUi.undoWas = undo; avatarUi.redoWas = redo;
}

function updateLabels() {
    ui.select("avatar.preview");
    ui.setText("preview-status", format("%s  rev %d / preview %d",
        avatarUi.avatar.getSelectedId(), avatarUi.avatar.getRevision(),
        avatarUi.avatar.getPreviewRevision()));
    ui.select("avatar.inspector");
    ui.setText("selection", avatarUi.avatar.getSelectedType() + " " + avatarUi.avatar.getSelectedId());
    ui.setText("revision", "Revision " + avatarUi.avatar.getRevision());
    if (avatarUi.avatar.getSelectedType() == "avatar.layer") {
        local z = 0;
        for (local i = 0; i < avatarUi.avatar.getLayerCount(); ++i)
            if (avatarUi.avatar.getLayerId(i) == avatarUi.avatar.getSelectedId()) {
                ui.setChecked("visible", avatarUi.avatar.getLayerVisible(i));
                z = avatarUi.avatar.getLayerZ(i);
            }
        ui.setValue("z", z.tofloat());
    }
    ui.setEnabled("undo", avatarUi.avatar.canUndo());
    ui.setEnabled("redo", avatarUi.avatar.canRedo());
    ui.select("avatar.layers");
    ui.setText("layer-selection", "Active: " + avatarUi.avatar.getSelectedId());
    ui.select("avatar.parameters");
    for (local i = 0; i < avatarUi.avatar.getParameterCount(); ++i)
        ui.setValue("param-" + avatarUi.avatar.getParameterId(i), avatarUi.avatar.getParameterValue(i));
}

function drawPreview() {
    ui.select("avatar.preview");
    local canvas = ui.viewportCanvas("avatar-pose");
    if (canvas == null) return;
    gfx.setCanvas(canvas); gfx.clear();
    local width = avatarUi.workspace.getRegionW("center") - 20.0;
    local height = avatarUi.workspace.getRegionH("center") - 72.0;
    gfx.drawSolidRect(0.0, 0.0, width, height, 0.05, 0.06, 0.08, 1.0);
    local count = avatarUi.avatar.getPreviewLayerCount();
    for (local i = 0; i < count; ++i) {
        local x = avatarUi.avatar.getPreviewX(i) * 2.0;
        local y = height - (avatarUi.avatar.getPreviewY(i) + avatarUi.avatar.getPreviewH(i)) * 2.0 - 40.0;
        local w = avatarUi.avatar.getPreviewW(i) * 2.0;
        local h = avatarUi.avatar.getPreviewH(i) * 2.0;
        gfx.drawSolidRect(x, y, w, h, avatarUi.avatar.getPreviewR(i), avatarUi.avatar.getPreviewG(i),
                          avatarUi.avatar.getPreviewB(i), avatarUi.avatar.getPreviewA(i));
        if (avatarUi.avatar.getPreviewSelected(i))
            gfx.drawSolidRect(x, y, w, 3.0, 1.0, 0.82, 0.32, 1.0);
    }
    gfx.setCanvas(null);
}

eve_init = function() {
    gfx.setBackgroundColor(0.09, 0.11, 0.14, 1.0);
    buildWorkspace(); ui.setTheme("dark"); ui.setNavKeyboard(true); mountPanels();
    print("avatar-document-editor: layers=" + avatarUi.avatar.getLayerCount() +
          " expressions=" + avatarUi.avatar.getExpressionCount() + "\n");
};

eve_update = function(dt) {
    handleUiEvents(); updatePreviewPointer(); updateKeyboardShortcuts(); updateLabels();
};

eve_render = function() {
    gfx.clear();
    drawPreview();
    ui.beginFrameAndRender();
    avatarUi.frame += 1;
    if (!avatarUi.screenshotSaved && avatarUi.frame > 90 && gfx.saveFramePng("avatar-document-editor.png")) {
        avatarUi.screenshotSaved = true;
        print("avatar-document-editor: saved avatar-document-editor.png\n");
    }
};
