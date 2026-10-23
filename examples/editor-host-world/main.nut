// Gold sample: multi-domain EditorHost in <30 lines of project glue.
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
    hostWorld.host.exportMcp();
    // Prefer eve.mcp.attachEditor when DevTools is present; exportMcp is equivalent.
    if ("mcp" in eve && "attachEditor" in eve.mcp) eve.mcp.attachEditor(hostWorld.host);
    hostWorld.status = "mounted=" + mounted + " panels=" + hostWorld.host.getPanelCount();
}

function onUpdate(dt) {
    boot();
}

function onRender() {}
