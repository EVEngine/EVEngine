# AGENTS.md

## Cursor Cloud specific instructions

EVEngine is a C++20, Vulkan-based game engine (SDL2 + Squirrel scripting). On this
Linux VM the host target is `build/linux-debug`. Standard build/test/run commands live
in [`Readme.en.md`](Readme.en.md) and the root [`Makefile`](Makefile) — use those; the
notes below only cover the non-obvious cloud caveats.

### Environment is prepared by the startup update script
The update script installs all system dependencies (build tools, X11/Wayland dev libs,
audio, `libvulkan-dev` + `glslc`/`glslang-tools`, Mesa Lavapipe software Vulkan,
`vulkan-tools`, `xvfb`), initializes the `external/*` git submodules, and pins the
default C/C++ compiler to GCC. You normally do not need to install anything.

### Compiler: use GCC (do not switch to clang)
CI builds Linux with GCC, and the update script points the `cc`/`c++` alternatives at
`gcc`/`g++`. The image's out-of-the-box default `cc`/`c++` is clang-18, which selects the
GCC-14 tree and then fails to link (`cannot find -lstdc++`) because only `libgcc-14-dev`
ships by default. Keep GCC as the default; the `Makefile` invokes plain `cmake` with no
compiler override.

### Building
- `make debug` configures + builds `build/linux-debug` (equivalently: cmake configure,
  then `--target deps`, then the engine).
- The first configure downloads the `third-party/` sources from GitHub and the `deps`
  target compiles them (SDL2, Poco, OpenAL, Box2D/box3d, FreeType, …). This is slow the
  first time and cached afterward. `build/` is git-ignored and tied to its source tree.
- Memory: keep parallelism modest (e.g. `make debug JOBS=4`) — this VM has ~4 cores.

### Running the engine and tests is headless — needs software Vulkan + Xvfb + null audio
`eve` and the graphics unit tests create a real `SDL_WINDOW_VULKAN` surface, so they need
a display and a Vulkan ICD. This VM has no GPU and no audio device. Export these before
running anything that opens a window (mirrors CI):

```sh
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json   # Mesa Lavapipe (llvmpipe) software Vulkan
export XDG_RUNTIME_DIR=/tmp/xdg-runtime && mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"
export ALSOFT_DRIVERS=null                                     # OpenAL Soft null backend (no audio card)
```

- Unit tests (full suite, ~1400 tests, a couple of minutes):
  `VK_ICD_FILENAMES=... ALSOFT_DRIVERS=... xvfb-run -a make test/linux-debug`
- Run an example (long-lived GUI loop — use `timeout` for a smoke run):
  `cd examples/basic && VK_ICD_FILENAMES=... ALSOFT_DRIVERS=... XDG_RUNTIME_DIR=... xvfb-run -a ../../build/linux-debug/src/engine/eve run`
- `scripts/smoke_examples.sh <name>` launches an example headless and greps for error
  markers (PASS/FAIL) — a good quick check under `xvfb-run`.
- The benign `[ALSOFT] Failed to set real-time priority` warning is expected on this VM.

### Capturing a rendered frame (visual artifact)
There is no on-screen display; grab the framebuffer from a manual Xvfb display:
`Xvfb :99 -screen 0 800x600x24 &` then run `eve` with `DISPLAY=:99`, and
`ffmpeg -f x11grab -video_size 800x600 -i :99 -frames:v 1 out.png`.
