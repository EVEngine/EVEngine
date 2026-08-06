# linux target SDK notes

This directory is the packaging template shipped inside `dist/eve-sdk/linux[-debug]`.

Publish a Linux game by shipping:

1. `bin/eve` from the SDK
2. Your game folder (`config.nut`, `main.nut`, assets)
3. Optional native plugins (`.so`) loaded via `eve.Plugins().load(...)`

Prefer a fully static host when possible; plugins remain shared objects.
