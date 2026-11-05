# Warship — 巨型舰体与可重组舱段

演示 `eve.Warship()`：注册舱段功能与船体模板，生成驱逐舰 / 帆船 / 航母 /
星际巡洋舰，运行时放置、交换舱段，并观察电力 / 推力 / 机库等能力投影。

## 运行

```bash
make run/<platform>-debug GAME=examples/warship
```

操作：

- `1` / `2` / `3` / `4` — 选中驱逐舰 / 帆船 / 航母 / 星际舰
- `Space` — 向当前选中舰下达 `moveTo` 巡逻点
- `H` — 在空舱放置机库（hangar）
- `R` — 用 JSON 原子重组布局
- `C` — 清空一格并观察欠功率（underpowered）

## 相关

- 用户文档：`docs/usr/modules/warship.md`
- 设计：`docs/dev/通用战舰系统设计.md`
