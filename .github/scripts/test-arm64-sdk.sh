#!/usr/bin/env bash
# Validate the actual archive, with a fresh consumer CMake build and hard run gate.
set -euo pipefail
archive="${1:?SDK zip required}"
platform="${2:?runtime platform required}"
version="${3:-}"
scripts="$(cd "$(dirname "$0")" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
python3 - "$archive" "$work" <<'PY'
import pathlib
import sys
import zipfile

root = pathlib.Path(sys.argv[2]).resolve()
with zipfile.ZipFile(sys.argv[1]) as archive:
    for entry in archive.infolist():
        target = (root / entry.filename).resolve()
        if not target.is_relative_to(root):
            raise SystemExit(f"unsafe SDK archive entry: {entry.filename}")
    archive.extractall(root)
PY
sdk="$work/eve-sdk/$platform-arm64"
test -d "$sdk"
if [ "$platform" = linux ]; then chmod +x "$sdk/bin/eve"; fi
export SDK_TEST_ARCH=arm64 SDK_TEST_REQUIRE_RUN=1
bash "$scripts/test-sdk.sh" "$sdk" "$platform" "$version"
