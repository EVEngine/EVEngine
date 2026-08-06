# win32 target SDK notes

This directory is the packaging template shipped inside `dist/eve-sdk/win32[-debug]`.

Publish a Windows game by shipping:

1. `bin/eve.exe` from the SDK
2. Your game folder (`config.nut`, `main.nut`, assets)
3. Optional native plugins (`.dll`) next to the game; load via `eve.Plugins().load(...)`

Develop on a Windows machine (or cross-build plugins matching the MSVC ABI of the SDK).
