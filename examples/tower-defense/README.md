# Tower Defense / 塔防竖切

把引擎积木焊成一条可玩的迷宫塔防环：

- `building`：占地放塔 + 鬼影
- `map.Pathfinder.keepsPathOpenFootprint`：禁止封死 spawn→goal
- `towerdefense.WaveDirector`：数据驱动波次
- `towerdefense.DefenseBase`：漏怪扣命
- `sensing.circleSorted`：炮塔 `first` 选敌
- `economy`：买塔扣金币、击杀/波次奖励

```sh
make run/linux-debug GAME=examples/tower-defense
# 或 win32-debug / macosx-debug
```

## 操作

- `1` / `2` 选择哨塔 / 重炮
- 左键放置（必须保持出生点到终点通路）
- `Space` 开始下一波（或继续日程）
- `R` 重置关卡

敌人沿格子路径走向终点；到达则漏怪。炮塔在射程内按 `first`（路径进度最高）开火。
