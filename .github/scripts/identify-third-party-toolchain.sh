#!/usr/bin/env bash
# Fingerprint the selected target toolchain, excluding installation paths.
set -euo pipefail
platform="${1:?target platform required}"
identity="$(mktemp)"
trap 'rm -f "$identity"' EXIT
{
  cmake --version
  case "$platform" in
    linux)
      g++ -dumpmachine
      g++ -dumpfullversion -dumpversion
      ;;
    macosx|ios)
      clang --version
      xcodebuild -version
      if [ "$platform" = ios ]; then
        xcrun --sdk iphoneos --show-sdk-version
        echo "arch=${IOS_ARCH:-arm64} deployment=${IOS_DEPLOYMENT_TARGET:-13.0} sdk=iphoneos"
      else
        xcrun --sdk macosx --show-sdk-version
        uname -m
      fi
      ;;
    android)
      test -f "$ANDROID_NDK/source.properties"
      cat "$ANDROID_NDK/source.properties"
      echo "abi=${ANDROID_ABI:-arm64-v8a} platform=${ANDROID_PLATFORM:-android-24} stl=${ANDROID_STL:-c++_shared}"
      ;;
    *) echo "unsupported target toolchain: $platform" >&2; exit 2 ;;
  esac
} > "$identity"
# Python is available in every CI lane, including Windows Git Bash.
python3 - "$identity" <<'PY'
import hashlib
from pathlib import Path
import sys

print('id=' + hashlib.sha256(Path(sys.argv[1]).read_bytes()).hexdigest())
PY
