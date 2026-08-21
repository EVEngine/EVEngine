EVEngine
=======================
Evolutionary Vision Engine

[中文版](Readme.md)

## Documentation

- [Game developer docs](docs/usr/README.md): install, run, and project layout; [module handbook](docs/usr/MODULES.md) covers script APIs and examples one by one.
- [Engine developer docs](docs/dev/README.md): architecture, module design, testing strategy, and implementation notes.
- [C++ API reference (Doxygen)](docs/api/html/index.html): generated from `src/` and `docs/usr/` — run `make docs` after installing doxygen (config: [`docs/Doxyfile.in`](docs/Doxyfile.in)).

Many game engines are slow to iterate, hard to debug, and awkward for rapid prototyping. EVEngine aims to be a simple, practical engine focused on 2D and third-person 3D gameplay components—not a full-featured 3D scene-management suite.


Design goals:

> Status legend: ✅ shipped · 🟡 partially shipped · 🛠 roadmap (not implemented yet)

1. ✅ A Love2D-style interpretive engine that loads scripts and data directly
2. ✅ Many high-performance C++ modules; you can compile other C++ libraries as shared libs and interact with them directly (see `examples/native-plugin`)
3. 🟡 A full development stack: the dev system is the game runtime—only the shipping runtime strips DevTools (or keep them for player modding); scripts can build editing GUIs at runtime (`editor` / `ui` building blocks, see `examples/terrain-editor`)
4. ✅ Built-in asset management and hot reload—no separate pipeline required
5. 🛠 Class scanning with auto-generated editing GUIs (property reflection not landed yet; `editor.Inspector` currently registers fields manually—see roadmap)
6. 🟡 GUIs that create and sync live from scripts (declarative `ui` module + hot reload; reflective two-way binding is on the roadmap)
7. ✅ Pause the game loop for debugging (`eve run --debug`, F-keys / DAP / snapshots, see `examples/devlab`)
8. 🟡 Independent state-machine models (e.g. animation) exist; systematic "edit code → affected state machines hot-update" support is on the roadmap
9. ✅ Mixed 3D and 2D rendering

> Fastest way to feel goals 3 / 4 / 7: `make devlab` (a developer-experience example that walks you through F4 / F6 / F7 / F9 and hot reload).


Built-in systems:

1. 2D tilemap — with an extensible map editor
2. 2D camera
3. Simple database
4. Box2D / Box3D physics
5. RPG framework
6. Extensible combat model components
7. Dialogs and scripting
8. Layered avatar rendering
9. 2D fluid engine (`Physics.newFluid`; also interactive cloth via `newCloth`)
10. Particle system
11. Sprite-stacking pseudo-3D
12. Real 3D model rendering



Build an RPG with minimal ceremony, with fast edit/debug for events, logic, and cutscene scripts—for example, hot-patch a function in a script and re-run it to preview the effect.


## Requirements

### Common dependencies

| Dependency | Version | Notes |
|------------|---------|-------|
| Git | Any recent version | Clone the repo; first configure may download `third-party` |
| CMake | **≥ 3.21** | Primary build system |
| C++ compiler | **C++20** | MSVC / GCC / Clang |
| Vulkan SDK | **≥ 1.2** | Graphics backend; environment variables must be set correctly |

