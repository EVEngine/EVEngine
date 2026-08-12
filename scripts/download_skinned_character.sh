#!/usr/bin/env bash
# Download a free skinned character (Khronos CesiumMan) for skeletal animation tests.
# Output: test/assets/skinned/cesium_man/glTF/
#
# Usage:
#   scripts/download_skinned_character.sh
#   EVENGINE_SKINNED_DIR=/path scripts/download_skinned_character.sh
#
# Idempotent: skips files that already exist with the expected size.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT_ROOT="${EVENGINE_SKINNED_DIR:-$ROOT/test/assets/skinned}"
BASE_RAW="https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models"
API_BASE="https://api.github.com/repos/KhronosGroup/glTF-Sample-Assets/contents/Models"

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "error: required command not found: $1" >&2
    exit 1
  }
}

need_cmd curl
need_cmd python3

download_model_gltf() {
  local model="$1"
  local dest="$OUT_ROOT/$2/glTF"
  local api_url="$API_BASE/$model/glTF?ref=main"
  local listing_json
  local listing_file

  mkdir -p "$dest"
  echo "==> Listing $model via GitHub API"
  listing_file="$(mktemp)"
  # GitHub API rejects anonymous requests without User-Agent; CI may 403 without a token.
  curl_args=(-fsSL -H "User-Agent: evengine-skinned-character" -H "Accept: application/vnd.github+json")
  if [ -n "${GITHUB_TOKEN:-}" ]; then
    curl_args+=(-H "Authorization: Bearer ${GITHUB_TOKEN}")
  fi
  curl "${curl_args[@]}" "$api_url" -o "$listing_file"
  listing_json="$listing_file"

  python3 - "$dest" "$BASE_RAW/$model/glTF" "$listing_json" <<'PY'
import json, os, sys, urllib.request

dest, raw_base, listing_path = sys.argv[1], sys.argv[2], sys.argv[3]
with open(listing_path, "r", encoding="utf-8") as f:
    items = json.load(f)

def fetch(url: str, path: str, expected) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if os.path.isfile(path) and expected is not None and os.path.getsize(path) == expected:
        print(f"  skip  {os.path.basename(path)} ({expected} bytes)")
        return
    if os.path.isfile(path) and expected is None and os.path.getsize(path) > 0:
        print(f"  skip  {os.path.basename(path)}")
        return
    print(f"  get   {os.path.basename(path)}")
    req = urllib.request.Request(url, headers={"User-Agent": "evengine-skinned-character"})
    with urllib.request.urlopen(req, timeout=180) as r, open(path, "wb") as out:
        while True:
            chunk = r.read(1024 * 256)
            if not chunk:
                break
            out.write(chunk)
    if expected is not None and os.path.getsize(path) != expected:
        raise SystemExit(
            f"size mismatch for {path}: got {os.path.getsize(path)}, want {expected}"
        )

for it in items:
    if it.get("type") != "file":
        continue
    name = it["name"]
    url = f"{raw_base}/{name}"
    fetch(url, os.path.join(dest, name), it.get("size"))
PY
  rm -f "$listing_json"
  echo "==> $model ready under $dest"
}

mkdir -p "$OUT_ROOT"

# CesiumMan (~0.5 MB) — free skinned walking character (CC-BY 4.0 via Khronos samples).
download_model_gltf "CesiumMan" "cesium_man"

cat >"$OUT_ROOT/.downloaded" <<EOF
cesium_man
EOF

echo "Skinned character installed under $OUT_ROOT"
du -sh "$OUT_ROOT/cesium_man" 2>/dev/null || true
