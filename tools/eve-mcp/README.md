# eve-mcp

stdio MCP bridge to a live EVEngine process, with optional auto-start.

## Why

Cursor and Claude Desktop speak [MCP](https://modelcontextprotocol.io/) over **stdio**.
EVEngine embeds the MCP server **inside the running game** (`--mcp-port`) so agents can
pause, eval, snapshot, inject playtest input, and slice errors against a live session.
Lookup EveScript APIs with `eve_api_search` / `eve_api_get` before writing `.nut`. This
Node script forwards stdio JSON-RPC lines to that TCP port (same newline-delimited framing).

## Usage

### A. Attach to an already-running game

```bash
eve run --debug --mcp-port=7529 examples/basic
```

```json
{
  "mcpServers": {
    "evengine": {
      "command": "node",
      "args": ["tools/eve-mcp/server.js"],
      "env": {
        "EVE_MCP_HOST": "127.0.0.1",
        "EVE_MCP_PORT": "7529"
      }
    }
  }
}
```

### B. Auto-start the engine (Godot-MCP-style lifecycle)

```json
{
  "mcpServers": {
    "evengine": {
      "command": "node",
      "args": ["tools/eve-mcp/server.js"],
      "env": {
        "EVE_MCP_AUTO_START": "1",
        "EVE_MCP_EVE_BIN": "build/linux-debug/src/engine/eve",
        "EVE_MCP_GAME": "examples/basic",
        "EVE_MCP_PORT": "7529",
        "EVE_MCP_TOOLSET": "core,play,input,scene"
      }
    }
  }
}
```

The bridge spawns `eve run --debug --mcp-port=… <game>`, waits for the TCP port, then
forwards stdio. On client disconnect / SIGTERM it stops the child process.

## Playtest input (new)

| Tool | Role |
|------|------|
| `eve_input_status` | Whether injection / UI / capture providers are live |
| `eve_input_batch` | Inject `key` / `mouse_move` / `mouse_button` / `wheel` / `text` / `ui_click` / `wait`; optional `waitFrames` + `capture` |
| `eve_input_poll` | Drain a pending wait/capture across game-loop polls |
| `eve_scene_batch` | Apply many `eve_scene_modify`-shaped white-box actions in one round-trip |
| `eve_whitebox_playtest` | Recipe: optional reset + scene batch + input playtest + optional `eve_play` batch |

For a Cursor-facing stdio MCP that auto-polls input and evaluates `assert[]`, see
[`tools/whitebox-playtest/`](../whitebox-playtest/).

Example closed loop:

```json
{
  "actions": [
    {"op": "key", "key": "W", "down": true},
    {"op": "wait", "frames": 3},
    {"op": "key", "key": "W", "down": false}
  ],
  "waitFrames": 1,
  "capture": true,
  "capturePath": "playtest.png"
}
```

Then call `eve_input_poll` until `status=ready`.

## Tool surface filters (engine-side env)

| Variable | Meaning |
|----------|---------|
| `EVE_MCP_TOOLSET` | Comma tags: `all` (default) or `core,debug,play,editor,host,scene,input,domain,agent` |
| `EVE_MCP_READONLY` | `1` hides mutating tools |
| `EVE_MCP_EXCLUDE` | Comma-separated exact tool names to drop |

Filtered tools are omitted from `tools/list` and rejected on `tools/call` with `hint` / `nextTools`.

## Bridge env

| Variable | Default | Meaning |
|----------|---------|---------|
| `EVE_MCP_HOST` | `127.0.0.1` | Engine MCP host |
| `EVE_MCP_PORT` | `7529` | Engine MCP port |
| `EVE_MCP_CONNECT_TIMEOUT_MS` | `5000` | Single connect attempt timeout |
| `EVE_MCP_AUTO_START` | off | Spawn `eve` when the port is closed |
| `EVE_MCP_EVE_BIN` | `eve` | Engine binary (or set `EVE_BIN`) |
| `EVE_MCP_GAME` | `.` | Game directory argument for `eve run` |
| `EVE_MCP_EVE_ARGS` | empty | Extra args inserted before the game path |
| `EVE_MCP_SPAWN_WAIT_MS` | `60000` | How long to wait for MCP after spawn |

## Headless host (`eve mcp`)

For AI-centric development (Codex / Cursor / Claude), the engine itself can run as a
**headless MCP host** over stdio — no game process, no Node bridge, no TCP port needed:

```bash
eve mcp                    # stdio MCP (Codex: command "eve", args ["mcp"])
eve mcp --port 7529        # TCP mode, same wire shape as the bridge
eve mcp --root path/to/game
```

The host exposes the `eve_host_*` tool family: create windows, apply JSON-defined
editors (View), register Squirrel ViewModel tables (MVVM two-way binding), read human
interaction events, capture PNGs, run Squirrel snippets, persist editors to the
project `editors/` directory. See the [English setup guide](../../docs/en/usr/AI-MCP.md) and
[full protocol design (Chinese)](../../docs/dev/AI与MCP支持.md) for the JSON protocol,
tool list, and terrain/material editor examples.

## Native execution budgets and SDK recovery

The SDK's `bin/eve` contains the stdio watchdog. No Python, Node, shell script,
extra executable, or source checkout is required:

```sh
/path/to/sdk/bin/eve mcp --timeout-ms 2000 --root /path/to/project
```

Plain `eve mcp` enables this supervision by default with a 2000 ms response
budget. Increase `--timeout-ms` for legitimate long tasks. The positive integer
budget covers each forwarded request, including pipe writes, compilation,
execution, and response overhead. Requests are serialized. The supervisor starts
the **same executable at its resolved absolute location** as a private worker,
so SDK relocation and spaces in SDK/project paths do not require PATH helpers.
SDK installation continues to ship the existing native host and its normal
runtime libraries; no additional runtime distribution is needed.

Squirrel has no safe forced-interruption API, and native bindings can block
without executing script instructions. On deadline expiry, the native supervisor
kills and reaps its worker before returning an MCP tool result with
`isError=true`. The diagnostic uses schema `eve.mcp.watchdog`, version 1, with
`type=execution_timeout`, origin, source, `budget_ms`, message,
`vm_state=terminated`, and recovery instructions. Ignore unknown fields.
Disconnects/crashes use `host_failure`. Client waiting alone is not considered
proof that script execution stopped.

The next request starts a new worker and replays only the successful MCP
initialization handshake. Scripts are never replayed. VM globals, unsaved state,
handles, and in-memory snapshots/error history are lost. External side effects
are not rolled back; explicitly restore saved project/checkpoint state. A worker
stuck in an uninterruptible OS task can delay reaping; no stopped diagnostic is
returned until termination is confirmed. Normal stdin EOF reaps the worker.
POSIX SIGTERM/SIGINT termination also kills/reaps it; Windows uses a lifetime job
with kill-on-close. The internal `--worker` mode bypasses supervision and is only
used by the parent. TCP live hosts (`--port`, including the Node bridge above)
need external process supervision and do not offer this stdio recovery contract.

`eve_error_slice` preserves the supervisor's last failure across worker restart
until a new script error supersedes it. Compile/runtime snippet errors replace
the latest engine diagnostic and clear unrelated prior slicing data. They retain
error type, origin, source, line, column (`-1` means unknown), and message.
Runtime reports can include captured frames. Compile errors never synthesize
runtime frames. Successful calls retain the latest error for inspection.

Repository validation uses Python only as a test driver, while launching the
native binary with an empty PATH and a temporary project outside the repository:

```sh
SDL_VIDEODRIVER=dummy EVE_MCP_TEST_BIN=/absolute/path/to/rebuilt/or/installed/eve \
  python3 -m unittest discover -s scripts/tests -p test_mcp_watchdog.py -v
```
