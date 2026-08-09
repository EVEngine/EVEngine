# Live2D backend plugin skeleton

Shows how to replace the built-in `NullLive2DBackend` with a real Cubism Core
runtime **without changing game scripts**.

## Contract

1. Implement `eve::avatar::ILive2DBackend`.
2. Export a factory `ILive2DBackend *createMyBackend()`.
3. In `eve_plugin_init`, call:

```cpp
eve::avatar::Avatar::registerLive2DBackend(&createMyBackend);
```

4. Load the plugin before creating avatars:

```squirrel
plugins <- eve.Plugins()
plugins.load("./live2d_backend_plugin.so")  // .dylib / .dll
av <- avatar.newLive2DAvatar()
print(av.getLive2DBackendName())  // your backend name
av.loadLive2DModel("models/hiyori")
```

## Cubism notes

- Live2D Cubism SDK is proprietary; ship it in your own plugin / app binary.
- Map Cubism parameters to `setParameter` / `getParameter`.
- Map Cubism expression / motion JSON ids to `setExpression` / `setMotion`.
- Optionally implement `collectDrawItems` to push textured quads into the 2D queue,
  or draw into a Canvas and expose a Texture to an Image avatar layer.

This example registers a **Demo** backend (in-memory parameters only) so the
registration path can be smoke-tested without Cubism.

## Build

Requires an installed EVEngine SDK (same as `examples/native-plugin`):

```bash
cmake -B build -DEVEngine_DIR=$PWD/../../dist/eve-sdk/linux-debug/cmake
cmake --build build
```
