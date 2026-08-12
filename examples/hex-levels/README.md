# Hex Levels — 六边形引擎功能测试关卡

用**程序化生成的六边形 tilemap** 串联验证：

| 关卡 | 重点能力 |
|------|----------|
| 1 | `procgen` → hex `TileLayer` + A\* / Flow Field 寻路 |
| 2 | `map.Fov` 动态视野 / 探索记忆 |
| 3 | `Light2D` 火把跟随的 2D 动态光照 |
| 4 | `SpatialHash2D` 拾取碰撞 + `Inventory` |
| 5 | `Particles` 火把与拾取爆发 |
| 0 | 综合通关（以上全部） |

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
| 1–5 | 切换专题关卡 |
| 0 | 综合模式 |
| N | 下一关 |
| R | 换种子重生成 |
| F / G / H | BSP / Cellular / WFC 算法 |

## 自动化测试

C++ 对应用例在 `test/hex_level_simulation.cpp`：

```bash
./build/linux-debug/unit_test --testcase='^hex\.level\..*$'
```
