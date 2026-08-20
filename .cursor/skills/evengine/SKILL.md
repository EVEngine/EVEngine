---
name: evengine
description: >-
  Builds, runs, and debugs EVEngine games: C++20/Vulkan engine with Squirrel
  scripting, cross-platform (Windows/Linux/macOS/Android/iOS/WebGPU). Use when
  working with this repository, writing or editing Squirrel game scripts, running
  examples, connecting the eve-mcp bridge to a live engine session, or when the
  user asks an AI assistant to create or iterate on an EVEngine game.
disable-model-invocation: true
---

# EVEngine

EVEngine 是一个 C++20 / Vulkan 游戏引擎，游戏逻辑用 Squirrel 脚本编写。
本 skill 让 AI 助手能直接构建、运行、改脚本、调试游戏。

## 快速开始（Windows / Linux / macOS）

```sh
# 已克隆仓库时
make debug                # 构建当前宿主平台 Debug（首次会编译第三方依赖，较慢）
make run                  # 运行内置演示（无 main.nut 时自动回退）
make basic                # 运行 examples/basic（tilemap + Box2D + 粒子）
make run/win32-debug GAME=examples/rpg   # 指定平台与示例
```

头一次构建慢是正常的（SDL2/Poco/OpenAL 等从源码编译）。增量构建很快。

## 游戏目录结构

一个游戏就是带脚本的目录，例如 `examples/basic/`：

- `config.nut`：窗口尺寸、标题、热重载等配置（`config = { width=..., height=..., title=..., hotReload=true }`）
- `main.nut`：入口脚本，定义 `eve_init()`（初始化）与 `eve_update(dt)`（每帧）
- 资源相对脚本路径读取（文件系统模块提供统一虚拟路径）

改脚本保存后，热重载会自动生效（`hotReload=true` 时）。

## 常用脚本 API（模块化）

引擎能力按模块提供，完整手册见 `docs/usr/modules/*.md`。常用入口：

```squirrel
eve_init = function() {
    local g = eve.Graphics();          // 2D/3D 绘制
    local w = eve.Window();            // 窗口
    local p = eve.Physics();           // Box2D / Box3D
    local m = eve.Map();               // tilemap
    local parts = eve.Particles();
}
eve_update = function(dt) { /* 每帧逻辑 */ }
```

先查 `docs/usr/MODULES.md` 找模块，再读对应模块文档确认调用约定；写脚本前用
`has_module("slot")` 判断模块是否启用（裁剪构建时）。

## 运行测试

```sh
make test/win32-debug FILTER=math.*     # 按前缀过滤
make test/linux-debug                   # 全量（约 1500+ 用例）
```

注意：测试二进制在 Release 下会保留 REQUIRE/CHECK（见 `test/CMakeLists.txt` 的
`-UZEROERR_NO_ASSERT`），不要在测试里把带副作用的调用写进 REQUIRE 里。

## 连接 AI（本地 MCP）

仓库自带 `tools/eve-mcp`：stdio MCP 桥，直连**正在运行**的引擎会话。

```sh
eve run --debug --mcp-port=7529 examples/basic
```

然后在 Codex / Cursor / Claude Desktop 配置 MCP 服务器（详见
`tools/eve-mcp/README.md`）：

```json
{ "mcpServers": { "evengine": {
    "command": "node",
    "args": ["tools/eve-mcp/server.js"],
    "env": { "EVE_MCP_HOST": "127.0.0.1", "EVE_MCP_PORT": "7529" } } } }
```

可用工具：`eve_status` / `eve_eval` / `eve_pause` / `eve_snapshot_capture` /
`eve_error_slice`。

## 平台注意事项

- **Windows**：需要 Visual Studio（含 C++ 工作负载）与 Vulkan SDK；构建走
  `cmake\with-msvc.cmd`（vcvars + Ninja）。
- **Linux（无头/CI）**：跑 GUI 需要
  `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json`、
  `XDG_RUNTIME_DIR=/tmp/xdg-runtime`、`ALSOFT_DRIVERS=null`，并用
  `xvfb-run -a` 包裹。
- **macOS**：需要 LunarG Vulkan SDK（MoltenVK）；发布/打包注意运行时 dylib。
- **多 agent 协作（本机 Mac mini）**：主仓库共享在
  `~/Workspace/Agents/EVEngine`，使用前先 `~/Workspace/Agents/new-worktree.sh <名字>`
  建独立 worktree，详见本机 `.local-debug/CI-DEBUG-PLAYBOOK.md`（勿提交）。

## 参考

- 用户指南：`docs/usr/README.md`
- 模块手册索引：`docs/usr/MODULES.md`
- 构建与测试命令：根目录 `Readme.en.md` 与 `Makefile`
- 发布流程：`docs/dev/发布流程.md`
