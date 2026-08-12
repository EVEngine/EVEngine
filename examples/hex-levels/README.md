# Hex Levels — 六边形引擎功能测试关卡

用**程序化生成的六边形 tilemap** 串联验证引擎能力。

## 可玩关卡（`examples/hex-levels`）

| 关卡 | 重点能力 |
|------|----------|
| 1 | `procgen` → hex `TileLayer` + A\* 寻路 |
| 2 | `map.Fov` 动态视野 / 探索记忆 |
| 3 | `Light2D` 火把跟随的 2D 动态光照 |
| 4 | `SpatialHash2D` 拾取碰撞 + `Inventory` |
| 5 | `Particles` 火把与拾取爆发 |
| 6 | Flow Field 群体寻路（多单位同目标） |
| 7 | 格子代价绕路（`setCellCost`） |
| 8 | 多观察者 FOV + 感知半径 |
| 9 | FoW 遮罩强度 / FOV 算法切换（`T`） |
| 0 | 综合通关 |

## 运行

```bash
make run/linux-debug GAME=examples/hex-levels
# 或对应平台：run/macosx-debug / run/win32-debug
```

## 操作

| 键 | 作用 |
|----|------|
| WASD / 方向键 | 在六角格上移动 |
| E | 拾取附近掉落物 |
| 1–9 | 切换专题关卡 |
| 0 | 综合模式 |
| N | 下一关 |
| R | 换种子重生成 |
| T | 切换 FOV 算法（shadowcast → raycast → permissive → rectangle） |
| F / G / H | BSP / Cellular / WFC 算法 |

## 自动化测试（17 用例）

C++ 对应用例在 `test/hex_level_simulation.cpp`：

| 用例 | 覆盖 |
|------|------|
| `01`–`05` | 寻路 / FOV / 光照 / 拾取 / 粒子 |
| `06` | Flow Field 群体 |
| `07` | 格子代价绕路 |
| `08` | 多观察者 + 感知 / 隐身 |
| `09` | FoW mask + 算法切换 |
| `10` | Camera2D 屏幕↔世界↔hex 拾取 |
| `11` | Dual-grid（六角逻辑层） |
| `12` | cave / maze / wfc / drunkard 变体 |
| `13` | QuadTree 视口裁剪 |
| `14` | 点光 + 方向光 |
| `15` | 粒子生命周期 + 背包→仓库转移 |
| `pipeline.dungeonCrawl` | 综合爬塔 |
| `pipeline.fogRaid` | 感知门控拾取 + Flow 护送 |

```bash
./build/linux-debug/test/unit_test --testcase='^hex\.level\..*$'
```
