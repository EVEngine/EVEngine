#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
game_dir="$repo_dir/examples/hex-terrain"
output=${1:-"$game_dir/hex-terrain-vulkan-latest.png"}

if [ "${HEX_TERRAIN_CAPTURE_INSIDE_XVFB:-0}" != 1 ]; then
    HEX_TERRAIN_CAPTURE_INSIDE_XVFB=1 exec xvfb-run -a \
        --server-args="-screen 0 800x600x24" "$0" "$output"
fi

export ALSOFT_DRIVERS=null
export VK_ICD_FILENAMES=${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.json}

cd "$game_dir"
"$repo_dir/build/linux-hex/src/engine/eve" run > /tmp/hexterrain-capture.log 2>&1 &
engine_pid=$!
cleanup() {
    kill -TERM "$engine_pid" 2>/dev/null || true
    sleep 1
    kill -KILL "$engine_pid" 2>/dev/null || true
    wait "$engine_pid" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

sleep "${HEX_TERRAIN_CAPTURE_DELAY:-4}"
ffmpeg -hide_banner -loglevel error -y -f x11grab -draw_mouse 0 \
    -video_size 800x600 -i "$DISPLAY" -frames:v 1 "$output"
