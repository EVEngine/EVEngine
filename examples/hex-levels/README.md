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

## 自动化测试

C++ 对应用例：

| 文件 | 覆盖 |
|------|------|
| `test/hex_level_simulation.cpp` | 关卡 01–15 + dungeonCrawl / fogRaid 管线 |
| `test/hex_level_data.cpp` | 夹具 JSON：catalog / items / loot / seeds / particles / 手搓地图 / 感知用例 |

```bash
./build/linux-debug/test/unit_test --testcase='^hex\.(level|data)\..*$'
```

## 测试数据（`data/`）

| 资源 | 用途 |
|------|------|
| `catalog.json` | 关卡目录（种子 / 尺寸 / 算法 / 特性标签） |
| `items.json` | 物品定义（示例与单测共用） |
| `loot_tables.json` | 掉落表与感知门控 |
| `seeds_matrix.json` | 种子 × 算法连通性冒烟 |
| `perception_cases.json` | FOV 感知数值用例 |
| `maps/*.json` | 手搓六角地图 |
| `particles/*.json` | 粒子发射器配置 |
| `fixtures.nut` | 示例脚本加载的 catalog / loot 镜像 |

示例启动时会读取 `data/items.json` 与 `data/particles/*.json`，并用 `fixtures.nut` 按关卡放置掉落。