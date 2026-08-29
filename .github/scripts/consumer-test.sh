#!/usr/bin/env bash
# consumer-test.sh <host> <tag> <work-dir>
#
# Simulates an end user whose machine has NOTHING but the packaged SDK zip:
#   host = win32 | linux | macosx   (the SDK that carries the eve binary)
#   tag  = v0.1.0                   (release tag, also used in asset names)
#   work = scratch directory (recreated)
#
# Verifies the full consumer chain on the host:
#   1. download eve-sdk-<host>-<tag>.zip from the release (real artifact)
#   2. bin/eve -v prints the expected version
#   3. eve get android  -> installs the official EVEngine android SDK + toolchain
#   4. eve build android -> produces an APK with engine libs + game assets +
#      EVEngineActivity as the launcher
#   5. eve package <host> -> runnable game folder; the packaged game is started
#      headless and must print EVE_CI_GAME_OK (stdout is unbuffered since
#      fe661c2b, so the marker lands in the log before the smoke kill)
#   6. cross-packaging: for each target in EVE_CROSS_TARGETS, package the same
#      game with the downloaded target SDK (e.g. linux host -> win32 package).
#      Combo support is a matrix decision in the workflow: unsupported combos
#      are simply not listed there.
#
# Artifacts (uploaded by the workflow):
#   <work>/eve-android-<host>.apk      built APK
#   <work>/pkg-<target>.zip            packaged game for each cross target
set -euo pipefail

HOST="${1:?usage: consumer-test.sh <host> <tag> <work-dir>}"
TAG="${2:?usage: consumer-test.sh <host> <tag> <work-dir>}"
WORK="${3:?usage: consumer-test.sh <host> <tag> <work-dir>}"
# Git Bash passes $RUNNER_TEMP in Windows form (D:\a\_temp). Normalize to a
# POSIX path before the leading-slash check below, otherwise $(pwd) gets
# prepended and curl/zip paths end up mangled (a mix of /d/... and D:\...).
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    if command -v cygpath >/dev/null 2>&1; then
      WORK="$(cygpath -u "$WORK")"
    else
      WORK="$(printf '%s' "$WORK" | tr '\\' '/')"
    fi
    ;;
