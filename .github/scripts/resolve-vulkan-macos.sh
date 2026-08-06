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
  if [ ! -d "$RESOLVED/../iOS/lib/MoltenVK.xcframework" ]; then
    mkdir -p "$RESOLVED/../iOS/lib"
    ln -sfn "$XC" "$RESOLVED/../iOS/lib/MoltenVK.xcframework"
    echo "Linked iOS MoltenVK -> $RESOLVED/../iOS/lib/MoltenVK.xcframework"
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
