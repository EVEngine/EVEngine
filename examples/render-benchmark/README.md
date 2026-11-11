# Render benchmark

Fixed 6400-cube scene at 1280x720. The camera and objects remain static; all cubes
share one mesh. VSync is disabled. The first five seconds are warmup, followed by
30 seconds of frame intervals from `timer.step()` (SDL monotonic performance counter).
The EVEngine scene disables G-buffer, AO, GI, shadows, AA, MSAA and atmosphere
to focus this run on opaque instance submission.
The program buffers samples until the measurement ends, then prints one
`BENCH_SUMMARY` line and average `BENCH_RENDER_CPU_MS`. Close the window
after `BENCH_DONE`.

Build with `make build/win32` and run `build/win32/src/engine/eve.exe run`
from this example directory on Windows. Capture
stdout and pass it to `scripts/summarize_render_benchmark.py`. This measures the
engine loop interval, including update, rendering, present, and system stalls. It
is not an independently measured scanout or input-to-photon latency. For an
ARM64 custom CMake build, check that `CMAKE_BUILD_TYPE` is `Release` and
`CMAKE_CXX_FLAGS_RELEASE` in its `CMakeCache.txt` contains `/O2`; an incomplete
first configure can leave the Release flags empty. For an
engine-to-engine comparison, reproduce this scene and camera in an exported Godot
player and use the same external presentation timing tool for every player.
`godot/` contains the matching scene generator.
The engines use their specified feature sets; the results measure the full
frame loop rather than equal image quality.
In Godot, import `godot/project.godot`, then run the project with
`godot --path examples/render-benchmark/godot` or export a release player. Godot
emits `BENCH_FRAME_MS` chunks; EVEngine emits `BENCH_SUMMARY`. Both
end with `BENCH_DONE`, and the summarizer accepts either format. The result is an engine loop
interval comparison. Use an external present timing trace for both players
when reporting maximum displayed-frame intervals.
