# EVEngine 用户指南

EVEngine 是一个以 Squirrel 脚本驱动的轻量游戏引擎，适合快速制作 2D、第三人称 3D，以及 2D/3D 混合的游戏原型。本指南面向“使用引擎制作游戏”的用户；如果你要修改引擎实现，请阅读[开发者文档](https://github.com/EVEngine/EVEngine/blob/main/docs/dev/README.md)。

> 在线版文档（API + 用户手册）持续构建于 GitHub Pages：
> <https://evengine.github.io/EVEngine/>。

> 当前版本为早期开发版本，API 仍在演进。最可靠的学习方式是运行并修改示例：下载的 SDK 自带 `share/eve/examples/basic/`，仓库中还有更多 `examples/` 可参考。

本指南从“下载引擎”讲到“打包发布”；覆盖全部脚本模块的[模块使用手册](MODULES.md)按功能逐章说明 API 与示例。先跑通一个示例，再按需查阅对应模块即可。

## 1. 获取引擎：从官网下载，无需编译

<b>做游戏不需要编译引擎。</b> 到[官网发布页](https://github.com/EVEngine/EVEngine/releases)下载目标平台的 SDK 压缩包即可：

| 目标平台 | 下载文件 | 说明 |
|----------|----------|------|
| Windows | `eve-sdk-win32-<版本>.zip` | Windows 10/11 x64 |
| Linux | `eve-sdk-linux-<版本>.zip` | Ubuntu 20.04+ 等（需 Vulkan 驱动） |
| macOS | `eve-sdk-macosx-<版本>.zip` | macOS 12+（Apple Silicon / Intel，内置 MoltenVK） |
| Android | `eve-sdk-android-<版本>.zip` | 在开发机上组装 arm64 APK |
| iOS | `eve-sdk-ios-<版本>.zip` | 在 macOS 开发机上组装 arm64 .app |

解压后即为一个独立的 SDK 目录，<b>无需安装</b>。它包含：

- `bin/eve(.exe)`：引擎运行时（桌面平台）；
- `share/eve/examples/basic/`：可直接运行的参考游戏；
- `platform/`：目标平台打包模板（Android APK 工程等）；
- `share/eve/licenses/`：引擎与第三方许可文本；
- `include/`、`lib/`、`cmake/`：原生插件开发文件（做纯脚本游戏用不到）。

<b>要求只有一条：桌面运行时需要 Vulkan 驱动</b>（显卡驱动通常已自带；macOS 由 SDK 内置 MoltenVK 承担；Linux 需安装 `mesa-vulkan-drivers` 或厂商驱动）。不需要安装 Git、CMake、C++ 编译器或 LunarG Vulkan SDK。

验证安装：

```sh
# Windows
bin\eve.exe run share\eve\examples\basic

# macOS / Linux
bin/eve run share/eve/examples/basic
```

能正常打开窗口并看到示例画面，说明引擎可用。下面命令中的 `eve` 均指你解压目录里的 `bin/eve`（Windows 为 `bin\eve.exe`）。

想先直观感受引擎的渲染效果？可以浏览[示例渲染画廊](../images/examples/README.md)——其中每张截图都由引擎把交换链上实际呈现的一帧原样输出。

## 2. 创建并运行你的第一个游戏

```sh
eve create mygame     # 在当前目录生成 mygame/
eve run mygame        # 运行它
```

一个最小游戏只需要两个文件：

```text
my-game/
├── config.nut    # 窗口与开发选项
└── main.nut      # 游戏逻辑
```

`config.nut`：

```squirrel
config <- {
    width = 960
    height = 540
    title = "My EVEngine Game"
    debug = true
    hotReload = true
};
```

`main.nut`：

```squirrel
local x = 100.0;

// 游戏启动时调用一次。
eve_init <- function() {
    gfx.setBackgroundColor(0.08, 0.10, 0.16, 1.0);
};

// 每帧更新；dt 是距上一帧的秒数。
eve_update <- function(dt) {
    x += 80.0 * dt;
    if (x > config.width) x = -48.0;
};

// 每帧绘制。
eve_render <- function() {
    gfx.clear();
    gfx.drawSolidRect(x, 220.0, 48.0, 48.0, 0.3, 0.75, 1.0, 1.0);
};
```

运行后修改 `main.nut` 并保存，即可看到热重载效果。也可以直接复制 `share/eve/examples/basic/`，逐步替换脚本和资源。

### 推荐的目录布局

```text
my-game/
├── config.nut
├── main.nut
├── scripts/       # 拆分后的脚本，通过 dofile(...) 加载
├── maps/          # Tiled JSON 或地图数据
├── particles/     # 粒子 JSON
├── textures/
├── fonts/
├── audio/
└── models/
```

资源路径以游戏工作目录为基准。

## 3. 生命周期与热重载

引擎识别以下回调：

| 回调 | 何时调用 | 适合做什么 |
|---|---|---|
| `eve_init()` | 游戏载入后一次 | 创建世界、实体、资源和 UI |
| `eve_update(dt)` | 每个运行帧 | 输入、物理、AI 与状态更新 |
| `eve_render()` | 每个绘制帧 | 清屏并提交地图、精灵、UI 等绘制 |
| `eve_reload()` | 脚本软重载后（可选） | 调整已存在的运行时状态 |
| `eve_asset_reload(path)` | 非脚本资源变化后（可选） | 响应纹理、地图等资源变化 |
| `eve_quit()` | 退出时（可选） | 保存数据或执行清理 |

启用 `config.hotReload` 后，引擎会监视脚本和资源。热重载会再次执行脚本，因此应避免无条件重置全局状态。推荐模式：

```squirrel
if (!("player" in getroottable())) player <- null;

function eve_init() {
    if (player == null) {
        // 只创建一次；重载时保留对象引用。
        player = createPlayer();
    }
}
```

把一次性初始化放进 `eve_init`，把可重复执行的声明保留在顶层，并参考 `share/eve/examples/basic/main.nut` 中的完整写法。

## 4. 你能用引擎做哪些事

引擎默认包含全部模块，脚本侧统一放在全局 `eve` 表中；常用模块也会直接提供 `gfx`、`keyboard`、`map`、`particles`、`physics`、`ui` 等全局实例。模块通常由 `eve` 表中的构造器创建，请以示例实际使用的接口为准。

| 需求 | 入口或模块 | 示例 |
|---|---|---|
| 2D/3D 绘制、纹理、字体 | `gfx` / `eve.Graphics` | `share/eve/examples/basic`、渲染测试 |
| 键盘、鼠标、触摸、手柄 | `keyboard`、`mouse`、`touch`、`joystick` | 仓库 `examples/procgen`、`test/*_cpp.cpp` |
| 物理 | `physics` / `eve.Physics` | `share/eve/examples/basic` |
| Tilemap | `map` / `eve.Map` | `share/eve/examples/basic/maps` |
| 粒子 | `particles` / `eve.Particles` | `share/eve/examples/basic/particles` |
| 声明式 UI | `ui` / `eve.UI` | `share/eve/examples/basic/ui_demo.nut`、`ui_component.nut` |
| ECS | `eve.Component`、`eve.Entity`、`eve.System` | 仓库 `examples/ecs` |
| RPG | `eve.RPG` | 仓库 `examples/rpg` |
| 程序化生成 | `procgen` / `eve.Procgen` | 仓库 `examples/procgen` |
| 文件、数据、事件 | `eve.Filesystem`、`eve.DataModule`、`eve.Event` | 仓库 `test/filesystem.nut` 等 |
| 音频、动画、IK | `eve.Audio`、`eve.Animation`、`eve.IK` | 仓库对应 `test/` 示例 |
| GPU 计算、张量 | `eve.Gpgpu`、`eve.TF` | 仓库 `examples/basic/compute`、对应测试 |
| 原生扩展 | `eve.Plugins` | 仓库 `examples/native-plugin` |

每个模块的详细 API、最小示例与目标导向任务见[模块使用手册](MODULES.md)。

### UI 最小示例

```squirrel
eve_init <- function() {
    ui.beginBuild();
    ui.beginWindow("Status", "root");
    ui.text("Ready", "message");
    ui.button("Start", "start");
    ui.end();
    ui.mountBuildAs("hud");
};

eve_update <- function(dt) {
    local id = ui.consumeClick();
    while (id != "") {
        if (id == "hud/start") ui.setText("message", "Running");
        id = ui.consumeClick();
    }
};

eve_render <- function() {
    gfx.clear();
    ui.beginFrameAndRender();
};
```

### ECS 最小思路

通过继承 `eve.Component` 声明数据，继承 `eve.Entity` 组合组件，再用 `eve.System` 查询并更新实体。可直接从仓库 `examples/ecs/main.nut` 复制完整的可运行模板。

### 命令行速查

SDK 中的 `eve` 可执行文件本身就是命令行工具：

| 命令 | 作用 |
|---|---|
| `eve create <名字>` | 从模板创建新游戏 |
| `eve run [目录]` | 运行游戏（不带参数运行当前目录；无游戏时进入内置演示） |
| `eve run --debug` | 调试模式：暂停、断点、监视、快照 |
| `eve run --dap-port=4711` | 启动 VS Code 调试适配器服务 |
| `eve run --mcp-port=7529` | 启动 MCP 服务，供 AI 代理接入 |
| `eve dev [--port 8765]` | 启动热重载开发服务器（移动端真机热更新用） |
| `eve zip <目录>` | 把游戏压缩为 `.eve` 归档 |
| `eve package <目录> -o <输出> --sdk <SDK目录>` | 打包为含运行时的可分发游戏目录 |
| `eve test` | 运行游戏目录中的测试配置 |
| `eve doc <名字>` | 查询在线文档 |
| `eve build` | 从源码构建（需要源码与工具链，见[开发者文档](https://github.com/EVEngine/EVEngine/blob/main/docs/dev/README.md)） |

## 5. 调试

调试模式支持暂停、断点、监视、快照，以及面向 AI Agent 的 MCP：

```sh
eve run --debug /绝对路径/my-game
eve run --debug --dap-port=4711 /绝对路径/my-game
eve run --debug --mcp-port=7529 /绝对路径/my-game
```

脚本中可在调试模式下使用：

```squirrel
eve.dev.togglePause();
eve.dev.setBreakpoint("main.nut", 42);
eve.dev.addWatch("score");
eve.dev.markStateRoot("gameState");
eve.dev.saveSnapshot("boss.json");
eve.dev.loadSnapshot("boss.json");
eve.dev.ai.note("boss phase");  // DevTools AI 会话日志；F9 切换面板
```

> 想 5 分钟上手上面这些能力？源码仓库里的 `examples/devlab` 是配套的开发者体验示例：
> 在仓库根目录执行 `make devlab`（自动带 `--debug`），依次按 F4（控制台/REPL）、
> F6/F7（快照）、F9（AI 面板）、Pause（暂停）即可体验「运行时即编辑器」的开发循环。

VS Code 调试适配器位于仓库 `tools/vscode-eve-debug/`；Cursor 等 Agent 可通过 `tools/eve-mcp/` 连接 `--mcp-port`（见[开发者文档：AI 与 MCP](https://github.com/EVEngine/EVEngine/blob/main/docs/dev/AI与MCP支持.md)）。如果只需快速定位问题，先确认：

1. `config.nut` 和 `main.nut` 位于传给 `run` 的同一游戏目录；
2. 每帧绘制前调用了 `gfx.clear()`；
3. 创建物理、地图或粒子模块后，在 `eve_update` 中调用相应的 `update(dt)`；
4. 资源路径相对于游戏目录，而不是 SDK 根目录；
5. Vulkan 驱动和运行时环境有效。

## 6. 打包发布

### 桌面平台

```sh
# 1. 压缩游戏为 .eve 归档（可选，便于分发单个文件）
eve zip mygame

# 2. 生成包含运行时 + 游戏的可分发目录
eve package mygame -o mygame-package --sdk <SDK目录>
# Windows 产物含 eve.exe 与运行时 DLL；macOS / Linux 为 eve 与相关动态库
```

`eve package` 需要指定与你当前 SDK 同版本、同平台的 SDK 目录（不传时自动从 `bin/eve` 所在位置推断）。把生成的目录整体发给玩家即可运行，玩家无需安装引擎。

### 移动端真机热更新（`eve dev`）

应用包内资源只读，无法直接改设备文件。在<b>开发机</b>上编辑，由设备拉取：

```sh
# 1. 开发机上启动开发服务器（服务当前游戏目录）
eve dev --port 8765

# 2. 设备端 config.nut 配置开发机地址（或命令行注入）
config.devServer = "http://192.168.1.5:8765"   # 开发机局域网 IP
```

设备端会按清单轮询 `eve dev`，把变更文件下载到可写覆盖目录并复用本地热更新管线（脚本 `dofile`、资源 `tryReload`），无需重装应用。命令行也可用 `eve run --dev-server http://192.168.1.5:8765`。

### Android / iOS

下载对应平台的 SDK，用其自带模板在开发机上组装安装包：

```sh
# Android：把游戏脚本放进 SDK 的 platform/apk/app/src/main/assets/game/，
# 然后执行 SDK 内的 Gradle 工程生成 APK（需要 Android SDK/NDK）
# iOS：用 SDK 的 platform/ios 模板在 Xcode 中生成 .app（需要 macOS、Xcode 与签名）
```

移动端的 SDK、NDK、签名和真机要求较严格，请在打包前阅读根目录 [Readme.md 的 Android 章节](https://github.com/EVEngine/EVEngine/blob/main/Readme.md#androidarm64-v8a-debug-apk)。

## 7. 需要注意的事项

1. <b>无需编译，但需要 Vulkan 驱动</b>：运行时基于 Vulkan。Windows 显卡驱动通常自带 Vulkan 运行时；macOS 由 SDK 内置 MoltenVK；Linux 需要安装 Vulkan 驱动（`mesa-vulkan-drivers` 或厂商驱动）。如果游戏启动后窗口黑屏或报 Vulkan 初始化失败，优先检查驱动。
2. <b>SDK 按平台独立、版本必须匹配</b>：每个 SDK 只面向一个目标平台；`eve package`、原生插件必须与 SDK 的平台、版本一致，不同平台的 SDK 不能混用。
3. <b>脚本 API 以“模块手册 + 示例”为准</b>：C++ 头文件中的 public 方法不一定是脚本 API；调用约定见[API 使用约定](../dev/API-CONVENTIONS.md)。
4. <b>热重载会重新执行脚本</b>：用 `if (!("x" in getroottable()))` 保护需要跨重载保留的全局引用（见第 3 节）。
5. <b>模块可以裁剪，但默认全开</b>：脚本里用 `has_module("slot")` 判断模块是否存在；构建时裁剪只对“从源码构建”的用户有效，下载的 SDK 默认包含全部模块（见[按需裁剪模块](TRIMMING.md)）。
6. <b>许可</b>：EVEngine 采用双许可，商用营收超过免费门槛后需商业授权，详见根目录 [许可证](https://github.com/EVEngine/EVEngine/blob/main/Readme.md#许可证)。
7. <b>版本与分支</b>：Release 与 `main` 分支对应正式版；早期开发版 API 可能变化，升级 SDK 后请先运行内置示例回归。

## 8. 从源码构建 / 修改引擎（可选）

只有需要修改引擎源码、参与开发或自行打包时才需要从源码构建：环境要求、编译步骤见根目录 [Readme.md 的“从源码构建”章节](https://github.com/EVEngine/EVEngine/blob/main/Readme.md#从源码构建引擎开发者--贡献者)；架构与设计文档见[开发者文档](https://github.com/EVEngine/EVEngine/blob/main/docs/dev/README.md)。

## 9. 下一步

建议按以下顺序学习：

1. 运行 `share/eve/examples/basic/`，修改背景色、重力和粒子配置；
2. 用 `eve create` 建新游戏，熟悉 `eve_init` / `eve_update` / `eve_render` 与热重载；
3. 按需求查阅[模块使用手册](MODULES.md)（UI、ECS、RPG、程序化生成等）；
4. 需要原生能力时，再使用 SDK 的 CMake 包编写插件（见仓库 `examples/native-plugin`）；
5. 发布前阅读第 6 节打包步骤与许可条款。

本指南讲解稳定的使用流程，内部架构、设计取舍和实现进度统一维护在 [`docs/dev/`](https://github.com/EVEngine/EVEngine/blob/main/docs/dev/README.md)，避免把尚未实现的设计稿误当作用户 API。
