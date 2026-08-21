#!/usr/bin/env bash
# DevLab developer-experience behavior test.
#
# Verifies the two claims examples/devlab is built around, against a real
# running engine:
#   1. `--debug` attaches DevTools (console / snapshot / AI panel / debugger);
#   2. editing a script function triggers soft hot-reload without restart.
#
# The example is copied to a temp dir so the repo stays clean.
#
# Usage:
#   scripts/test_devlab.sh [path-to-eve]        # e.g. build/linux-debug/src/engine/eve
#   EVE=... RUN_SECONDS_PER=30 scripts/test_devlab.sh

set -euo pipefail

EVE_BIN="${1:-${EVE:-build/linux-debug/src/engine/eve}}"
RUN_SECONDS_PER="${RUN_SECONDS_PER:-25}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

cp -R "$ROOT/examples/devlab" "$WORK/devlab"
log="$WORK/devlab.log"

(
  cd "$WORK/devlab"
  "$EVE_BIN" run --debug >"$log" 2>&1
) &
pid=$!

wait_for() {
  local needle="$1" timeout="${2:-15}" waited=0
  while [ "$waited" -lt "$timeout" ]; do
    if grep -qF "$needle" "$log" 2>/dev/null; then
      return 0
    fi
    sleep 1
    waited=$((waited + 1))
  done
  return 1
}

fail() {
  echo "FAIL: $1"
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
  exit 1
}

wait_for "DevTools attached" 20 || fail "DevTools did not attach (did the build include DevTools?)"

# Hot-edit the bounce coefficient in the copied game: 0.85 -> 0.5.
sed -i 's/return (v < 0.0 || v > maxV) ? -0.85 : 1.0;/return (v < 0.0 || v > maxV) ? -0.5 : 1.0;/' \
  "$WORK/devlab/main.nut"

wait_for "script hot-reloaded" 15 || fail "hot reload did not trigger after editing main.nut"

echo "PASS: DevLab — DevTools attached and script hot-reload verified on a live game."
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
