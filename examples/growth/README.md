# Growth — 动植物生长模拟

演示 `eve.Growth()`：小麦三阶段生长与收获、两头牛成熟后产奶与繁殖。独立 ECS 实体，不绑 hex。

## 运行

```bash
make run/<platform>-debug GAME=examples/growth
```

无操作键：示例每帧自动浇水/喂养并按 `secondsPerSimDay=2` 推进仿真日。
窗口内 HUD 显示日历、小麦/牛状态与最近生命周期事件（需要 `font` 模块与
`assets/fonts/DejaVuSans-Bold.ttf`；授权见同目录 `NOTICE.txt`）。

## 相关

- 设计：`docs/dev/生长模拟系统设计.md`
- 用户文档：`docs/usr/modules/growth.md`
