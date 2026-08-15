# 昼夜循环模块

**脚本入口：** `eve.DayNight()`

一个随时间流逝驱动太阳、天空与光照的昼夜循环系统。模块维护一个 24 小时
时钟（速度可调），每帧按当前**太阳高度角 / 方位角**重新摆放方向光，并按需
生成一张程序化天空盒立方体贴图（天空渐变 + 太阳光盘 + 夜间星空）作为 3D
IBL 环境光，使**天空盒角度与反射天空都跟随太阳转动**。

入夜后（太阳低于地平线）可切换互补的光照系统，每个都以一组 `Light3D`
实体实现：

- **月光（moonlight）**：来自月亮方向的冷色方向光；
- **星光（starlight）**：微弱偏蓝的环境光，柔和地托起夜色；
- **火焰（fire）**：位于调用者指定篝火位置的暖色点光源；
- **萤火虫（fireflies）**：一组暖色、缓慢漂移的点光源。

演示场景见 `examples/daynight`（`eve run examples/daynight`）。

## 基本用法

```squirrel
local daynight = eve.DayNight();
daynight.init(gfx);
daynight.setTimeOfDay(12.0);        // 正午
daynight.setSpeed(0.5);             // 每个真实秒前进 0.5 个模拟小时
daynight.setFirePosition(0.0, 0.6, 0.0);
daynight.addFirefly(2.0, 0.8, 1.0);
daynight.setNightLight("fireflies", true);

// 每帧、在 gfx.render3D() 之前：
daynight.update(dt, gfx);
camera.setAmbient(daynight.getAmbientR(),
                  daynight.getAmbientG(),
                  daynight.getAmbientB());
gfx.render3D();
```

`daynight.update` 会同步更新方向光、背景色与程序化天空盒；相机环境光需通过
`getAmbientR/G/B()`（或 `getAmbientBrightness()`）由脚本喂给相机。

## 时钟与太阳

| API | 说明 |
|-----|------|
| `setTimeOfDay(h)` / `getTimeOfDay()` | 设置 / 读取钟表时间（`[0,24)` 小时） |
| `setSpeed(hprs)` / `getSpeed()` | 每个真实秒前进的模拟小时数 |
| `setPaused(b)` / `isPaused()` | 暂停 / 恢复时间流逝 |
| `isNight()` | 太阳是否低于地平线（入夜） |
| `getSunElevation()` / `getSunAzimuth()` | 太阳高度角 / 方位角（度） |
| `getSunDirX/Y/Z()` | 指向太阳的世界方向（单位向量） |
| `getSunIntensity()` | 太阳能量 `[0,1]`，低于地平线为 0 |

太阳高度角在正午达到约 `70°`，方位角在一天内转动 360°。

## 天空与环境

| API | 说明 |
|-----|------|
| `setSkyboxEnabled(b)` / `isSkyboxEnabled()` | 开关程序化天空盒（IBL 环境光） |
| `getSkyR/G/B()` | 地平线处天空基调色（用于清屏背景） |
| `getAmbientR/G/B()` / `getAmbientBrightness()` | 建议的相机环境光 |

天空盒仅在太阳角变化到新“桶”时才重新生成，避免逐帧 GPU 分配。

## 夜间光照系统

| API | 说明 |
|-----|------|
| `setNightLight(name, b)` / `isNightLight(name)` | 开关命名光照系统 |
| `setFirePosition(x,y,z)` | 设置篝火点光源位置 |
| `addFirefly(x,y,z)` | 添加一只萤火虫锚点（上限 8 只） |
| `clearFireflies()` / `getFireflyCount()` | 清空 / 读取萤火虫数量 |

命名系统：`"moonlight"` / `"starlight"` / `"fire"` / `"fireflies"`。除星光通过
环境光实现外，其余均为逐帧启停与摆位的 `Light3D` 实体。

## 实现方式

- **太阳轨道**：由时钟推导高度角 / 方位角，再换算为世界方向驱动方向光，
  高度角低时太阳能量平滑衰减至 0。
- **程序化天空盒**：每张面按像素计算“天顶蓝 → 地平线浅色”渐变，叠加太阳
  光盘与宽泛辉光，夜间再撒上确定性散列的星光；随太阳角分桶重新生成并写入
  IBL 环境光，达到“天空随太阳转动”的效果。
- **夜间光源**：月亮为方向光，篝火为暖色点光，萤火虫为绕锚点正弦漂移的一
  组点光；全部按命名开关启停。
