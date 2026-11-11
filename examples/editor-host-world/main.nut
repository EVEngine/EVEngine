// Multi-domain EditorHost with a visible status view of the real mount state.
// Domains advertise EditorPackage; Host autoMount + attachEditor completes MCP.

persist hostWorld = {
    editor = null,
    host = null,
    action = null,
    voxel = null,
    status = "boot"
};

function boot() {
    if (hostWorld.editor != null) return;
    hostWorld.editor = eve.Editor();
    hostWorld.action = eve.ActionEditorModule();
    hostWorld.voxel = eve.VoxelEditorModule();
    hostWorld.host = hostWorld.editor.newHost("world", "World Host");
    local mounted = hostWorld.host.autoMount();
    if (mounted < 0) throw "EditorHost package mount failed";
    if (!hostWorld.host.exportMcp()) throw "EditorHost MCP export failed";
    // Prefer eve.mcp.attachEditor when DevTools is present; exportMcp is equivalent.
    if ("mcp" in eve && "attachEditor" in eve.mcp) eve.mcp.attachEditor(hostWorld.host);
    hostWorld.status = "Mounted packages: " + mounted + " | Panels: " + hostWorld.host.getPanelCount();
    ui.beginBuild();
    ui.beginWindow("EditorHost / World", "root");
    ui.text("Native editor package composition", "title");
    ui.separator("sep");
    ui.text(hostWorld.status, "status");
    ui.text("Action package: " + (hostWorld.host.isMounted("action_editor") ? "mounted" : "not mounted"), "action");
    ui.text("Voxel package: " + (hostWorld.host.isMounted("voxel_editor") ? "mounted" : "not mounted"), "voxel");
    ui.text("MCP scope: world. Domain panels are UI-neutral descriptors.", "scope");
    ui.end();
    ui.mountBuildAs("world-status");
    ui.select("world-status");
    ui.setHostOverlay(true);
    ui.setHostPos(24.0, 24.0, 0.0, 0.0);
    ui.setHostSize(760.0, 260.0);
}

function eve_init() {
    boot();
}

function eve_render() {
    gfx.clear();
    ui.beginFrameAndRender();
}
