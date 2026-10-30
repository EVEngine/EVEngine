#!/usr/bin/env bash
# Prepare foreign-host game inputs with the actual target SDK. Native eve package
# must run acceptance; this never manufactures or bypasses a verified package.
set -euo pipefail
target="${1:?target platform required}"
work="${2:?extracted transfer directory required}"
case "$target" in
  win32) runtime=eve.exe ;;
  linux) runtime=eve ;;
  *) echo "unsupported target: $target" >&2; exit 1 ;;
esac
work="$(cd "$work" && pwd)"
cd "$work"
shopt -s nullglob
archives=(eve-sdk-"$target"-*.zip)
[ "${#archives[@]}" -eq 1 ] || { echo "expected one target SDK archive" >&2; exit 1; }
archive="${archives[0]}"
python3 - "$archive" <<'CHECKSUM'
import hashlib
from pathlib import Path
import re
import sys

archive = Path(sys.argv[1])
entries = []
for line in Path("SHA256SUMS").read_text(encoding="utf-8").splitlines():
    match = re.fullmatch(r"([0-9a-fA-F]{64}) [ *](.+)", line)
    if match and match[2] == archive.name:
        entries.append(match[1].lower())
if len(entries) != 1:
    raise SystemExit("expected exactly one SDK checksum")
digest = hashlib.sha256()
with archive.open("rb") as source:
    for chunk in iter(lambda: source.read(1024 * 1024), b""):
        digest.update(chunk)
if digest.hexdigest() != entries[0]:
    raise SystemExit("SDK checksum mismatch")
CHECKSUM
python3 -m zipfile -e "$archive" .
python3 -m zipfile -e game.eve source
sdk="$work/eve-sdk/$target"
[ "$(cat "$sdk/share/eve/TARGET_PLATFORM")" = "$target" ]
chmod +x "$sdk/bin/$runtime"
"$sdk/bin/$runtime" package "$work/source" --sdk "$sdk" -o "$work/verified"
grep -qx 'acceptance=passed' verified/build-identity.manifest
grep -qx 'frames=30' verified/build-identity.manifest
grep -q '\[package-check\] passed: 30 frames' verified/acceptance.log
