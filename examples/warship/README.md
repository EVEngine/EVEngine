# Warship — Type 055 表现 + 可重组舱段

仿真走 `eve.Warship()`（船体定义、舱段布局、电力/推力投影、`moveTo`）。
表现层挂载仓库内 [Type 055 模型](../../assets/models/type055/README.md)
（`assets/models/type055`，180 m，Y 向上，+X 舰首），用 `model3d` 自定义 3D 视图。
其它类别舰用同一网格缩放上色，仅作编队对照，不是独立资产。

```bash
make run/<platform>-debug GAME=examples/warship
```

操作：

- `1` / `2` / `3` / `4` — 选中驱逐舰 / 帆船 / 航母 / 星际舰
- `Space` — 向当前选中舰下达 `moveTo` 巡逻点
- `H` — 在空舱放置机库（hangar）
- `R` — 用 JSON 原子重组布局
- `C` — 清空一格并观察欠功率（underpowered）
- `T` — 跟随 / 俯视镜头

## 相关

- 用户文档：`docs/usr/modules/warship.md`
- 设计：`docs/dev/通用战舰系统设计.md`
- 海洋驾驶演示（同模型）：`examples/ocean-voyage`
