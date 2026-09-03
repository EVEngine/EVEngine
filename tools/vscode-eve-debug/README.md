# EVEngine VS Code Extension

EveScript language support and debugger for EVEngine. Follows the same debug
shape as [microsoft/vscode-mock-debug](https://github.com/microsoft/vscode-mock-debug),
plus a stdio Language Server spawned from `eve language-server`.

| Piece | Role |
|-------|------|
| `package.json` languages / grammar | `.nut` as EveScript, comments, brackets, highlighting |
| `lib/language.js` | Spawns `eve language-server --root`, incremental LSP |
| `package.json` `contributes.debuggers` | Declares type `eve`, launch schema, snippets |
| `DebugConfigurationProvider` | Fills defaults, auto-finds `eve`, allocates DAP port |
| `DebugAdapterDescriptorFactory` | Spawns `eve run --debug --dap-port` → `DebugAdapterServer` |
| Engine DAP (`DebugAdapter.cpp`) | Implements continue / next / pause / breakpoints |

Opening a `.nut` file starts the language server for the nearest game directory
(`config.nut`). That provides diagnostics, completion, hover, go-to-definition,
references, rename, outline, signature help, format, folding, and semantic
highlighting (class / function / method / variable).

Set `eve.executable` if `eve` is not on `PATH` and the repo `build/` tree is not
the workspace root. Set `eve.languageServer.enabled` to `false` to keep debug-only.

## Test

```bash
# Engine DAP + Debugger unit tests
./build/macosx-debug/test/unit_test --testcase='^devtools\.(debugger|dap)\..*$'

# Extension helpers + live DAP (needs a display; skips cleanly if SDL has no video)
node tools/vscode-eve-debug/test/extension.test.js
```

## Install (Cursor / VS Code)

Cursor does not install an unpacked folder. Pack a `.vsix` with [vsce](https://github.com/microsoft/vscode-vsce), then install that file:

```bash
cd tools/vscode-eve-debug
npm install
npm run package
cursor --install-extension eve-debug-0.3.0.vsix
```

In Cursor: **Extensions → ⋯ → Install from VSIX…** and pick the generated file.

`code --install-extension <folder>` only works in VS Code, not Cursor.

Reload the window after installing. Re-pack and re-install after editing the extension.

VS Code can still load the folder as an Extension Development Host (F5).

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
  VARIABLES view while stopped → *EVEngine: 查看实例 (Inspect Instance)* opens a
  webview that walks the object's children through DAP `variables` requests,
  so tables / arrays / instances / classes can be explored side-by-side with
  the stack (Godot-style remote inspector).
- **Error slice**: while a `--debug` session is running, **EVEngine: Show Error
  Slice** asks the adapter for DAP `errorSlice` (same CallGraph backward slice
  as the in-game DevTools / MCP `eve_error_slice`). The report goes to
  **EVEngine Debug**; slice locations become diagnostics and the first site
  opens in the editor. A `stopped` event with `reason: exception` also opens
  the slice automatically. This is runtime-only — it is not an LSP feature.
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
