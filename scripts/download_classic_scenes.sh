#!/usr/bin/env bash
# Download large classic graphics test scenes into test/assets/classic/.
# Small scenes (e.g. Cornell Box) are committed under that tree; this script
# fetches Khronos glTF Sample Assets used by ClassicScenes.* tests.
#
# Usage:
#   scripts/download_classic_scenes.sh
#   EVENGINE_CLASSIC_SCENES_DIR=/path scripts/download_classic_scenes.sh
#
# Idempotent: skips files that already exist with the expected size.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT_ROOT="${EVENGINE_CLASSIC_SCENES_DIR:-$ROOT/test/assets/classic}"
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
  curl_args=(-fsSL -H "User-Agent: evengine-classic-scenes" -H "Accept: application/vnd.github+json")
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
    req = urllib.request.Request(url, headers={"User-Agent": "evengine-classic-scenes"})
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

# Large atrium — lighting / shadow / GI torture test.
download_model_gltf "Sponza" "sponza"

# Compact PBR / reflection showcases.
download_model_gltf "DamagedHelmet" "damaged_helmet"
download_model_gltf "SciFiHelmet" "scifi_helmet"
download_model_gltf "FlightHelmet" "flight_helmet"
download_model_gltf "BoomBox" "boom_box"

# Material / orientation / silhouette fixtures.
download_model_gltf "MetalRoughSpheres" "metal_rough_spheres"
download_model_gltf "Suzanne" "suzanne"
download_model_gltf "Duck" "duck"
download_model_gltf "Avocado" "avocado"
download_model_gltf "WaterBottle" "water_bottle"
download_model_gltf "Lantern" "lantern"
download_model_gltf "AntiqueCamera" "antique_camera"
download_model_gltf "CesiumMilkTruck" "cesium_milk_truck"
download_model_gltf "BarramundiFish" "barramundi_fish"
download_model_gltf "Corset" "corset"

cat >"$OUT_ROOT/.downloaded" <<EOF
sponza
damaged_helmet
scifi_helmet
flight_helmet
boom_box
metal_rough_spheres
suzanne
duck
avocado
water_bottle
lantern
antique_camera
cesium_milk_truck
barramundi_fish
corset
EOF

echo "Classic scenes installed under $OUT_ROOT"
du -sh "$OUT_ROOT"/* 2>/dev/null || true
