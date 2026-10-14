// Fixed IDE chrome from EditorWorkspace descriptors — one locked host with
// AppFrame / DockArea / PanelFrame / TabContainer, not one draggable panel each.

persist shell = {
    workspace = null,
    uiEditor = null,
    revision = -1,
    status = "Ready",
};

function ensureWorkspace() {
    if (shell.workspace != null) return;
    local editor = eve.Editor();
    shell.workspace = editor.newWorkspace("demo.shell", "Workspace Layout Shell");
    shell.workspace.registerPanel("assets", "Assets", "left", 0);
    shell.workspace.setPanelCapability("assets", "demo.shell");
    shell.workspace.setPanelContext("assets", "list");
    shell.workspace.registerPanel("viewport", "Viewport", "center", 0);
    shell.workspace.setPanelCapability("viewport", "demo.shell");
    shell.workspace.setPanelContext("viewport", "preview");
    shell.workspace.registerPanel("inspector", "Inspector", "right", 0);
    shell.workspace.setPanelCapability("inspector", "demo.shell");
    shell.workspace.setPanelContext("inspector", "inspector");
    shell.workspace.registerPanel("timeline", "Timeline", "bottom", 0);
    shell.workspace.setPanelCapability("timeline", "demo.shell");
    shell.workspace.setPanelContext("timeline", "timeline");
    shell.workspace.registerPanel("history", "History", "bottom", 1);
    shell.workspace.setPanelCapability("history", "demo.shell");
    shell.workspace.setPanelContext("history", "history");
    shell.workspace.activatePanel("viewport");
    shell.workspace.setMode("edit");
}

function mountShell() {
    ensureWorkspace();
    if (shell.uiEditor == null) shell.uiEditor = eve.UiEditorModule();
    local ok = shell.uiEditor.mountWorkspaceShell(ui, shell.workspace, "workspace-shell");
    if (!ok) {
        shell.status = "mount failed";
        return;
    }
    shell.revision = shell.workspace.getRevision();
    shell.status = "Fixed shell mounted (not freely draggable)";
}

function buildManualChrome() {
    // Imperative layout primitives for the same IDE shape without the C++ shell helper.
    ui.beginBuild();
    ui.beginWindow("Manual AppFrame", "manual-root");
    ui.beginAppFrame("manual-frame");
    ui.beginToolbar("manual-toolbar");
    ui.text("Manual AppFrame + DockArea", "manual-title");
    ui.spacer("manual-fill", 1.0);
    ui.badge("FIXED", "manual-badge");
    ui.end();

    ui.beginSplitPane("row", 0.24, "manual-split");
    ui.beginDockArea("left", "manual-left");
    ui.beginPanelFrame("Palette", "manual-palette");
    ui.text("DockArea left", "manual-left-body");
    ui.button("Select", "manual-select");
    ui.end();
    ui.end();

    ui.beginDockArea("center", "manual-center");
    ui.beginTabContainer("page-a", "manual-tabs");
    ui.beginTabBar("manual-tabbar");
    ui.addTab("Page A", "page-a", true, "tab-a");
    ui.addTab("Page B", "page-b", false, "tab-b");
    ui.end();
    ui.beginPanelFrame("Page A", "page-a");
    ui.text("TabContainer page A", "page-a-body");
    ui.end();
    ui.beginPanelFrame("Page B", "page-b");
    ui.text("TabContainer page B", "page-b-body");
    ui.end();
    ui.end();
    ui.end();
    ui.end();

    ui.beginStatusBar("manual-status");
    ui.text(shell.status, "manual-status-text");
    ui.spacer("manual-status-fill", 1.0);
    ui.badge("LOCKED", "manual-locked");
    ui.end();
    ui.end();
    ui.end();
    ui.mountBuildAs("manual-shell");
    ui.setHostMovable(false);
    ui.setHostResizable(false);
    ui.setHostSize(520.0, 420.0);
    ui.setHostPos(40.0, 40.0, 0.0, 0.0);
}

eve_init = function() {
    ui.setTheme("dark");
    mountShell();
    buildManualChrome();
};

eve_update = function(dt) {
    local click = ui.consumeClick();
    while (click != "") {
        if (click == "manual-shell/manual-select") {
            shell.status = "Palette Select clicked";
            ui.select("manual-shell");
            ui.setText("manual-status-text", shell.status);
        } else if (click == "manual-shell/tab-a" || click == "manual-shell/tab-b") {
            shell.status = "Tab switched via layout component";
            ui.select("manual-shell");
            ui.setText("manual-status-text", shell.status);
        }
        click = ui.consumeClick();
    }

    if (shell.workspace != null && shell.workspace.getRevision() != shell.revision) {
        mountShell();
    }
};

eve_render = function() {
    gfx.clear();
    ui.beginFrameAndRender();
};
