本开发工具提供了可视化的对象检测、修改、调试功能
可以方便连接外部编辑器、启动 language server


## 组件检测器

可以检测引擎内所有组件、用户定义的脚本等
可以查看游戏对象的属性、资源、依赖关系等等


## 脚本调试器 / 动态切片（Slicer）

对我们的脚本可以调试执行，设置断点等。

出错时可用类似 **program slicer** 的动态后向切片，定位「哪些代码 / 渲染步骤导致了错误」：

1. **调用栈**：通过 Squirrel `sq_setnativedebughook` + `sq_stackinfos` 记录 Call/Return/Line
2. **数据流**：采样局部变量变更，建立 Def→Use 到达定值边；出错点再对相关变量记 Use
3. **后向切片**：从错误点（及可疑变量）沿数据依赖与控制前驱回溯，得到相关源码位置
4. **环形缓冲**：默认最多保留 10 万条事件；满员后每新增 1 条就丢掉最旧 1 条（可用 `setMaxEvents`）
5. **渲染流程**：`RenderFlow` 记录 Frame/Pass/Target/Bind/Draw；`Exception` 构造时标记 Error，可切片到出错时所在 Pass 与绑定的资源

### 暂停 / 断点 / Watch / Snapshot

引擎视为**无状态**：可恢复的游戏状态都在脚本变量里。`Debugger` + `Snapshot` + DAP 服务在 `--debug` 下启用。| 能力 | 说明 |
|------|------|
| Pause 键 | 帧级暂停：跳过 `eve_update`，仍渲染 / 泵事件（`load.nut`） |
| F5 | 暂停时继续执行到下一断点（`continueRun`） |
| F10 | 单步跳过：下一条语句，不进入函数（`stepOver`） |
| F11 | 单步进入：下一条语句，进入函数（`stepInto`） |
| F8 | 单步一帧（`stepFrame`，次要） |
| F6 / F7 | 保存 / 加载脚本状态快照 `eve_snapshot.json` |
| 断点 | `Debugger::setBreakpoint` 或 VS Code；命中时在 line hook 内阻塞；支持条件断点（`condition`）；行首次被执行时回传 DAP `breakpoint` 事件做验证 |
| 异常断点 | DAP `setExceptionBreakpoints` / VS Code BREAKPOINTS 面板「Script Errors」；错误经错误钩子（未捕获：精确停在抛出点，调用栈从抛出函数开始）或 `load.nut` catch（停在游戏脚本 catch 语句）路由到调试器，等价 Godot「Break on Error」 |
| 跳过所有断点 | `Debugger::setBreakpointsEnabled(false)` / `eve.dev.setBreakpointsEnabled(false)` |
| 变量 | 多帧作用域（Locals 按 `frameId`）+ Globals scope；table / array / class / instance / closure(upvalue) 可展开为变量树；可展开变量带 `__vscodeVariableMenuContext` 标记，VS Code 变量视图右键「查看实例」打开对象检查面板（webview 经 DAP `variables` 递归取子项） |
| 表达式求值 | DAP `evaluate` 支持完整 Squirrel 表达式（算术 / 调用 / 下标），在选中帧的 locals + roottable 环境中编译求值 |
| Watch | `addWatch` / DAP `evaluate`（仅 `context=watch` 注册为持久 watch）；求值失败时显示错误 |
| Snapshot | JSON 序列化标记根（或 `gameState` / `eve_state` / 启发式非引擎槽） |
| Profiler | line hook 计时：`eve.dev.profileReport()` / `profileClear()`（按函数统计调用次数、行数、耗时） |
| MCP / AI | `--mcp-port` 嵌入 MCP；`AiPanel` 会话日志；F9 切换 ImGui「AI / MCP」面板 |
| 运行时控制台 | `ConsolePanel`: print 捕获 + 级别化日志 + Squirrel REPL; F4 切换 ImGui「Console」面板 |

### 状态驱动 bug 复现（基线快照 + 步骤回放）

引擎视为无状态：恢复游戏状态 = 恢复脚本变量 + 原生 `IStateProvider`。因此一个 bug 场景可被描述为 **基线快照 + 一串逐帧输入/事件**，从基线重放这串事件即可确定性复现（前提是游戏逻辑与 RNG seed 确定）。

- **录制**：`eve.dev.beginScenario()` 抓基线快照并挂上事件队列观察钩子（`event::Event::setPollObserver`），`eve.dev.scenarioFrame()` 每帧标记帧边界，把该帧从事件队列消费到的键鼠/异步消息按帧记下；出错时 `notifyError` 会把错误报告与出错位置写进场景。`eve.dev.endScenario("scenario_boss.json")` 落盘。
- **回放**：`eve.dev.beginReplay("scenario_boss.json")` 恢复基线快照；随后每驱动一帧游戏逻辑前调 `eve.dev.replayFrame()` 把该帧记录的事件重新注入事件队列，直到 `eve.dev.replayRemaining()==0`。若同一错误再次抛出即复现成功，`eve.dev.replayErrorReport()` 可读出原错误报告。
- 脚本 API：`beginScenario / scenarioFrame / scenarioRecording / endScenario / cancelScenario / beginReplay / replayFrame / replayRemaining / replayErrorReport`。
- C++：`eve::dev::ScenarioRecorder`（`record / end / beginReplay / stageFrame`）。

