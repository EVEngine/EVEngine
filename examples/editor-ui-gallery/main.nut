// Complete desktop-editor composition built from the reusable UI primitives.

if (!("darkTheme" in getroottable())) darkTheme <- false;

function buildGallery() {
    // `switch` is a Squirrel keyword, so bind the native method explicitly.
    local addSwitch = ui["switch"].bindenv(ui);
    ui.beginBuild();
    ui.beginWindow("Editor UI Kit", "root");

    ui.beginMenuBar("main-menu");
    ui.beginMenu("File", "file-menu");
    ui.menuItem("New Scene", "Ctrl+N", "new-scene");
    ui.menuItem("Save", "Ctrl+S", "menu-save");
    ui.end();
    ui.beginMenu("Edit", "edit-menu");
    ui.menuItem("Undo", "Ctrl+Z", "menu-undo");
    ui.menuItem("Redo", "Ctrl+Y", "menu-redo");
    ui.end();
    ui.beginMenu("View", "view-menu");
    ui.menuItem("Toggle Theme", "", "theme");
    ui.end();
    ui.end();

    ui.beginToolbar("top-toolbar");
    ui.iconButton("folder-open", "", "open");
    ui.setItemTooltip("Open project");
    ui.iconButton("save", "", "save");
    ui.setItemTooltip("Save scene");
    ui.iconButton("undo", "", "undo");
    ui.iconButton("redo", "", "redo");
    ui.spacer("toolbar-fill", 1.0);
    ui.badge("EDIT MODE", "mode");
    ui.iconButton("play", "Run", "play");
    ui.iconButton("settings", "", "settings");
    ui.end();

    ui.beginSplitPane("row", 0.23, "workspace");

    ui.beginSidebar("left-sidebar", 0.0);
    ui.searchField("Search tools and assets...", "", "tool-search");
    ui.setItemTooltip("Filter the editor toolbox");
    ui.sectionHeader("TOOLBOX", "toolbox-title");
    ui.beginToolbox("tools", 0.0, 3);
    ui.iconButton("pointer", "", "select");
    ui.setItemTooltip("Select");
    ui.iconButton("move", "", "move");
    ui.setItemTooltip("Move");
    ui.iconButton("paint-brush", "", "paint");
    ui.setItemTooltip("Paint");
    ui.iconButton("cube", "", "cube");
    ui.setItemTooltip("Create cube");
    ui.iconButton("image", "", "image");
    ui.setItemTooltip("Import image");
    ui.iconButton("camera", "", "camera");
    ui.setItemTooltip("Camera");
    ui.iconButton("layers", "", "layers");
    ui.setItemTooltip("Layers");
    ui.iconButton("database", "", "database");
    ui.setItemTooltip("Database");
    ui.iconButton("code", "", "code");
    ui.setItemTooltip("Script editor");
    ui.end();
    ui.sectionHeader("PROJECT", "project-title");
    ui.beginCard("project-card");
    ui.icon("folder-open", "project-icon");
    ui.sameLine("project-line");
    ui.text("EVEngine / scenes", "project-path");
    ui.text("3 scenes  ·  18 assets", "project-meta");
    ui.end();
    ui.end();

    ui.beginCard("inspector-card");
    ui.beginRow("inspector-heading", 8.0);
    ui.sectionHeader("INSPECTOR", "inspector-title");
    ui.spacer("inspector-fill", 1.0);
    ui.badge("MODIFIED", "modified");
    ui.end();

    ui.text("Selected object", "selection-label");
    ui.inputText("Name", "Player Camera", "object-name");
    ui.combo("Type", "Perspective\nOrthographic\nPanoramic", 0, "camera-type");
    ui.separator("properties-separator");

    ui.sectionHeader("RENDERING", "rendering-title");
    addSwitch("Visible", true, "visible");
    addSwitch("Post processing", true, "post-processing");
    addSwitch("Debug bounds", false, "debug-bounds");
    ui.slider("Exposure", 0.72, 0.0, 1.0, "exposure");
    ui.progress(0.72, "exposure-progress", "72%");

    ui.sectionHeader("QUICK ACTIONS", "actions-title");
    ui.beginRow("quick-actions", 8.0);
    ui.iconButton("save", "Save", "save-selection");
    ui.iconButton("refresh", "Reset", "reset");
    ui.iconButton("eye", "Preview", "preview");
    ui.end();

    ui.sectionHeader("WEB / DESKTOP COMMON", "common-title");
    ui.beginRow("common-row", 8.0);
    ui.badge("READY", "ready-badge");
    ui.badge("LOCAL", "local-badge");
    ui.button("Primary action", "primary-action");
    ui.end();
    ui.textWrapped("Cards, compact controls, semantic icons and stable spacing share one modern visual language.", 620.0, "description");
    ui.end();

    ui.end();

    ui.beginStatusBar("status-bar");
    ui.icon("check", "status-icon");
    ui.text("Editor UI ready", "status-text");
    ui.spacer("status-fill", 1.0);
    ui.text("Scene 01", "scene-name");
    ui.badge("60 FPS", "fps");
    ui.end();

    ui.end();
    ui.mountBuildAs("framework-editor");
    ui.setHostMovable(true);
    ui.setHostResizable(true);
    ui.setHostSize(820.0, 690.0);
    ui.setHostPos(30.0, 28.0, 0.0, 0.0);
}

