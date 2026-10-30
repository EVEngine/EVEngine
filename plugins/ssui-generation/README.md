# SSUI Generation native plugin

Native `eve.Plugins()` dynamic library and optional material-editor panel. Runtime uses C++/Squirrel only. HTTP remains the open SSUI v1 protocol; the server has no EVEngine dependency.

Build with a matching EVEngine SDK and Poco Net/JSON/Foundation installation:

```sh
cmake -S plugins/ssui-generation -B build/ssui-generation -DCMAKE_BUILD_TYPE=Release \
  -DEVEngine_DIR=/path/to/matching-sdk/cmake -DCMAKE_PREFIX_PATH=/path/to/poco-prefix
cmake --build build/ssui-generation
ctest --test-dir build/ssui-generation --output-on-failure
cmake --install build/ssui-generation --prefix /path/to/material-project
```

Start SSUI's `yarn dev:bridge`, then run the matching `eve mcp --root /path/to/material-project`. The installed editor is restored automatically. Generate → Preview → Apply → Undo runs entirely inside EVEngine. Change the service URL with Connect. Material bindings/history are stored under `generated/material.json`; PNGs have SHA256 filenames. Use a new project rather than the older Python prototype's document format.

To use without the panel:

```squirrel
local plugins = eve.Plugins();
plugins.load("./ssui_generation.dylib"); // .dll on Windows, .so on Linux
local client = eve.SSUIGeneration();
client.configure("http://127.0.0.1:7423", ".");
client.start("mock.checker@1", "{\"prompt\":\"brick\",\"size\":256}", "{}");
// Poll busy/status/progress each frame. Once complete, candidate() returns a PNG path.
// Load candidate() with Graphics before calling apply(); undo() restores the binding.
// cancel() requests cancellation; shutdown() joins outstanding work.
```

Keep the dynamic library loaded for the lifetime of the host. The engine's current plugin registry cannot unregister exposed module classes; `shutdown()` stops work but does NOT make `plugins.unload()` safe. Do not hot-reload/unload this native module.

`cmake --build build/ssui-generation --target package` creates a platform-specific ZIP containing the library and editor assets. Install/ship with the same platform, architecture and Debug/Release ABI as the host.

Prototype constraints: HTTP only (no silent TLS downgrade), trusted local service, PNG/baseColor editor, single-writer material document, no scene-selection adapter. The library accepts generic recipe IDs and JSON inputs/context; the bundled panel uses `mock.checker@1`. Requests have five-second socket timeouts and job polling is capped at 120 seconds; cancellation/shutdown waits for in-flight HTTP. Artifacts are limited to 16 MiB, same-origin URLs and SHA256 checks. Network work never touches engine graphics or the VM.

Native integration verification (SSUI mock service must be running):

```sh
python3 plugins/ssui-generation/tests/native_smoke.py --eve /path/to/matching/eve \
  --project /path/to/disposable-installed-plugin-project
```

This development test reuses the repository's stdio MCP driver, exercises the actual loaded C++ plugin and Squirrel panel, saves `native-applied.png`, then verifies undo and cancellation. Python is only used as the test driver. CTest additionally checks JSON validation, persistence, revision conflicts and asset-path validation. Tested on macOS arm64 Debug; other platform builds are not verified here.
