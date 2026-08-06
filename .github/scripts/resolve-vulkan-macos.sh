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

XC=$(find "$SDK_ROOT" "$RESOLVED" "$RESOLVED/.." -path '*/MoltenVK.xcframework' -type d 2>/dev/null | head -1 || true)
if [ -z "$XC" ]; then
  echo "WARNING: MoltenVK.xcframework not found; iOS configure may fail."
else
  echo "Found MoltenVK.xcframework at $XC"
  # CMake looks for $VULKAN_SDK/lib/MoltenVK.xcframework (CI layout) and
  # $VULKAN_SDK/../iOS/lib/... (LunarG desktop layout). Prefer not writing into
  # the installed SDK (often not writable on GHA). If the xcframework is not
  # already under $VULKAN_SDK/lib, mirror it into a writable HOME shim that the
  # CMake HOME/VulkanSDK/*/iOS glob can also see.
  if [ -d "$RESOLVED/lib/MoltenVK.xcframework" ]; then
    echo "MoltenVK.xcframework already at \$VULKAN_SDK/lib — nothing to link."
  elif [ -d "$RESOLVED/../iOS/lib/MoltenVK.xcframework" ]; then
    echo "MoltenVK.xcframework already at sibling iOS/lib — nothing to link."
  else
    SHIM_ROOT="${HOME}/VulkanSDK/ci-shim"
    mkdir -p "$SHIM_ROOT/iOS/lib" "$SHIM_ROOT/macOS"
    ln -sfn "$XC" "$SHIM_ROOT/iOS/lib/MoltenVK.xcframework"
    # Keep a macOS pointer so desktop find_package still works if needed.
    if [ ! -e "$SHIM_ROOT/macOS/lib" ]; then
      ln -sfn "$RESOLVED/lib" "$SHIM_ROOT/macOS/lib" 2>/dev/null || true
    fi
    echo "Mirrored MoltenVK.xcframework -> $SHIM_ROOT/iOS/lib/MoltenVK.xcframework"
  fi
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