> 局限：只序列化事件队列里字符串/整型载荷（指针载荷丢弃），因此**输入/异步事件驱动**的 bug 可复现；依赖非 `IStateProvider` 原生状态或外部时序（网络、墙钟）的 bug 仍需配合稳定触发条件近似复现。

### C++ API

- `eve::dev::CallGraph`：脚本事件 + `sliceBackward()` / `formatErrorReport()`
- `eve::dev::RenderFlow`：渲染 Pass/Bind/Draw 图 + 后向切片（实现 `eve::debug::IRenderTracer`）
- `eve::debug::rt*`（`common/RenderTrace.h`）：Graphics 侧零开销钩子（未安装 tracer 时为空操作）
- `eve::dev::Debugger`：pause / step / breakpoints / watches
- `eve::dev::Snapshot`：脚本状态 capture / restore / 文件
- `eve::dev::DebugAdapter`：DAP TCP（供 VS Code 插件连接）
- `eve::dev::McpServer`：MCP JSON-RPC TCP（AI Agent 测试 / 辅助开发）
- `eve::dev::AiPanel`: AI 会话日志；ImGui 绘制由 `ImGuiHostPanels.cpp` 注册（避免 EVDevTools 拉入 imgui.h）
- `eve::dev::ConsolePanel`: 运行时控制台环形缓冲（线程安全）+ Squirrel `print` 捕获 + REPL 求值；ImGui 绘制同样由 `ImGuiHostPanels.cpp` 注册
- `eve::dev::DevTool`：`attach` + `exposeScriptApi` + 可选 `startDap` / `startMcp`

### 脚本 API（`eve.dev`，仅 `--debug`）

```squirrel
eve.dev.togglePause();
eve.dev.continueRun();     // F5 — continue to next breakpoint (`resume`/`continue` are Squirrel keywords)
eve.dev.stepOver();        // F10 — next statement, skip calls
eve.dev.stepInto();        // F11 — next statement, enter calls
eve.dev.stepOut();         // run until caller (DAP Shift+F11)
eve.dev.stepFrame();       // F8 — one game frame
eve.dev.step();            // convenience: stepOver mid-script, else one frame
eve.dev.setBreakpoint("main.nut", 42);
eve.dev.addWatch("score");
eve.dev.setBreakOnError(true);   // Godot-style break on script errors
eve.dev.setBreakpointsEnabled(false);  // skip all breakpoints
eve.dev.reportError("boom");     // forward an error to the debugger / slicer
eve.dev.profileReport();         // function timing CSV
eve.dev.registerSource("gen.nut", "local x = 1");  // virtual source for compilestring
eve.dev.markStateRoot("gameState");
eve.dev.saveSnapshot("boss.json");
eve.dev.loadSnapshot("boss.json");

// AI / MCP (DevTools panel + agent session log)
eve.dev.ai.status();
eve.dev.ai.note("reached boss");
eve.dev.ai.toggleVisible();  // F9
// Runtime console / log / REPL
eve.dev.console.log("hello");       // level: info
eve.dev.console.info("hi");         // info
eve.dev.console.warn("careful");    // warn
eve.dev.console.error("boom");      // error
eve.dev.console.debug("trace");     // debug
eve.dev.console.eval("score + 1");  // Squirrel expression evaluation
eve.dev.console.recent(64);         // recent log entries
eve.dev.console.format(64);         // recent log text
eve.dev.console.clear();
eve.dev.console.toggleVisible();    // F4
```

### 使用方式

```bash
eve run --debug .
eve run --debug --dap-port=4711 .
eve run --debug --mcp-port=7529 .
eve run --debug --dap-port=4711 --mcp-port=7529 .
```

Cursor / Claude Desktop 通过 [`tools/eve-mcp`](../../../tools/eve-mcp/) 将 stdio MCP 桥到 `--mcp-port`。设计说明见 [AI与MCP支持.md](../../../docs/dev/AI与MCP支持.md)。

异常时 stderr 会打印脚本切片，以及（若有渲染事件）：

- Active passes（如 `RenderSystem2D` / `RenderSystem3D` / `ShadowPass`）
- Relevant render events（bind / draw / target）

### VS Code 插件

见 [`tools/vscode-eve-debug/`](../../../tools/vscode-eve-debug/)：`type: eve` 的 debug adapter，launch 时启动 `eve run --debug --dap-port`，attach 则连接已有进程。

桌面 Debug 构建链接 `EVDevTools`；Android / iOS 精简运行时不包含本模块（`rt*` 钩子保持空）。


## language server

可以创建自动提示功能等，方便代码编写
