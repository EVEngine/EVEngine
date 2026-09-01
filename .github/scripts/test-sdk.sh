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
# find_package(EVEngine) does not resolve a relative EVEngine_DIR; normalize
# the SDK root to an absolute path so the consumer configure is reproducible.
case "$SDK" in
  /*) ;;
  *) SDK="$(cd "$(dirname "$SDK")" && pwd)/$(basename "$SDK")" ;;
esac
PLAT="${2:?usage: test-sdk.sh <sdk-root> <platform> [expected-version]}"
EXPECTED="${3:-}"

case "$PLAT" in
  win32)           RUNTIME="eve.exe" ;;
  linux|macosx)    RUNTIME="eve" ;;
  android)         RUNTIME="" ;;   # verified by the APK smoke below
  *) echo "SKIP: no host runtime to test for platform '$PLAT'"; exit 0 ;;
esac

fail() { echo "ERROR: $*" >&2; exit 1; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# --- Android: assemble an APK from the SDK packaging template ----------------
# Hard gate: the template + SDK libs + a minimal game must produce an APK.
if [ "$PLAT" = "android" ]; then
  if [ -z "${ANDROID_SDK:-}" ] || [ -z "${JAVA_HOME:-}" ]; then
    echo "SKIP: android APK smoke needs ANDROID_SDK + JAVA_HOME in the environment"
    exit 0
  fi
  [ -f "$SDK/share/eve/TARGET_PLATFORM" ] || fail "missing share/eve/TARGET_PLATFORM"
  [ "$(cat "$SDK/share/eve/TARGET_PLATFORM")" = "android" ] || fail "TARGET_PLATFORM != android"
  VERSION_FILE="$(cat "$SDK/share/eve/VERSION")"
  if [ -n "$EXPECTED" ] && [ "$VERSION_FILE" != "$EXPECTED" ]; then
    fail "share/eve/VERSION '$VERSION_FILE' != expected '$EXPECTED'"
  fi
  TEMPLATE="$SDK/platform/apk"
  [ -f "$TEMPLATE/gradlew" ] || [ -f "$TEMPLATE/gradlew.bat" ] || fail "android SDK missing gradle template"
  # The packaged game must launch the game host when the user taps the icon.
  # EVTestActivity is the unit-test host and is started explicitly (am start);
  # if it leaks back into the launcher slot the game never starts.
  MANIFEST="$TEMPLATE/app/src/main/AndroidManifest.xml"
  if ! sed -n '/android:name="\.EVEngineActivity"/,/<\/activity>/p' "$MANIFEST" \
       | grep -q 'android.intent.action.MAIN'; then
    fail "android template launcher must be EVEngineActivity (MAIN intent missing)"
  fi
  if sed -n '/android:name="\.EVTestActivity"/,/<\/activity>/p' "$MANIFEST" \
       | grep -q 'android.intent.action.MAIN'; then
    fail "android template launcher must be EVEngineActivity (EVTestActivity has MAIN)"
  fi

  JNI="$TEMPLATE/app/src/main/jniLibs/arm64-v8a"
  mkdir -p "$JNI"
  [ -f "$SDK/lib/libmain.so" ] || fail "missing lib/libmain.so"
  cp "$SDK/lib/libmain.so" "$JNI/"
  for lib in libSDL2.so libhidapi.so libc++_shared.so; do
    [ -f "$SDK/lib/$lib" ] && cp "$SDK/lib/$lib" "$JNI/"
  done

  GAME="$TEMPLATE/app/src/main/assets/game"
  mkdir -p "$GAME"
  cat > "$GAME/config.nut" <<'EOF'
config = { width=320 height=240 title="sdk-test" debug=false hotReload=false };
EOF
  cat > "$GAME/main.nut" <<'EOF'
eve_init = function() { print("SDK_TEST_GAME_OK\n"); };
eve_update = function(dt) {};
EOF
  printf 'sdk.dir=%s\n' "$ANDROID_SDK" > "$TEMPLATE/local.properties"

  echo "== [test-sdk] assembling APK from $TEMPLATE =="
  ( cd "$TEMPLATE" && sh gradlew :app:assembleDebug --no-daemon --console=plain ) \
    >"$WORK/apk-build.log" 2>&1
  rc=$?
  if [ $rc -ne 0 ]; then
    tail -n 40 "$WORK/apk-build.log"
    fail "gradle assembleDebug failed"
  fi
  APK="$TEMPLATE/app/build/outputs/apk/debug/app-debug.apk"
  [ -f "$APK" ] || fail "assembleDebug did not produce app-debug.apk"
  echo "OK: android APK assembled: $(basename "$APK") ($(du -h "$APK" | cut -f1))"
  echo "== [test-sdk] PASS (android) =="
  exit 0
fi

echo "== [test-sdk] platform=$PLAT sdk=$SDK runtime=$RUNTIME =="

# --- 1. SDK layout + markers ---------------------------------------------------
[ -x "$SDK/bin/$RUNTIME" ] || fail "missing runtime bin/$RUNTIME"
[ -d "$SDK/include/eve" ] || fail "missing include/eve"
[ "$(find "$SDK/include/eve" -name '*.h' | wc -l)" -gt 0 ] || fail "no headers under include/eve"
[ -f "$SDK/include/zeroerr/assert.h" ] || fail "missing public dependency include/zeroerr/assert.h"
case "$PLAT" in
  win32|android)
    # Windows ships eve.lib for plugins; Android ships libmain.so + packaging .so.
    [ -d "$SDK/lib" ] || fail "missing lib/"
    ;;
  # linux/macosx have no engine import libs: plugins are built by the consumer.
esac
[ -f "$SDK/cmake/EVEngineConfig.cmake" ] || fail "missing cmake/EVEngineConfig.cmake"
[ -f "$SDK/cmake/EVEnginePlugin.cmake" ] || fail "missing cmake/EVEnginePlugin.cmake"
[ -d "$SDK/platform" ] || fail "missing platform/"
[ -f "$SDK/share/eve/TARGET_PLATFORM" ] || fail "missing share/eve/TARGET_PLATFORM"
[ -f "$SDK/share/eve/VERSION" ] || fail "missing share/eve/VERSION"
[ -f "$SDK/share/eve/examples/basic/config.nut" ] || fail "missing share/eve/examples/basic/config.nut"
[ -f "$SDK/share/eve/examples/basic/main.nut" ] || fail "missing share/eve/examples/basic/main.nut"
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
VER="$("$SDK/bin/$RUNTIME" -v 2>&1)" || true
echo "  eve -v -> ${VER:-<empty>}"
if [ -n "$EXPECTED" ]; then
  echo "$VER" | grep -qF "$EXPECTED" || \
    fail "eve -v did not contain expected version '$EXPECTED' (output: $VER)"
else
  echo "$VER" | grep -qE '[0-9]+(\.[0-9]+)+' || fail "eve -v did not print a version (output: $VER)"
fi
echo "OK: host runtime executes"

# --- 3 + 4. zip + package a sample game ----------------------------------------
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

# --- 5. Native C++ plugin compiles against the SDK -----------------------------
# Hard gate: find_package(EVEngine) + add_eve_plugin() is the SDK's primary
# consumer path; a plugin must configure, compile and link on the host.
PLUGIN="$WORK/plugin"
mkdir -p "$PLUGIN"
cat > "$PLUGIN/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.21)
project(eve_sdk_plugin LANGUAGES CXX)
find_package(EVEngine REQUIRED)
add_eve_plugin(eve_sdk_plugin SOURCES plugin.cpp)
EOF
cat > "$PLUGIN/plugin.cpp" <<'EOF'
#include "common/Export.h"
#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::sdktest {
class SdkTestPlugin : public Module {
public:
    Module_REG(SdkTestPlugin);
    SdkTestPlugin() = default;
    ~SdkTestPlugin() override = default;
};
Module_IMPL(SdkTestPlugin, new SdkTestPlugin());
void SdkTestPlugin::expose(ssq::Table& table) {
    auto cls = table.addClass(name, SdkTestPlugin::create, false);
    expose(cls);
}
void SdkTestPlugin::expose(ssq::Class& cls) {
    cls.addFunc("getName", &SdkTestPlugin::getName);
}
}  // namespace eve::sdktest

EVE_PLUGIN_EXPORT int eve_plugin_init(void) { return 0; }
EOF

CMAKE_ARGS=(-S "$PLUGIN" -B "$PLUGIN/build" -DEVEngine_DIR="$SDK/cmake" -DCMAKE_BUILD_TYPE=Release)
if [ "$PLAT" = "win32" ]; then
  # Default generator: GitHub-hosted windows images ship VS 2026, local dev
  # machines may have 2022/2026. Let CMake pick the installed instance.
  CMAKE_ARGS+=(-A x64)
fi
cmake "${CMAKE_ARGS[@]}" >"$WORK/plugin-cmake.log" 2>&1 || {
  tail -n 30 "$WORK/plugin-cmake.log"
  fail "plugin cmake configure failed"
}
cmake --build "$PLUGIN/build" --config Release --parallel 2 >"$WORK/plugin-build.log" 2>&1 || {
  tail -n 40 "$WORK/plugin-build.log"
  fail "plugin cmake build failed"
}
case "$PLAT" in
  win32)   PLUGIN_OUT="$(find "$PLUGIN/build" -type f -name 'eve_sdk_plugin.dll' | head -1)" ;;
  macosx)  PLUGIN_OUT="$(find "$PLUGIN/build" -type f -name 'eve_sdk_plugin.dylib' | head -1)" ;;
  *)       PLUGIN_OUT="$(find "$PLUGIN/build" -type f -name 'eve_sdk_plugin.so' | head -1)" ;;
esac
[ -n "$PLUGIN_OUT" ] || fail "plugin shared library not produced"
echo "OK: native C++ plugin compiled: $(basename "$PLUGIN_OUT")"

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

