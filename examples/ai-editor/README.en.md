# AI Editor — Build a Project-Specific Live Editor

[中文](README.md) · [AI/MCP setup](../../docs/en/usr/AI-MCP.md)

This example uses the `eve mcp` host: an agent submits a JSON View and Squirrel ViewModel, and the engine creates an interactive editor window with MVVM two-way binding. A project can compose its own terrain, material or event editor inside the runtime.

This is the Editor path of an Agent Development Session. Register the objective and acceptance criteria first, then follow Discover → Modify → Run → Observe → Verify. Reactive convergence, undo/redo and screenshots provide separate evidence.

## Start and reproduce

From the repository root with a matching desktop engine:

```sh
eve mcp --port 7531 --root examples/ai-editor
```

In another terminal:

```sh
python examples/ai-editor/editor_demo.py 7531
```

The example is not bundled with the SDK. The host initially needs no game/window; the demonstration subsequently creates a real window and rendering resources, so its visual path still needs a working desktop graphics environment.

The standard-library client attaches, opens a window, registers a ViewModel, submits the editor and creates live SceneHost/Renderable3D objects. An Editor target borrows them. Reactive observation sessions persist across agent calls; scene-node observation rejects missing nodes before writes, and renderable observation correlates before/after state, transaction receipts and snapshots.

The demo deliberately submits a stale generation, proves rejection without runtime mutation, corrects the identity, submits an off-target material candidate and observes/corrects until convergence. Identical samples are suppressed by distinctUntilChanged. It closes subscriptions explicitly, replays changes with undo/redo, projects authoritative results into the UI, captures the green material cube through the engine, closes targets and exits. Successful assertions print `PASS`. The Chinese source records end-to-end verification on Windows; this translation does not establish support on every platform or released SDK.

## Connect an MCP client

The native host supports stdio without a Node bridge. In clients accepting an mcpServers configuration, use absolute executable/project paths:

```json
{
  "mcpServers": {
    "evengine-host": {
      "command": "/absolute/path/to/eve",
      "args": ["mcp", "--root", "/absolute/path/to/EVEngine/examples/ai-editor"]
    }
  }
}
```

Replace placeholders and use your client's actual configuration format; Windows JSON paths need escaped backslashes or forward slashes. Host tools apply editors, create/inspect targets, observe mutations and capture frames. Discover their schemas instead of guessing argument shapes.

## Edit and reload

Edit `editors/terrain.editor.json` or `editors/terrain.vm.nut` while the host runs. Files reload without restarting/recompiling; the connection and current control values remain. Compilation failure keeps the old ViewModel working; inspect `eve_host_hot_reload_status`, fix the script and save again.

Projects can add `mcp.nut` or `mcp/*.nut` for custom tool logic. For explicit refresh, call `eve_host_resource_reload` with the resource path.

## Files and ownership

- terrain.editor.json: widget tree, theme and layout.
- terrain.vm.nut: readable/writable ViewModel fields and button `apply(editor, widget)` callbacks.
- editor_demo.py: complete TCP driver workflow.

Button callbacks handle UI interactions; authoritative SceneHost changes use the Editor command/transaction protocol. They do not keep another scene-state copy in the ViewModel. Closing a target releases borrowed adapters/publication sinks, not the ECS-owned SceneHost/Renderable3D.
