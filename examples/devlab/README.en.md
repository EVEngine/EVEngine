# DevLab — Developer Experience Lab

[中文](README.md) · [Example guide](../README.en.md)

A short walkthrough of EVEngine's shared game/developer runtime. **Run with `--debug`**; otherwise DevTools are not loaded.

From the repository root, using an engine version matching the example:

```sh
eve run --debug examples/devlab
# Source-build shortcut (already supplies --debug):
make devlab
```

DevLab is a repository example, not part of the basic example bundled with the SDK. See [how to obtain other examples](../README.en.md).

## Five-minute walkthrough

1. **F4: console / REPL.** The script writes startup logs through `eve.dev.console.info`. Open the console and evaluate `eve.dev.console.eval("lab.count")` to inspect the frame count. Evaluate `eve.dev.console.eval("lab.vx = -200.0")` to reverse the moving ball.
2. **Hot reload.** Edit `bounceDir` in `main.nut`, for example changing `0.85` to `1.0`, and save. The console/HUD show `script hot-reloaded`; the ball uses the edited logic without restarting the engine.
3. **F6/F7: snapshot and restore.** F6 records the script state. Let the ball move, then press F7: the count and position return to the saved point. This supports edit/reset/replay workflows.
4. **Pause and step.** Pause freezes the loop and displays `[PAUSED]`. F8 advances one frame; F10/F11 step statements; F5 resumes.
5. **Inspect an error.** Press Space or the runtime-error button. In debug mode, execution stops at the throw site. The console, DAP and MCP expose an error slice identifying the source line and call path.
6. **F9: AI/MCP panel.** Open the panel to see the example's session note. To connect an agent, launch with `eve run --debug --mcp-port=7529 examples/devlab` and follow [AI/MCP setup](../../docs/en/usr/AI-MCP.md).

These shortcuts apply while the game window is focused; editor/DAP shortcuts have their own focus context. See [VS Code extension instructions](../../tools/vscode-eve-debug/README.md).

## What this demonstrates

The console, debugger, snapshots, AI panel and game share a process. Script functions hot-reload without rebuilding C++ entities. Pause/step and snapshots make a repeatable test loop possible. A working demo is a workflow example, not a promise that every stateful native resource can be serialized by script snapshots.
