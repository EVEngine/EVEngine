# AGENTS.md

EVEngine is a C++20, Vulkan-based game engine (SDL2 + Squirrel scripting). Standard
build/test/run commands live in [`Readme.en.md`](Readme.en.md) and the root
[`Makefile`](Makefile) — use those; the notes below cover the non-obvious platform
caveats. The Cursor Cloud VM is Linux; on a Windows host use the Windows section.

## Cursor Cloud specific instructions

On this Linux VM the host target is `build/linux-debug`.

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

## Windows (native) instructions

On a Windows host the same root `Makefile` drives MSVC builds. There is no startup
update script here — configure the environment once, then build with the Makefile.

### One-time environment setup
Required:
- **Visual Studio 2026** with the "Desktop development with C++" workload
  (or 2022 — set `VS_GENERATOR="Visual Studio 17 2022"` in the Makefile). The
  debug build locates `vcvars64.bat` via vswhere (`cmake\with-msvc.cmd`), so the
  workload must include the MSVC x64/x86 tools component.
- **CMake ≥ 3.21** and **Ninja** on `PATH` (the debug build uses the Ninja generator;
  release uses the Visual Studio generator).
- **Git for Windows** on `PATH`. The first configure downloads the `third-party/`
  sources from GitHub and the `deps` target compiles them (SDL2, Poco, OpenAL,
  Box2D/box3d, FreeType, …) — network access is required the first time.
- **Vulkan SDK** (LunarG): install, then set the user environment variable
  `VULKAN_SDK` (e.g. `C:\VulkanSDK\1.4.357.0`) and add `%VULKAN_SDK%\Bin` to `PATH`
  (provides `glslc`). **Open a new terminal / restart the IDE afterwards.**
- Optional but recommended: `choco install doxygen` for `make docs`.

Initialize the git submodules (the Linux VM's update script does this
automatically; on Windows it is a manual step):

```powershell
git submodule update --init --recursive
```

### Building
- Debug (Ninja + MSVC `cl`, fastest local iteration):
  `make build/win32-debug`
- Release (Visual Studio generator):
  `make build/win32`
- Do not invoke `cl.exe` manually outside a Developer prompt; the `cmake\with-msvc.cmd`
  wrapper calls vcvars64 before CMake so the MSVC compiler and STL are found.
- First build compiles third-party through the `deps` target — slow once, cached
  afterward. Keep parallelism modest on small machines:
  `make build/win32-debug JOBS=8`.
- The root `CMakeLists.txt` adds MSVC-specific flags automatically: `/utf-8`
  (Chinese Windows code page 936 avoids C4819 on UTF-8 sources), `/EHsc`
  (exceptions, required by zeroerr ASSERT), `/FS` and `/bigobj` (large TUs such as
  `Graphics.cpp` exceed the default COFF section limit under `/GL`).

### Running and testing
Windows has a real GPU and audio device, so the Linux software-Vulkan/Xvfb/null-audio
setup is **not** needed:
- Run an example: `make run/win32-debug GAME=examples/basic`
- Unit tests (zeroerr suite via CTest): `make test/win32-debug`
  (Release: `make test/win32`)
- Filter by test-name prefix: `make test/win32-debug FILTER=math.*`

### Docs and assertions
- API docs: `make docs` → `docs/api/html/` (requires doxygen on `PATH`).
- zeroerr ASSERT checks (`EV_PARAM_CHECK` / `EV_ASSERT` in
  `src/engine/common/Assert.h`): enabled by default in Debug, compiled out in
  Release unless explicitly enabled:
  `make build/win32 CMAKE_EXTRA_ARGS=-DEVENGINE_ENABLE_ASSERTS=ON`

### WSL2 alternative
To use the Linux toolchain against the same tree (e.g. for headless CI parity or a
Linux-only dependency), install WSL2 + the Linux packages from
[`Readme.en.md`](Readme.en.md), then:

```sh
make wsl/linux-debug
```

## Code conventions (API documentation)
- Public APIs in headers must use Doxygen comments: Javadoc style
  `/** @brief ... @param ... @return ... */` (or `/**\n * @brief ...\n */` blocks).
  Plain `//` or `/* */` comments are for implementation notes only — Doxygen's
  `make docs` output is built from the `@brief`/`@param`/`@return`/`@throws`
  commands, so undocumented public functions show up as empty entries.
- Parameter validation and internal invariants use `EV_PARAM_CHECK` / `EV_ASSERT`
  from `src/engine/common/Assert.h` (zeroerr-backed): enabled in Debug, compiled
  out in Release unless the build is configured with `-DEVENGINE_ENABLE_ASSERTS=ON`.
