#!/bin/sh
# Copy MoltenVK.framework into the app bundle and sign it separately: installd
# rejects nested dynamic frameworks that are not signed in their own right.
#
# Usage: ios_embed_moltenvk.sh <source MoltenVK.framework> <app bundle dir>
# Signing settings come from the Xcode script-phase environment.
set -e

SRC_FW="$1"
APP_DIR="$2"
DEST_FW="$APP_DIR/Frameworks/MoltenVK.framework"

mkdir -p "$APP_DIR/Frameworks"
rm -rf "$DEST_FW"
cp -R "$SRC_FW" "$DEST_FW"

if [ "${CODE_SIGNING_ALLOWED:-NO}" != "YES" ] || [ -z "${EXPANDED_CODE_SIGN_IDENTITY:-}" ]; then
    exit 0
fi

codesign --force --sign "$EXPANDED_CODE_SIGN_IDENTITY" \
    --timestamp=none --generate-entitlement-der "$DEST_FW"

# Re-seal the app: its signature covers the Frameworks directory we just changed.
ENTITLEMENTS="${TARGET_TEMP_DIR}/${FULL_PRODUCT_NAME}.xcent"
if [ -f "$ENTITLEMENTS" ]; then
    codesign --force --sign "$EXPANDED_CODE_SIGN_IDENTITY" \
        --entitlements "$ENTITLEMENTS" \
        --timestamp=none --generate-entitlement-der "$APP_DIR"
else
    codesign --force --sign "$EXPANDED_CODE_SIGN_IDENTITY" \
        --timestamp=none --generate-entitlement-der "$APP_DIR"
fi
