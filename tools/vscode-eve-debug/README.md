# EVEngine VS Code Debug Extension

Connects VS Code to the engine's built-in **Debug Adapter Protocol** server so you can:

- Set breakpoints in `.nut` scripts
- Pause / continue / step
- Watch variables (Evaluate → Watches scope)
- Pair with in-engine Pause key + snapshot hotkeys

## Install (local)

```bash
code --install-extension tools/vscode-eve-debug
# or: open this folder as an extension development host (F5)
```

## Launch

1. Build a desktop Debug `eve` binary.
2. Add a launch config (or use the snippet):

```json
{
  "type": "eve",
  "request": "launch",
  "name": "EVEngine: Debug game",
  "program": "${workspaceFolder}/examples/basic",
  "evePath": "${workspaceFolder}/build/linux-debug/src/engine/eve",
  "port": 4711
}
```

3. Press F5 in VS Code.

Equivalent CLI:

```bash
eve run --debug --dap-port=4711 examples/basic
```

## In-engine keys (`eve run --debug`)

| Key | Action |
|-----|--------|
| Pause | Toggle frame-level pause |
| F10 | Step one frame (while paused) |
| F5 | Save script-state snapshot → `eve_snapshot.json` |
| F9 | Restore snapshot from `eve_snapshot.json` |

## Script API (`eve.dev`)

```squirrel
eve.dev.pause();
eve.dev.resume();
eve.dev.setBreakpoint("main.nut", 42);
eve.dev.addWatch("score");
eve.dev.markStateRoot("gameState");
eve.dev.saveSnapshot("boss.json");
eve.dev.loadSnapshot("boss.json");
```

Snapshots serialize **script variables only** (engine modules are treated as stateless).
