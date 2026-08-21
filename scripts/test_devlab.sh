#!/usr/bin/env bash
# DevLab developer-experience behavior test.
#
# Verifies the two claims examples/devlab is built around, against a real
# running engine:
#   1. `--debug` attaches DevTools (MCP attached, script state reachable);
#   2. editing a script function triggers soft hot-reload without restart.
#
# With `--debug` the engine captures Squirrel print() into the DevTools console,
# so this test verifies through the embedded MCP server instead of grepping
# stdout: "MCP listening" (stderr) proves DevTools attached, then a tiny Python
# client evaluates lab.reloads before/after editing the script.
#
# The example is copied to a temp dir so the repo stays clean.
#
# Usage:
#   scripts/test_devlab.sh [path-to-eve]        # e.g. build/linux-debug/src/engine/eve
#   EVE=... MCP_PORT=7530 scripts/test_devlab.sh

set -euo pipefail

EVE_BIN="${1:-${EVE:-build/linux-debug/src/engine/eve}}"
MCP_PORT="${MCP_PORT:-7530}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

cp -R "$ROOT/examples/devlab" "$WORK/devlab"
log="$WORK/devlab.log"

(
  cd "$WORK/devlab"
  "$EVE_BIN" run --debug --mcp-port="$MCP_PORT" >"$log" 2>&1
) &
pid=$!

wait_for() {
  local needle="$1" timeout="${2:-15}" waited=0
  while [ "$waited" -lt "$timeout" ]; do
    if grep -qF "$needle" "$log" 2>/dev/null; then
      return 0
    fi
    if ! kill -0 "$pid" 2>/dev/null; then
      return 1
    fi
    sleep 1
    waited=$((waited + 1))
  done
  return 1
}

fail() {
  echo "FAIL: $1"
  tail -n 15 "$log" 2>/dev/null | sed 's/^/       | /'
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
  exit 1
}

wait_for "MCP listening on 127.0.0.1:$MCP_PORT" 30 \
  || fail "DevTools/MCP did not start (did the build include DevTools?)"

# Hot-edit the bounce coefficient in the copied game: 0.85 -> 0.5.
sed -i 's/return (v < 0.0 || v > maxV) ? -0.85 : 1.0;/return (v < 0.0 || v > maxV) ? -0.5 : 1.0;/' \
  "$WORK/devlab/main.nut"

PYTHON="${PYTHON:-python3}"
python3 - "$MCP_PORT" <<'PY'
import json
import socket
import sys
import time

port = int(sys.argv[1])
s = socket.create_connection(("127.0.0.1", port), timeout=10)
buf = b""
seq = 0


def send(obj):
    s.sendall((json.dumps(obj) + "\n").encode())


def recv(req_id):
    global buf
    while True:
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            if not line.strip():
                continue
            msg = json.loads(line)
            if msg.get("id") == req_id:
                return msg
        chunk = s.recv(65536)
        if not chunk:
            raise RuntimeError("MCP connection closed")
        buf += chunk


def call(method, params=None):
    global seq
    seq += 1
    req = {"jsonrpc": "2.0", "id": seq, "method": method}
    if params is not None:
        req["params"] = params
    send(req)
    return recv(seq)


def tool(name, args=None):
    r = call("tools/call", {"name": name, "arguments": args or {}})
    return "\n".join(
        c.get("text", "")
        for c in r.get("result", {}).get("content", [])
        if c.get("type") == "text"
    )


call(
    "initialize",
    {"protocolVersion": "2025-06-18", "clientInfo": {"name": "devlab-test", "version": "1"}},
)
send({"jsonrpc": "2.0", "method": "notifications/initialized"})
time.sleep(0.2)

status = tool("eve_status")
assert '"attached":true' in status, f"DevTools not attached: {status[:200]}"

# The script was edited before this client connected; wait for the reload.
deadline = time.time() + 20
while time.time() < deadline:
    r = tool("eve_eval", {"expression": "lab.reloads"})
    if '"value":"1"' in r or '"value":1' in r:
        print("PASS: DevLab — MCP attached and script hot-reload verified on a live game.")
        sys.exit(0)
    time.sleep(0.5)

print(f"FAIL: hot reload counter never reached 1 (last eval: {r})")
sys.exit(1)
PY
rc=$?

kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
exit "$rc"
