# EVEngine User Guide

[中文](../../usr/README.md) · [Module handbook](MODULES.md) · [EveScript tutorial](EVESCRIPT.md)

EVEngine is a lightweight, Squirrel-driven engine for 2D, third-person 3D and mixed 2D/3D games. This guide covers downloading the SDK, creating a game, debugging and packaging it. To change the engine itself, see the [engine developer documentation](../dev/README.md).

This English guide is maintained by hand. The Chinese documentation is authoritative; changes to behavior should be documented there first and then reflected here. APIs are still evolving. Run the SDK's `share/eve/examples/basic/` after an upgrade before adapting your game.

The [online documentation](https://evengine.github.io/EVEngine/) includes user guides and the generated C++ API reference. For callable EveScript methods, use the SDK's `share/eve/ai/eve-api.json`, editor completion or MCP API lookup; C++ public methods are not necessarily script bindings. See [API conventions](API-CONVENTIONS.md).

## 1. Download the SDK

**You do not need to compile the engine to make a game.** Download an SDK for your target platform from [GitHub Releases](https://github.com/EVEngine/EVEngine/releases).

| Platform | Archive | Target |
| --- | --- | --- |
| Windows | `eve-sdk-win32-<version>.zip` | Windows 10/11 x64 |
| Linux | `eve-sdk-linux-<version>.zip` | Ubuntu 20.04+ or equivalent, with a Vulkan driver |
| macOS | `eve-sdk-macosx-<version>.zip` | macOS 12+, Apple Silicon / Intel; bundled MoltenVK |
| Android | `eve-sdk-android-<version>.zip` | Assemble an arm64 APK on a development machine |
| iOS | `eve-sdk-ios-<version>.zip` | Assemble an arm64 app on macOS |

Unzip the archive. No engine installation is required. The SDK contains:

- `bin/eve` (`bin/eve.exe` on Windows): the desktop runtime and command-line tool.
- `share/eve/examples/basic/`: a runnable reference game.
- `share/eve/ai/`: this SDK's EveScript API catalog, agent skill and MCP bridge.
- `llms.txt`: the entry point for coding agents.
- `platform/`: target packaging templates, including the Android APK project.
- `share/eve/licenses/`: engine and third-party licenses.
- `include/`, `lib/`, `cmake/`: native plugin development files; pure script games do not need them.

The desktop runtime needs a **Vulkan driver**, not the Vulkan development SDK. Windows graphics drivers normally provide it; macOS uses bundled MoltenVK; Linux needs `mesa-vulkan-drivers` or the manufacturer's driver. You do not need Git, CMake or a C++ compiler to run a desktop SDK.

From the unzipped SDK directory, run:

```sh
# Windows
bin\eve.exe run share\eve\examples\basic

# macOS / Linux
bin/eve run share/eve/examples/basic
```

An open window displaying the example confirms that the runtime starts. In the commands below, `eve` means your SDK's `bin/eve` or `bin\eve.exe`; use its actual path if it is not on `PATH`.

The [render gallery](../../images/examples/README.en.md) shows frames captured by the engine.

See [Examples](EXAMPLES.md), [AI and MCP setup](AI-MCP.md) and the [FAQ](FAQ.md) for the next steps.

## 2. Create your first game

```sh
eve create mygame
eve run mygame
```

A minimal game has two files:

```text
mygame/
├── config.nut    # Window and development settings
└── main.nut      # Game logic
```

`config.nut`:

```squirrel
config <- {
    width = 960
    height = 540
    title = "My EVEngine Game"
    debug = true
    hotReload = true
};
```

`main.nut`:

```squirrel
local x = 100.0;

// Called once when the game starts.
eve_init <- function() {
    gfx.setBackgroundColor(0.08, 0.10, 0.16, 1.0);
};

// dt is the time since the previous frame, in seconds.
eve_update <- function(dt) {
    x += 80.0 * dt;
    if (x > config.width) x = -48.0;
};

eve_render <- function() {
    gfx.clear();
    gfx.drawSolidRect(x, 220.0, 48.0, 48.0, 0.3, 0.75, 1.0, 1.0);
};
```

Edit and save `main.nut` while the game runs to see hot reload. This example uses ordinary temporary state; use `persist` below to keep state across reloads. You can also copy `share/eve/examples/basic/` and replace its scripts and assets gradually.

For a larger project:

```text
mygame/
├── config.nut
├── main.nut
├── scripts/       # EveScript modules loaded with import
├── maps/          # Tiled JSON or map data
├── particles/     # Particle JSON
├── textures/
├── fonts/
├── audio/
└── models/
```

Asset paths are relative to the game's working directory, not the SDK directory.

## 3. Lifecycle and hot reload

| Callback | When | Typical work |
| --- | --- | --- |
| `eve_init()` | Once after loading the game | Create worlds, entities, resources and UI |
| `eve_update(dt)` | Each running frame | Input, physics, AI and state updates |
| `eve_render()` | Each rendered frame | Clear and draw maps, sprites and UI |
| `eve_reload()` | After a script soft reload; optional | Adjust existing runtime state |
| `eve_asset_reload(path)` | After a non-script asset changes; optional | Respond to changed textures or maps |
| `eve_quit()` | On exit; optional | Save data and clean up |

With `config.hotReload` enabled, the engine watches scripts and assets. The default watch root is the game directory. An `apps/<editor>` entry point that needs sibling `editors/` or `lib/` files can set these fields in `config.nut`:

```squirrel
hotReload = true
hotReloadWatch = [".."]   // String or array; default ["."]
```

Change paths are reported relative to the watch root. Use `persist` for root state that must survive a reload:

```squirrel
persist player = null

function eve_init() {
    if (player == null) {
        player = createPlayer(); // Your game's factory; create only once.
    }
}
```

The initializer runs only when the persistent name first appears. Keep one-time setup in `eve_init`; keep repeatable declarations at the top level. Changing a persistent data layout requires explicit migration in `eve_reload`. See the [EveScript tutorial](EVESCRIPT.md).

## 4. Choose modules and examples

Script constructors are exposed through the global `eve` table. The startup environment also provides common instances such as `gfx`, `keyboard`, `mouse`, `physics` and `ui`. Explicit `config.modules` / `optionalModules` lists limit which instances are created; see the language tutorial.

Start with [graphics](modules/graphics.md), [keyboard](modules/keyboard.md), [mouse](modules/mouse.md), [UI](modules/ui.md), [ECS](modules/entity.md) and [files and hot reload](modules/filesystem.md). The [module handbook](MODULES.md) links to the remaining capabilities and marks guides that are still in Chinese.

### A minimal interactive UI

```squirrel
eve_init <- function() {
    ui.beginBuild();
    ui.beginWindow("Status", "root");
    ui.text("Ready", "message");
    ui.button("Start", "start");
    ui.end();
    ui.mountBuildAs("hud");
};

eve_update <- function(dt) {
    local id = ui.consumeClick();
    while (id != "") {
        if (id == "hud/start") ui.setText("message", "Running");
        id = ui.consumeClick();
    }
};

eve_render <- function() {
    gfx.clear();
    ui.beginFrameAndRender();
};
```

For ECS, derive data from `eve.Component`, compose it in `eve.Entity`, and query entities from `eve.System`. The repository's `examples/ecs/main.nut` is a runnable starting point.

### Command-line tasks

| Command | Purpose |
| --- | --- |
| `eve create <name>` | Create a game from the template |
| `eve run [directory]` | Run a game; without a directory, use the current directory, or the built-in demo if no game is present |
| `eve run --debug` | Enable pause, breakpoints, watches and snapshots |
| `eve run --dap-port=4711` | Start the debugger adapter service |
| `eve run --mcp-port=7529` | Start the MCP service for agents |
| `eve dev [--port 8765]` | Serve game files for remote hot reload |
| `eve zip <directory>` | Create a `.eve` archive |
| `eve package <directory> -o <output> --sdk <SDK-directory>` | Package a game with its runtime |
| `eve test` | Run the game directory's test configuration |
| `eve doc <name>` | Look up online documentation |
| `eve build` | Build from source; requires the source tree and development tools |

## 5. Debug a game

```sh
eve run --debug /path/to/mygame
eve run --debug --dap-port=4711 /path/to/mygame
eve run --debug --mcp-port=7529 /path/to/mygame
```

In debug mode:

```squirrel
eve.dev.togglePause();
eve.dev.setBreakpoint("main.nut", 42);
eve.dev.addWatch("score");
eve.dev.markStateRoot("gameState");
eve.dev.saveSnapshot("boss.json");
eve.dev.loadSnapshot("boss.json");
eve.dev.ai.note("boss phase"); // DevTools session log; F9 toggles the panel.
```

In the source tree, `make devlab` runs the interactive developer example with debug enabled. Try F4 (console/REPL), F6/F7 (snapshots), F9 (AI panel) and Pause.

The repository's `tools/vscode-eve-debug/` provides EveScript language support and DAP debugging; `tools/eve-mcp/` connects agents to the MCP port.

When a game fails, check:

1. `config.nut` and `main.nut` are in the directory passed to `run`.
2. Rendering begins with `gfx.clear()`.
3. Systems that need `update(dt)` are advanced from `eve_update`.
4. Asset paths are relative to the game directory.
5. The Vulkan driver and runtime environment work.

Debug snapshots are development tools. For player saves, use the [save-game guide](SAVE.md).

## 6. Package and distribute

### Desktop

```sh
# Optional: bundle game content into a single archive.
eve zip mygame

# Create a directory containing the runtime and game.
eve package mygame -o mygame-package --sdk /path/to/sdk
```

Use an SDK matching the platform and version of the runtime. Without `--sdk`, the tool infers the SDK location from `bin/eve`. Distribute the entire output directory, including runtime libraries. Players do not need to install the engine.

### Remote hot reload on mobile devices

Packaged app resources are read-only. Edit on your development machine and let the device fetch changes:

```sh
# Run from the game directory on the development machine.
eve dev --port 8765
```

In the device's configuration, set the development machine's LAN address:

```squirrel
config.devServer = "http://192.168.1.5:8765"
```

The device polls the manifest, downloads changed files into a writable overlay and uses the existing reload pipeline. You can also pass `eve run --dev-server http://192.168.1.5:8765`. No reinstall is needed for each script or asset change.

### Android and iOS

Download the target SDK and use its packaging templates on a development machine. For Android, put the game in `platform/apk/app/src/main/assets/game/` and build the SDK's Gradle project with the required Android tools. For iOS, use `platform/ios` in Xcode on macOS with signing configured. Read the target requirements in the [English repository README](../../../Readme.en.md) before packaging.

## 7. Compatibility and licensing

- SDKs are platform-specific. Packaging and native plugins must match the SDK platform and version.
- Script-callable methods come from bindings, not every C++ public declaration. Check the SDK catalog and [API conventions](API-CONVENTIONS.md).
- Use `persist` to keep root state through hot reload.
- Modules can be trimmed when building from source. Use `has_module("slot")` for optional features; see [module trimming](TRIMMING.md).
- EVEngine uses dual licensing. Read the [authoritative license section (Chinese)](../../../Readme.md#许可证), especially before commercial distribution.
- Releases and `main` describe released versions; `dev` documentation can describe capabilities not yet present in your downloaded SDK.

## 8. Build from source only when needed

To change the engine, contribute native code or produce your own SDK, follow [Building from source](../../../Readme.en.md#building-from-source-engine-developers--contributors). Ordinary game development uses the prebuilt SDK.

## 9. Suggested learning path

1. Run and modify the bundled basic example.
2. Create a game and learn lifecycle callbacks and hot reload.
3. Read the [EveScript tutorial](EVESCRIPT.md); split scripts with `import` / `export`.
4. Use the [module handbook](MODULES.md) for gameplay tasks.
5. Read [saving games](SAVE.md) when you need persistent progress.
6. Use the SDK's native plugin CMake package only when native code is needed.
7. Check packaging and licensing before distribution.
