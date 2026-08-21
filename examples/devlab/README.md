# DevLab — 开发者体验实验室

把 EVEngine「运行时即编辑器」的开发循环浓缩成一个 5 分钟可跑的示例。
**必须用 `--debug` 运行**（`make devlab` 已带该参数），否则 DevTools 不加载。

## 运行

```bash
make devlab
# 等价于：make run/<platform>-debug GAME=examples/devlab RUN_ARGS=--debug
```

## 体验路线（按顺序）

1. **F4 运行时控制台 / REPL**：启动时脚本已写入日志（`eve.dev.console.info`）。
   按 F4 打开控制台，输入 `eve.dev.console.eval("lab.count")` 回车，可读到当前帧计数；
   也可以在控制台直接改状态，比如 `eve.dev.console.eval("lab.vx = -200.0")`，小球立刻反向。
2. **热重载**：打开 `main.nut`，改 `bounceDir` 的返回值（例如 `0.85` → `1.0`），保存。
   控制台与 HUD 都会显示 `script hot-reloaded`，小球运动立即按新逻辑变化——不用重启引擎。
3. **F6/F7 状态快照**：按 F6 保存当前脚本状态；让小球继续跑几秒；按 F7 恢复。
   帧计数与小球位置会回到保存点——这是 AI 测试和「改代码→复位→重跑」闭环的基础。
4. **Pause 暂停循环**：按 Pause 暂停游戏循环（HUD 显示 `[PAUSED]`），F8 单帧、F10/F11 语句级单步、F5 继续。
5. **错误切片**：按空格或点「触发运行时错误」。`--debug` 下错误会停在抛出点
   （break on error），并在控制台 / DAP / MCP 生成错误切片，指出是哪一行、哪个调用路径导致的。
6. **F9 AI / MCP 面板**：按 F9 打开面板，可看到本示例写入的 session note。
   配合 `eve run --debug --mcp-port=7529`，Cursor / Claude 等 Agent 可以像人一样暂停、求值、
   截图、改场景、复位状态（见 [`docs/dev/AI与MCP支持.md`](../../docs/dev/AI与MCP支持.md)）。

## 它展示了什么

| 设计目标 | 对应体验 |
|---|---|
| 开发系统 = 游戏运行时 | 调试器、控制台、快照与游戏共用同一个进程 |
| 内置热重载 | 改脚本函数保存即生效，C++ 实体不重建 |
| 暂停游戏循环调试 | Pause / F5 / F8 / F10 / F11 与 DAP |
| 运行快照 | F6 保存 / F7 恢复，供人机闭环测试 |
| AI 原生控制面 | F9 面板 + MCP 工具（求值、快照、截图、场景操作） |
