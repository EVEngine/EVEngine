# macosx target SDK notes

This directory is the packaging template shipped inside `dist/eve-sdk/macosx[-debug]`.

Publish a macOS game by shipping:

1. `bin/eve` from the SDK
2. Your game folder (`config.nut`, `main.nut`, assets)
3. Optional native plugins (`.dylib`) loaded via `eve.Plugins().load(...)`

Requires a Vulkan-capable environment (MoltenVK) on the player machine, same as the engine host.
