# macosx target SDK notes

This directory is the packaging template shipped inside `dist/eve-sdk/macosx-<arm64|x86_64>[-debug]`.

Publish a macOS game by shipping:

1. `bin/eve` from the SDK
2. `lib/` from the SDK — this now contains the bundled Vulkan loader
   (`libvulkan.1.dylib`), MoltenVK (`libMoltenVK.dylib`) and its ICD manifest
   (`MoltenVK_icd.json`); see `cmake/macosx_bundle_vulkan.cmake`
3. Your game folder (`config.nut`, `main.nut`, assets)
4. Optional native plugins (`.dylib`) loaded via `eve.Plugins().load(...)`

The SDK install renames eve's loader reference to `@rpath/libvulkan.1.dylib`
and gives eve the `@loader_path/../lib` rpath, so the whole tree is
self-contained: **the player does not need the LunarG Vulkan SDK or any
Vulkan environment variables.** At startup eve points SDL and the Vulkan
loader at the bundled files (`eve::macosx::bootstrapBundledVulkan` in
`platform/macosx/macosx.mm`). The player machine only needs a Metal-capable
macOS (the published SDKs require macOS 13.3+).

Apple's paravirtual Metal device cannot create argument encoders even for a
single resource. The runtime identifies `AppleParavirtDevice` before loading
MoltenVK and selects its supported discrete resource binding mode. This applies
to both SDK and packaged games; physical GPU defaults stay unchanged. An explicit
`MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS` setting takes precedence. The SDK's real
30-frame package acceptance remains mandatory in either mode.

Build a locally signed app with the macOS host SDK:

```sh
eve build macos ./game --sdk /path/to/macosx-arm64 -o ../mac-output \
  --name MyGame --bundle-id com.example.mygame
```

The command needs Xcode command line tools and their Python 3 interpreter.
It places dylibs in `Contents/Frameworks`, and game resources plus
`MoltenVK_icd.json` in `Contents/Resources`, then signs nested code before the app.
Startup never modifies the signed bundle. The output directory must be new and
outside the game and SDK. The result uses ad-hoc signing for local testing;
Developer ID signing, notarization and App Store submission are separate.

Download the matching `eve-sdk-macosx-arm64-<version>.zip` or
`eve-sdk-macosx-x86_64-<version>.zip`. `share/eve/TARGET_PLATFORM` is `macosx`;
`share/eve/TARGET_ARCHITECTURE` identifies the CPU. Native plugins must use that
same architecture (`find_package(EVEngine)` exports `EVENGINE_TARGET_ARCHITECTURE`).