// The game HUD uses the same retained toolkit as the editor shell, but owns a
// separate host, input order and theme scope. It deliberately stays dark when
// the editor switches to the light preset.
function buildGameHud() {
    ui.beginBuild();
    ui.beginWindow("Game HUD", "hud-root");
    ui.setThemeScope("dark");
    ui.beginGroup("hud-surface");
    ui.beginRow("hud-heading", 8.0);
    ui.badge("LIVE GAME", "runtime-mode");
    ui.spacer("hud-fill", 1.0);
    ui.text("Rooftop District", "location");
    ui.end();

    ui.text("Player", "player-title");
    ui.progress(0.78, "health", "Health 78 / 100");
    ui.progress(0.46, "stamina", "Stamina 46 / 100");
    ui.beginRow("hud-actions", 8.0);
    ui.iconButton("eye", "Inspect", "inspect-target");
    ui.setItemTabIndex(0);
    ui.setItemFocusOrder("", "interact");
    ui.setItemAccessibility("button", "Inspect target", "Show target details");
    ui.button("Interact", "interact");
    ui.setItemTabIndex(1);
    ui.setItemFocusOrder("inspect-target", "");
    ui.setItemAccessibility("button", "Interact", "Use the selected object");
    ui.end();
    ui.text("Tab / arrows share the retained focus graph", "hud-help");
    ui.end();
    ui.end();
    ui.mountBuildAs("game-hud");
    ui.setHostOverlay(true);
    ui.setHostOverlayAlpha(0.94);
    ui.setHostSize(350.0, 290.0);
    ui.setHostPos(890.0, 48.0, 0.0, 0.0);
    ui.requestFocus("inspect-target");
}

eve_init = function() {
    ui.setTheme(darkTheme ? "dark" : "light");
    ui.setNavKeyboard(true);
    buildGallery();
    buildGameHud();
};

eve_update = function(dt) {
    local click = ui.consumeClick();
    while (click != "") {
        if (click == "framework-editor/theme") {
            darkTheme = !darkTheme;
            ui.setTheme(darkTheme ? "dark" : "light");
            ui.select("framework-editor");
            ui.setText("status-text", darkTheme ? "Dark theme active" : "Light theme active");
        } else if (click == "framework-editor/play") {
            ui.select("framework-editor");
            ui.setText("status-text", "Preview started");
        } else if (click == "framework-editor/save" || click == "framework-editor/save-selection") {
            ui.select("framework-editor");
            ui.setText("status-text", "Scene saved");
        } else if (click == "game-hud/inspect-target") {
            ui.select("framework-editor");
            ui.setText("status-text", "Runtime target selected in shared inspector");
        } else if (click == "game-hud/interact") {
            ui.select("game-hud");
            ui.setText("hud-help", "Interaction dispatched through the shared UI router");
        }
        click = ui.consumeClick();
    }
};

eve_render = function() {
    gfx.clear();
    ui.beginFrameAndRender();
};
