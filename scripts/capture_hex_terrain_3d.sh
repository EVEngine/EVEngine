#!/bin/sh
# Capture a reproducible frame of the interactive hex map example.
#
# The example captures its own frame with the engine-owned `gfx.saveFramePng`
# path (readback inside the renderer) instead of grabbing the X server, so this
# wrapper only needs to start it headlessly and collect the result.
#
# This is the capture helper for `examples/hex-terrain-3d` (the `hexmap` module's
# interactive example). `scripts/capture_hex_terrain.sh` is the unrelated one for
# the `mesh.hexterrain` procgen example in `examples/hex-terrain`.
#
# Usage:
#   scripts/capture_hex_terrain_3d.sh [output.png]
#
# Environment:
#   HEX_TERRAIN_3D_CAPTURE_DELAY  seconds to let the example run before collecting
#                                 the captured frame (default 8)
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
game_dir="$repo_dir/examples/hex-terrain-3d"
default_output="$game_dir/hex-map.png"
output=${1:-"$default_output"}

if [ "${HEX_TERRAIN_3D_CAPTURE_INSIDE_XVFB:-0}" != 1 ]; then
    HEX_TERRAIN_3D_CAPTURE_INSIDE_XVFB=1 exec xvfb-run -a \
        --server-args="-screen 0 800x600x24" "$0" "$output"
fi

export ALSOFT_DRIVERS=null
export VK_ICD_FILENAMES=${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.json}

if [ -x "$repo_dir/build/linux-debug/src/engine/eve" ]; then
    eve_bin="$repo_dir/build/linux-debug/src/engine/eve"
elif [ -x "$repo_dir/build/win32-debug/src/engine/eve.exe" ]; then
    eve_bin="$repo_dir/build/win32-debug/src/engine/eve.exe"
else
    echo "capture_hex_terrain_3d: no engine binary found (build linux-debug or win32-debug)" >&2
    exit 1
fi

rm -f "$default_output"
cd "$game_dir"
"$eve_bin" run > /tmp/hexmap-3d-capture.log 2>&1 &
engine_pid=$!
cleanup() {
    kill -TERM "$engine_pid" 2>/dev/null || true
    sleep 1
    kill -KILL "$engine_pid" 2>/dev/null || true
    wait "$engine_pid" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

sleep "${HEX_TERRAIN_3D_CAPTURE_DELAY:-8}"

if [ ! -f "$default_output" ]; then
    echo "capture_hex_terrain_3d: the example did not capture a frame; see /tmp/hexmap-3d-capture.log" >&2
    exit 1
fi

if [ "$output" != "$default_output" ]; then
    cp "$default_output" "$output"
fi
echo "capture_hex_terrain_3d: wrote $output"
