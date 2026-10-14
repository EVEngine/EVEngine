# Trim Modules When Building From Source

[中文](../../usr/TRIMMING.md) · [User guide](README.md)

This guide is maintained by hand; Chinese is authoritative. **This is optional engine-build configuration.** Prebuilt SDK users can skip it. Selecting `config.modules` controls script instance creation, not which native modules are compiled into the SDK.

## Choose a profile

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DEVENGINE_PROFILE=2d
```

- `minimal`: the engine shell and its essential services.
- `2d`: the minimal profile plus common 2D game capabilities.
- `3d`: adds 3D, scene, rendering and authoring capabilities.
- `full`: all modules; the default.
- `web`: the browser-compatible set, selected for Emscripten builds.

Profiles are starting points. Check the enabled-module output during configuration rather than relying on a fixed module count; the engine continues to add and reorganize modules.

## Override individual modules

```sh
cmake -B build -DEVENGINE_MODULE_DATABASE=OFF -DEVENGINE_MODULE_NETWORK=OFF
cmake -B build -DEVENGINE_PROFILE=2d -DEVENGINE_MODULE_DIALOGUE=ON
```

Names match module directories and are case-insensitive. Enabling a module also enables required dependencies. Explicitly disabling a dependency still needed by another enabled module causes a configuration error; either disable the consumer or re-enable its dependency.

## Keep scripts compatible

```squirrel
if (has_module("particles")) {
    particles.update(dt);
}
```

`eve.moduleList` describes compiled script bindings. Project module lists can further limit constructed root instances; see [EveScript](EVESCRIPT.md).

Core runtime dependencies cannot be disabled arbitrarily. DevTools also pulls in gameplay/rendering dependencies, and the CLI's UI dependency limits trimming. Read CMake's diagnostic when a requested combination is rejected.

For exact current profiles, dependencies and native module declarations, consult [`cmake/module_manifest.cmake`](../../../cmake/module_manifest.cmake). The [architecture guide (Chinese)](../../dev/模块编排与裁剪架构.md) explains the design. Historical binary-size measurements in earlier documents are not a guarantee for your compiler, platform or SDK version.
