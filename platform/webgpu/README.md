# WebGPU target (browser WASM + native Dawn)

The `webgpu` platform runs the engine on WebGPU instead of Vulkan. It shares a
single backend under `src/modules/graphics/webgpu/` (and `src/modules/gpgpu/webgpu/`)
that targets the standard `webgpu.h` API, and can be produced two ways:

- **Browser**: compiled with Emscripten to WASM, rendering with the browser's
  native WebGPU (Chromium/Edge/Firefox with `--enable-unsafe-webgpu`). Shaders
  must be WGSL.
- **Native desktop**: links Google Dawn (`dawn::webgpu_dawn`), which accepts WGSL (and
  SPIR-V) and runs on top of Vulkan/Metal/D3D12.

## Prerequisites

- **Emscripten** (`emsdk`, `emcmake`) with WebGPU support. Emscripten ships the
  `webgpu.h`/`webgpu_cpp.h` headers.
- **CMake 3.29.x** (pip-installed). CMake 4.x is **incompatible** with the
  Emscripten SDK fork of `CheckTypeSize.cmake` (`/CheckTypeSize.c.in does not
  exist`), which breaks third-party configure (e.g. mpg123). Install and force
  CMake 3.29:
  ```sh
  python -m pip install cmake==3.29.6
  # EMSCRIPTEN builds must use this cmake for BOTH the main and the
  # third-party aggregate — the root CMakeLists detects and forces it.
  ```
- **Native Dawn**: fetched automatically via CMake `FetchContent` (fixed commit).
- A WebGPU-capable browser (Chrome 113+ / Edge 113+ / Firefox 118+).

## Browser build (Emscripten / WASM)

```sh
# activate emsdk once
emsdk activate latest && emsdk_env.bat   # Windows
emcmake cmake -B build/webgpu-web -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/webgpu-web           # produces eve.js / eve.wasm / eve.data
```

The Emscripten build **trims** third-party dependencies and engine modules:

- **Skipped deps**: Poco, OpenAL-soft, medialoader (assimp/mpg123/vorbis), FreeType,
  googletest. SDL2 + zlib come from Emscripten ports (`-sUSE_SDL=2`, `-sUSE_ZLIB`).
  Kept: squirrel/simplesquirrel, physfs, lz4, Box2D, xxHash, imgui, and a minimal
  `medialoader_image` (stb/lodepng/dds/tinyexr image decoders, needed by EVImage).
- **Compiled-out modules** (their deps were dropped):
  `EVAnimation EVAudio EVAvatar EVBuilding EVDatabase EVDialogue EVFont EVInventory
  EVMap EVModel3D EVNetwork EVParticles EVPlugins EVProcgen EVRPG EVSound EVStylize
  EVVoxel`, plus `graphics/Font.cpp` and the Poco-heavy `DataModule/JsonDocument/
  XmlDocument`.
- **Threads**: `main()` runs on the browser main thread so the WebGPU surface
  can reach the DOM canvas directly (no `PROXY_TO_PTHREAD`); pthreads stay
  available for the Thread module, and **all TUs are compiled with `-matomics
  -mbulk-memory`** (engine and third-party). The page must still be served
  with COOP/COEP headers so `SharedArrayBuffer` is available.

### Serving + browser verification

The output directory is `build/webgpu-web/src/engine/`. Serve it with a real
HTTP server that sends the cross-origin-isolation headers:

```sh
python scripts/serve_webgpu.py   # serves http://127.0.0.1:8090/eve.html
                                 # with COOP/COEP + no-store headers
```

Open `http://127.0.0.1:8090/eve.html` in Chrome/Edge (WebGPU enabled). The page
is the **online playground** — see the next section. Its status bar prints
`crossOriginIsolated` and the resolved WebGPU adapter, so init failures are
visible without DevTools.

## Playground (online script editor)

`shell.html` is a self-contained playground UI around the running engine:

- **Editor** (left): write `main.nut` in Squirrel with syntax highlighting.
  `Ctrl+Enter` (or **应用/Apply**) writes the script to the engine VFS and
  soft-hot-reloads it — the next frame runs the new code.
- **State preservation**: the engine's soft reload re-runs `main.nut` and then
  calls the optional `eve_reload()` hook; root-table state survives. Playground
  demos keep their state in the `__pg` table (`if (!("__pg" in getroottable()))
  __pg <- {};`), so editing `eve_update`/`eve_render` never resets the game.
- **重置状态/Restart**: clears `__pg`, re-applies the script and re-runs
  `eve_init` to rebuild the scene from scratch. A full browser refresh always
  gives a completely fresh engine (and re-applies your last session from
  localStorage).