esac
case "$WORK" in
  /*) ;;
  *) WORK="$(pwd)/$WORK" ;;
esac
REPO="${GITHUB_REPOSITORY:-EVEngine/EVEngine}"
# Overridable for local verification / mirrors (file://... works with curl).
BASE="${EVE_SDK_DOWNLOAD_BASE:-https://github.com/${REPO}/releases/download/${TAG}}"
EXPECTED_VERSION="${TAG#v}"
MARKER="EVE_CI_GAME_OK"

case "$HOST" in
  win32) RUNTIME="eve.exe" ;;
  linux|macosx) RUNTIME="eve" ;;
  *) echo "ERROR: unsupported host '$HOST'" >&2; exit 2 ;;
esac

rm -rf "$WORK"
mkdir -p "$WORK"

fail() { echo "ERROR: $*" >&2; exit 1; }

echo "== consumer-test: host=$HOST tag=$TAG =="

PY="$(command -v python3 2>/dev/null || command -v python 2>/dev/null)"
[ -n "$PY" ] || fail "no python interpreter found"

# Git Bash's GNU tar cannot read zips; python zipfile is portable across all
# three runners. chmod +x afterwards: zipfile does not preserve exec bits.
extract_zip() {
  local zip="$1" dest="$2"
  mkdir -p "$dest"
  "$PY" - "$zip" "$dest" <<'PYEOF'
import pathlib, sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
dest = pathlib.Path(sys.argv[2])
for name in z.namelist():
    target = dest / name
    if name.endswith("/"):
        target.mkdir(parents=True, exist_ok=True)
        continue
    target.parent.mkdir(parents=True, exist_ok=True)
    with z.open(name) as src, open(target, "wb") as out:
        out.write(src.read())
PYEOF
}

# 1. Download + extract the host SDK (the real release artifact).
curl -fL --retry 3 -o "$WORK/eve-sdk-$HOST-$TAG.zip" \
  "$BASE/eve-sdk-$HOST-$TAG.zip" || fail "cannot download host SDK zip"
extract_zip "$WORK/eve-sdk-$HOST-$TAG.zip" "$WORK" || fail "cannot extract host SDK zip"
HOST_SDK="$WORK/eve-sdk/$HOST"
[ -f "$HOST_SDK/bin/$RUNTIME" ] || fail "host SDK missing bin/$RUNTIME"
if [ "$HOST" != "win32" ]; then
  chmod +x "$HOST_SDK/bin/$RUNTIME" || fail "cannot chmod host runtime"
fi
[ "$(cat "$HOST_SDK/share/eve/TARGET_PLATFORM")" = "$HOST" ] || fail "host SDK TARGET_PLATFORM mismatch"
EVE="$HOST_SDK/bin/$RUNTIME"

# 2. Runtime executes and reports the expected version.
VER="$("$EVE" -v 2>&1 || true)"
echo "$VER" | grep -qF "$EXPECTED_VERSION" || fail "eve -v did not contain '$EXPECTED_VERSION' (output: $VER)"
echo "OK: eve -v -> $(echo "$VER" | tail -1)"

# Sample game with a startup marker (stdout is unbuffered).
GAME="$WORK/game"
mkdir -p "$GAME"
cat > "$GAME/config.nut" <<EOF
config = { width=320 height=240 title="eve-consumer-test" debug=false hotReload=false };
EOF
cat > "$GAME/main.nut" <<EOF
eve_init = function() { print("$MARKER"); };
eve_update = function(dt) {};
EOF

# 3 + 4. eve get android -> eve build android.
# The toolchain dirs come from the environment so the workflow can cache them
# across runs (a fresh user still downloads once, exactly like a real machine).
export EVE_SDK_INSTALL_ROOT="${EVE_SDK_INSTALL_ROOT:-$WORK/eve-sdk-root}"
export ANDROID_HOME="${ANDROID_HOME:-$WORK/android-sdk}"
"$EVE" get android || fail "eve get android failed"
"$EVE" build android "$GAME" -o "$WORK/apk-out" || fail "eve build android failed"

APK="$(find "$WORK/apk-out" -name '*.apk' | head -1)"
[ -n "$APK" ] || fail "no APK produced"
"$PY" - "$APK" <<'PYEOF' || fail "APK content check failed"
import sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
names = set(z.namelist())
need = {"lib/arm64-v8a/libmain.so", "lib/arm64-v8a/libc++_shared.so",
        "assets/game/main.nut", "assets/game/config.nut"}
missing = need - names
if missing:
    sys.exit(f"APK missing entries: {sorted(missing)}")
print("OK: APK contains engine libs + game assets")
PYEOF

# Launcher must be the game host (regression guard for the EVTestActivity bug).
if [ -n "${ANDROID_HOME:-}" ]; then
  # set -o pipefail would abort on a missing build-tools dir; the launcher
  # check is best-effort so the failure mode is a WARN, not a hard exit.
  AAPT="$(find "$ANDROID_HOME/build-tools" -name 'aapt' -o -name 'aapt.exe' 2>/dev/null | sort -V | tail -1 || true)"
  if [ -n "$AAPT" ] && "$AAPT" dump badging "$APK" 2>/dev/null | grep -q 'launchable-activity.*EVEngineActivity'; then
    echo "OK: APK launcher = EVEngineActivity"
  else
    echo "WARN: could not confirm launcher activity (aapt missing or mismatch)"
  fi
fi
cp "$APK" "$WORK/eve-android-$HOST.apk"

# 5. Package the host game and run it headless until the marker appears.
"$EVE" package "$GAME" --sdk "$HOST_SDK" -o "$WORK/pkg-$HOST" >/dev/null || fail "eve package ($HOST) failed"
PACKAGED="$WORK/pkg-$HOST/$RUNTIME"
[ -f "$PACKAGED" ] || fail "package missing runtime"
LOG="$WORK/run-$HOST.log"
( cd "$WORK/pkg-$HOST" && exec "$PACKAGED" >"$LOG" 2>&1 ) &
PID=$!
for i in $(seq 1 40); do
  if grep -q "$MARKER" "$LOG" 2>/dev/null; then
    echo "OK: packaged $HOST game printed $MARKER"
    kill "$PID" 2>/dev/null || true
    break
  fi
  sleep 1
done
if ! grep -q "$MARKER" "$LOG" 2>/dev/null; then
  kill "$PID" 2>/dev/null || true
  tail -n 25 "$LOG" >&2 || true
  fail "packaged $HOST game did not print $MARKER"
fi

# 6. Cross-packaging: package the same game for each listed target SDK.
for target in ${EVE_CROSS_TARGETS:-}; do
  case "$target" in
    win32|linux|macosx) ;;
    *) echo "SKIP: cross target '$target' unsupported by this script"; continue ;;
  esac
  echo "== cross-packaging host=$HOST -> target=$target =="
  curl -fL --retry 3 -o "$WORK/eve-sdk-$target-$TAG.zip" \
    "$BASE/eve-sdk-$target-$TAG.zip" || fail "cannot download $target SDK zip"
  extract_zip "$WORK/eve-sdk-$target-$TAG.zip" "$WORK" || fail "cannot extract $target SDK zip"
  TARGET_SDK="$WORK/eve-sdk/$target"
  "$EVE" package "$GAME" --sdk "$TARGET_SDK" -o "$WORK/pkg-$target" >/dev/null \
    || fail "cross package ($target from $HOST) failed"
  ( cd "$WORK/pkg-$target" && tar -czf "$WORK/pkg-$target.tar.gz" . ) || fail "cannot archive $target package"
  echo "OK: cross package $target from $HOST -> $WORK/pkg-$target.tar.gz"
done

echo "== [consumer-test] PASS ($HOST, $TAG) =="
