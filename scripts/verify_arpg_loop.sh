#!/usr/bin/env bash
# Launch examples/arpg-loop with MCP, run agent_check.py, then tear down.
#
# Proves the Phase B1 acceptance path (three kills → growth → checkpoint) with
# real eve_play inputs. Requires a built eve binary and a display (Xvfb on CI).
#
# Usage:
#   scripts/verify_arpg_loop.sh [path-to-eve]
#   EVE=build/linux-debug/src/engine/eve MCP_PORT=7538 scripts/verify_arpg_loop.sh
#
# Environment:
#   EVE / first arg   eve binary (default build/linux-debug/src/engine/eve)
#   MCP_PORT          listen port (default 7538)
#   EVIDENCE_DIR      agent evidence directory (default /tmp/arpg-loop-evidence)
#   STARTUP_SECONDS   wait for MCP listening (default 45)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EVE_BIN="${1:-${EVE:-$ROOT/build/linux-debug/src/engine/eve}}"
MCP_PORT="${MCP_PORT:-7538}"
EVIDENCE_DIR="${EVIDENCE_DIR:-/tmp/arpg-loop-evidence}"
STARTUP_SECONDS="${STARTUP_SECONDS:-45}"
GAME="$ROOT/examples/arpg-loop"

if [[ ! -x "$EVE_BIN" ]]; then
  echo "FAIL: eve binary not executable: $EVE_BIN" >&2
  exit 1
fi

WORK="$(mktemp -d)"
log="$WORK/arpg-loop.log"
pid=""

cleanup() {
  if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  fi
  rm -rf "$WORK"
}
trap cleanup EXIT

(
  cd "$GAME"
  "$EVE_BIN" run --debug --mcp-port="$MCP_PORT" . >"$log" 2>&1
) &
pid=$!

wait_for() {
  local needle="$1" timeout="$2" waited=0
  while (( waited < timeout )); do
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
  echo "FAIL: $1" >&2
  tail -n 40 "$log" 2>/dev/null | sed 's/^/       | /' >&2 || true
  exit 1
}

wait_for "MCP listening on 127.0.0.1:$MCP_PORT" "$STARTUP_SECONDS" \
  || fail "MCP did not start for arpg-loop (is DevTools linked into this build?)"

mkdir -p "$EVIDENCE_DIR"
python3 "$GAME/agent_check.py" "$MCP_PORT" "$EVIDENCE_DIR"
echo "PASS verify_arpg_loop (evidence: $EVIDENCE_DIR)"
