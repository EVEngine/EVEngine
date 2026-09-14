// Project-composed biome editor. Native code owns the rules, PointSet preview,
// transactions and undo/redo.

persist biomeUi = {
    workspace = null, rules = null,
    status = "Loading preview forest...",
    undoWas = false, redoWas = false,
    frame = 0, screenshotSaved = false,
};

function requireResult(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return result.value;
}

function buildWorkspace() {
    biomeUi.workspace = editor.newWorkspace("preview.biome", "Preview Forest Editor");
    biomeUi.rules = requireResult(eve.BiomeEditorModule().create("asset.preview.forest"),
                                  "Create biome rules editor");
    requireResult(biomeUi.rules.configureWorkspace(biomeUi.workspace), "Compose workspace");
    biomeUi.status = "Seeded forest layer";
    biomeUi.workspace.setRegionSize("left", 220.0);
    biomeUi.workspace.setRegionSize("right", 280.0);
    biomeUi.workspace.setRegionSize("bottom", 160.0);
    biomeUi.workspace.layout(config.width.tofloat(), config.height.tofloat());
}

function panelLayers() {
    ui.text("Layers", "layers-title");
    for (local i = 0; i < biomeUi.rules.getLayerCount(); ++i)
        ui.listItem(biomeUi.rules.getLayerName(i), "layer-" + biomeUi.rules.getLayerId(i));
    ui.text("", "layer-selection");
}

function panelPreview() {
    ui.text("Fixed seed PointSet. Gold dots are placements.", "preview-help");
    ui.text("", "preview-status");
    ui.viewport("biome-world", biomeUi.workspace.getRegionW("center") - 20.0,
                biomeUi.workspace.getRegionH("center") - 72.0);
}

function panelInspector() {
    ui.text("Biome Inspector", "inspector-title");
    ui.text("", "selection");
    ui.text("", "revision");
    ui.separator("inspector-sep-1");
    ui.slider("Density", 1.0, 0.0, 1.0, "density");
    ui.slider("Priority", 1.0, -4.0, 8.0, "priority");
    ui.separator("inspector-sep-2");
    ui.text("Exclusions", "exclusions-title");
    ui.text("", "exclusion-list");
    ui.beginRow("exclusion-actions", 8.0);
    ui.button("Add clearing", "add-exclusion");
    ui.button("Clear exclusions", "clear-exclusions");
    ui.end();
    ui.beginRow("inspector-history", 8.0); ui.button("Undo", "undo"); ui.button("Redo", "redo"); ui.end();
}

function panelAssets() {
    ui.text("Layer Assets", "assets-title");
    for (local i = 0; i < biomeUi.rules.getAssetCount(); ++i)
        ui.listItem(biomeUi.rules.getAssetRef(i), "asset-" + biomeUi.rules.getAssetId(i));
    ui.slider("Weight", 1.0, 0.1, 8.0, "weight");
}

panelBuilders <- {
    ["biome.layers"]=panelLayers,
    ["biome.preview"]=panelPreview,
    ["biome.inspector"]=panelInspector,
    ["biome.assets"]=panelAssets
};

function mountPanels() {
    for (local i = 0; i < biomeUi.workspace.getPanelCount(); ++i) {
        local id = biomeUi.workspace.getPanelId(i);
        if (!(id in panelBuilders)) continue;
        ui.beginBuild(); ui.beginWindow(biomeUi.workspace.getPanelTitle(i), "root");
        panelBuilders[id](); ui.end(); ui.mountBuildAs(id); ui.select(id);
        local region = biomeUi.workspace.getPanelRegion(i);
        ui.setHostPos(biomeUi.workspace.getRegionX(region), biomeUi.workspace.getRegionY(region), 0.0, 0.0);
        ui.setHostSize(biomeUi.workspace.getRegionW(region), biomeUi.workspace.getRegionH(region));
        ui.setHostOverlay(false);
    }
}

function applyHistory(command) {
    local result = command == "undo" ? biomeUi.rules.undo() : biomeUi.rules.redo();
    biomeUi.status = result.ok ? command + " applied" : command + ": " + result.status.summary;
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
        else if (id == "add-exclusion") {
            local result = biomeUi.rules.addExclusion("asset://preview/clearing.spatial");
            biomeUi.status = result.ok ? "Clearing excluded · " + biomeUi.rules.getPointCount() + " points"
                                       : result.status.summary;
        } else if (id == "clear-exclusions") {
            local result = biomeUi.rules.removeExclusion("asset://preview/clearing.spatial");
            biomeUi.status = result.ok ? "Exclusions cleared" : result.status.summary;
        } else if (host == "biome.layers" && id.find("layer-") == 0) {
            requireResult(biomeUi.rules.selectLayer(id.slice(6)), "Select layer");
            biomeUi.status = "Selected layer " + biomeUi.rules.getSelectedId();
        } else if (host == "biome.assets" && id.find("asset-") == 0) {
            requireResult(biomeUi.rules.selectAsset(id.slice(6)), "Select asset");
            biomeUi.status = "Selected asset " + biomeUi.rules.getSelectedId();
        }
        click = ui.consumeClick();
    }

    local changed = ui.consumeChange();
    while (changed != "") {
        local event = eventParts(changed); local host = event[0]; local id = event[1];
        if (host == "biome.inspector") {
            if (biomeUi.rules.getSelectedType() != "biome.layer")
                requireResult(biomeUi.rules.selectLayer("forest"), "Select forest layer");
            local result = { ok = true, status = { summary = "" } };
            if (id == "density") result = biomeUi.rules.setLayerDensity(ui.getValue("density"));
            else if (id == "priority") result = biomeUi.rules.setLayerPriority(ui.getValue("priority").tointeger());
            biomeUi.status = result.ok ? id + " committed · revision " + biomeUi.rules.getRevision()
                                       : result.status.summary;
        } else if (host == "biome.assets" && id == "weight") {
            if (biomeUi.rules.getSelectedType() != "biome.asset")
                requireResult(biomeUi.rules.selectAsset("oak"), "Select oak asset");
            local result = biomeUi.rules.setAssetWeight(ui.getValue("weight"));
            biomeUi.status = result.ok ? "weight committed" : result.status.summary;
        }
        changed = ui.consumeChange();
    }
}

