# AI Game — 让 AI Agent 驾驶一个真实游戏

一个最小但真实的 2D 对战游戏，专为展示 EVEngine 的 **Play Host**：
Agent 通过 `game.agent.json` 声明观察面，再用 `eve_play` 暂停、推进帧、观察、截图、checkpoint 复位。
不要把 `eve_eval` 当作官方读状态路径。

## 启动

```bash
make run/<platform>-debug GAME=examples/ai-game RUN_ARGS="--debug --mcp-port=7529"
# 或直接：eve run --debug --mcp-port=7529 examples/ai-game
```

## Agent 工作流（MCP `eve_play`）

| 步骤 | `op` | 效果 |
|---|---|---|
| 发现契约 | `status` | 确认 `game.agent.json`、暂停状态、gameplay domains |
| 看见画面 | `capture` | 引擎 readback PNG |
| 暂停 | `clock` `mode=pause` | 冻结 host clock |
| 读状态 | `observe` `observation=combat-alive` | 契约声明的 `gameState` 字段 |
| 推进 | `step` `clock=frame` `count=N` | 跑 N 帧后暂停 |
| 检查点 | `checkpoint` `mode=capture\|restore` | 标记脚本根快照 |

完整请求对象的 `schemaId` 为 `evengine.play-request`，`schemaVersion` 为 `1`。底层工具 `eve_pause` / `eve_eval` 仍可用，但本示例的验收不依赖它们。

示例同时使用 `eve_agent_session_*` 把这些调用组织为版本化开发会话。开始时声明状态观察、快照恢复和
视觉证据三项验收条件；只有 required 条件都有证据，且阶段已经走到 Verify，才允许 Complete。

## 一键复现（无需 LLM）

先启动游戏（上面的命令），再运行驱动脚本：

```bash
python examples/ai-game/agent_demo.py 7529
```

脚本会走完「截图 → 暂停 → 观察 → checkpoint → 推进入口帧 → 观察 tick 前进 → 恢复 → tick 回退」的闭环，
全部断言通过后输出 `PASS`。

> 说明：截图在暂停前拍摄（暂停时循环不再呈现新帧）；依赖引擎屏幕 readback，
> 个别平台暂不可用时驱动脚本会打印警告并继续——核心闭环（观察 / 步进 / 快照 / 复位）始终严格断言。

## 脚本里有什么

`main.nut` 把纯数据权威根与命令 API 分开：

- 可读：`gameState.tick` / `gameState.time` / `gameState.player.hp` / `gameState.enemy.hp` 等；
- 可改：`game.setEnemyHp(v)` / `game.setPlayerHp(v)` / `game.reset()`（开发者作弊入口，不是 Play 主路径）；
- 快照：`persist gameState` 负责热重载保留，`eve.dev.markStateRoot("gameState")` 把同一权威根显式纳入
  checkpoint 捕获与恢复。

任何 Agent（Cursor / Claude / Codex）通过 `tools/eve-mcp` 桥或直接 TCP
连接这个端口即可驾驶；热重载照常生效。
