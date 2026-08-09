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

引擎视为**无状态**：可恢复的游戏状态都在脚本变量里。`Debugger` + `Snapshot` + DAP 服务在 `--debug` 下启用。

| 能力 | 说明 |
|------|------|
| Pause 键 | 帧级暂停：跳过 `eve_update`，仍渲染 / 泵事件（`load.nut`） |
| F10 | 暂停时单步一帧 |
| F5 / F9 | 保存 / 加载脚本状态快照 `eve_snapshot.json` |
| 断点 | `Debugger::setBreakpoint` 或 VS Code；命中时在 line hook 内阻塞 |
| Watch | `addWatch` / DAP `evaluate`；读 local 或 roottable（含 `a.b`） |
| Snapshot | JSON 序列化标记根（或 `gameState` / `eve_state` / 启发式非引擎槽） |

### C++ API

- `eve::dev::CallGraph`：脚本事件 + `sliceBackward()` / `formatErrorReport()`
- `eve::dev::RenderFlow`：渲染 Pass/Bind/Draw 图 + 后向切片（实现 `eve::debug::IRenderTracer`）
- `eve::debug::rt*`（`common/RenderTrace.h`）：Graphics 侧零开销钩子（未安装 tracer 时为空操作）
- `eve::dev::Debugger`：pause / step / breakpoints / watches
- `eve::dev::Snapshot`：脚本状态 capture / restore / 文件
- `eve::dev::DebugAdapter`：DAP TCP（供 VS Code 插件连接）
- `eve::dev::DevTool`：`attach` + `exposeScriptApi` + 可选 `startDap`

### 脚本 API（`eve.dev`，仅 `--debug`）

```squirrel
eve.dev.togglePause();
eve.dev.setBreakpoint("main.nut", 42);
eve.dev.addWatch("score");
eve.dev.markStateRoot("gameState");
eve.dev.saveSnapshot("boss.json");
eve.dev.loadSnapshot("boss.json");
```

### 使用方式

```bash
eve run --debug .
eve run --debug --dap-port=4711 .
```

异常时 stderr 会打印脚本切片，以及（若有渲染事件）：

- Active passes（如 `RenderSystem2D` / `RenderSystem3D` / `ShadowPass`）
- Relevant render events（bind / draw / target）

### VS Code 插件

见 [`tools/vscode-eve-debug/`](../../../tools/vscode-eve-debug/)：`type: eve` 的 debug adapter，launch 时启动 `eve run --debug --dap-port`，attach 则连接已有进程。

桌面 Debug 构建链接 `EVDevTools`；Android / iOS 精简运行时不包含本模块（`rt*` 钩子保持空）。


## language server

可以创建自动提示功能等，方便代码编写
