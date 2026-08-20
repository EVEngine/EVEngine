# EVEngine 用户指南

EVEngine 是一个以 Squirrel 脚本驱动的轻量游戏引擎，适合快速制作 2D、第三人称 3D，以及 2D/3D 混合的游戏原型。本指南面向“使用引擎制作游戏”的用户；如果你要修改引擎实现，请阅读[开发者文档](../dev/README.md)。

> 在线版文档（API + 用户手册）持续构建于 GitHub Pages：
> <https://evengine.github.io/EVEngine/>。

> 当前版本为早期开发版本。最可靠的学习方式是运行并修改仓库中的 `examples/`。

本目录不仅包含入门说明，还提供覆盖所有脚本模块的[模块使用手册](MODULES.md)，并按“完成某项游戏开发任务”的方式给出具体步骤。如果你已经能够运行示例，可以直接从模块目录按功能查阅。

引擎默认包含全部模块。只用到其中一部分时，可以按 [按需裁剪模块](TRIMMING.md) 在构建时把其余的裁掉。

## 1. 开始之前

桌面平台需要：

- Git；
- CMake 3.21 或更高版本；
- 支持 C++20 的编译器；
- Vulkan SDK 1.2 或更高版本（macOS 还需要 SDK 中的 MoltenVK）；
- `make`（Linux、macOS、WSL 或 Git Bash）。

