#!/usr/bin/env bash
set -euo pipefail

apk_dir="${1:-apk}"
mapfile -t apk_files < <(find "$apk_dir" -maxdepth 1 -type f -name '*.apk' -print | sort)

if [[ "${#apk_files[@]}" -ne 3 ]]; then
  echo "ERROR: expected 3 consumer APKs, found ${#apk_files[@]}" >&2
  exit 1
fi

echo "Emulator ABI list: $(adb shell getprop ro.product.cpu.abilist)"

build_tools_dir="$(find "${ANDROID_HOME:?ANDROID_HOME is required}/build-tools" -mindepth 1 -maxdepth 1 -type d | sort -V | tail -1)"
zipalign="$build_tools_dir/zipalign"
apksigner="$build_tools_dir/apksigner"
if [[ ! -x "$zipalign" || ! -x "$apksigner" ]]; then
  echo "ERROR: zipalign/apksigner not found under $build_tools_dir" >&2
  exit 1
fi

signing_dir="$(mktemp -d)"
trap 'rm -rf "$signing_dir"' EXIT
keytool -genkeypair \
  -keystore "$signing_dir/ci-test.keystore" \
  -storepass android -keypass android -alias androiddebugkey \
  -dname "CN=EVEngine CI,OU=CI,O=EVEngine,L=CI,ST=CI,C=US" \
  -keyalg RSA -keysize 2048 -validity 1 -noprompt >/dev/null 2>&1

for apk_file in "${apk_files[@]}"; do
  apk_name="$(basename "$apk_file")"
  echo "Testing $apk_name"

  if ! unzip -Z1 "$apk_file" | grep -E '^lib/arm64-v8a/.+\.so$' >/dev/null; then
    echo "ERROR: $apk_name does not contain ARM64 native libraries" >&2
    exit 1
  fi
  if unzip -Z1 "$apk_file" | grep -E '^lib/(x86|x86_64)/' >/dev/null; then
    echo "ERROR: $apk_name unexpectedly contains x86 native libraries" >&2
    exit 1
  fi

  aligned_apk="$signing_dir/${apk_name%.apk}-aligned.apk"
  signed_apk="$signing_dir/${apk_name%.apk}-signed.apk"
  "$zipalign" -f -p 4 "$apk_file" "$aligned_apk"
  "$apksigner" sign \
    --ks "$signing_dir/ci-test.keystore" \
    --ks-pass pass:android --key-pass pass:android \
    --out "$signed_apk" "$aligned_apk"
  "$apksigner" verify "$signed_apk"

  adb logcat -c
  adb install -r "$signed_apk"
  adb shell am force-stop com.evengine.example || true
  adb shell am start -W -n com.evengine.example/.EVEngineActivity

  ok=0
  for _ in $(seq 1 90); do
    if adb logcat -d | grep -F EVE_CI_GAME_OK >/dev/null; then
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
