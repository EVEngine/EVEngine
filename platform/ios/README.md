# ios target SDK notes

This tree is the iOS packaging template inside `dist/eve-sdk/ios[-debug]`.

On a **macOS dev machine** with Xcode:

1. Use the prebuilt `bin/eve.app` (or rebuild packaging around the SDK host) as the app shell.
2. Place game assets under the app's `game/` resources (see `game-shell/`).
3. Ensure MoltenVK is embedded as in the engine build.
4. Sign and archive for device / App Store.

Native plugins are typically embeddable `.dylib` / frameworks loaded via `eve.Plugins().load(...)`.
