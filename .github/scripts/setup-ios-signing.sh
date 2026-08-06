#!/usr/bin/env bash
# Optional iOS code-signing setup for GitHub Actions.
#
# If IOS_CERTIFICATE_P12_BASE64 is unset/empty → print "unsigned" and exit 0.
# Otherwise import the .p12 into a temporary keychain and install an optional
# provisioning profile.
#
# Required secrets (when signing):
#   IOS_CERTIFICATE_P12_BASE64   base64-encoded .p12 (Apple Development or Distribution)
#   IOS_CERTIFICATE_PASSWORD     password for the .p12
#   IOS_DEVELOPMENT_TEAM         10-char Team ID (OU in the cert subject)
#
# Optional:
#   IOS_PROVISIONING_PROFILE_BASE64  base64-encoded .mobileprovision
#   IOS_KEYCHAIN_PASSWORD            keychain password (default: random)
#
# On success exports to GITHUB_ENV (when present):
#   IOS_DEVELOPMENT_TEAM
#   IOS_SIGNING=1
set -euo pipefail

if [ -z "${IOS_CERTIFICATE_P12_BASE64:-}" ]; then
  echo "iOS signing secrets not configured — building unsigned."
  echo "IOS_SIGNING=0" >> "${GITHUB_ENV:-/dev/null}"
  echo "IOS_DEVELOPMENT_TEAM=" >> "${GITHUB_ENV:-/dev/null}"
  exit 0
fi

: "${IOS_CERTIFICATE_PASSWORD:?IOS_CERTIFICATE_PASSWORD is required when a certificate is provided}"
: "${IOS_DEVELOPMENT_TEAM:?IOS_DEVELOPMENT_TEAM is required when a certificate is provided}"

KEYCHAIN_PASSWORD="${IOS_KEYCHAIN_PASSWORD:-$(openssl rand -base64 24)}"
KEYCHAIN_PATH="${RUNNER_TEMP:-/tmp}/evengine-ios-signing.keychain-db"
CERT_PATH="${RUNNER_TEMP:-/tmp}/evengine-ios-cert.p12"

echo "Importing iOS signing certificate into temporary keychain..."
echo "$IOS_CERTIFICATE_P12_BASE64" | base64 --decode > "$CERT_PATH"

security create-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN_PATH"
security set-keychain-settings -lut 21600 "$KEYCHAIN_PATH"
security unlock-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN_PATH"
security import "$CERT_PATH" -P "$IOS_CERTIFICATE_PASSWORD" -A -t cert -f pkcs12 \
  -k "$KEYCHAIN_PATH"
security list-keychain -d user -s "$KEYCHAIN_PATH" $(security list-keychain -d user | sed 's/"//g')
security set-key-partition-list -S apple-tool:,apple:,codesign: -s \
  -k "$KEYCHAIN_PASSWORD" "$KEYCHAIN_PATH"

rm -f "$CERT_PATH"

if [ -n "${IOS_PROVISIONING_PROFILE_BASE64:-}" ]; then
  PROFILE_DIR="$HOME/Library/MobileDevice/Provisioning Profiles"
  mkdir -p "$PROFILE_DIR"
  PROFILE_PATH="${RUNNER_TEMP:-/tmp}/evengine-ios.mobileprovision"
  echo "$IOS_PROVISIONING_PROFILE_BASE64" | base64 --decode > "$PROFILE_PATH"
  # Profile UUID is the correct install name.
  UUID=$(/usr/libexec/PlistBuddy -c 'Print UUID' /dev/stdin \
    <<< "$(security cms -D -i "$PROFILE_PATH")" 2>/dev/null \
    || python3 -c '
import plistlib, subprocess, sys
data = subprocess.check_output(["security", "cms", "-D", "-i", sys.argv[1]])
print(plistlib.loads(data)["UUID"])
' "$PROFILE_PATH")
  cp "$PROFILE_PATH" "$PROFILE_DIR/$UUID.mobileprovision"
  echo "Installed provisioning profile $UUID"
  rm -f "$PROFILE_PATH"
fi

echo "IOS_DEVELOPMENT_TEAM=$IOS_DEVELOPMENT_TEAM" >> "${GITHUB_ENV:-/dev/null}"
echo "IOS_SIGNING=1" >> "${GITHUB_ENV:-/dev/null}"
echo "iOS signing ready (team=$IOS_DEVELOPMENT_TEAM)"
security find-identity -v -p codesigning "$KEYCHAIN_PATH" || true
