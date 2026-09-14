// Project-composed UI theme editor. Native code owns the catalog, token
// preview, transactions and undo/redo.

persist themeUi = {
    workspace = null, themes = null,
    status = "Loading theme catalog...",
    undoWas = false, redoWas = false,
    frame = 0, screenshotSaved = false,
};

function requireResult(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return result.value;
}

function buildWorkspace() {
    themeUi.workspace = editor.newWorkspace("preview.ui-theme", "Preview Theme Editor");
    themeUi.themes = requireResult(eve.UiEditorModule().create("asset.preview.ui-theme"),
                                   "Create UI theme editor");
    requireResult(themeUi.themes.configureWorkspace(themeUi.workspace), "Compose workspace");
    themeUi.status = "Seeded Dark and Light";
    themeUi.workspace.setRegionSize("left", 220.0);
    themeUi.workspace.setRegionSize("right", 300.0);
    themeUi.workspace.setRegionSize("bottom", 0.0);
    themeUi.workspace.layout(config.width.tofloat(), config.height.tofloat());
}

function panelThemes() {
    ui.text("Themes", "themes-title");
    for (local i = 0; i < themeUi.themes.getThemeCount(); ++i) {
        local label = themeUi.themes.getThemeName(i);
        if (themeUi.themes.getThemeActive(i)) label += " (active)";
        ui.listItem(label, "theme-" + themeUi.themes.getThemeId(i));
    }
    ui.text("", "theme-selection");
    ui.separator("themes-sep");
    ui.beginRow("theme-create", 8.0);
    ui.button("New from Dark", "new-dark");
    ui.button("New from Light", "new-light");
    ui.end();
    ui.beginRow("theme-actions", 8.0);
    ui.button("Duplicate", "duplicate");
    ui.button("Delete", "delete");
    ui.end();
}

function panelPreview() {
    ui.text("Gallery uses a host-local theme override.", "preview-help");
    ui.text("", "preview-status");
    ui.separator("preview-sep");
    ui.button("Primary action", "gallery-button");
    ui.slider("Sample slider", 0.45, 0.0, 1.0, "gallery-slider");
    ui.colorPalette("Sample color", 0.18, 0.42, 0.86, 1.0, "gallery-color");
    ui.checkbox("Sample checkbox", true, "gallery-check");
    ui.inputText("Label", "Preview text", "gallery-input");
    ui.beginCard("gallery-card");
    ui.text("Card body inherits the selected scheme.", "gallery-card-text");
    ui.end();
}

function panelInspector() {
    ui.text("Theme Inspector", "inspector-title");
    ui.text("", "selection");
    ui.text("", "revision");
    ui.separator("inspector-sep-1");
    ui.colorPalette("Button", 0.18, 0.20, 0.24, 1.0, "button-color");
    ui.colorPalette("Window Bg", 0.14, 0.14, 0.16, 1.0, "window-color");
    ui.slider("Frame rounding", 3.0, 0.0, 16.0, "frame-rounding");
    ui.slider("Font scale", 1.0, 0.5, 2.0, "font-scale");
    ui.beginRow("inspector-actions", 8.0);
    ui.button("Activate", "activate");
    ui.button("Reset base", "reset");
    ui.end();
    ui.beginRow("inspector-history", 8.0);
    ui.button("Undo", "undo");
    ui.button("Redo", "redo");
    ui.end();
    ui.textWrapped("Activate publishes to the process theme. Preview stays on the selected asset.",
                   260.0, "inspector-help");
}

panelBuilders <- {
    ["ui.themes"]=panelThemes,
    ["ui.preview"]=panelPreview,
    ["ui.inspector"]=panelInspector
};

function mountPanels() {
    for (local i = 0; i < themeUi.workspace.getPanelCount(); ++i) {
        local id = themeUi.workspace.getPanelId(i);
        if (!(id in panelBuilders)) continue;
        ui.beginBuild(); ui.beginWindow(themeUi.workspace.getPanelTitle(i), "root");
        panelBuilders[id](); ui.end(); ui.mountBuildAs(id); ui.select(id);
        local region = themeUi.workspace.getPanelRegion(i);
        local x = themeUi.workspace.getRegionX(region);
        local y = themeUi.workspace.getRegionY(region);
        local w = themeUi.workspace.getRegionW(region);
        local h = themeUi.workspace.getRegionH(region);
        ui.setHostPos(x, y, 0.0, 0.0);
        ui.setHostSize(w, h);
        ui.setHostOverlay(false);
    }
}

function applyHistory(command) {
    local result = command == "undo" ? themeUi.themes.undo() : themeUi.themes.redo();
    themeUi.status = result.ok ? command + " applied" : command + ": " + result.status.summary;
}

function eventParts(path) {
    local slash = path.find("/");
    return slash == null ? ["", path] : [path.slice(0, slash), path.slice(slash + 1)];
}

function commitColor(id, token) {
    return themeUi.themes.setColor(token, ui.getColorR(id), ui.getColorG(id),
        ui.getColorB(id), ui.getColorA(id));
}

