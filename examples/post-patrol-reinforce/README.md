# Post / Patrol / Reinforce

竖切演示 `eve.Roster()`：岗哨占用、小队巡逻意图、哨兵被干掉后延迟补员请求，以及 ack 后再上岗。

```sh
eve run examples/post-patrol-reinforce
# 或
make run/linux-debug GAME=examples/post-patrol-reinforce
MIN_RUN_SECONDS=2 RUN_SECONDS=6 bash scripts/smoke_examples.sh post-patrol-reinforce
```

启动后自动跑完「两人守门 → 一人阵亡 → 延迟 2 tick 出 spawn 请求 → 回退民兵定义失败一次 → 成功补员」契约，并在 HUD 打印关键步骤。这是编制层示例：不接导航网格或 RTS 生产建筑。

## 文件

- `main.nut`：生命周期、绘制与观察状态
- `simulation.nut`：Roster 装配与确定性契约
- `config.nut`：启用 `roster` 槽位
