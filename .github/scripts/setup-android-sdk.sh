#!/usr/bin/env bash
# Locate Android SDK roots / sdkmanager / NDK after android-actions/setup-android.
# Writes ANDROID_HOME, ANDROID_SDK, ANDROID_NDK (forward-slash paths) to GITHUB_ENV.
set -euo pipefail

ANDROID_API="${ANDROID_API:-34}"
ANDROID_NDK_VERSION="${ANDROID_NDK_VERSION:-27.3.13750724}"

to_unix() {
  local p="${1//\\//}"
  if command -v cygpath >/dev/null 2>&1; then
    cygpath -u "$p" 2>/dev/null || echo "$p"
  else
    echo "$p"
  fi
}

candidates=()
[ -n "${ANDROID_HOME:-}" ] && candidates+=("$ANDROID_HOME")
[ -n "${ANDROID_SDK_ROOT:-}" ] && candidates+=("$ANDROID_SDK_ROOT")
[ -n "${ANDROID_SDK:-}" ] && candidates+=("$ANDROID_SDK")
[ -n "${LOCALAPPDATA:-}" ] && candidates+=("${LOCALAPPDATA}/Android/Sdk")
candidates+=("$HOME/Android/Sdk" "$HOME/Library/Android/sdk" "/usr/local/lib/android/sdk")

SDK_ROOT=""
for c in "${candidates[@]}"; do
  [ -n "$c" ] || continue
  u=$(to_unix "$c")
  if [ -d "$u" ]; then
    SDK_ROOT="$u"
    break
  fi
done

if [ -z "$SDK_ROOT" ]; then
  echo "ANDROID_HOME / Android SDK root not found"
  echo "ANDROID_HOME=${ANDROID_HOME:-}"
  echo "ANDROID_SDK_ROOT=${ANDROID_SDK_ROOT:-}"
  echo "PATH=$PATH"
  exit 1
fi

find_sdkmanager() {
  local root="$1"
  local c
  for c in \
    "$root/cmdline-tools/latest/bin/sdkmanager" \
    "$root/cmdline-tools/latest/bin/sdkmanager.bat" \
    "$root"/cmdline-tools/*/bin/sdkmanager \
    "$root"/cmdline-tools/*/bin/sdkmanager.bat \
    "$root/tools/bin/sdkmanager" \
    "$root/tools/bin/sdkmanager.bat"; do
    if [ -f "$c" ]; then
      echo "$c"
      return 0
    fi
  done
  # Also accept whatever is already on PATH.
  if command -v sdkmanager >/dev/null 2>&1; then
    command -v sdkmanager
    return 0
  fi
  if command -v sdkmanager.bat >/dev/null 2>&1; then
    command -v sdkmanager.bat
    return 0
  fi
  return 1
}

SDKMANAGER="$(find_sdkmanager "$SDK_ROOT" || true)"
if [ -z "$SDKMANAGER" ]; then
  echo "sdkmanager not found under $SDK_ROOT"
  ls -la "$SDK_ROOT" || true
  ls -la "$SDK_ROOT/cmdline-tools" 2>/dev/null || true
  exit 1
fi

echo "Using Android SDK: $SDK_ROOT"
echo "Using sdkmanager: $SDKMANAGER"

# Install requested packages if missing.
need_install=0
[ -d "$SDK_ROOT/platforms/android-${ANDROID_API}" ] || need_install=1
[ -d "$SDK_ROOT/build-tools/${ANDROID_API}.0.0" ] || need_install=1
[ -d "$SDK_ROOT/ndk/${ANDROID_NDK_VERSION}" ] || need_install=1

if [ "$need_install" -eq 1 ]; then
  echo "Installing Android packages via sdkmanager..."
  yes | "$SDKMANAGER" --sdk_root="$SDK_ROOT" --install \
    "platform-tools" \
    "platforms;android-${ANDROID_API}" \
    "build-tools;${ANDROID_API}.0.0" \
    "ndk;${ANDROID_NDK_VERSION}" || true
  yes | "$SDKMANAGER" --sdk_root="$SDK_ROOT" --licenses >/dev/null || true
fi

NDK_DIR="$SDK_ROOT/ndk/${ANDROID_NDK_VERSION}"
if [ ! -f "$NDK_DIR/build/cmake/android.toolchain.cmake" ]; then
  # Fall back to any installed NDK.
  NDK_DIR="$(ls -d "$SDK_ROOT"/ndk/* 2>/dev/null | sort -V | tail -1 || true)"
fi
if [ -z "${NDK_DIR:-}" ] || [ ! -f "$NDK_DIR/build/cmake/android.toolchain.cmake" ]; then
  echo "Android NDK toolchain not found under $SDK_ROOT/ndk"
  ls -la "$SDK_ROOT/ndk" 2>/dev/null || true
  exit 1
fi

{
  echo "ANDROID_HOME=$SDK_ROOT"
  echo "ANDROID_SDK_ROOT=$SDK_ROOT"
  echo "ANDROID_SDK=$SDK_ROOT"
  echo "ANDROID_NDK=$NDK_DIR"
} >> "${GITHUB_ENV:?}"

# Ensure cmdline-tools bin is on PATH for later steps.
BIN_DIR="$(dirname "$SDKMANAGER")"
echo "$BIN_DIR" >> "${GITHUB_PATH:?}"
echo "$SDK_ROOT/platform-tools" >> "${GITHUB_PATH:?}"

echo "ANDROID_NDK=$NDK_DIR"