function handleUiEvents() {
    local click = ui.consumeClick();
    while (click != "") {
        local event = eventParts(click); local host = event[0]; local id = event[1];
        if (id == "undo" || id == "redo") applyHistory(id);
        else if (id == "new-dark") {
            local result = themeUi.themes.createFromPreset("dusk", "Dusk", "dark");
            themeUi.status = result.ok ? "Created Dusk" : result.status.summary;
        } else if (id == "new-light") {
            local result = themeUi.themes.createFromPreset("dawn", "Dawn", "light");
            themeUi.status = result.ok ? "Created Dawn" : result.status.summary;
        } else if (id == "duplicate") {
            local result = themeUi.themes.duplicateSelected("studio", "Studio");
            themeUi.status = result.ok ? "Duplicated Studio" : result.status.summary;
        } else if (id == "delete") {
            local result = themeUi.themes.deleteSelected();
            themeUi.status = result.ok ? "Deleted theme" : result.status.summary;
        } else if (id == "activate") {
            local result = themeUi.themes.setActiveSelected();
            themeUi.status = result.ok ? "Activated " + themeUi.themes.getActiveId()
                                       : result.status.summary;
        } else if (id == "reset") {
            local result = themeUi.themes.resetSelectedToBase();
            themeUi.status = result.ok ? "Reset to base preset" : result.status.summary;
        } else if (host == "ui.themes" && id.find("theme-") == 0) {
            requireResult(themeUi.themes.selectTheme(id.slice(6)), "Select theme");
            themeUi.status = "Selected " + themeUi.themes.getSelectedId();
        }
        click = ui.consumeClick();
    }

    local changed = ui.consumeChange();
    while (changed != "") {
        local event = eventParts(changed); local id = event[1];
        local result = { ok = true, status = { summary = "" } };
        if (id == "button-color") result = commitColor("button-color", "color.button");
        else if (id == "window-color") result = commitColor("window-color", "color.windowBg");
        else if (id == "frame-rounding")
            result = themeUi.themes.setFloat("geometry.frameRounding", ui.getValue("frame-rounding"));
        else if (id == "font-scale")
            result = themeUi.themes.setFloat("typography.fontScale", ui.getValue("font-scale"));
        if (id == "button-color" || id == "window-color" ||
            id == "frame-rounding" || id == "font-scale")
            themeUi.status = result.ok ? id + " committed · revision " + themeUi.themes.getRevision()
                                       : result.status.summary;
        changed = ui.consumeChange();
    }
}

function updateKeyboardShortcuts() {
    local control = keyboard.isDown("lctrl") || keyboard.isDown("rctrl") || keyboard.isDown("ctrl");
    local undo = control && (keyboard.isDown("z") || keyboard.isDown("Z"));
    local redo = control && (keyboard.isDown("y") || keyboard.isDown("Y"));
    if (undo && !themeUi.undoWas) applyHistory("undo");
    if (redo && !themeUi.redoWas) applyHistory("redo");
    themeUi.undoWas = undo; themeUi.redoWas = redo;
}

function updateLabels() {
    ui.select("ui.preview");
    ui.setText("preview-status", format("%s  rev %d / preview %d  runtime %s",
        themeUi.themes.getSelectedId(), themeUi.themes.getRevision(),
        themeUi.themes.getPreviewRevision(), themeUi.themes.getPreviewRuntimeName()));
    themeUi.themes.applyPreviewHost("ui.preview");
    ui.select("ui.inspector");
    ui.setText("selection", "ui.theme " + themeUi.themes.getSelectedId());
    ui.setText("revision", "Revision " + themeUi.themes.getRevision() +
        "  active " + themeUi.themes.getActiveId());
    ui.setColor("button-color", themeUi.themes.getColorR("color.button"),
        themeUi.themes.getColorG("color.button"), themeUi.themes.getColorB("color.button"),
        themeUi.themes.getColorA("color.button"));
    ui.setColor("window-color", themeUi.themes.getColorR("color.windowBg"),
        themeUi.themes.getColorG("color.windowBg"), themeUi.themes.getColorB("color.windowBg"),
        themeUi.themes.getColorA("color.windowBg"));
    ui.setValue("frame-rounding", themeUi.themes.getFloat("geometry.frameRounding"));
    ui.setValue("font-scale", themeUi.themes.getFloat("typography.fontScale"));
    ui.setEnabled("undo", themeUi.themes.canUndo());
    ui.setEnabled("redo", themeUi.themes.canRedo());
    ui.select("ui.themes");
    ui.setText("theme-selection", "Selected: " + themeUi.themes.getSelectedId());
}

eve_init = function() {
    gfx.setBackgroundColor(0.09, 0.11, 0.14, 1.0);
    buildWorkspace(); ui.setTheme("dark"); ui.setNavKeyboard(true); mountPanels();
    print("ui-theme-editor: themes=" + themeUi.themes.getThemeCount() + "\n");
};

eve_update = function(dt) {
    handleUiEvents(); updateKeyboardShortcuts(); updateLabels();
};

eve_render = function() {
    gfx.clear();
    ui.beginFrameAndRender();
    themeUi.frame += 1;
    if (!themeUi.screenshotSaved && themeUi.frame > 90 && gfx.saveFramePng("ui-theme-editor.png")) {
        themeUi.screenshotSaved = true;
        print("ui-theme-editor: saved ui-theme-editor.png\n");
    }
};