首次构建会准备第三方依赖，因此耗时会比后续增量构建长。Linux 用户还需安装 X11、音频和 Vulkan 等开发包；完整的平台依赖和移动端工具链请参阅仓库根目录的 [`Readme.md`](../../Readme.md#环境要求)。

获取源码：

```sh
git clone --recurse-submodules https://github.com/EVEngine/EVEngine
cd EVEngine
```

默认克隆的是 `main`（最新正式版，可直接编译）。要改引擎请：

```sh
git checkout dev
git pull
```

如果克隆时没有拉取子模块：

```sh
git submodule update --init --recursive
```

## 2. 构建并运行第一个示例

在仓库根目录执行：

```sh
# 构建当前宿主平台的 Debug 版本
make debug

# 运行内置演示
make run

# 运行最适合作为起点的脚本示例
make basic
```

也可以明确指定桌面平台和游戏目录：

```sh
make run/linux-debug GAME=examples/basic
make run/macosx-debug GAME=examples/basic
make run/win32-debug GAME=examples/basic
```

常用快捷入口：

| 命令 | 内容 |
|---|---|
| `make basic` | Tilemap、Box2D、粒子与基础绘制 |
| `make ecs` | 脚本 ECS 与移动系统 |
| `make rpg` | 属性、状态、技能和战斗结算 |
| `make run/$(平台)-debug GAME=examples/procgen` | 程序化地图与纹理 |
| `make run/$(平台)-debug GAME=examples/roguelike-generator` | Roguelike 风格地牢：房间/走廊、地板图案、墙方向、装饰（2D/2.5D） |
| `make test` | 当前平台 Debug 测试 |

`$(平台)` 可替换为 `linux`、`macosx` 或 `win32`。Windows 的 Release 构建默认使用 Visual Studio 生成器，Debug 构建使用项目 Makefile 中配置的工具链。

## 3. 游戏目录是什么样的

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

运行它：

```sh
make run/linux-debug GAME=/绝对路径/my-game
# macOS 或 Windows 请替换目标名
```

也可以直接使用已经构建好的宿主程序。`run` 会切换到指定目录并从该目录读取 `config.nut`、`main.nut` 和资源：

```sh
build/linux-debug/src/engine/eve run /绝对路径/my-game
```

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

资源路径以游戏工作目录为基准。可以直接复制 `examples/basic`，逐步替换脚本和资源。

## 4. 生命周期与热重载

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

把一次性初始化放进 `eve_init`，把可重复执行的声明保留在顶层，并参考 `examples/basic/main.nut` 中的完整写法。

## 5. 常用模块

模块通常由 `eve` 表中的构造器创建；基础运行环境也会提供 `gfx`、`keyboard`、`map`、`particles`、`physics`、`ui` 等常用实例。请以示例实际使用的接口为准。

| 需求 | 入口或模块 | 示例 |
|---|---|---|
| 2D/3D 绘制、纹理、字体 | `gfx` / `eve.Graphics` | `examples/basic`、渲染测试 |
| 键盘、鼠标、触摸、手柄 | `keyboard`、`mouse`、`touch`、`joystick` | `examples/procgen`、`test/*_cpp.cpp` |
| 物理 | `physics` / `eve.Physics` | `examples/basic` |
| Tilemap | `map` / `eve.Map` | `examples/basic/maps` |
| 粒子 | `particles` / `eve.Particles` | `examples/basic/particles` |
| 声明式 UI | `ui` / `eve.UI` | `examples/basic/ui_demo.nut`、`ui_component.nut` |
| ECS | `eve.Component`、`eve.Entity`、`eve.System` | `examples/ecs` |
| RPG | `eve.RPG` | `examples/rpg` |
| 程序化生成 | `procgen` / `eve.Procgen` | `examples/procgen` |
| 文件、数据、事件 | `eve.Filesystem`、`eve.DataModule`、`eve.Event` | `test/filesystem.nut` 等 |
| 音频、动画、IK | `eve.Audio`、`eve.Animation`、`eve.IK` | 对应 `test/` 示例 |
| GPU 计算、张量 | `eve.Gpgpu`、`eve.TF` | `examples/basic/compute`、对应测试 |
| 原生扩展 | `eve.Plugins` | `examples/native-plugin` |

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

通过继承 `eve.Component` 声明数据，继承 `eve.Entity` 组合组件，再用 `eve.System` 查询并更新实体。可直接从 `examples/ecs/main.nut` 复制完整的可运行模板。

## 6. 调试

命令行调试模式支持暂停、断点、监视、快照，以及面向 AI Agent 的 MCP：

```sh
build/linux-debug/src/engine/eve run --debug /绝对路径/my-game
build/linux-debug/src/engine/eve run --debug --dap-port=4711 /绝对路径/my-game
build/linux-debug/src/engine/eve run --debug --mcp-port=7529 /绝对路径/my-game
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

VS Code 调试适配器位于 `tools/vscode-eve-debug/`；Cursor 等 Agent 可通过 `tools/eve-mcp/` 连接 `--mcp-port`（见[开发者文档：AI 与 MCP](../dev/AI与MCP支持.md)）。如果只需快速定位问题，先确认：

1. `config.nut` 和 `main.nut` 位于传给 `run` 的同一游戏目录；
2. 每帧绘制前调用了 `gfx.clear()`；
3. 创建物理、地图或粒子模块后，在 `eve_update` 中调用相应的 `update(dt)`；
4. 资源路径相对于游戏目录，而不是引擎仓库根目录；
5. Vulkan SDK、驱动和运行时环境变量有效。

## 7. 发布与原生插件

构建目标平台 SDK：

```sh
make sdk/linux-debug
make sdk/macosx-debug
make sdk/win32-debug
```

SDK 输出到 `dist/eve-sdk/<平台>/`。原生插件无需包含完整引擎源码，可使用 CMake 的 `find_package(EVEngine)` 和 `add_eve_plugin(...)`；完整模板见 `examples/native-plugin/`。插件必须与宿主平台及构建配置匹配。

移动端提供仓库级打包入口：

```sh
# Android arm64 Debug APK
make build/android-debug ANDROID_GAME=/绝对路径/my-game

# iOS/iPadOS Debug app（需要 macOS、Xcode 和签名配置）
make build/ios-debug IOS_GAME=/绝对路径/my-game
```

移动端的 SDK、NDK、签名和真机要求较严格，请在打包前阅读根目录 [`Readme.md`](../../Readme.md#androidarm64-v8a-debug-apk) 中的对应章节。

## 8. 下一步

建议按以下顺序学习：

1. 运行 `examples/basic`，修改背景色、重力和粒子配置；
2. 运行 `examples/ecs`，添加一个组件和系统；
3. 根据类型选择 `examples/rpg` 或 `examples/procgen`；
4. 查阅 `test/` 中按模块命名的脚本与 C++ 用例，了解尚未进入教程的 API；
5. 需要原生能力时再使用目标平台 SDK 和 `examples/native-plugin`。

本指南讲解稳定的使用流程，内部架构、设计取舍和实现进度统一维护在 [`docs/dev/`](../dev/README.md)，避免把尚未实现的设计稿误当作用户 API。
