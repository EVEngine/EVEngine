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
| `EVE_MCP_PROFILE` | Named task profile: `verify.play`, `content.scene`, `content.map`, `script.bind`, `fix.compile`, `workshop.rpg` |
| `EVE_MCP_READONLY` | `1` hides mutating tools |
| `EVE_MCP_EXCLUDE` | Comma-separated exact tool names to drop |

CLI shorthand (sets `EVE_MCP_PROFILE` and, if unset, `EVE_MCP_TOOLSET`):

```bash
node tools/eve-mcp/server.js --profile=verify.play
```

Filtered tools are omitted from `tools/list` and rejected on `tools/call` with
`hint` / `nextTools` / optional `escalate`. Session start also installs a profile
via `eve_agent_session_start` (`taskKind` / `profile`; default `verify` /
`verify.play`). See `docs/dev/2026-10-20-agent-task-profile-mcp-plan.md`.

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
