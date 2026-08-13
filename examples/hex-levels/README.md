# Hex Levels — 六边形引擎功能测试关卡

用**程序化生成的六边形 tilemap** 串联验证引擎能力。Catalog 共 **31** 关（id 0–30），示例可用 `N`/`B` 循环全部关卡。

## 可玩关卡（`examples/hex-levels`）

| 关卡 | 重点能力 |
|------|----------|
| 0 | 综合通关（raid 掉落 + 感知 + Flow + 代价） |
| 1–9 | 寻路 / FOV / 光照 / 拾取 / 粒子 / Flow / 代价 / 感知 / FoW |
| 10–15 | 相机 / DualGrid / 变体 / 裁剪 / 多光 / 仓库 |
| 16–20 | 朝向锥 / Flow+代价 / 种子复现 / 拐角窥视 / 装备掉落 |
| 21–25 | 世界拾格 / 探索记忆 / 群体寻路 / FOV 画廊 / 动态阻挡 |
| 26–30 | 醉汉洞穴 / 迷宫 / WFC / 迷雾粒子 / 突袭综合 |

## 运行

```bash
make run/linux-debug GAME=examples/hex-levels
```

## 操作

| 键 | 作用 |
|----|------|
| WASD / 方向键 | 在六角格上移动 |
| E | 拾取附近掉落物 |
| 1–9 / 0 | 快速跳到对应关卡 |
| N 或 `]` | 下一关（遍历 catalog 0–30） |
| B 或 `[` | 上一关 |
| R | 换种子重生成 |
| T | 切换 FOV 算法 |
| F / G / H | BSP / Cellular / WFC 生成算法 |

## 自动化测试

| 文件 | 覆盖 |
|------|------|
| `test/hex_level_simulation.cpp` | 关卡 `01`–`30` + 多条 pipeline |
| `test/hex_level_data.cpp` | catalog（31 关）/ items / loot / seeds / particles / maps |

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
| `fov` | 算法 / 半径 / 感知 / 朝向锥 / cornerPeek |
| `light` | 点光半径与颜色 |
| `cellCost` | 绕路代价条带 |
| `enable.*` | 功能开关 |

`N`/`B` 切换会重新应用该关配置；`R` 仅递增种子并保留当前关其它设置。

## 测试数据（`data/`）

| 资源 | 用途 |
|------|------|
| `catalog.json` | 关卡目录 0–30（种子 / 尺寸 / 算法 / FOV·光照·代价·enable） |
| `items.json` | 物品定义 |
| `loot_tables.json` | 掉落表与感知门控 |
| `seeds_matrix.json` | 种子 × 算法连通性冒烟 |
| `perception_cases.json` | FOV 感知数值用例 |
| `maps/*.json` | 手搓六角地图 |
| `particles/*.json` | 粒子发射器配置 |
| `fixtures.nut` | 示例脚本加载的 catalog / loot 镜像 |
