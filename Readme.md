# EVEngine

=======================

Evolutionary Vision Engine

[English](Readme.en.md)

## 文档导航

- 想先直观感受引擎的渲染效果？浏览[示例渲染画廊](docs/images/examples/README.md)——每张截图都由引擎把交换链上实际呈现的一帧原样输出。
- [游戏开发者文档](docs/usr/README.md)：下载引擎、创建并运行游戏、调试与打包发布；[模块使用手册](docs/usr/MODULES.md) 逐个说明脚本 API 和示例。
- [在线 API 参考（Doxygen）](https://evengine.github.io/EVEngine/)：随 GitHub Pages 持续发布，包含 C++ API 与用户手册。
- [开发者文档](docs/dev/README.md)：面向引擎维护者与贡献者，包含架构、模块设计、测试策略与实施记录。

> **给游戏开发者**：使用 EVEngine 做游戏**不需要编译引擎**。请直接到[官网发布页](https://github.com/EVEngine/EVEngine/releases)下载对应平台的预编译 SDK，解压即可开始创作——无需安装 Git、CMake、C++ 编译器或 Vulkan SDK。只有想修改引擎本身的开发者才需要阅读下文“从源码构建”。

## 简介

针对很多游戏引擎开发效率低、难以调试、难以动态快速实现游戏原型的问题，我们设计了一款非常简单好用的游戏引擎。它主要提供 2D、第三人称 3D 与 2D/3D 混合的游戏组件能力，不提供复杂 3D 引擎中的场景管理等重量级功能。

设计思路：

> 状态图例：✅ = 已落地　🟡 = 部分落地　🛠 = 规划中（未实现）

1. ✅ 采用类似 Love2D 的解析式引擎，可以直接加载脚本和数据；
2. ✅ 内部有众多 C++ 编写的高性能模块，你还可以把自己的 C++ 库编译成动态链接库与这些模块直接交互（见 `examples/native-plugin`）；
3. 🟡 有完整的开发环境：自己写的逻辑可以直接用 GUI 工具编辑，且编辑器运行时就是游戏运行时——只有发布用的运行时才会裁剪开发工具（也可以选择保留，供玩家二次创作；脚本可现场生成编辑用 GUI，见 `examples/terrain-editor`）；
4. ✅ 内置资产管理、热重载，无需额外管线；
5. 🛠 可扫描类并自动为其生成编辑用 GUI（属性反射尚未落地；当前 `editor.Inspector` 为手动登记字段，见 Roadmap）；
6. 🟡 GUI 可以即时根据脚本动态创建和同步（声明式 `ui` 模块 + 热重载；反射式双向绑定规划中）；
7. ✅ 可暂停游戏循环进行调试（`eve run --debug`，F 键 / DAP / 快照，见 `examples/devlab`）；
8. 🟡 拥有众多独立状态机模型（动画状态机等）；「修改代码 → 局部状态机自动热更新」的系统化支持规划中；
9. ✅ 支持 3D 与 2D 混合渲染。

> 想最快体验第 3 / 4 / 7 条：`make devlab`（内置开发者体验示例，跑一遍 F4 / F6 / F7 / F9 与热重载）。

内置系统：

1. 2D tilemap——含可扩展的地图编辑器；
2. 2D 摄像机；
3. 简单的数据库；
4. Box2D / Box3D 物理；
5. RPG 框架；
6. 可扩展的战斗模型组件；
7. 对话框及脚本；
8. Avatar 分层渲染；
9. 2D 流体引擎（`Physics.newFluid`；另含可交互布料 `newCloth`）；
10. 粒子系统；
11. Sprite-stacking 伪 3D 技术；
12. 真实 3D 模型渲染。

支持以最简单的方式实现一个 RPG 游戏，并提供对事件、逻辑、过场脚本等快速编辑调试的功能。例如你可以修改脚本中的某个函数并反复执行，实时观察效果（热更新）。

## 快速开始（游戏开发者：下载即用，无需编译）

### 1. 从官网下载引擎

前往 [EVEngine 发布页](https://github.com/EVEngine/EVEngine/releases)，选择最新正式版，下载目标平台的 SDK 压缩包：

| 目标平台 | 下载文件 | 说明 |
|----------|----------|------|
| Windows | `eve-sdk-win32-<版本>.zip` | Windows 10/11 x64 |
| Linux | `eve-sdk-linux-<版本>.zip` | Ubuntu 20.04+ 等（需 Vulkan 驱动） |
| macOS | `eve-sdk-macosx-<版本>.zip` | macOS 12+（Apple Silicon / Intel） |
| Android | `eve-sdk-android-<版本>.zip` | 在开发机上组装 arm64 APK |
| iOS | `eve-sdk-ios-<版本>.zip` | 在 macOS 开发机上组装 arm64 .app |

解压后即为一个独立的 SDK 目录，无需安装。以 Windows 为例：

```text
eve-sdk-win32/
├── bin/eve.exe          # 引擎运行时
├── lib/                 # Windows：插件链接库；macOS：运行时动态库
├── include/eve/         # 原生插件头文件（做纯脚本游戏用不到）
├── cmake/               # 原生插件 CMake 支持（做纯脚本游戏用不到）
├── platform/            # 目标平台打包模板（如 Android APK 工程）
└── share/eve/
    ├── VERSION          # 引擎版本号
    ├── TARGET_PLATFORM  # 目标平台标记
    ├── examples/basic/  # 可运行的参考游戏
    └── licenses/        # 引擎与第三方许可
```

### 2. 运行内置示例，验证安装

```sh
# Windows
bin\eve.exe run share\eve\examples\basic

# macOS / Linux
bin/eve run share/eve/examples/basic
```

### 3. 创建并运行你的第一个游戏

```sh
bin\eve.exe create mygame     # Windows（macOS / Linux：bin/eve create mygame）
bin\eve.exe run mygame
```

`eve create` 会生成最小的游戏模板：`config.nut`（窗口与开发选项）和 `main.nut`（游戏逻辑）。修改 `main.nut` 后保存即可看到热重载效果。游戏目录结构、生命周期回调（`eve_init` / `eve_update` / `eve_render`）、模块用法、调试与发布，详见[用户指南](docs/usr/README.md)。

### 拿到引擎后，你可以做这些事

- **制作 2D / 第三人称 3D / 2D+3D 混合游戏**：Tilemap、摄像机、Box2D/Box3D 物理、粒子、动画、RPG、背包、UI、程序化生成等 40+ 模块，全部通过 EveScript（兼容 Squirrel）调用；先阅读[EveScript 完整教程](docs/usr/EVESCRIPT.md)，逐个模块的接口见[模块使用手册](docs/usr/MODULES.md)。
- **脚本与资源热重载**：桌面端改完脚本/贴图立即生效；移动端用 `eve dev` 开发服务器把改动推送到真机，无需重装应用。
- **调试**：暂停游戏循环、断点、监视、快照，支持 VS Code 调试适配器与 AI（MCP）辅助开发。
- **打包发布**：`eve zip` 把游戏压缩为 `.eve` 归档；`eve package --sdk <SDK目录>` 生成包含运行时与游戏的可分发目录；Android / iOS 用 SDK 自带模板组装 APK / .app。
- **原生扩展**：需要 C++ 性能代码时，用 SDK 的 CMake 包编写插件动态库（见 `examples/native-plugin`），纯脚本用户完全不需要。

### 需要注意什么

1. **运行时需要 Vulkan 驱动，但不需要 Vulkan SDK**：引擎基于 Vulkan 渲染。Windows 的显卡驱动通常已自带 Vulkan 运行时；macOS 由 SDK 内置 MoltenVK 承担；Linux 需要安装 `mesa-vulkan-drivers`（AMD/Intel）或厂商驱动（NVIDIA）。安装 Git、CMake、编译器、LunarG SDK 只对“从源码构建”有意义。
2. **SDK 按平台独立、版本需匹配**：每个 SDK 只面向一个目标平台；原生插件必须与宿主平台及 SDK 版本一致，不同平台/版本的 SDK 不能混用。
3. **正式版与开发版**：Release 与 `main` 分支对应正式版；想尝鲜最新功能或修改引擎，请从源码构建，并基于 `dev` 分支开发。
4. **许可**：EVEngine 采用双许可，请在使用前阅读[许可证](#许可证)；商用营收超过免费门槛后需要商业授权。

## 从源码构建（引擎开发者 / 贡献者）

> 下面的环境要求与编译步骤只面向需要修改引擎源码、参与引擎开发或自行打包的开发者。普通游戏开发者请使用上方“快速开始”的预编译版本，无需阅读本节。

### 环境要求

#### 通用依赖

| 依赖 | 版本要求 | 说明 |
|------|----------|------|
| Git | 任意较新版本 | 拉取源码；首次配置时会自动下载 `third-party` |
| CMake | **≥ 3.21** | 主构建系统 |
| C++ 编译器 | 支持 **C++20** | MSVC / GCC / Clang |
| Vulkan SDK | **≥ 1.2** | 图形后端；需正确设置环境变量 |

第三方库（SDL2、Box2D、Squirrel、Poco、ImGui、OpenAL Soft、FreeType、GLM、VKBuilder 等）由 CMake 的 `ExternalProject` 自动从 [EVEngine/third-party](https://github.com/EVEngine/third-party) 拉取并编译，一般无需手动安装。

#### Windows

| 组件 | 推荐配置 |
|------|----------|
| 操作系统 | Windows 10 / 11（x64） |
| IDE / 工具链 | **Visual Studio 2026**（含「使用 C++ 的桌面开发」工作负载） |
| CMake 生成器 | `Visual Studio 18 2026`（与仓库根目录 `Makefile` 一致） |
| Vulkan SDK | 从 [LunarG](https://vulkan.lunarg.com/) 安装，安装后确认用户环境变量 `VULKAN_SDK`（例如 `C:\VulkanSDK\1.4.357.0`），并把 `%VULKAN_SDK%\Bin` 加入 `PATH`（含 `glslc`）。装完后请**新开终端 / 重启 Cursor** 再 cmake |
| 可选 | Git for Windows、将 CMake / Ninja 加入 `PATH` |

> 若使用 Visual Studio 2022，可将 `Makefile` 中的生成器改为 `"Visual Studio 17 2022"`。

#### Linux

| 组件 | 推荐配置 |
|------|----------|
| 发行版 | Ubuntu 20.04+ 或同等环境（含 WSL2） |
| 编译器 | GCC 10+ 或 Clang 12+ |
| 基础包 | `build-essential`、`cmake`、`git`、`ninja-build`、`pkg-config` |
| 系统库 | X11 / SDL2：`libx11-dev`、`libxext-dev`、`libxxf86vm-dev`、`libxrandr-dev`、`libxcursor-dev`、`libxi-dev`、`libxinerama-dev`、`libxss-dev`；可选 Wayland：`libxkbcommon-dev`、`libwayland-dev`；OpenAL/音频：`libasound2-dev`、`libpulse-dev`；图像压缩：`zlib1g-dev`、`libpng-dev`、`libbz2-dev` |
| Vulkan | `libvulkan-dev` + `glslang-tools`（提供 `glslc`），或按 LunarG 安装完整 Vulkan SDK |

Ubuntu / WSL 示例：

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
# 也可按 LunarG 官方文档安装完整 Vulkan SDK（含 validation layers）
```

#### macOS

| 组件 | 推荐配置 |
|------|----------|
| 操作系统 | macOS 12+（Apple Silicon / Intel） |
| 工具链 | **Xcode Command Line Tools**（`xcode-select --install`） |
| CMake | Homebrew：`brew install cmake`（≥ 3.21） |
| Vulkan | 从 [LunarG](https://vulkan.lunarg.com/) 安装 **macOS Vulkan SDK**（内含 **MoltenVK**），仅**构建/开发**需要；`dist/eve-sdk/macosx` 发布包已内置 Vulkan loader + MoltenVK，玩家机器无需再装 SDK |

配置环境（每次新开终端执行，或写入 `~/.zshrc`）：

```sh
# 将 <version> 换成本机 SDK 目录名，例如 1.4.357.0
source ~/VulkanSDK/<version>/setup-env.sh
# 确认：
echo "$VULKAN_SDK"
ls "$VULKAN_SDK/lib/libvulkan.dylib" "$VULKAN_SDK/lib/libMoltenVK.dylib"
```

`setup-env.sh` 会设置 `VULKAN_SDK`、`VK_ICD_FILENAMES`（MoltenVK ICD）等。CMake 在 Apple 宿主上**要求**能 `find_package(Vulkan)` 成功，不会回退到 Windows 的 `vulkan-1.lib`。

#### Android（arm64-v8a Debug APK）

| 组件 | 推荐配置 |
|------|----------|
| 宿主 | macOS / Linux（本仓库 Makefile 默认路径面向 macOS Homebrew） |
| JDK | **OpenJDK 17**（`brew install openjdk@17`） |
| Android SDK | command-line tools + `platform-tools` + `platforms;android-34` + `build-tools;34.0.0` |
| NDK | **26.1.10909125**（`sdkmanager "ndk;26.1.10909125"`） |
| CMake（SDK） | `cmake;3.22.1`（Gradle 可用；引擎本体用宿主机 CMake/Ninja 交叉编译） |
| ABI / minSdk | **arm64-v8a** / **24** |
| 设备 | 支持 **Vulkan** 的真机（模拟器未作为首版验收目标） |

环境变量示例（写入 `~/.zshrc` 或构建前 export）：

```sh
export JAVA_HOME="$(brew --prefix openjdk@17)/libexec/openjdk.jdk/Contents/Home"
export ANDROID_HOME="$HOME/Library/Android/sdk"
export ANDROID_NDK="$ANDROID_HOME/ndk/26.1.10909125"
export PATH="$JAVA_HOME/bin:$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$PATH"
```

在 `platform/android/apk/local.properties` 写入（**勿提交**）：

```text
sdk.dir=/Users/<you>/Library/Android/sdk
```

一键构建 Debug APK：

```sh
make build/android-debug
# 产物：platform/android/apk/app/build/outputs/apk/debug/app-debug.apk
```

流程：NDK CMake 交叉编译第三方 + `libmain.so` → `sync/android-libs` → `sync/android-assets` → Gradle `assembleDebug`。

默认 `ANDROID_GAME=demo`：把 `platform/android/game-shell/`（无 `main.nut`）同步进 APK，启动后走嵌入的 `eve.demoScript`。打包 example 目录：

```sh
make build/android-debug ANDROID_GAME=example
# 或仅刷新 assets：
make sync/android-assets ANDROID_GAME=example
```

APK 启动时将 `assets/game` 复制到应用内部目录，并以 `run <内部路径>` 进入现有 CLI。

真机安装 / 运行 / 日志：

```sh
adb devices
make install/android-debug
make run/android-debug
make log/android
```

#### iOS / iPadOS（arm64 真机，最低 13.0）

| 组件 | 推荐配置 |
|------|----------|
| 宿主 | macOS + **完整 Xcode**（不能只有 Command Line Tools；需 `iphoneos` SDK） |
| 部署目标 | **iOS / iPadOS 13.0+** |
| 架构 / 设备族 | **arm64**；`TARGETED_DEVICE_FAMILY=1,2`（iPhone + iPad） |
| Vulkan | LunarG SDK 自带的 `iOS/lib/MoltenVK.xcframework` |
| 签名 | Apple Development；设置 `IOS_DEVELOPMENT_TEAM=<TeamID>` |
| 验收设备 | 物理 **iPad**（iPhone 兼容）；模拟器非首版目标 |

一键构建 Debug `.app`：

```sh
# 已登录 Apple ID 时会自动探测 Team ID；也可手动指定：
#   security find-certificate -a -c "Apple Development" -p | openssl x509 -noout -subject
# 取其中的 OU=<TeamID>（注意证书名括号里的是证书 ID，不是 Team ID）
export IOS_DEVELOPMENT_TEAM=XXXXXXXXXX
make build/ios-debug
```

流程：Xcode 生成器配置 iOS 工具链 → 交叉编译第三方（SDL UIKit 静态库等）→ 链接/嵌入 MoltenVK → 打包 `example/` 到 `Resources/game` → 产出 `eve.app`。启动时若无 CLI 参数，会注入 `run <Resources/game>`。

真机安装 / 运行 / 日志：

```sh
# 用 USB 连接 iPad，开启开发者模式并信任此电脑
make install/ios-debug
make run/ios-debug
make log/ios
```

可选变量：`IOS_DEPLOYMENT_TARGET`（默认 13.0）、`IOS_BUNDLE_ID`（默认 `com.evengine.example`）、`VULKAN_SDK`。

排错要点：

1. **签名**：本机需在 Xcode → Settings → Accounts 登录 Apple ID，并有可用的 Apple Development 证书；`security find-identity -v -p codesigning` 应能看到证书。未设置 `IOS_DEVELOPMENT_TEAM` 时 `make build/ios-debug` 仍可产出未签名 `.app`，但无法安装到真机。若报 `No Account for Team "XXXX"`，说明用的是证书 ID 而非 Team ID（Team ID 是证书主题里的 `OU`）。
2. **设备**：USB 连接 iPad，开启开发者模式并信任此电脑；`xcrun devicectl list devices` 应显示 `connected`。若提示 `no DDI`，先用 Xcode 打开一次设备窗口以安装 Developer Disk Image。
3. **依赖**：第三方用 Ninja + `cmake/ios.toolchain.cmake` 交叉编译（SDL UIKit）；失败时可清 `build/third-party/ios-debug` 与 `build/third-party-binary/ios-debug` 后重跑。
4. **MoltenVK**：当前 LunarG xcframework 可能按 iOS 14+ 构建；真机系统需满足该要求。
5. 首版不做模拟器、App Store 分发与多架构 fat binary。

#### WSL（同时开发 Windows / Linux）

推荐在 **WSL2（Ubuntu）** 中开发 Linux 目标，在 Windows 主机侧用 Visual Studio 编译 Win32 目标。源码放在 Windows 文件系统（例如 `C:\Users\...\EVEngine`），通过 `/mnt/c/...` 挂载到 WSL。

要点：

1. Windows 侧安装 VS 2026 + Vulkan SDK；WSL 侧按上面的 Linux 依赖安装一套工具链与 Vulkan。
2. 在仓库根目录直接使用 `make`：Windows 目标走 `cmake.exe`，Linux 目标走 WSL 内的 `cmake`。
3. 第三方产物按平台分目录存放（如 `build/third-party/win32`、`build/third-party/linux-debug`），互不覆盖。
4. 若在 WSL 里编译位于 `/mnt/c` 的源码，I/O 可能较慢；追求速度可将仓库 clone 到 WSL 原生文件系统（如 `~/src/EVEngine`），Windows 侧再单独 clone 一份。

### 获取源码

```sh
git clone --recurse-submodules https://github.com/EVEngine/EVEngine
cd EVEngine
# 若已 clone 未带子模块：
# git submodule update --init --recursive
```

默认克隆的是 `main`（最新正式版）。参与引擎开发请基于 `dev`：`git checkout dev && git pull`。

首次 CMake 配置时会自动拉取 `third-party`；若目录已存在且含 `CMakeLists.txt`，则直接使用本地副本。ECS 使用子模块 [`external/ECS.hpp`](https://github.com/sunxfancy/ECS.hpp)；IK 使用子模块 [`external/ik.hpp`](https://github.com/sunxfancy/ik.hpp)。

### 快速构建（推荐：根目录 Makefile）

仓库根目录的 `Makefile` 封装了 Windows / Linux / macOS 的 Debug / Release 配置与编译。在已安装 `make` 的环境中（Git Bash、WSL、Linux、macOS）于仓库根目录执行：

```sh
# 当前宿主平台的 Debug / Release（Darwin → macosx，Linux → linux，Windows → win32）
make debug
make release

# 仅 Windows Release / Debug
make build/win32
make build/win32-debug

# 仅 Linux Release / Debug
make build/linux
make build/linux-debug

# 仅 macOS Release / Debug（需已 source Vulkan SDK setup-env.sh）
make build/macosx
make build/macosx-debug

# Android arm64 Debug APK（需 ANDROID_HOME / ANDROID_NDK / JAVA_HOME）
make build/android-debug
make install/android-debug
make run/android-debug
make log/android
```

产物目录约定：

| 目标 | 构建目录 | 说明 |
|------|----------|------|
| Win32 Release | `build/win32` | VS 工程 + Release 二进制 |
| Win32 Debug | `build/win32-debug` | Ninja + MSVC Debug 二进制 |
| Linux Release | `build/linux` | Unix Makefiles |
| Linux Debug | `build/linux-debug` | Unix Makefiles |
| macOS Release | `build/macosx` | Unix Makefiles + MoltenVK |
| macOS Debug | `build/macosx-debug` | Unix Makefiles + MoltenVK |
| Android Debug | `build/android-debug` + Gradle APK | NDK `libmain.so` → `app-debug.apk` |
| iOS Debug | `build/ios-debug`（Xcode） | `eve.app` + 嵌入 MoltenVK，可装 iPad/iPhone |

主程序可执行文件名一般为 `eve`（Windows 下为 `eve.exe`，路径随 VS 的 `Release` / `Debug` 配置子目录变化）。Android 为 SDLActivity 加载的 `libmain.so`（包名 `com.evengine.example`）。

### 分步编译说明

#### 1. 配置（Configure）

等价于 Makefile 中的配置步骤，也可手动执行。

**Windows（Visual Studio 2026，x64）：**

```sh
cmake.exe -G "Visual Studio 18 2026" -A x64 -DCMAKE_BUILD_TYPE=Release -B build/win32 -S .
# Debug：
cmake.exe -G "Visual Studio 18 2026" -A x64 -DCMAKE_BUILD_TYPE=Debug -B build/win32-debug -S .
```

**Linux：**

```sh
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -B build/linux -S .
# Debug：
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug -B build/linux-debug -S .
```

**macOS：**

```sh
source ~/VulkanSDK/<version>/setup-env.sh
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DBUILD_PLATFORM=macosx -B build/macosx -S .
# Debug：
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug -DBUILD_PLATFORM=macosx -B build/macosx-debug -S .
```

可选 CMake 变量：

| 变量 | 默认 | 说明 |
|------|------|------|
| `CMAKE_BUILD_TYPE` | `Debug`（未指定时） | `Debug` 或 `Release`；影响第三方库安装路径后缀（如 `win32-debug`） |
| `BUILD_PLATFORM` | 按宿主自动选择 | `win32` / `linux` / `macosx` 等 |
| `BUILD_TESTING` | `ON` | 是否编译单元测试 |

#### 2. 编译第三方依赖（deps）

第三方通过自定义目标 `deps` / `third-party` 构建。配置完成后：

```sh
# 以 Windows Debug 为例
cmake.exe --build build/win32-debug --target deps -j 32

# Linux
cmake --build build/linux-debug --target deps -j 32

# macOS
cmake --build build/macosx-debug --target deps -j 32
```

首次编译第三方耗时较长，产物位于 `build/third-party/<平台>[-debug]/` 与 `build/third-party-binary/<平台>[-debug]/`。之后增量构建会复用已安装结果。

#### 3. 编译引擎本体

```sh
# Windows（需指定 --config，因为是多配置生成器）
cmake.exe --build build/win32 --config Release -j 32
cmake.exe --build build/win32-debug --config Debug -j 32

# Linux
cmake --build build/linux -j 32
cmake --build build/linux-debug -j 32

# macOS
cmake --build build/macosx -j 32
cmake --build build/macosx-debug -j 32
```

也可直接用前文的 `make build/win32` / `make build/macosx-debug` 等目标一步完成配置 + 编译。

### 运行单元测试

默认开启 `BUILD_TESTING`。编译完成后：

```sh
make test/win32-debug    # 运行 build/win32-debug/test/Debug/unit_test.exe
make test/win32          # 运行 Release 测试
make test/linux-debug
make test/linux
make test/macosx-debug
make test/macosx
```

或直接执行对应路径下的 `unit_test` / `unit_test.exe`。

### Docker（可选）

仓库提供基于 Ubuntu 20.04 的 `Dockerfile`（含 `build-essential`、`cmake`、`clang-12` 等）。可用于隔离的 Linux 构建环境：

```sh
docker build . --tag evengine
docker run -it --rm --volume="$(pwd):/home/evengine/src" evengine /bin/bash
```

进入容器后，在挂载的源码目录按 Linux 流程配置并编译即可。注意容器内仍需自行准备 Vulkan 相关运行时/SDK（镜像默认未安装完整 Vulkan SDK）。

### 常见问题

1. **CMake 报找不到 Vulkan**  
   确认已安装 Vulkan SDK，且 `VULKAN_SDK` 指向正确路径；macOS 请 `source ~/VulkanSDK/<version>/setup-env.sh` 后再配置。重新打开终端后再 cmake。

2. **Windows 生成器名称不匹配**  
   `Makefile` 当前使用 `Visual Studio 18 2026`。若本机是 VS 2022，请改生成器字符串或升级 IDE。

3. **首次 `deps` 很慢或网络失败**  
   `third-party` 从 GitHub 拉取。可手动 `git clone https://github.com/EVEngine/third-party` 到仓库根目录的 `third-party/`，再重新配置。

4. **`CMAKE_BUILD_TYPE` 与第三方路径**  
   Debug / Release 使用不同的第三方安装目录（带 `-debug` 后缀）。切换构建类型后需重新跑对应目录下的 `deps`。

5. **并行度**  
   Makefile 默认 `-j 32`。机器核心较少时可改为 `-j$(nproc)`（Linux）/`-j$(sysctl -n hw.ncpu)`（macOS）或较小数字，避免内存不足。

6. **macOS 窗口/渲染失败**  
   如果报错是 `Installed Vulkan doesn't implement the VK_KHR_surface extension`（SDL 原文），说明运行时可用的 Vulkan 没有实现 surface 扩展，macOS 上即 MoltenVK 未被加载。**发布包**请确认把 `dist/eve-sdk/macosx` 的 `lib/` 一起发给玩家（内含 `libvulkan.1.dylib`、`libMoltenVK.dylib`、`MoltenVK_icd.json`），eve 启动时会自动指向这些文件（`platform/macosx` 的 `bootstrapBundledVulkan`），玩家无需安装 SDK。**开发构建**请确认 `VK_ICD_FILENAMES` 指向 MoltenVK ICD（`setup-env.sh` 会设置），且 SDK 的 `lib` 在 `DYLD_LIBRARY_PATH` 中。引擎通过 SDL2 Vulkan surface + MoltenVK 运行，无需单独写 Metal 后端。

7. **Android APK / 真机**  
   确认 NDK 版本与 Makefile 一致；`local.properties` 的 `sdk.dir` 正确；设备开启 USB 调试且 `adb devices` 可见。首版要求真机 Vulkan；无设备时至少验证 `make build/android-debug` 产出 APK。

8. **iOS / iPadOS 真机**  
   必须安装完整 Xcode（`xcode-select -p` 应指向 `Xcode.app/Contents/Developer`，且 `xcrun --sdk iphoneos --show-sdk-path` 可用）。设置 `IOS_DEVELOPMENT_TEAM`，用 USB 连接已信任的 iPad/iPhone。MoltenVK 来自 LunarG SDK 的 `iOS/lib/MoltenVK.xcframework`。

## 项目结构（简要）

```text
EVEngine/
├── CMakeLists.txt      # 主 CMake：平台检测、第三方、子目录
├── Makefile            # Windows / Linux / macOS / Android / iOS 快捷入口
├── platform/           # 平台相关代码与打包模板（含 android/apk、ios Info.plist）
├── src/
│   ├── engine/         # 引擎核心、DevTools、命令行（Android libmain / iOS eve.app）
│   ├── modules/        # 功能模块
│   └── scripts/        # 脚本侧集成
├── test/               # 单元测试
├── third-party/        # 自动下载的第三方源码（可本地预置）
├── cmake/              # 源码扫描等辅助脚本
└── docs/               # 用户文档（usr）与开发者文档（dev）
```

使用说明见 `docs/usr/`，引擎设计说明见 `docs/dev/`。

## 许可证

EVEngine 采用**双许可**：

| 用途 | 许可文件 | 费用 |
|------|----------|------|
| 开源项目；或项目年营收 ≤ 人民币 10 万元 | [LICENSE-OPENSOURCE](LICENSE-OPENSOURCE) | 免费 |
| 年营收超过人民币 10 万元 | [LICENSE-COMMERCIAL](LICENSE-COMMERCIAL) | 收费；超标后 **1 个月内** 须申请，否则不得继续使用 |

总说明见 [LICENSE](LICENSE)。商业授权咨询：`sunxfancy@gmail.com`。
