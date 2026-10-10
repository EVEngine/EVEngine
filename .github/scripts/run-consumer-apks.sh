#!/usr/bin/env bash
set -euo pipefail

apk_dir="${1:-apk}"
mapfile -t apk_files < <(find "$apk_dir" -maxdepth 1 -type f -name '*.apk' -print | sort)

if [[ "${#apk_files[@]}" -ne 3 ]]; then
  echo "ERROR: expected 3 consumer APKs, found ${#apk_files[@]}" >&2
  exit 1
fi

echo "Emulator ABI list: $(adb shell getprop ro.product.cpu.abilist)"

for apk_file in "${apk_files[@]}"; do
  apk_name="$(basename "$apk_file")"
  echo "Testing $apk_name"

  if ! unzip -Z1 "$apk_file" | grep -Eq '^lib/arm64-v8a/.+\.so$'; then
    echo "ERROR: $apk_name does not contain ARM64 native libraries" >&2
    exit 1
  fi
  if unzip -Z1 "$apk_file" | grep -Eq '^lib/(x86|x86_64)/'; then
    echo "ERROR: $apk_name unexpectedly contains x86 native libraries" >&2
    exit 1
  fi

  adb logcat -c
  adb install -r "$apk_file"
  adb shell am force-stop com.evengine.example || true
  adb shell am start -W -n com.evengine.example/.EVEngineActivity

  ok=0
  for _ in $(seq 1 90); do
    if adb logcat -d | grep -q EVE_CI_GAME_OK; then
      ok=1
      break
    fi
    sleep 1
  done
  if [[ "$ok" -ne 1 ]]; then
    echo "ERROR: $apk_name did not print EVE_CI_GAME_OK (logcat tail):" >&2
    adb logcat -d | tail -n 60 || true
    exit 1
  fi
  echo "OK: $apk_name printed EVE_CI_GAME_OK on the emulator"
done
