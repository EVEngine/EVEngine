# android target SDK notes

This tree (under the android SDK's `platform/`) is the APK packaging template.

On a **dev machine** (not on-device):

1. Copy SDK `lib/libmain.so`, `libSDL2.so`, `libc++_shared.so` (and `libhidapi.so` if present)
   into `apk/app/src/main/jniLibs/<abi>/`.
2. Copy your game into `apk/app/src/main/assets/game/`.
3. Build with Gradle (`./gradlew assembleDebug` / `assembleRelease`).

Native plugins (`.so`) for the same ABI go into `jniLibs` and are loaded from script
with `eve.Plugins().load(...)`.
