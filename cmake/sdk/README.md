# EVEngine target-platform SDK

Each install prefix is an **independent SDK for one publish target**
(`win32` / `linux` / `macosx` / `android` / `ios`).

You do **not** need to build the engine to use this SDK: download the matching
`eve-sdk-<platform>-<version>.zip` from the [official release page](https://github.com/EVEngine/EVEngine/releases),
unzip it, and run `bin/eve` directly. It is used on a **dev machine** to create a
game that ships **to that target**. It is not meant to be “installed and run” on a
phone or as a universal multi-platform kit.

## Layout

- `bin/` — host runtime (`eve` / `eve.exe` / iOS `.app`) when applicable
- `lib/` — Android `libmain.so` + packaging `.so`s; macOS runtime dylibs; Windows `eve.lib` for plugins
- `include/eve/` — public headers for native plugins (not needed for script-only games)
- `cmake/` — `find_package(EVEngine)` + `add_eve_plugin()` (not needed for script-only games)
- `platform/` — **this target only** packaging template (e.g. the Android APK project)
- `share/eve/` — `VERSION`, `TARGET_PLATFORM`, runnable `examples/basic`, `licenses/`

`share/eve/TARGET_PLATFORM` must match the SDK directory name.

## Quick start (script-only game, no compilation)

```sh
bin/eve -v                     # verify the runtime works
bin/eve run share/eve/examples/basic   # run the bundled example
bin/eve create mygame          # scaffold a game (config.nut + main.nut)
bin/eve run mygame             # run it (hot reload is on by default)
bin/eve zip mygame             # compress into mygame.eve
bin/eve package mygame -o out --sdk <this-sdk-root>   # runnable game folder
```

The runtime needs a Vulkan driver (Windows/macOS/Linux desktop; macOS uses the
bundled MoltenVK). No compiler, CMake, or Vulkan SDK is required for script-only
development. The full user guide lives at `docs/usr/README.md` in the source
repository and in the [online docs](https://evengine.github.io/EVEngine/).

## Install (from engine source tree)

This section is only for **engine developers** who build the SDK themselves:

```bash
cmake --install build/macosx-debug --prefix dist/eve-sdk/macosx-debug
cmake --install build/android-debug --prefix dist/eve-sdk/android-debug
```

Or: `make sdk/macosx-debug` / `make sdk/android-debug`.

Do **not** install multiple platforms into the same prefix.

## Native plugin

Only needed when you want to extend the engine with C++ code. The plugin must be
built against the **same platform and version** of the SDK:

```cmake
find_package(EVEngine REQUIRED PATHS /path/to/eve-sdk/macosx-debug)
add_eve_plugin(myplugin SOURCES myplugin.cpp)
```

Export `eve_plugin_init` (see `examples/native-plugin`). From Squirrel:

```squirrel
local plugins = eve.Plugins();
plugins.load("./myplugin.dylib");  // or .so / .dll
```

## Android packaging sketch

1. Copy SDK `lib/libmain.so`, `libSDL2.so`, `libc++_shared.so` into the template `jniLibs/<abi>/`.
2. Copy game scripts into `assets/game/`.
3. Build the Gradle project under `platform/apk/`.
