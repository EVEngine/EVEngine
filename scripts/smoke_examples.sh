#!/usr/bin/env bash
# Smoke-test every runnable example under examples/.
#
# Each example is launched as `eve run` from inside its own directory (the same
# thing `make run GAME=examples/<name>` does) and killed after a few seconds.
# Its stdout/stderr is scanned for known error markers; a clean run reports
# PASS, otherwise FAIL with the offending lines.
#
# Usage:
#   scripts/smoke_examples.sh                          # all examples
#   scripts/smoke_examples.sh basic tree-generator    # only these examples
#   EVE=.../eve.exe RUN_SECONDS=8 scripts/smoke_examples.sh
#
# Exit status is non-zero if any example failed.

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EXAMPLES_DIR="$ROOT/examples"

# Host debug binary (win32 / linux / macosx).
if [[ -n "${EVE:-}" ]]; then
  EVE_BIN="$EVE"
elif [[ -x "$ROOT/build/win32-debug/src/engine/eve.exe" ]]; then
  EVE_BIN="$ROOT/build/win32-debug/src/engine/eve.exe"
elif [[ -x "$ROOT/build/linux-debug/src/engine/eve" ]]; then
  EVE_BIN="$ROOT/build/linux-debug/src/engine/eve"
else
  EVE_BIN="$ROOT/build/macosx-debug/src/engine/eve"
fi

RUN_SECONDS_PER="${RUN_SECONDS:-6}"

is_windows() {
  case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) return 0 ;;
    *) return 1 ;;
  esac
}

# Error markers emitted by load.nut / Run.cpp / Squirrel failures.
read -r -d '' MARKERS_SCRIPT <<'EOF' || true
frame error
Run failed:
Runtime error
eve_init failed
present error
Cannot chdir
Cannot open root
failed to load:
EOF
mapfile -t MARKERS <<<"$MARKERS_SCRIPT"

# Runnable examples = dirs containing a main.nut.
if [[ $# -gt 0 ]]; then
  EXAMPLES=("$@")
else
  EXAMPLES=()
  for d in "$EXAMPLES_DIR"/*/; do
    name="$(basename "$d")"
    [[ -f "$d/main.nut" ]] && EXAMPLES+=("$name")
  done
fi

FAILED=0
PASSED=0
declare -a FAILED_NAMES=()

for name in "${EXAMPLES[@]}"; do
  dir="$EXAMPLES_DIR/$name"
  [[ -f "$dir/main.nut" ]] || { echo "SKIP  $name (no main.nut)"; continue; }

  log="$(mktemp)"
  if is_windows; then
    # GNU timeout on MSYS cannot reliably terminate the native Windows GUI
    # process (SIGTERM is ignored), so launch in background and force-kill by
    # image name with taskkill.
    (
      cd "$dir" || exit 1
      "$EVE_BIN" run >"$log" 2>&1 &
      pid=$!
      sleep "$RUN_SECONDS_PER"
      taskkill //F //IM "$(basename "$EVE_BIN")" >/dev/null 2>&1 || true
      kill "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
    )
  else
    if command -v timeout >/dev/null 2>&1; then
      ( cd "$dir" && timeout "$RUN_SECONDS_PER" "$EVE_BIN" run >"$log" 2>&1 )
    else
      ( cd "$dir" && "$EVE_BIN" run >"$log" 2>&1 ) &
      pid=$!
      sleep "$RUN_SECONDS_PER"
      kill "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
    fi
  fi

  hit=""
  for m in "${MARKERS[@]}"; do
    if grep -qF "$m" "$log"; then
      hit="$m"
      break
    fi
  done

  if [[ -n "$hit" ]]; then
    echo "FAIL  $name  (marker: $hit)"
    grep -nF "$hit" "$log" | head -3 | sed 's/^/       /'
    FAILED=$((FAILED + 1))
    FAILED_NAMES+=("$name")
  else
    echo "PASS  $name"
    PASSED=$((PASSED + 1))
  fi
  rm -f "$log"
done

echo
echo "Summary: $PASSED passed, $FAILED failed"
if [[ $FAILED -gt 0 ]]; then
  printf 'Failed: %s\n' "${FAILED_NAMES[*]}"
  exit 1
fi
