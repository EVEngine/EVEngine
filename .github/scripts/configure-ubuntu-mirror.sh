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
      if [ "$source" = "$apt_root/apt-mirrors.txt" ]; then
        # Preserve the canonical HTTPS alternative on subsequent invocations.
        sed -i -E "/^https:\/\/archive\.ubuntu\.com\/ubuntu\/?([[:space:]]|$)/!s#$pattern#https://mirrors.edge.kernel.org/ubuntu/#g" "$source"
      else
        sed -i -E "s#$pattern#https://mirrors.edge.kernel.org/ubuntu/#g" "$source"
      fi
    fi
    # ARM ports have a distinct signed archive; never map them to the x64 root.
    if grep -qE "$ports_pattern" "$source"; then
      sed -i -E "s#$ports_pattern#https://mirrors.ocf.berkeley.edu/ubuntu-ports/#g" "$source"
    fi
  fi
done

# The hosted runner uses mirror+file for indexes AND package archives. Keep a
# second Ubuntu archive available to APT's native per-file failover when the
# registered replica is unreachable. APT still verifies HTTPS, signed metadata
# and each package's hash; no retry wrapper can turn an index failure into success.
# https://manpages.ubuntu.com/manpages/noble/en/man1/apt-transport-mirror.1.html
mirrorlist="$apt_root/apt-mirrors.txt"
if [ -f "$mirrorlist" ] && grep -qE '^https://(mirrors\.edge\.kernel\.org|archive\.ubuntu\.com)/ubuntu/?([[:space:]]|$)' "$mirrorlist"; then
  if ! grep -qE '^https://mirrors\.edge\.kernel\.org/ubuntu/?([[:space:]]|$)' "$mirrorlist"; then
    printf '\nhttps://mirrors.edge.kernel.org/ubuntu/\tpriority:1\n' >> "$mirrorlist"
  fi
  if ! grep -qE '^https://archive\.ubuntu\.com/ubuntu/?([[:space:]]|$)' "$mirrorlist"; then
    printf '\nhttps://archive.ubuntu.com/ubuntu/\tpriority:0\n' >> "$mirrorlist"
  fi
  # Prefer the canonical service so a known-unreachable replica does not spend
  # a connection timeout before every file. Preserve all other mirror metadata.
  sed -i -E '/^https:\/\/archive\.ubuntu\.com\/ubuntu\/?([[:space:]]|$)/ { s/priority:[0-9]+/priority:0/; t; s/$/\tpriority:0/; }' "$mirrorlist"
  sed -i -E '/^https:\/\/mirrors\.edge\.kernel\.org\/ubuntu\/?([[:space:]]|$)/ { s/priority:[0-9]+/priority:1/; t; s/$/\tpriority:1/; }' "$mirrorlist"
fi
