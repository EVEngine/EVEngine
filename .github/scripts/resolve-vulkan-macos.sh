#!/usr/bin/env bash
# Resolve VULKAN_SDK + MoltenVK.xcframework layout after install-vulkan-sdk-action.
# Usage: resolve-vulkan-macos.sh [SDK_ROOT]
# Writes VULKAN_SDK (and optional VK_ICD_FILENAMES/PATH) into GITHUB_ENV when set.
set -euo pipefail

SDK_ROOT="${1:-${RUNNER_TEMP:-/tmp}/VulkanSDK}"

CANDIDATES=(
  "${VULKAN_SDK:-}"
  "$SDK_ROOT"
  "$SDK_ROOT"/*
  "$SDK_ROOT"/*/macOS
  "$HOME/vulkan-sdk"
  "$HOME/vulkan-sdk"/*
  "$HOME/vulkan-sdk"/*/macOS
)

RESOLVED=""
for c in "${CANDIDATES[@]}"; do
  [ -n "$c" ] && [ -d "$c" ] || continue
  if [ -f "$c/lib/libvulkan.dylib" ] || [ -f "$c/lib/libMoltenVK.dylib" ] || [ -d "$c/lib/MoltenVK.xcframework" ]; then
    RESOLVED="$c"
    break
  fi
  if [ -f "$c/macOS/lib/libvulkan.dylib" ]; then
    RESOLVED="$c/macOS"
    break
  fi
done

if [ -z "$RESOLVED" ]; then
  echo "Could not locate Vulkan SDK under $SDK_ROOT"
  find "$SDK_ROOT" -maxdepth 4 -type d 2>/dev/null | head -80 || true
  exit 1
fi

echo "Using VULKAN_SDK=$RESOLVED"
if [ -n "${GITHUB_ENV:-}" ]; then
  echo "VULKAN_SDK=$RESOLVED" >> "$GITHUB_ENV"
fi
export VULKAN_SDK="$RESOLVED"

# Prefer the dynamic MoltenVK.framework xcframework (iOS/lib). The macOS/lib
# xcframework only ships ios-arm64 as static libMoltenVK.a — usable as a
# fallback, but -framework MoltenVK will fail against it.
IOS_FW_XC=""
for c in \
  "$RESOLVED/../iOS/lib/MoltenVK.xcframework" \
  "$SDK_ROOT"/*/iOS/lib/MoltenVK.xcframework \
  "$RESOLVED/iOS/lib/MoltenVK.xcframework"
do
  if [ -d "$c/ios-arm64/MoltenVK.framework" ]; then
    IOS_FW_XC="$c"
    break
  fi
done

STATIC_XC=""
for c in \
  "$RESOLVED/lib/MoltenVK.xcframework" \
  "$SDK_ROOT"/*/macOS/lib/MoltenVK.xcframework
do
  if [ -f "$c/ios-arm64/libMoltenVK.a" ]; then
    STATIC_XC="$c"
    break
  fi
done

if [ -n "$IOS_FW_XC" ]; then
  echo "Found MoltenVK.framework xcframework at $IOS_FW_XC"
  # Prefer not writing into the installed SDK (often not writable on GHA).
  # Mirror into a HOME shim so CMake's ~/VulkanSDK/*/iOS glob can see it when
  # the sibling iOS/ tree is missing from a partial extract.
  if [ -d "$RESOLVED/../iOS/lib/MoltenVK.xcframework/ios-arm64/MoltenVK.framework" ]; then
    echo "MoltenVK.framework already at sibling iOS/lib — nothing to link."
  else
    SHIM_ROOT="${HOME}/VulkanSDK/ci-shim"
    mkdir -p "$SHIM_ROOT/iOS/lib" "$SHIM_ROOT/macOS"
    ln -sfn "$IOS_FW_XC" "$SHIM_ROOT/iOS/lib/MoltenVK.xcframework"
    if [ ! -e "$SHIM_ROOT/macOS/lib" ]; then
      ln -sfn "$RESOLVED/lib" "$SHIM_ROOT/macOS/lib" 2>/dev/null || true
    fi
    echo "Mirrored MoltenVK.framework xcframework -> $SHIM_ROOT/iOS/lib/MoltenVK.xcframework"
  fi
elif [ -n "$STATIC_XC" ]; then
  echo "WARNING: MoltenVK.framework (iOS/lib) not found; CMake will link static libMoltenVK.a from $STATIC_XC"
else
  echo "WARNING: MoltenVK.xcframework not found; iOS configure may fail."
  find "$SDK_ROOT" -maxdepth 5 -name 'MoltenVK.xcframework' -type d 2>/dev/null | head -20 || true
fi

if [ -f "$RESOLVED/setup-env.sh" ]; then
  # shellcheck disable=SC1090
  source "$RESOLVED/setup-env.sh"
elif [ -f "$RESOLVED/../setup-env.sh" ]; then
  # shellcheck disable=SC1090
  source "$RESOLVED/../setup-env.sh"
fi

if [ -n "${GITHUB_ENV:-}" ]; then
  [ -n "${VK_ICD_FILENAMES:-}" ] && echo "VK_ICD_FILENAMES=$VK_ICD_FILENAMES" >> "$GITHUB_ENV"
  echo "PATH=$PATH" >> "$GITHUB_ENV"
fi
