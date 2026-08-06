# EVEngine target-platform SDK

Each install prefix is an **independent SDK for one publish target**
(`win32` / `linux` / `macosx` / `android` / `ios`).

It is used on a **dev machine** to create a game that ships **to that target**.
It is not meant to be “installed and run” on a phone or as a universal multi-platform kit.

## Layout

- `bin/` — host (`eve` / `eve.exe` / iOS `.app`) when applicable
- `lib/` — Android `libmain.so` + packaging `.so`s; Windows `eve.lib` for plugins
- `include/eve/` — public headers for native plugins
- `cmake/` — `find_package(EVEngine)` + `add_eve_plugin()`
- `platform/` — **this target only** packaging template
- `share/eve/TARGET_PLATFORM` — must match the directory name

## Install (from engine source tree)

```bash
cmake --install build/macosx-debug --prefix dist/eve-sdk/macosx-debug
cmake --install build/android-debug --prefix dist/eve-sdk/android-debug
```

Or: `make sdk/macosx-debug` / `make sdk/android-debug`.

Do **not** install multiple platforms into the same prefix.

## Native plugin

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
