# Engine Dependencies

[中文](../../dev/依赖项.md) · [Building](BUILDING.md) · [Developer index](README.md)

Dependencies have distinct responsibilities:

- Squirrel: scripting; SimpleSquirrel: C++ bindings.
- VKBuilder: Vulkan integration combining features of vk-bootstrap and Vookoo; submodule `external/VKBuilder`.
- ECS.hpp: class-enabled ECS for entities/declarative rendering; `external/ECS.hpp`.
- ik.hpp: header-only FABRIK/joint constraints exposed by the `ik` module; `external/ik.hpp`. Chain `solveChain` / pole / influence / tip rotation currently arrive via `cmake/patches/ik-chain-solve-pole-influence.patch` until upstream lands the same API.
- zeroerr: tests, assertions and logging; `external/zeroerr`.
- Poco: networking, database support and filesystem watching.
- medialoader and its codecs: image/audio loading extracted for standalone use.
- SDL2: windows, platform messages and input.
- FreeType: fonts; GLM: mathematics.
- Dear ImGui: GUI backend. Third-party CMake builds `third-party/imgui/` and exposes headers. The `ui` module uses SDL/Vulkan backends.
- OpenAL: spatial audio; PhysFS: virtual filesystem.
- Box2D / Box3D: 2D / 3D physics.
- LZ4 / zlib: compression; smmalloc: allocator used in snapshot-related work.
- CLI11: command-line parsing; rang: terminal colors.

This is a role list, not a package lockfile. Current source pins/build declarations determine versions; historical labels in design notes do not override them.

## Source management

Initialize `external/*` submodules after cloning. Root CMake obtains the separately pinned `EVEngine/third-party` repository and validates its revision. It can contain its own submodules, such as Box3D and Dungeon Template Library.

Most libraries use the dependency build targets rather than individual host installation. Host window/audio/Vulkan tools still need the [building](BUILDING.md) setup.

## Read-only prebuilt installation

With a complete third-party install tree:

```sh
cmake -S . -B build -DEVENGINE_THIRD_PARTY_BINARY_DIR=/path/to/third-party-binary/win32-debug
```

It must contain `include/` and `lib/` with matching platform/configuration libraries. CMake reads it and creates a compatible empty `third-party` target; it does not configure, build or install into that directory. Each engine checkout keeps its own mutable build tree.

## Procedural generation

Dungeon Template Library (BSL-1.0) is integrated through `third-party/DungeonTemplateLibrary` for BSP dungeons, cellular caves, MazeDig and Perlin terrain. FastNoise2 (MIT) is an optional extension for fast/tileable noise; fast-wfc (MIT) is an optional Wave Function Collapse extension. An optional design entry is not an already integrated dependency.

`cave.drunkard` is implemented in the module because DTL lacks that algorithm. See the [procedural generation design (Chinese)](../../dev/程序化生成模块设计.md).

`EVENGINE_PREBUILT_THIRD_PARTY` remains a deprecated compatibility alias; prefer `EVENGINE_THIRD_PARTY_BINARY_DIR`.
