# Building from Source

Sources: [Chinese build instructions](../../../Readme.md), [contributor instructions](../../../AGENTS.md). [Developer index](README.md).

This guide is for changing the engine. Script-only games use the prebuilt SDK.

## Tools and checkout

Use Git, CMake 3.21 or newer, a C++20 compiler and a Vulkan development environment. CMake builds most third-party libraries from pinned sources; host window/audio development packages are still required.

```sh
git clone --recurse-submodules https://github.com/EVEngine/EVEngine
cd EVEngine
git checkout dev
git pull
# If the original clone omitted submodules:
git submodule update --init --recursive
```

`main` contains the latest official release; everyday contributions target `dev`. Initial configuration downloads the pinned `third-party/` repository. `external/*` is managed separately through submodules. See [dependencies](DEPENDENCIES.md).

### Windows

Install Visual Studio 2026 with Desktop development with C++, CMake, Ninja and Git for Windows. The current Windows Debug and Release Makefile targets use Ninja and MSVC through `cmake/with-msvc.cmd`, which finds compiler setup with `vswhere`. Visual Studio 2022 is also supported; explicitly configured Visual Studio projects must select the matching installed generator.

Install the Vulkan SDK, set `VULKAN_SDK` to its directory and add its `Bin` directory to `PATH` so `glslc` is available. Open a new terminal after environment changes. Run Makefile targets with `make` and Git Bash available.

### Linux / WSL2

Use GCC with C++20 support, CMake, Git, Ninja, `pkg-config` and development packages for X11/Wayland, ALSA/PulseAudio, zlib, PNG, bzip2 and Brotli. Install Vulkan headers/loader and a shader compiler (`glslc`); verify the executable rather than assuming a package name supplies it. The Chinese README lists the Ubuntu packages.

The prepared Linux CI environment uses GCC. Switching compilers changes standard-library requirements; a compiler-only installation may be insufficient.

### macOS

Install Xcode Command Line Tools, CMake and the macOS Vulkan SDK with MoltenVK. Load its environment in each build shell:

```sh
source ~/VulkanSDK/<version>/setup-env.sh
```

CMake must find Vulkan on the Apple host. The distributed macOS SDK already bundles the runtime loader and MoltenVK; this setup is for engine development.

## Build targets

From the repository root:

```sh
make debug JOBS=4
make release JOBS=4
make build/win32-debug
make build/win32
make build/linux-debug
make build/linux
make build/macosx-debug
make build/macosx
```

The platform/type appears in the build directory, such as `build/win32-debug` or `build/macosx`. Choose parallelism for available memory; the first dependency build is much slower than incremental builds.

A manually configured build has three stages:

```sh
cmake -S . -B build/linux-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux-debug --target deps -j 4
cmake --build build/linux-debug -j 4
```

Multi-configuration generators require `--config Debug` or `--config Release`. Prefer the Windows Makefile targets to keep generator/compiler setup consistent.

## Incremental builds and profiles

CMake can automatically discover `sccache` on `PATH`. Request it explicitly or disable discovery:

```sh
make build/win32-debug CMAKE_EXTRA_ARGS=-DEVENGINE_COMPILER_CACHE=SCCACHE
make build/win32-debug CMAKE_EXTRA_ARGS=-DEVENGINE_COMPILER_CACHE=OFF
make build/win32-debug CMAKE_EXTRA_ARGS="-DEVENGINE_COMPILER_CACHE=SCCACHE -DEVENGINE_PROFILE=2d"
```

A compiler cache shares reusable outputs; each checkout still needs its own CMake/Ninja build directory. [Module trimming](../usr/TRIMMING.md) avoids compiling unused domains. A complete dependency installation can be reused read-only through `EVENGINE_THIRD_PARTY_BINARY_DIR`; see [dependencies](DEPENDENCIES.md).

`EVENGINE_MODULE_LINKAGE=SHARED` is the Windows/Linux development default; macOS, mobile and WebAssembly keep OBJECT by default. Cross-group symbols need the correct `EVENGINE_API_<GROUP>` export macro; Linux visibility can hide missing Windows exports until link time. Release/package targets explicitly use `OBJECT` for the shipped executable. Keep these modes distinct when diagnosing failures.

## Mobile builds

Android needs `ANDROID_HOME`, `ANDROID_NDK` and `JAVA_HOME`:

```sh
make build/android-debug
make install/android-debug
make run/android-debug
make log/android
```

For iOS, use macOS/Xcode. `make build/ios-debug` builds a device application; `make build/ios-sim-debug` builds the simulator path. Device installation requires signing. Consult the [Chinese Android/iOS instructions](../../../Readme.md) for full toolchain/template setup.

## Check and run

Platform recipes live in `cmake/platforms/*.mk`; feature checks live in
`cmake/tasks/*.mk`. Keep the root Makefile as composition and shared defaults.
Module-specific graphics, physics, procgen and UI settings live beside their
sources in `src/modules/<host>/BuildSettings.cmake`, included in the original
order and scope. Module declarations still belong exclusively to the manifest.

Run [source checks and affected tests](TESTING.md) before submitting changes. `make devlab` exercises hot reload/debugging. Missing headers, unavailable SDKs or changed generators need environment/configuration repairs before engine behavior changes.

Assertions use `EV_PARAM_CHECK` / `EV_ASSERT` in `src/engine/common/Assert.h`. Debug enables them; non-Debug builds remove them by default. Explicitly retain them with `-DEVENGINE_ENABLE_ASSERTS=ON`.

Debug builds use a two-tier compiler profile (`EVENGINE_DEBUG_LEVEL`, Makefile `DEBUG_LEVEL`):
`normal` (default, CI) is `-g0 -O2` / MSVC `/Od` with no DWARF/PDB so compiler-cache
entries stay small (MSVC stays unoptimized: `/O2` cold-compiles past the Windows CI
budget); `detail` is `-g2 -O1` / MSVC `/Zi /O1` for richer DWARF when debugging.
Switch with `make debug DEBUG_LEVEL=detail`.
