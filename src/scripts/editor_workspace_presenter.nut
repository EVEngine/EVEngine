// UI projection only. Native WorkspacePresentation owns validation and drafts;
// the domain editor remains the sole document, selection and history owner.
// One root presenter owns and drains the UI event queue for its workspace.
class EditorWorkspacePresenter {
    ui = null; workspace = null; source = null; snapshot = null;
    status = ""; inspector = ""; statusPanel = ""; statusWidget = "";
    hosts = null; actions = null; previousKeys = null;
    onClick = null; draft = false; closed = false;
    constructor(uiApi, workspaceModel, domainEditor) {
        ui = uiApi; workspace = workspaceModel; source = domainEditor;
        hosts = []; actions = { ["undo"]=true, ["redo"]=true, ["play-pause"]=true, ["stop"]=true };
        previousKeys = {};
        refresh();
    }
    function checked(result) {
        if (!result.ok) { status = result.status.summary; return false; }
        return true;
    }
    function refresh() {
        local result = source.presentationSnapshot();
        if (checked(result)) snapshot = result.value;
    }
    function layout(window, regions) {
        local dpi = window.getDPIScale();
        foreach (region, pixels in regions) workspace.setRegionSize(region, pixels / dpi);
        workspace.layout(window.getWidth().tofloat() / dpi, window.getHeight().tofloat() / dpi);
    }
    function buildHistory() {
        ui.beginRow("history", 8.0);
        ui.button("Undo", "undo"); ui.button("Redo", "redo"); ui.end();
    }
    function buildInspector() {
        ui.text("", "revision");
        buildHistory();
        ui.text("Preview pending", "preview-diagnostic");
        ui.textWrapped("", workspace.getRegionW("right") - 20.0, "presentation-diagnostic");
        foreach (row in snapshot.rows) {
            if (!row.editable) {
                ui.textWrapped(row.label + ": " + row.display, workspace.getRegionW("right") - 20.0, row.path);
            } else if (row.kind == "bool") {
                ui.checkbox(row.label, row.value, row.path);
            } else if (row.kind == "number") {
                ui.text(row.label, row.path + "-label");
                ui.slider("##" + row.path, row.value, row.minimum, row.maximum, row.path);
                ui.setItemAccessibility("slider", row.label, "");
            } else if (row.kind == "enum") {
                local index = row.choices.find(row.value);
                local options = "";
                foreach (i, choice in row.choices) options += (i == 0 ? "" : "\n") + choice;
                ui.combo(row.label, options, index == null ? 0 : index, row.path);
            } else {
                ui.inputText(row.label, row.value.tostring(), row.path);
            }
            ui.setItemEnabled(row.editable);
        }
    }
    function mount(builders) {
        for (local i = 0; i < workspace.getPanelCount(); ++i) {
            local id = workspace.getPanelId(i);
            local context = workspace.getPanelContext(i);
            ui.beginBuild(); ui.beginWindow(workspace.getPanelTitle(i), "root");
            if (context == "inspector") { inspector = id; buildInspector(); }
            else if (id in builders) builders[id]();
            else ui.text("No presenter for " + workspace.getPanelCapability(i) + "/" + context, "unavailable");
            ui.end();
            if (!ui.mountBuildAs(id)) throw "Could not mount " + id;
            hosts.append(id); ui.select(id);
            local region = workspace.getPanelRegion(i);
            ui.setHostPos(workspace.getRegionX(region), workspace.getRegionY(region), 0.0, 0.0);
            ui.setHostSize(workspace.getRegionW(region), workspace.getRegionH(region));
            ui.setHostMovable(false); ui.setHostResizable(false); ui.setHostOverlay(false);
        }
        sync();
    }
    function action(id) {
        if (closed) return;
        draft = false;
        local result = source.presentationAction(id);
        if (checked(result)) status = id + " applied";
        refresh(); sync();
    }
    function parts(path) {
        local slash = path.find("/");
        return slash == null ? ["", path] : [path.slice(0, slash), path.slice(slash + 1)];
    }
    function owns(host) { return hosts.find(host) != null; }
    function dispatch(path, changed, pointerDown) {
        if (closed) return;
        local event = parts(path); local host = event[0]; local id = event[1];
        if (!owns(host)) return;
        if (!changed) {
            if (id in actions) action(id);
            else if (onClick != null) onClick(host, id);
            return;
        }
        if (host != inspector) return;
        foreach (row in snapshot.rows) {
            if (row.path != id) continue;
            ui.select(host);
            local value = row.kind == "bool" ? ui.getChecked(id) : row.kind == "text" ? ui.getValueText(id) : ui.getValue(id);
            if (row.kind == "enum") value = row.choices[value.tointeger()];
            if (snapshot.playing) action("play-pause");
            local staged = pointerDown && row.kind == "number";
            local result = source.presentationEdit(id, value, staged);
            if (checked(result)) {
                draft = staged;
                if (!staged) { status = row.label + " committed"; refresh(); sync(); }
            } else { source.presentationCancel(); draft = false; refresh(); sync(); }
            return;
        }
    }
    function edge(id, down) {
        local was = id in previousKeys ? previousKeys[id] : false;
        previousKeys[id] <- down;
        return down && !was;
    }
    function observeState() {
        if (!draft) {
            local state = source.presentationState();
            if (checked(state) && inspector != "") { ui.select(inspector); ui.setText("preview-diagnostic", "Preview " + state.value.preview); }
            if (state.ok && (state.value.revision != snapshot.revision || (!state.value.playing && state.value.context != snapshot.context) ||
                state.value.playing != snapshot.playing)) { refresh(); sync(); }
        }
    }
    function update(keyboard, pointerDown, focused = true) {
        if (closed) return;
        observeState();
        if (!focused) {
            while (ui.consumeClick() != "") {}
            while (ui.consumeChange() != "") {}
            if (draft) { source.presentationCancel(); draft = false; status = "Edit cancelled on focus loss"; refresh(); sync(); }
            return;
        }
        local click = ui.consumeClick();
        while (click != "") { dispatch(click, false, pointerDown); click = ui.consumeClick(); }
        if (closed) return;
        local change = ui.consumeChange();
        while (change != "") { dispatch(change, true, pointerDown); change = ui.consumeChange(); }
        if (closed) return;
        local escape = edge("escape", keyboard.isDown("escape") || keyboard.isDown("Escape"));
        if (escape && draft) { source.presentationCancel(); draft = false; status = "Edit cancelled"; refresh(); sync(); }
        if (draft && !pointerDown) {
            local result = source.presentationCommit(); draft = false;
            if (checked(result)) status = "Edit committed";
            refresh(); sync();
        }
        local modifier = keyboard.isDown("lctrl") || keyboard.isDown("rctrl") || keyboard.isDown("ctrl") ||
                         keyboard.isDown("lgui") || keyboard.isDown("rgui");
        local shift = keyboard.isDown("lshift") || keyboard.isDown("rshift");
        local z = keyboard.isDown("z") || keyboard.isDown("Z");
        local undo = edge("undo", modifier && z && !shift);
        local redo = edge("redo", modifier && ((z && shift) || keyboard.isDown("y") || keyboard.isDown("Y")));
        local play = edge("play", keyboard.isDown("space") || keyboard.isDown("Space"));
        if (!ui.wantCaptureKeyboard()) {
            if (undo && snapshot.canUndo) action("undo");
            if (redo && snapshot.canRedo) action("redo");
            if (play) action("play-pause");
        }
        if (inspector != "") { ui.select(inspector); ui.setText("presentation-diagnostic", status); }
        if (statusPanel != "") { ui.select(statusPanel); ui.setText(statusWidget, status); }
    }
    function sync() {
        if (closed || inspector == "") return;
        ui.select(inspector); ui.setText("revision", "Revision " + snapshot.revision);
        foreach (row in snapshot.rows) {
            if (!row.editable) ui.setText(row.path, row.label + ": " + row.display);
            else if (row.kind == "bool") ui.setChecked(row.path, row.value);
            else if (row.kind == "number") ui.setValue(row.path, row.value);
            else if (row.kind == "enum") {
                local index = row.choices.find(row.value); ui.setValue(row.path, index == null ? 0 : index);
            } else if (row.editable) ui.setValueText(row.path, row.value.tostring());
            ui.setEnabled(row.path, row.editable);
        }
        foreach (host in hosts) {
            ui.select(host); ui.setEnabled("undo", snapshot.canUndo); ui.setEnabled("redo", snapshot.canRedo);
        }
    }
    function close() {
        if (closed) return;
        source.presentationCancel();
        checked(source.presentationAction("stop")); draft = false; closed = true;
        foreach (host in hosts) { ui.select(host); ui.setHostVisible(false); }
        source.presentationClose();
        hosts.clear(); source = null; onClick = null;
    }
}
