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
- **Conditional breakpoints**: right-click a breakpoint → *Edit Condition…* and
  enter any Squirrel expression (e.g. `score > 10`). Evaluated in the frame's
  locals + global scope; a failed/unreadable condition stops (safer default).
- **Break on error**: tick **Script Errors** under the BREAKPOINTS panel (or
  `"filters": ["script_error"]` in `setExceptionBreakpoints`). Script errors
  are routed to the debugger and pause (Godot-style "Break on Error"):
  - Uncaught errors pause at the exact **throw site** (the Squirrel error hook
    fires before the stack unwinds), and the call stack starts at the throwing
    function with its locals intact.
  - Caught errors (the main loop wraps `eve_init` / `eve_update` / hot-reload
    in `try/catch`) pause at the game's catch statement that reported the
    error — never inside `load.nut` internals.
- **查看实例 (Inspect Instance)**: right-click an expandable object in the
  VARIABLES view while stopped 鈫?*EVEngine: 查看实例 (Inspect Instance)* opens a
  webview that walks the object's children through DAP `variables` requests,
  so tables / arrays / instances / classes can be explored side-by-side with
  the stack (Godot-style remote inspector).
- **Breakpoint verification**: breakpoints start hollow and turn solid once the
  line is actually executed (DAP `breakpoint` events), so a wrong line number
  or path mismatch is visible immediately.
- **Variables**: Locals follow the selected call-stack frame; tables, arrays,
  classes, instances and closures expand inline; a Globals scope lists root
  slots. Hover / Watch evaluate full expressions (arithmetic, calls, indexing).
  Expandable variables carry a `__vscodeVariableMenuContext` marker so the
  context menu only offers "Inspect Instance" for real objects.
- Enable `"trace": true` in launch.json to log DAP traffic to **EVEngine Debug**.
- `"stopOnEntry": true` pauses before the first update after attach.
- Game stdout/stderr is mirrored to the Debug Console and the output channel.