Third-party libraries (SDL2, Box2D, Squirrel, Poco, ImGui, OpenAL Soft, FreeType, GLM, VKBuilder, etc.) are fetched and built automatically via CMake `ExternalProject` from [EVEngine/third-party](https://github.com/EVEngine/third-party). Manual installs are usually unnecessary.

### Windows

| Component | Recommended |
|-----------|-------------|
| OS | Windows 10 / 11 (x64) |
| IDE / toolchain | **Visual Studio 2026** (with the “Desktop development with C++” workload) |
| CMake generator | `Visual Studio 18 2026` (matches the root `Makefile`) |
| Vulkan SDK | Install from [LunarG](https://vulkan.lunarg.com/). Confirm user env `VULKAN_SDK` (e.g. `C:\VulkanSDK\1.4.357.0`) and add `%VULKAN_SDK%\Bin` to `PATH` (includes `glslc`). **Open a new terminal / restart Cursor** before running cmake |
| Optional | Git for Windows; add CMake / Ninja to `PATH` |

> If you use Visual Studio 2022, change the generator in `Makefile` to `"Visual Studio 17 2022"`.

### Linux

| Component | Recommended |
|-----------|-------------|
| Distro | Ubuntu 20.04+ or equivalent (including WSL2) |
| Compiler | GCC 10+ or Clang 12+ |
| Base packages | `build-essential`, `cmake`, `git`, `ninja-build`, `pkg-config` |
| System libs | X11 / SDL2: `libx11-dev`, `libxext-dev`, `libxxf86vm-dev`, `libxrandr-dev`, `libxcursor-dev`, `libxi-dev`, `libxinerama-dev`, `libxss-dev`; optional Wayland: `libxkbcommon-dev`, `libwayland-dev`; OpenAL/audio: `libasound2-dev`, `libpulse-dev`; image compression: `zlib1g-dev`, `libpng-dev`, `libbz2-dev` |
| Vulkan | `libvulkan-dev` + `glslang-tools` (provides `glslc`), or install the full Vulkan SDK from LunarG |

Ubuntu / WSL example:

```sh
sudo apt update
sudo apt install -y \
    build-essential cmake git ninja-build pkg-config \
    libx11-dev libxext-dev libxxf86vm-dev libxrandr-dev \
    libxcursor-dev libxi-dev libxinerama-dev libxss-dev \
    libxkbcommon-dev libwayland-dev \
    libgl1-mesa-dev libegl1-mesa-dev \
    libasound2-dev libpulse-dev \
    zlib1g-dev libpng-dev libbz2-dev libbrotli-dev \
    libvulkan-dev vulkan-tools glslang-tools \
    python3
# Or install the full Vulkan SDK per LunarG docs (includes validation layers)
```

### macOS

| Component | Recommended |
|-----------|-------------|
| OS | macOS 12+ (Apple Silicon / Intel) |
| Toolchain | **Xcode Command Line Tools** (`xcode-select --install`) |
| CMake | Homebrew: `brew install cmake` (≥ 3.21) |
| Vulkan | Install the **macOS Vulkan SDK** from [LunarG](https://vulkan.lunarg.com/) (includes **MoltenVK**) — build/development only; `dist/eve-sdk/macosx` bundles the Vulkan loader + MoltenVK, so player machines do not need the SDK |

Set up the environment (run in each new terminal, or add to `~/.zshrc`):

```sh
# Replace <version> with your local SDK folder name, e.g. 1.4.357.0
source ~/VulkanSDK/<version>/setup-env.sh
# Verify:
echo "$VULKAN_SDK"
ls "$VULKAN_SDK/lib/libvulkan.dylib" "$VULKAN_SDK/lib/libMoltenVK.dylib"
```

`setup-env.sh` sets `VULKAN_SDK`, `VK_ICD_FILENAMES` (MoltenVK ICD), and related vars. On Apple hosts, CMake **requires** a successful `find_package(Vulkan)` and will not fall back to Windows `vulkan-1.lib`.

### Android (arm64-v8a Debug APK)

| Component | Recommended |
|-----------|-------------|
| Host | macOS / Linux (Makefile paths default to macOS Homebrew) |
| JDK | **OpenJDK 17** (`brew install openjdk@17`) |
| Android SDK | command-line tools + `platform-tools` + `platforms;android-34` + `build-tools;34.0.0` |
| NDK | **26.1.10909125** (`sdkmanager "ndk;26.1.10909125"`) |
| CMake (SDK) | `cmake;3.22.1` (for Gradle; the engine itself cross-compiles with host CMake/Ninja) |
| ABI / minSdk | **arm64-v8a** / **24** |
| Device | Physical device with **Vulkan** (emulator is not a v1 acceptance target) |

Environment variables (add to `~/.zshrc` or export before building):

```sh
export JAVA_HOME="$(brew --prefix openjdk@17)/libexec/openjdk.jdk/Contents/Home"
export ANDROID_HOME="$HOME/Library/Android/sdk"
export ANDROID_NDK="$ANDROID_HOME/ndk/26.1.10909125"
export PATH="$JAVA_HOME/bin:$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$PATH"
```

Write `platform/android/apk/local.properties` (**do not commit**):

```
sdk.dir=/Users/<you>/Library/Android/sdk
```

One-shot Debug APK build:

```sh
make build/android-debug
# Output: platform/android/apk/app/build/outputs/apk/debug/app-debug.apk
```

Pipeline: NDK CMake cross-builds third-party + `libmain.so` → `sync/android-libs` → `sync/android-assets` → Gradle `assembleDebug`.

Default `ANDROID_GAME=demo`: syncs `platform/android/game-shell/` (no `main.nut`) into the APK and starts with the embedded `eve.demoScript`. To package an example directory:

```sh
make build/android-debug ANDROID_GAME=example
# Or refresh assets only:
make sync/android-assets ANDROID_GAME=example
```

On launch, the APK copies `assets/game` into the app’s internal storage and enters the existing CLI via `run <internal path>`.

Install / run / logs on device:

```sh
adb devices
make install/android-debug
make run/android-debug
make log/android
```

### iOS / iPadOS (arm64 device, min 13.0)

| Component | Recommended |
|-----------|-------------|
| Host | macOS + **full Xcode** (Command Line Tools alone are not enough; need `iphoneos` SDK) |
| Deployment target | **iOS / iPadOS 13.0+** |
| Arch / device family | **arm64**; `TARGETED_DEVICE_FAMILY=1,2` (iPhone + iPad) |
| Vulkan | LunarG SDK’s `iOS/lib/MoltenVK.xcframework` |
| Signing | Apple Development; set `IOS_DEVELOPMENT_TEAM=<TeamID>` |
| Acceptance device | Physical **iPad** (iPhone compatible); simulator is not a v1 target |

One-shot Debug `.app` build:

```sh
# Team ID is auto-detected when signed into an Apple ID; or set manually:
#   security find-certificate -a -c "Apple Development" -p | openssl x509 -noout -subject
# Use OU=<TeamID> (the ID in parentheses in the certificate name is not the Team ID)
export IOS_DEVELOPMENT_TEAM=XXXXXXXXXX
make build/ios-debug
```

Pipeline: Xcode generator configures the iOS toolchain → cross-builds third-party (SDL UIKit static libs, etc.) → links/embeds MoltenVK → packs `example/` into `Resources/game` → produces `eve.app`. If launched with no CLI args, injects `run <Resources/game>`.

Install / run / logs on device:

```sh
# Connect iPad over USB; enable Developer Mode and trust this computer
make install/ios-debug
make run/ios-debug
make log/ios
```

Optional vars: `IOS_DEPLOYMENT_TARGET` (default 13.0), `IOS_BUNDLE_ID` (default `com.evengine.example`), `VULKAN_SDK`.

Troubleshooting:

1. **Signing**: Sign in via Xcode → Settings → Accounts with an Apple ID that has an Apple Development certificate; `security find-identity -v -p codesigning` should list it. Without `IOS_DEVELOPMENT_TEAM`, `make build/ios-debug` can still produce an unsigned `.app`, but it will not install on device. `No Account for Team "XXXX"` usually means you used a certificate ID instead of the Team ID (`OU` in the certificate subject).
2. **Device**: USB-connect the iPad, enable Developer Mode, trust the computer; `xcrun devicectl list devices` should show `connected`. If you see `no DDI`, open the device once in Xcode to install the Developer Disk Image.
3. **Dependencies**: Third-party builds use Ninja + `cmake/ios.toolchain.cmake` (SDL UIKit). On failure, clear `build/third-party/ios-debug` and `build/third-party-binary/ios-debug` and retry.
4. **MoltenVK**: Current LunarG xcframeworks may require iOS 14+; the device OS must meet that requirement (e.g. iPadOS 26 is fine).
5. v1 does not cover simulator, App Store distribution, or multi-arch fat binaries.

### WSL (develop Windows and Linux together)

Prefer **WSL2 (Ubuntu)** for Linux targets and Visual Studio on the Windows host for Win32. Keep the source on the Windows filesystem (e.g. `C:\Users\...\EVEngine`) and access it from WSL via `/mnt/c/...`.

Notes:

1. Install VS 2026 + Vulkan SDK on Windows; install the Linux toolchain and Vulkan inside WSL as above.
2. Run `make` from the repo root: Windows targets use `cmake.exe`, Linux targets use WSL `cmake`.
3. Third-party outputs are per-platform (`build/third-party/win32`, `build/third-party/linux-debug`, etc.) and do not overwrite each other.
4. Building under `/mnt/c` can be slow; for speed, clone into the WSL native filesystem (e.g. `~/src/EVEngine`) and keep a separate Windows clone if needed.


## Get the source

```sh
git clone --recurse-submodules https://github.com/EVEngine/EVEngine
cd EVEngine
# If you already cloned without submodules:
# git submodule update --init --recursive
```

The default clone is `main` (latest release). Engine development happens on `dev`: `git checkout dev && git pull`.

The first CMake configure pulls `third-party` automatically; if the directory already exists with a `CMakeLists.txt`, the local copy is used. ECS uses submodule [`external/ECS.hpp`](https://github.com/sunxfancy/ECS.hpp); IK uses [`external/ik.hpp`](https://github.com/sunxfancy/ik.hpp).


## Quick start (recommended: root Makefile)

The root `Makefile` wraps Debug / Release configure and build for Windows / Linux / macOS. With `make` available (Git Bash, WSL, Linux, macOS), from the repo root:

```sh
# Host-platform Debug / Release (Darwin → macosx, Linux → linux, Windows → win32)
make debug
make release

# Windows only
make build/win32
make build/win32-debug

# Linux only
make build/linux
make build/linux-debug

# macOS only (source Vulkan SDK setup-env.sh first)
make build/macosx
make build/macosx-debug

# Android arm64 Debug APK (needs ANDROID_HOME / ANDROID_NDK / JAVA_HOME)
make build/android-debug
make install/android-debug
make run/android-debug
make log/android
```

Output layout:

| Target | Build dir | Notes |
|--------|-----------|-------|
| Win32 Release | `build/win32` | VS project + Release binaries |
| Win32 Debug | `build/win32-debug` | Ninja + MSVC Debug binaries |
| Linux Release | `build/linux` | Unix Makefiles |
| Linux Debug | `build/linux-debug` | Unix Makefiles |
| macOS Release | `build/macosx` | Unix Makefiles + MoltenVK |
| macOS Debug | `build/macosx-debug` | Unix Makefiles + MoltenVK |
| Android Debug | `build/android-debug` + Gradle APK | NDK `libmain.so` → `app-debug.apk` |
| iOS Debug | `build/ios-debug` (Xcode) | `eve.app` + embedded MoltenVK for iPad/iPhone |

The main executable is usually `eve` (`eve.exe` on Windows; path depends on VS `Release` / `Debug` config subdirs). On Android, SDLActivity loads `libmain.so` (package `com.evengine.example`).


## Step-by-step build

### 1. Configure

Equivalent to the Makefile configure steps; you can also run manually.

**Windows (Visual Studio 2026, x64):**

```sh
cmake.exe -G "Visual Studio 18 2026" -A x64 -DCMAKE_BUILD_TYPE=Release -B build/win32 -S .
# Debug:
cmake.exe -G "Visual Studio 18 2026" -A x64 -DCMAKE_BUILD_TYPE=Debug -B build/win32-debug -S .
```

**Linux:**

```sh
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -B build/linux -S .
# Debug:
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug -B build/linux-debug -S .
```

**macOS:**

```sh
source ~/VulkanSDK/<version>/setup-env.sh
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DBUILD_PLATFORM=macosx -B build/macosx -S .
# Debug:
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug -DBUILD_PLATFORM=macosx -B build/macosx-debug -S .
```

Optional CMake variables:

| Variable | Default | Notes |
|----------|---------|-------|
| `CMAKE_BUILD_TYPE` | `Debug` (if unset) | `Debug` or `Release`; affects third-party install path suffixes (e.g. `win32-debug`) |
| `BUILD_PLATFORM` | Auto from host | `win32` / `linux` / `macosx`, etc. |
| `BUILD_TESTING` | `ON` | Build unit tests |

### 2. Build third-party (deps)

Third-party code builds via custom targets `deps` / `third-party`. After configure:

```sh
# Windows Debug example
cmake.exe --build build/win32-debug --target deps -j 32

# Linux
cmake --build build/linux-debug --target deps -j 32

# macOS
cmake --build build/macosx-debug --target deps -j 32
```

The first third-party build takes a while. Outputs land in `build/third-party/<platform>[-debug]/` and `build/third-party-binary/<platform>[-debug]/`. Later incremental builds reuse installed results.

### 3. Build the engine

```sh
# Windows (multi-config: pass --config)
cmake.exe --build build/win32 --config Release -j 32
cmake.exe --build build/win32-debug --config Debug -j 32

# Linux
cmake --build build/linux -j 32
cmake --build build/linux-debug -j 32

# macOS
cmake --build build/macosx -j 32
cmake --build build/macosx-debug -j 32
```

Or use `make build/win32` / `make build/macosx-debug` etc. to configure and build in one step.


## Unit tests

`BUILD_TESTING` is on by default. After building:

```sh
make test/win32-debug    # runs build/win32-debug/test/Debug/unit_test.exe
make test/win32          # Release tests
make test/linux-debug
make test/linux
make test/macosx-debug
make test/macosx
```

Or run `unit_test` / `unit_test.exe` from the corresponding path directly.


## Docker (optional)

The repo includes an Ubuntu 20.04 `Dockerfile` (`build-essential`, `cmake`, `clang-12`, etc.) for an isolated Linux build:

```sh
docker build . --tag evengine
docker run -it --rm --volume="$(pwd):/home/evengine/src" evengine /bin/bash
```

Inside the container, configure and build with the Linux flow. You still need to supply Vulkan runtime/SDK yourself (the image does not ship a full Vulkan SDK).


## FAQ

1. **CMake cannot find Vulkan**  
   Confirm the Vulkan SDK is installed and `VULKAN_SDK` points at the right path; on macOS, `source ~/VulkanSDK/<version>/setup-env.sh` before configure. Open a new terminal, then re-run cmake.

2. **Windows generator name mismatch**  
   The `Makefile` uses `Visual Studio 18 2026`. For VS 2022, change the generator string or upgrade the IDE.

3. **First `deps` is slow or network fails**  
   `third-party` is fetched from GitHub. You can manually `git clone https://github.com/EVEngine/third-party` into the repo-root `third-party/`, then reconfigure.

4. **`CMAKE_BUILD_TYPE` and third-party paths**  
   Debug / Release use different install dirs (with a `-debug` suffix). After switching build type, re-run `deps` in the matching directory.

5. **Parallelism**  
   The Makefile defaults to `-j 32`. On machines with fewer cores, use `-j$(nproc)` (Linux) / `-j$(sysctl -n hw.ncpu)` (macOS) or a smaller number to avoid OOM.

6. **macOS window / rendering fails**  
   If the error is `Installed Vulkan doesn't implement the VK_KHR_surface extension` (SDL's wording), the runnable Vulkan lacks surface support — on macOS that means MoltenVK was not loaded. For **distributed builds**, ship the `lib/` folder from `dist/eve-sdk/macosx` (it contains `libvulkan.1.dylib`, `libMoltenVK.dylib` and `MoltenVK_icd.json`); eve points SDL and the loader at these bundled files at startup (`bootstrapBundledVulkan` in `platform/macosx`), so players do not need the SDK. For **dev builds**, confirm `VK_ICD_FILENAMES` points at the MoltenVK ICD (`setup-env.sh` sets this) and the SDK `lib` is on `DYLD_LIBRARY_PATH`. The engine uses SDL2 Vulkan surface + MoltenVK; no separate Metal backend is required.

7. **Android APK / device**  
   Match the NDK version to the Makefile; ensure `local.properties` `sdk.dir` is correct; enable USB debugging and check `adb devices`. v1 expects a Vulkan device; without one, at least verify `make build/android-debug` produces an APK.

8. **iOS / iPadOS device**  
   Full Xcode is required (`xcode-select -p` should point at `Xcode.app/Contents/Developer`, and `xcrun --sdk iphoneos --show-sdk-path` must work). Set `IOS_DEVELOPMENT_TEAM`, connect a trusted iPad/iPhone over USB. MoltenVK comes from the LunarG SDK’s `iOS/lib/MoltenVK.xcframework`.

## Project layout (brief)

```
EVEngine/
├── CMakeLists.txt      # Root CMake: platform detect, third-party, subdirs
├── Makefile            # Windows / Linux / macOS / Android / iOS shortcuts
├── platform/           # Platform code and packaging (android/apk, ios Info.plist)
├── src/
│   ├── engine/         # Core, DevTools, CLI (Android libmain / iOS eve.app)
│   ├── modules/        # Feature modules
│   └── scripts/        # Script-side integration
├── test/               # Unit tests
├── third-party/        # Auto-downloaded third-party sources (can be pre-placed)
├── cmake/              # Helpers such as source scanning
└── docs/               # User docs (usr) and developer docs (dev)
```

Usage docs: `docs/usr/`. Design notes: `docs/dev/`.

## License

EVEngine uses a **dual license**:

| Use | License file | Cost |
|-----|--------------|------|
| Open-source projects; or project Annual Revenue ≤ RMB 100,000 | [LICENSE-OPENSOURCE](LICENSE-OPENSOURCE) | Free |
| Annual Revenue exceeding RMB 100,000 | [LICENSE-COMMERCIAL](LICENSE-COMMERCIAL) | Paid; apply within **1 month** after exceeding the threshold, or you may not continue use |

See [LICENSE](LICENSE) for the overview. Commercial licensing: `sunxfancy@gmail.com`.
