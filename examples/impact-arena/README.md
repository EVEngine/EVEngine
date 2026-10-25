# IMPACT — Action Arena

俯视角动作游戏：三波敌人、轻攻击、暴击重攻击、闪避无敌、攻击前摇预警、死亡/胜利与重开。

```sh
make run/macosx-debug GAME=examples/impact-arena RUN_ARGS="--debug --mcp-port=7537"
```

Windows/Linux 使用对应 `run/win32-debug` / `run/linux-debug`。需要 graphics、font、combat。
字体来自仓库既有 DejaVu Sans，授权见 `assets/fonts/NOTICE.txt`。没有使用 Unity 插件资源。

- WASD/方向键：移动并改变攻击朝向。
- J：轻攻击；K：范围更大的重攻击，确定性暴击，无随机倍率。
- Space：闪避，只有 Active 阶段无敌；红色攻击弧线代表敌人前摇。
- P：暂停；R：重开；Tab：自动玩家，走相同的移动/攻击/闪避入口。

数据流：输入 → CombatRuntime 内建 Ability 的 Windup/Active/Recover → Active 窗口空间命中
→ DamageRuntime → 通用 SettlementPipeline → 原子提交血量/韧性 → canonical settlement
结果 → DamageNumbers。脚本只缓存 state() 的只读表现快照，不在脚本里扣血。

伤害数字显示实际入账数值；例如敌人只剩 14 点生命，46 点重击显示 `-14!`。
HUD 展示最近请求值和实际值。灰蓝 MISS 代表闪避成功，不伪造一次扣血结算。
游戏局部使用固定 60 Hz 仿真，长帧最多积累 0.1 秒；所有时间显式传入，无随机数。
热重载完整重建局部会话，不承诺持久化或 C++ runtime checkpoint 恢复。

Agent 通过 `game.agent.json` 定义的 `eve_play act/observe/step/capture` 验证。
`demo` 重开并启用自动输入；不通过 eve_eval 修改生命或越过战斗规则。

自动验证（先启动上述带 MCP 的示例，再从仓库根目录执行）：

```sh
python3 examples/impact-arena/agent_check.py 7537 /tmp/impact-arena-evidence
MIN_RUN_SECONDS=2 RUN_SECONDS=6 bash scripts/smoke_examples.sh impact-arena
```

验证脚本覆盖三波通关、暴击/过量伤害钳制、闪避动作、死亡和重开，输出观察记录、战斗/胜利/死亡截图，
并通过 `eve_agent_session_*` 保存验证收据。截图由引擎 readback 生成。
初始生命为 320，手动玩家和自动玩家使用同一套数值与规则。