function updateKeyboardShortcuts() {
    local control = keyboard.isDown("lctrl") || keyboard.isDown("rctrl") || keyboard.isDown("ctrl");
    local undo = control && (keyboard.isDown("z") || keyboard.isDown("Z"));
    local redo = control && (keyboard.isDown("y") || keyboard.isDown("Y"));
    if (undo && !biomeUi.undoWas) applyHistory("undo");
    if (redo && !biomeUi.redoWas) applyHistory("redo");
    biomeUi.undoWas = undo; biomeUi.redoWas = redo;
}

function exclusionText() {
    local count = biomeUi.rules.getExclusionCount();
    if (count == 0) return "None";
    local text = "";
    for (local i = 0; i < count; ++i) {
        if (i > 0) text += ", ";
        text += biomeUi.rules.getExclusionAsset(i);
    }
    return text;
}

function updateLabels() {
    ui.select("biome.preview");
    ui.setText("preview-status", format("%d points  seed %d  rev %d",
        biomeUi.rules.getPointCount(), biomeUi.rules.getSeed(), biomeUi.rules.getRevision()));
    ui.select("biome.inspector");
    ui.setText("selection", biomeUi.rules.getSelectedType() + " " + biomeUi.rules.getSelectedId());
    ui.setText("revision", "Revision " + biomeUi.rules.getRevision());
    ui.setText("exclusion-list", exclusionText());
    if (biomeUi.rules.getLayerCount() > 0) {
        local layerIndex = 0;
        ui.setValue("density", biomeUi.rules.getLayerDensity(layerIndex));
        ui.setValue("priority", biomeUi.rules.getLayerPriority(layerIndex).tofloat());
    }
    ui.setEnabled("undo", biomeUi.rules.canUndo());
    ui.setEnabled("redo", biomeUi.rules.canRedo());
    ui.select("biome.layers");
    ui.setText("layer-selection", "Active: " + biomeUi.rules.getSelectedId());
    ui.select("biome.assets");
    if (biomeUi.rules.getAssetCount() > 0) {
        local assetIndex = 0;
        ui.setValue("weight", biomeUi.rules.getAssetWeight(assetIndex));
    }
}

function drawPreview() {
    ui.select("biome.preview");
    local canvas = ui.viewportCanvas("biome-world");
    if (canvas == null) return;
    gfx.setCanvas(canvas); gfx.clear();
    local width = biomeUi.workspace.getRegionW("center") - 20.0;
    local height = biomeUi.workspace.getRegionH("center") - 72.0;
    gfx.drawSolidRect(0.0, 0.0, width, height, 0.07, 0.12, 0.08, 1.0);
    local scale = (width < height ? width : height) / 4.4;
    local originX = 24.0;
    local originZ = height - 24.0;
    local count = biomeUi.rules.getPointCount();
    for (local i = 0; i < count; ++i) {
        local x = originX + biomeUi.rules.getPointX(i) * scale;
        local y = originZ - biomeUi.rules.getPointZ(i) * scale;
        gfx.drawSolidRect(x, y, 6.0, 6.0, 0.95, 0.82, 0.28, 1.0);
    }
    gfx.setCanvas(null);
}

eve_init = function() {
    gfx.setBackgroundColor(0.09, 0.11, 0.14, 1.0);
    buildWorkspace(); ui.setTheme("dark"); ui.setNavKeyboard(true); mountPanels();
    print("biome-rules-editor: points=" + biomeUi.rules.getPointCount() + "\n");
};

eve_update = function(dt) {
    handleUiEvents(); updateKeyboardShortcuts(); updateLabels();
};

eve_render = function() {
    gfx.clear();
    drawPreview();
    ui.beginFrameAndRender();
    biomeUi.frame += 1;
    if (!biomeUi.screenshotSaved && biomeUi.frame > 90 && gfx.saveFramePng("biome-rules-editor.png")) {
        biomeUi.screenshotSaved = true;
        print("biome-rules-editor: saved biome-rules-editor.png\n");
    }
};
