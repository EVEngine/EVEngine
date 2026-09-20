#!/usr/bin/env bash
# Smoke-test every runnable example under examples/.
#
# Each example is launched as `eve run` from inside its own directory (the same
# thing `make run GAME=examples/<name>` does).  The harness enforces two things
# before it kills the process:
#
#   1. the example must still be running after MIN_RUN_SECONDS_PER seconds, so a
#      project that dies during startup can never be reported as a pass; and
#   2. its stdout/stderr must be free of the known error markers below.
#
# A process that exits on its own is reported instead of being silently accepted:
# a clean exit after the minimum window passes with an explicit note, while dying
# inside the window or exiting with a non-zero code fails the example.
#
# Usage:
#   scripts/smoke_examples.sh                          # all examples
#   scripts/smoke_examples.sh basic tree-generator    # only these examples
#   EVE=.../eve.exe RUN_SECONDS=8 scripts/smoke_examples.sh
#   MIN_RUN_SECONDS=2 RUN_SECONDS=6 scripts/smoke_examples.sh
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

# Every example must survive at least this long: "started, rendered and did not
# die" is the contract `scripts/check_examples.py` points at for new examples.
MIN_RUN_SECONDS_PER="${MIN_RUN_SECONDS:-2}"

if [[ "$MIN_RUN_SECONDS_PER" -gt "$RUN_SECONDS_PER" ]]; then
  echo "note: MIN_RUN_SECONDS=$MIN_RUN_SECONDS_PER > RUN_SECONDS=$RUN_SECONDS_PER; raising the run window" >&2
  RUN_SECONDS_PER="$MIN_RUN_SECONDS_PER"
fi

# Extra args appended to every `eve run` invocation (e.g. EVE_RUN_ARGS="--debug").
RUN_ARGS="${EVE_RUN_ARGS:-}"

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

# Runnable examples = dirs containing a main.nut.  scripts/check_examples.py
# guarantees that every non-exempt example directory has one, so this set is the
# complete runtime surface.
if [[ $# -gt 0 ]]; then
  EXAMPLES=("$@")
else
  EXAMPLES=()
  for d in "$EXAMPLES_DIR"/*/; do
    name="$(basename "$d")"
    [[ -f "$d/main.nut" ]] && EXAMPLES+=("$name")
  done
fi

echo "Smoke: ${#EXAMPLES[@]} example(s), alive window ${RUN_SECONDS_PER}s, minimum ${MIN_RUN_SECONDS_PER}s, binary ${EVE_BIN}"

FAILED=0
PASSED=0
declare -a FAILED_NAMES=()

for name in "${EXAMPLES[@]}"; do
  dir="$EXAMPLES_DIR/$name"
  [[ -f "$dir/main.nut" ]] || { echo "SKIP  $name (no main.nut)"; continue; }

  # devlab's whole point is DevTools: always smoke it with --debug.
  extra="$RUN_ARGS"
  if [[ "$name" == "devlab" ]]; then
    extra="$extra --debug"
  fi

  log="$(mktemp)"
  use_timeout=0
  if ! is_windows && command -v timeout >/dev/null 2>&1; then
    use_timeout=1
  fi

  if [[ "$use_timeout" -eq 1 ]]; then
    # `exec` makes $! the timeout process itself, so liveness of the timeout is
    # liveness of the example.
    ( cd "$dir" && exec timeout -k 3 "$RUN_SECONDS_PER" "$EVE_BIN" run $extra >"$log" 2>&1 ) &
  else
    # Windows: GNU timeout on MSYS cannot reliably terminate the native GUI
    # process (SIGTERM is ignored), so launch in the background and force-kill
    # by image name with taskkill below.
    ( cd "$dir" && exec "$EVE_BIN" run $extra >"$log" 2>&1 ) &
  fi
  pid=$!

  sleep "$MIN_RUN_SECONDS_PER"
  early_exit=0
  killed_by_harness=0
  if ! kill -0 "$pid" 2>/dev/null; then
    # Died inside the minimum window: always a failure, whatever the exit code.
    early_exit=1
    wait "$pid" 2>/dev/null
    rc=$?
  else
    remaining=$((RUN_SECONDS_PER - MIN_RUN_SECONDS_PER))
    [[ "$remaining" -gt 0 ]] && sleep "$remaining"
    if [[ "$use_timeout" -eq 0 ]] && kill -0 "$pid" 2>/dev/null; then
      killed_by_harness=1
      if is_windows; then
        taskkill //F //IM "$(basename "$EVE_BIN")" >/dev/null 2>&1 || true
      fi
      kill "$pid" 2>/dev/null || true
    fi
    wait "$pid" 2>/dev/null
    rc=$?
    if [[ "$killed_by_harness" -eq 0 && "$use_timeout" -eq 1 ]]; then
      # `timeout` reports 124 (SIGTERM) / 137 (SIGKILL) / 143 when it stopped a
      # healthy process; anything else means the example ended by itself.
      case "$rc" in
        124|137|143) killed_by_harness=1 ;;
      esac
    fi
  fi

  # Reset the per-example verdict: `reason` drives failure, `note` explains a
  # clean self-exit.  Both must be rebuilt every iteration.
  reason=""
  note=""
  if [[ "$early_exit" -eq 1 ]]; then
    reason="died inside the ${MIN_RUN_SECONDS_PER}s liveness window (exit code ${rc})"
  elif [[ "$killed_by_harness" -eq 0 ]]; then
    if [[ "$rc" -eq 0 ]]; then
      note="exited on its own before the ${RUN_SECONDS_PER}s window ended"
    else
      reason="exited on its own before the ${RUN_SECONDS_PER}s window ended (exit code ${rc})"
    fi
  fi

  hit=""
  if [[ -z "$reason" ]]; then
    for m in "${MARKERS[@]}"; do
      if grep -qF "$m" "$log"; then
        hit="$m"
        break
      fi
    done
    [[ -n "$hit" ]] && reason="marker: $hit"
  fi

  if [[ -n "$reason" ]]; then
    echo "FAIL  $name  (${reason})"
    if [[ -n "$hit" ]]; then
      # Print the marker WITH context: a marker line alone hides the rest of a
      # multi-line message (e.g. a compiler diagnostic on the following line),
      # which is exactly what a CI reader needs to debug the failure.
      grep -n -A 8 -F "$hit" "$log" 2>/dev/null | head -24 | sed 's/^/       /'
    fi
    echo "       --- key output (non-frame-error lines, head 30) ---"
    grep -vF "frame error" "$log" 2>/dev/null | head -n 30 | sed 's/^/       | /'
    echo "       --- example output (last 30 lines) ---"
    tail -n 30 "$log" | sed 's/^/       | /'
    FAILED=$((FAILED + 1))
    FAILED_NAMES+=("$name")
  else
    if [[ -n "$note" ]]; then
      echo "PASS  $name  ($note)"
    else
      echo "PASS  $name  (alive through the ${RUN_SECONDS_PER}s window)"
    fi
    if ! grep -qF "first present" "$log"; then
      echo "       note: no 'first present' startup line in the captured log"
    fi
    PASSED=$((PASSED + 1))
  fi
  rm -f "$log"
done

echo
echo "Summary: $PASSED passed, $FAILED failed (each example must stay alive >= ${MIN_RUN_SECONDS_PER}s with no error markers)"
if [[ $FAILED -gt 0 ]]; then
  printf 'Failed: %s\n' "${FAILED_NAMES[*]}"
  exit 1
fi
