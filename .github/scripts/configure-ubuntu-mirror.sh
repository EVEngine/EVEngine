#!/usr/bin/env bash
# Ubuntu's registered archive replicas retain the Ubuntu signing key.
# https://launchpad.net/ubuntu/+archivemirrors (Kernel.org)
# https://mirrors.kernel.org/ (Ubuntu archive, HTTPS service)
# https://launchpad.net/ubuntu/+mirror/mirrors.ocf.berkeley.edu-archive2 (ARM ports)
set -euo pipefail

apt_root="${1:-/etc/apt}"
test -d "$apt_root"
shopt -s nullglob
sources=("$apt_root/apt-mirrors.txt" "$apt_root/sources.list" "$apt_root"/sources.list.d/*.sources)
pattern='https?://([^/]*archive\.ubuntu\.com|security\.ubuntu\.com)/ubuntu/?'
ports_pattern='https?://ports\.ubuntu\.com/ubuntu-ports/?'
for source in "${sources[@]}"; do
  if [ -f "$source" ]; then
    # Keep suites, components, signing keys and non-Ubuntu repositories intact.
    if grep -qE "$pattern" "$source"; then
      sed -i -E "s#$pattern#https://mirrors.edge.kernel.org/ubuntu/#g" "$source"
    fi
    # ARM ports have a distinct signed archive; never map them to the x64 root.
    if grep -qE "$ports_pattern" "$source"; then
      sed -i -E "s#$ports_pattern#https://mirrors.ocf.berkeley.edu/ubuntu-ports/#g" "$source"
    fi
  fi
done
