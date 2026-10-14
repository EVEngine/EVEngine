# Editor Workspace Layout Shell

Demonstrates the systematic editor layout components that replace freely
draggable one-host-per-panel chrome:

- `AppFrame` — fixed toolbar + body + status bar
- `DockArea` — named fill slots for left / center / right / bottom
- `PanelFrame` — titled panel chrome inside a dock (not a free Window host)
- `TabBar` / `TabContainer` — stack multiple panels in one region
- `WorkspaceLayoutShell` — maps an `EditorWorkspace` descriptor registry onto
  that fixed chrome (`UiEditorModule.mountWorkspaceShell`)

The full-screen host is locked (`lockPos` / `lockSize`). A second manual host
builds the same primitives imperatively so projects can compose without the
C++ helper.

```sh
eve run
```

Or:

```sh
make run/linux-debug GAME=examples/editor-workspace-shell
```
