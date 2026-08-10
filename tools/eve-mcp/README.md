# eve-mcp

stdio MCP bridge to a live EVEngine process.

## Why

Cursor and Claude Desktop speak [MCP](https://modelcontextprotocol.io/) over **stdio**.
EVEngine embeds the MCP server **inside the running game** (`--mcp-port`) so agents can
pause, eval, snapshot, and slice errors against a live session. This Node script forwards
stdio JSON-RPC lines to that TCP port (same newline-delimited framing).

## Usage

1. Start the game with MCP enabled:

```bash
eve run --debug --mcp-port=7529 examples/basic
```

2. Point Cursor at this bridge (`.cursor/mcp.json` or user MCP settings):

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

3. Use tools such as `eve_status`, `eve_eval`, `eve_pause`, `eve_snapshot_capture`,
   `eve_error_slice`, or prompts `debug_failure` / `test_scenario`.

## Env

| Variable | Default | Meaning |
|----------|---------|---------|
| `EVE_MCP_HOST` | `127.0.0.1` | Engine MCP host |
| `EVE_MCP_PORT` | `7529` | Engine MCP port |
| `EVE_MCP_CONNECT_TIMEOUT_MS` | `5000` | Connect timeout |