- **暂停/Pause**: freezes `eve_update` (game logic) while rendering continues.
- **REPL** (console bottom): evaluates one Squirrel expression in the running
  VM, e.g. `__pg.frames`.
- **Examples dropdown**: loads `main.nut` or `playground/demos/*.nut` from the
  preloaded VFS into the editor.
- **Console**: captures engine `print`/`printErr` and script errors.

### JS ↔ engine bridge

`src/engine/cmdline/Run.cpp` exports four functions to the page
(`EMSCRIPTEN_KEEPALIVE`, browser build only):

| Function | Purpose |
|---|---|
| `eve_playground_apply(source)` | write `/game/main.nut` + `handle_change("main.nut")` (same path as the desktop watcher: `dofile` → `eve_reload()`) |
| `eve_playground_eval(source)` | compile + run one Squirrel snippet, return stringified result/error |
| `eve_playground_read(path)` | read a file from the VFS (relative to `/game`) |
| `eve_playground_set_paused(bool)` | set the `eve_playground_paused` root flag checked by `load.nut` |

The engine signals readiness by calling `window.eveEngineReady()` once
`load.nut` has finished (this is later than `onRuntimeInitialized`).

### Bundled demos and assets

`platform/webgpu/game-shell/` is preloaded at `/game`:

- `main.nut` — default 3D rotating sphere (hot-state pattern).
- `playground/demos/2d_sprites.nut` — 2D sprite + keyboard.
- `playground/demos/3d_textured.nut` — textured cube + orbiting sphere.
- `playground/demos/hot_state.nut` — explicit hot-update state demo.
- `playground/assets/` — CC0 textures (wood.jpg, background_forest.png,
  crate.png, tiles.png; see `ATTRIBUTION.txt`).

To add a demo, drop a `.nut` file under `platform/webgpu/game-shell/playground/
demos/` and add it to the `EXAMPLES` array in `shell.html`, then rebuild (the
game-shell is embedded into `eve.data`).

**Headless verification** (optional): headless Chromium needs explicit GPU flags
for WebGPU to be exposed — plain `--headless` reports "No available adapters":

```sh
msedge --headless=new --no-sandbox --disable-gpu-sandbox \
  --enable-unsafe-webgpu --enable-features=Vulkan --use-vulkan=swiftshader \
  --timeout=15000 --screenshot=frame.png http://127.0.0.1:8090/eve.html
```

Note: with `PROXY_TO_PTHREAD`, the main loop runs on a worker; use the real-time
`--timeout` wait (not `--virtual-time-budget`, which never advances the worker
clock) so the first frames are actually presented before the screenshot.

The shell (`platform/webgpu/shell.html`) provides the `<canvas id="canvas">`
that the WebGPU surface attaches to. Game assets under
`platform/webgpu/game-shell/` are preloaded at `/game`.

## Native Dawn build

```sh
cmake -B build/webgpu-native -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_PLATFORM=webgpu
cmake --build build/webgpu-native
./build/webgpu-native/src/engine/eve run <game-dir>
```

For backend-neutral render validation, configure with `-DBUILD_TESTING=ON` and
run `ctest --test-dir build/webgpu-native -R graphics.backendParity`. Set
`EVENGINE_RENDER_PARITY_DIR` to an absolute directory to emit PNG/JSON pairs;
compare them with Vulkan output using `scripts/compare_render_backends.py`.
The Windows CI lane performs this comparison with software Vulkan.

The native Dawn backend includes alpha-cutout shadow/GBuffer rendering and
box-projected decals (albedo, normal and material layers plus forward
composition). Their deterministic parity scenes run through the same public
API as Vulkan and are compared as PNG artifacts in CI.

Set `EVENGINE_WEBGPU_GAME_DIR` to point the platform helpers at a game folder
when not running from the game directory itself.

## Notes / limitations

- Runtime GLSL compilation (`gfx.newShader(glsl)`) is **not** available in the
  browser — shaders must ship as pre-compiled WGSL (see the `*_wgsl.inc`
  generation step). Native Dawn builds can compile GLSL via glslang+Tint.
- The Vulkan backend and `ui/imgui` Vulkan renderer are excluded from webgpu
  builds by the source scanner (`EVENGINE_EXCLUDED_BACKEND_SUBDIRS`).
- ImGui overlays use `imgui_impl_wgpu.cpp` (compiled by the EVUI module scan;
  `imgui_impl_wgpu.cpp` includes `common/config.h` **before** its
  `#if defined(EVENGINE_WEBGPU)` guard so the platform define is visible).
- Poco/OpenAL/medialoader/FreeType are not built on WASM; the corresponding
  modules are compiled out (see the trim list above). The native Dawn build
  still compiles them.
