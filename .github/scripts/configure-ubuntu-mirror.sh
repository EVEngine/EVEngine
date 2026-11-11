#!/usr/bin/env bash
# Ubuntu's registered Kernel.org archive replica retains the Ubuntu signing key.
# https://launchpad.net/ubuntu/+archivemirrors (Kernel.org)
# https://mirrors.kernel.org/ (Ubuntu archive, HTTPS service)
set -euo pipefail

apt_root="${1:-/etc/apt}"
test -d "$apt_root"
shopt -s nullglob
sources=("$apt_root/apt-mirrors.txt" "$apt_root/sources.list" "$apt_root"/sources.list.d/*.sources)
pattern='https?://([^/]*archive\.ubuntu\.com|security\.ubuntu\.com)/ubuntu/?'
for source in "${sources[@]}"; do
  if [ -f "$source" ] && grep -qE "$pattern" "$source"; then
    # Keep suites, components, signing keys and non-Ubuntu repositories intact.
    # Ubuntu ports use a separate archive and are intentionally untouched.
    sed -i -E \
      "s#$pattern#https://mirrors.edge.kernel.org/ubuntu/#g" \
      "$source"
  fi
done
