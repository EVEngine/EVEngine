# AI and MCP: Getting Started

[User guide](README.md) · [Examples](EXAMPLES.md) · [FAQ](FAQ.md)

EVEngine has two desktop MCP entry points: a running game, and a native host that can create editor windows. Use the API catalog supplied with your engine version; ask `eve_api_search` / `eve_api_get` before assuming a script method exists. The full [protocol design](../../dev/AI与MCP支持.md) remains available in Chinese.

## Connect to a running game

Start a game with its MCP service enabled. From the SDK directory, using the bundled reference game:

```sh
bin/eve run --debug --mcp-port=7529 share/eve/examples/basic
```

On Windows use `bin\eve.exe`. For repository examples, use their repository paths with a matching runtime. Keep this game process running while the client connects.

A client that launches MCP servers over standard input/output can use the shipped Node bridge. The SDK installs it at `share/eve/ai/eve-mcp/server.js`; the source tree has `tools/eve-mcp/server.js`. Install Node.js when using this bridge. For clients accepting an `mcpServers` configuration, adapt this example with an absolute bridge path:

```json
{
  "mcpServers": {
    "evengine": {
      "command": "node",
      "args": ["/absolute/path/to/sdk/share/eve/ai/eve-mcp/server.js"],
      "env": {
        "EVE_MCP_HOST": "127.0.0.1",
        "EVE_MCP_PORT": "7529"
      }
    }
  }
}
```

Use your client's own settings format if it differs. Windows paths in JSON need doubled backslashes or forward slashes. Read the [bridge README](../../../tools/eve-mcp/README.md) for connection options and tools.

Start with `eve_status` and error inspection. Debug tools can pause, evaluate scripts and capture development snapshots. For structured gameplay, follow [AI Game](../../../examples/ai-game/README.en.md): its `eve_play` requests use declared operations and observations rather than arbitrary evaluation. Call `tools/list` to discover what this engine/session actually exposes.

## Launch a native editor host

The engine can serve MCP directly, without the Node bridge or a running game:

```sh
eve mcp --root /absolute/path/to/project
```

Configure your client's server command as the absolute `eve` executable and arguments `mcp`, `--root`, and your project path. Standard output carries the MCP protocol; diagnostics belong on standard error. For a TCP client, use:

```sh
eve mcp --port 7531 --root examples/ai-editor
```

The host starts without a window. Its `eve_host_*` tools can create windows, apply JSON editor views, register Squirrel view models, inspect interaction events, capture images and save editors in the project. Creating/rendering windows requires a desktop display and graphics driver. This host path is not supported on Android, iOS or the browser.

Follow [AI Editor](../../../examples/ai-editor/README.en.md) for a reproducible Discover → Modify → Run → Observe → Verify loop. Its Python driver exercises the host without an LLM. A script-level assertion alone does not prove that rendering or user interaction worked; inspect the corresponding capture and events.

## Troubleshooting

- **Executable not found:** use absolute engine and bridge paths; GUI clients may have a different `PATH` from your terminal.
- **Connection refused:** start the game first and match `--mcp-port` to `EVE_MCP_PORT`. The native standard-I/O host needs no TCP bridge.
- **Unknown tool or method:** check `tools/list`, the matching SDK catalog and the example's source revision.
- **Missing files:** pass the correct game directory or host `--root`; most examples are not bundled in the SDK.
- **No image:** verify the desktop graphics environment. A host starting without a window is expected; window creation is a separate operation.
- **Unexpected restored state:** checkpoints and debug snapshots have explicit state boundaries. Read [saving games](SAVE.md) and the chosen example's limitations before using them as player saves.
