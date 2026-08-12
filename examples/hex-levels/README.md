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
| `01`–`20` | 寻路 / FOV / 光照 / 拾取 / 粒子 / Flow / 代价 / 感知 / FoW / 相机 / DualGrid / 变体 / 裁剪 / 多光 / 仓库 / 朝向锥 / Flow+代价 / 种子复现 / 拐角窥视 / 装备 |
| `pipeline.*` | dungeonCrawl / fogRaid / torchEscort / catalogRaid |

```bash
./build/linux-debug/test/unit_test --testcase='^hex\.(level|data)\..*$'
```

## 关卡启动配置

每个关卡从 `data/catalog.json`（示例侧镜像为 `data/fixtures.nut`）读取独立启动配置：

| 字段 | 作用 |
|------|------|
| `seed` / `algorithm` / `params` | 程序化生成 |
| `width` / `height` | 地图尺寸 |
| `lootTable` | 掉落表 |
| `fov` | 算法 / 半径 / 感知 |
| `light` | 点光半径与颜色 |
| `cellCost` | 绕路代价条带 |
| `enable.*` | 功能开关（path/fov/light/pickup/particles/flow…） |

切换数字键会重新应用该关配置；`R` 仅递增种子并保留当前关其它设置。

| 资源 | 用途 |
|------|------|
| `catalog.json` | 关卡目录（种子 / 尺寸 / 算法 / FOV·光照·代价·enable 启动配置） |
| `items.json` | 物品定义（示例与单测共用） |
| `loot_tables.json` | 掉落表与感知门控 |
| `seeds_matrix.json` | 种子 × 算法连通性冒烟 |
| `perception_cases.json` | FOV 感知数值用例 |
| `maps/*.json` | 手搓六角地图 |
| `particles/*.json` | 粒子发射器配置 |
| `fixtures.nut` | 示例脚本加载的 catalog / loot 镜像 |

示例启动时会读取 `data/items.json` 与 `data/particles/*.json`，并用 `fixtures.nut` 按关卡放置掉落。