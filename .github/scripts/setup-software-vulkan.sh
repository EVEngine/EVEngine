#!/usr/bin/env bash
# Point the Vulkan loader at a CPU ICD so headless CI can create
# SDL_WINDOW_VULKAN (needs VK_KHR_surface).
#
# Windows: SwiftShader from jakoch/install-vulkan-sdk-action
# Linux:   Mesa Lavapipe from mesa-vulkan-drivers
set -euo pipefail

os="$(uname -s | tr '[:upper:]' '[:lower:]')"
icd=""

case "$os" in
  mingw*|msys*|cygwin*|windows*)
    for c in \
      "/c/SwiftShader/vk_swiftshader_icd.json" \
      "/c/Swiftshader/vk_swiftshader_icd.json" \
      "/c/swiftshader/vk_swiftshader_icd.json" \
      "C:/SwiftShader/vk_swiftshader_icd.json" \
      "C:/Swiftshader/vk_swiftshader_icd.json" \
      "C:/swiftshader/vk_swiftshader_icd.json"
    do
      if [ -f "$c" ]; then
        # Vulkan loader on Windows wants a native path.
        icd="$(cygpath -w "$c" 2>/dev/null || echo "$c")"
        break
      fi
    done
    if [ -z "$icd" ]; then
      echo "SwiftShader ICD not found under C:\\SwiftShader"
      ls -la /c/SwiftShader /c/Swiftshader /c/swiftshader 2>/dev/null || true
      exit 1
    fi
    ;;
  linux*)
    for c in \
      /usr/share/vulkan/icd.d/lvp_icd.x86_64.json \
      /usr/share/vulkan/icd.d/lvp_icd.json \
      /usr/share/vulkan/icd.d/lvp_icd.*.json
    do
      # Expand globs carefully
      for f in $c; do
        if [ -f "$f" ]; then
          icd="$f"
          break 2
        fi
      done
    done
    if [ -z "$icd" ]; then
      echo "Lavapipe ICD not found. Is mesa-vulkan-drivers installed?"
      ls -la /usr/share/vulkan/icd.d/ 2>/dev/null || true
      exit 1
    fi
    ;;
  *)
    echo "setup-software-vulkan.sh: unsupported OS '$os' (macOS uses MoltenVK)"
    exit 0
    ;;
esac

echo "Using software Vulkan ICD: $icd"
if [ -n "${GITHUB_ENV:-}" ]; then
  echo "VK_ICD_FILENAMES=$icd" >> "$GITHUB_ENV"
fi
export VK_ICD_FILENAMES="$icd"

# Best-effort sanity check (do not fail the job if vulkaninfo is missing).
if command -v vulkaninfo >/dev/null 2>&1; then
  vulkaninfo --summary 2>/dev/null | head -n 40 || true
elif command -v vulkaninfoSDK >/dev/null 2>&1; then
  vulkaninfoSDK --summary 2>/dev/null | head -n 40 || true
fi
