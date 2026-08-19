#!/usr/bin/env bash
# test-sdk.sh <sdk-root> <platform> [expected-version]
#
# Verify a freshly-built target-platform SDK is usable by consumers, before it is
# zipped and published to a GitHub Release. Runs on the same host as the SDK:
#   win32 -> bin/eve.exe   (Windows runner)
#   linux -> bin/eve       (Linux runner)
#   macosx-> bin/eve       (macOS runner)
#   android/ios -> skipped (no host runtime binary)
#
# [expected-version] (e.g. "0.1.0") makes the version checks exact: the
# share/eve/VERSION marker must equal it and `eve -v` must contain it.
#
# Hard gate (fails CI on error):
#   1. SDK layout + markers (bin runtime, include/eve headers, lib, cmake config,
#      platform template, share/eve/TARGET_PLATFORM == expected platform).
#   2. Host runtime executes: `eve -v` prints a version.
#   3. `eve zip` compresses a sample game into a .eve archive.
#   4. `eve package --sdk` assembles a runnable game folder containing the runtime
#      + game.eve (and, on Windows, the runtime DLLs).
#
# Best-effort (WARN only, never blocks the release): try to run the packaged game
# from memory and check for the game's startup marker. Window/Vulkan init depends
# on a display / software ICD, so failures there are reported, not fatal.
set -euo pipefail

SDK="${1:?usage: test-sdk.sh <sdk-root> <platform> [expected-version]}"
PLAT="${2:?usage: test-sdk.sh <sdk-root> <platform> [expected-version]}"
EXPECTED="${3:-}"

case "$PLAT" in
  win32)           RUNTIME="eve.exe" ;;
  linux|macosx)    RUNTIME="eve" ;;
  *) echo "SKIP: no host runtime to test for platform '$PLAT'"; exit 0 ;;
esac

fail() { echo "ERROR: $*" >&2; exit 1; }

echo "== [test-sdk] platform=$PLAT sdk=$SDK runtime=$RUNTIME =="

# --- 1. SDK layout + markers ---------------------------------------------------
[ -x "$SDK/bin/$RUNTIME" ] || fail "missing runtime bin/$RUNTIME"
[ -d "$SDK/include/eve" ] || fail "missing include/eve"
[ "$(find "$SDK/include/eve" -name '*.h' | wc -l)" -gt 0 ] || fail "no headers under include/eve"
[ -d "$SDK/lib" ] || fail "missing lib/"
[ -f "$SDK/cmake/EVEngineConfig.cmake" ] || fail "missing cmake/EVEngineConfig.cmake"
[ -f "$SDK/cmake/EVEnginePlugin.cmake" ] || fail "missing cmake/EVEnginePlugin.cmake"
[ -d "$SDK/platform" ] || fail "missing platform/"
[ -f "$SDK/share/eve/TARGET_PLATFORM" ] || fail "missing share/eve/TARGET_PLATFORM"
[ -f "$SDK/share/eve/VERSION" ] || fail "missing share/eve/VERSION"
TP="$(cat "$SDK/share/eve/TARGET_PLATFORM")"
[ "$TP" = "$PLAT" ] || fail "TARGET_PLATFORM '$TP' != expected '$PLAT'"
echo "OK: SDK layout + markers"

VERSION_FILE="$(cat "$SDK/share/eve/VERSION")"
[ -n "$VERSION_FILE" ] || fail "share/eve/VERSION is empty"
if [ -n "$EXPECTED" ]; then
  [ "$VERSION_FILE" = "$EXPECTED" ] || \
    fail "share/eve/VERSION '$VERSION_FILE' != expected '$EXPECTED'"
fi
echo "OK: SDK version marker -> $VERSION_FILE"

# --- 2. Host runtime runs ------------------------------------------------------
VER="$("$SDK/bin/$RUNTIME" -v 2>&1)"
echo "  eve -v -> ${VER:-<empty>}"
if [ -n "$EXPECTED" ]; then
  echo "$VER" | grep -qF "$EXPECTED" || \
    fail "eve -v did not contain expected version '$EXPECTED'"
else
  echo "$VER" | grep -qE '[0-9]+(\.[0-9]+)+' || fail "eve -v did not print a version"
fi
echo "OK: host runtime executes"

# --- 3 + 4. zip + package a sample game ----------------------------------------
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
GAME="$WORK/sample"
mkdir -p "$GAME/assets"
cat > "$GAME/config.nut" <<'EOF'
config = { width=320 height=240 title="sdk-test" debug=false hotReload=false };
EOF
cat > "$GAME/main.nut" <<'EOF'
eve_init = function() { print("SDK_TEST_GAME_OK\n"); };
eve_update = function(dt) {};
EOF
echo "asset-data" > "$GAME/assets/data.txt"

"$SDK/bin/$RUNTIME" zip "$GAME" >/dev/null 2>&1 || fail "eve zip failed"
[ -f "$GAME.eve" ] || fail "eve zip did not produce $GAME.eve"
echo "OK: eve zip -> $(basename "$GAME.eve")"

PKG="$WORK/sample-pkg"
"$SDK/bin/$RUNTIME" package "$GAME" -o "$PKG" --sdk "$SDK" >/dev/null 2>&1 || fail "eve package failed"
[ -f "$PKG/$RUNTIME" ] || fail "package missing runtime $RUNTIME"
[ -f "$PKG/game.eve" ] || fail "package missing game.eve"
if [ "$PLAT" = "win32" ]; then
  [ "$(find "$PKG" -maxdepth 1 -name '*.dll' | wc -l)" -gt 0 ] || fail "win32 package has no runtime DLLs"
fi
echo "OK: eve package produced runnable game folder:"
ls -1 "$PKG" | sed 's/^/    /'

# --- Best-effort: run the packaged game from memory -----------------------------
echo "== best-effort in-memory run =="
if [ "$PLAT" = "linux" ] && [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ] \
   && ! command -v xvfb-run >/dev/null 2>&1; then
  echo "SKIP: headless Linux without xvfb; run gate covered by zip/package steps"
else
  LOG="$WORK/run.log"
  RUN_DIR="$PKG"
  if command -v xvfb-run >/dev/null 2>&1 && [ "$PLAT" = "linux" ]; then
    xvfb-run -a "$PKG/$RUNTIME" > "$LOG" 2>&1 &
  else
    ( cd "$RUN_DIR" && exec "$PKG/$RUNTIME" > "$LOG" 2>&1 ) &
  fi
  RUNPID=$!
  sleep 15
  # Terminate the game (bounded; never block the gate). WARN-only.
  kill "$RUNPID" 2>/dev/null || true
  sleep 1
  kill -9 "$RUNPID" 2>/dev/null || true
  sleep 1

  if grep -q "SDK_TEST_GAME_OK" "$LOG"; then
    echo "OK: packaged game ran from memory (scripts loaded from game.eve)"
  else
    echo "WARN: could not confirm in-memory run (window/Vulkan/headless). Logs:"
    sed 's/^/    /' "$LOG" | head -12
  fi
fi

echo "== [test-sdk] PASS ($PLAT) =="

