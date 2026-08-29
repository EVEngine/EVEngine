# AI Game — 让 AI Agent 驾驶一个真实游戏

一个最小但真实的 2D 对战游戏，专为展示 EVEngine 的 **AI 原生控制面**：
Agent 不是"改文件"，而是像人一样**直接驾驶运行中的游戏**——读状态、改数值、
拍照验证、快照复位、暂停单步。

## 启动

```bash
make run/<platform>-debug GAME=examples/ai-game RUN_ARGS="--debug --mcp-port=7529"
# 或直接：eve run --debug --mcp-port=7529 examples/ai-game
```

## Agent 工作流（MCP 工具）

| 步骤 | 工具 | 参数 | 效果 |
|---|---|---|---|
| 附着 | `eve_status` | — | 确认端口 / 暂停状态 / callgraph |
| 读状态 | `eve_eval` | `{"expression": "game.enemy.hp"}` | 返回当前敌人 HP |
| 改状态 | `eve_run_script` | `{"source": "game.setEnemyHp(20.0);"}` | 把敌人削弱到 20 HP |
| 验证 | `eve_eval` | `{"expression": "game.enemy.hp"}` | 确认已生效 |
| 看见画面 | `eve_screenshot` | `{"path": "ai_game.png"}` | 保存当前帧 PNG |
| 设检查点 | `eve_snapshot_capture` | — | 返回脚本状态 JSON |
| 搞坏再复位 | `eve_run_script` → `eve_snapshot_restore` | 上一步的 JSON | 状态回到检查点 |
| 暂停 / 单步 | `eve_pause` / `eve_step_frame` / `eve_continue` | — | 控制游戏循环 |

配合 `eve_render_describe`（视觉模型）与 `eve_error_slice`（错误切片），
Agent 可以"看见"画面、定位问题、自动修复再验证——这是通用代码助手给不了的闭环。

## 一键复现（无需 LLM）

先启动游戏（上面的命令），再运行驱动脚本：

```bash
python examples/ai-game/agent_demo.py 7529
```

脚本会走完一遍"截图 → 暂停 → 读 → 改 → 验证 → 快照 → 破坏 → 复位 → 再验证 → 继续"的完整闭环，
全部断言通过后输出 `PASS`。

> 说明：截图在暂停前拍摄（暂停时循环不再呈现新帧）；依赖引擎屏幕 readback，
> 个别平台暂不可用时驱动脚本会打印警告并继续——核心闭环（读 / 改 / 快照 / 复位）始终严格断言。

## 脚本里有什么

`main.nut` 暴露一个纯数据的根表 `game`：

- 可读：`game.tick` / `game.time` / `game.player.hp` / `game.enemy.hp` 等；
- 可改：`game.setEnemyHp(v)` / `game.setPlayerHp(v)` / `game.reset()`；
- 快照：整个根表状态可被 `eve_snapshot_*` 捕获与恢复。

任何 Agent（Cursor / Claude / Codex）通过 `tools/eve-mcp` 桥或直接 TCP
连接这个端口即可驾驶；热重载照常生效——Agent 改完 `main.nut` 保存，
正在运行的这局游戏立即按新逻辑继续。
