#!/usr/bin/env bash
# Validate the architecture of an installed or downloaded macOS SDK.
set -euo pipefail
sdk="${1:?usage: check-macos-sdk.sh <sdk-root> <arm64|x86_64>}"
architecture="${2:?expected architecture required}"
fail() { echo "ERROR: $*" >&2; exit 1; }
case "$architecture" in
  arm64|x86_64) ;;
  *) fail "unsupported macOS architecture: $architecture" ;;
esac
[ "$(cat "$sdk/share/eve/TARGET_PLATFORM")" = macosx ] || fail "SDK platform is not macosx"
[ "$(cat "$sdk/share/eve/TARGET_ARCHITECTURE")" = "$architecture" ] || fail "SDK architecture marker mismatch"
[ "$(lipo -archs "$sdk/bin/eve")" = "$architecture" ] || fail "eve is not a single $architecture executable"
for runtime in libvulkan.1.dylib libMoltenVK.dylib MoltenVK_icd.json; do
  [ -f "$sdk/lib/$runtime" ] || fail "missing bundled runtime: $runtime"
done
while IFS= read -r library; do
  lipo -archs "$library" | tr ' ' '\n' | grep -qx "$architecture" \
    || fail "library does not contain $architecture: $library"
done < <(find "$sdk/lib" \( -type f -o -type l \) -name '*.dylib')
echo "OK: macOS SDK runtime and libraries support $architecture"
