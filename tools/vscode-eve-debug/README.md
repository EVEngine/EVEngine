# EVEngine VS Code Debug Extension

VS Code debugger contribution for EVEngine Squirrel scripts. Follows the same
shape as [microsoft/vscode-mock-debug](https://github.com/microsoft/vscode-mock-debug):

| Piece | Role |
|-------|------|
| `package.json` `contributes.debuggers` | Declares type `eve`, launch schema, snippets |
| `DebugConfigurationProvider` | Fills defaults, auto-finds `eve`, allocates DAP port |
| `DebugAdapterDescriptorFactory` | Spawns `eve run --debug --dap-port` → `DebugAdapterServer` |
| Engine DAP (`DebugAdapter.cpp`) | Implements continue / next / pause / breakpoints |

## Test

```bash
# Engine DAP + Debugger unit tests
./build/macosx-debug/test/unit_test --testcase='^devtools\.(debugger|dap)\..*$'

# Extension helpers + live DAP (needs a display; skips cleanly if SDL has no video)
node tools/vscode-eve-debug/test/extension.test.js
```

## Install

```bash
# From the EVEngine repo root (directory install — no vsix needed):
code --install-extension tools/vscode-eve-debug

# Or: open tools/vscode-eve-debug as an Extension Development Host (F5)
```

Reload the window after installing / editing the extension.

## Launch

`.vscode/launch.json`:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "type": "eve",
      "request": "launch",
      "name": "EVEngine: Debug game",
      "program": "${workspaceFolder}/examples/basic",
      "evePath": "eve",
      "port": 0
    }
  ]
}
```

- `program` — game directory (cwd + mount root)
- `evePath` — path to `eve`, or `"eve"` to auto-detect `build/<platform>-debug/...`
- `port` — `0` picks a free TCP port

Press **F5** in VS Code (or Run and Debug ▶).

Equivalent CLI:

```bash
eve run --debug --dap-port=4711 examples/basic
```

Then use an **Attach** configuration.

## Keys (VS Code focused, while stopped)

| Key | Action |
|-----|--------|
| **F5** | Continue to next breakpoint |
| **F10** | Step Over (next statement, skip calls) |
| **F11** | Step Into (next statement, enter calls) |
| **Shift+F11** | Step Out (until return to caller) |
| **F8** | Step one game frame (custom `stepFrame`) |
| **F6** | Pause (while running) |
| Shift+F5 | Stop |

F5 / F8 are contributed for `debugType == 'eve'`. F10 / F11 / Shift+F11 use
VS Code’s standard debug commands and map to DAP `next` / `stepIn` / `stepOut`.

## Keys (game window focused)

Handled by `load.nut` when running with `--debug` (same semantics):

| Key | Action |
|-----|--------|
| Pause | Toggle frame-level pause |
| F5 | Continue |
| F10 | Step Over |
| F11 | Step Into |
| F8 | Step one frame |
| F6 / F7 | Save / load `eve_snapshot.json` |

## Tips

- Set breakpoints in `.nut` files before or during the session.
- Enable `"trace": true` in launch.json to log DAP traffic to **EVEngine Debug**.
- `"stopOnEntry": true` pauses before the first update after attach.
- Game stdout/stderr is mirrored to the Debug Console and the output channel.
