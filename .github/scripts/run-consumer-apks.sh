#!/usr/bin/env bash
set -euo pipefail

apk_dir="${1:-apk}"
expected_count="${2:-3}"
success_marker="${3:-EVE_CI_GAME_OK}"
expected_abi="${4:-arm64-v8a}"
mapfile -t apk_files < <(find "$apk_dir" -maxdepth 1 -type f -name '*.apk' -print | sort)

if [[ "${#apk_files[@]}" -ne "$expected_count" ]]; then
  echo "ERROR: expected $expected_count APKs, found ${#apk_files[@]}" >&2
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

  if ! unzip -Z1 "$apk_file" | grep -E "^lib/${expected_abi}/.+\\.so$" >/dev/null; then
    echo "ERROR: $apk_name does not contain $expected_abi native libraries" >&2
    exit 1
  fi
  if [[ "$expected_abi" == "arm64-v8a" ]] && unzip -Z1 "$apk_file" | grep -E '^lib/(x86|x86_64)/' >/dev/null; then
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
    if adb logcat -d | grep -F "$success_marker" >/dev/null; then
      ok=1
      break
    fi
    sleep 1
  done
  if [[ "$ok" -ne 1 ]]; then
    echo "ERROR: $apk_name did not print $success_marker" >&2
    echo "Application process state:" >&2
    adb shell ps -A | grep -E 'com\.evengine\.example|PID' >&2 || true
    echo "Relevant logcat entries:" >&2
    adb logcat -d -v threadtime \
      | grep -Ei 'com\.evengine\.example|EVEngine|SDL|AndroidRuntime|DEBUG|libc|linker|native.?bridge|EVE_CI' \
      | tail -n 300 >&2 || true
    exit 1
  fi
  echo "OK: $apk_name printed $success_marker on the emulator"
done
