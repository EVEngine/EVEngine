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

// 用同一套太阳方向和大气衰减颜色驱动体积云，日落霞光会与天空一致。
cloud.setLightDirection(daynight.getSunDirX(), daynight.getSunDirY(), daynight.getSunDirZ());
cloud.setCloudLightColor(daynight.getSunR(), daynight.getSunG(), daynight.getSunB());
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
| `setTurbidity(v)` / `getTurbidity()` | 大气浑浊度 `[1.5,10]`；越高，地平线雾霾与暖色霞光越强 |
| `setMieStrength(v)` / `getMieStrength()` | 米氏气溶胶强度 `[0,4]`，控制太阳周围光晕与地平线泛白 |
| `setSkyExposure(v)` / `getSkyExposure()` | 天空写入 RGBA8 cubemap 前的曝光 `[0.05,8]` |
| `applyAtmosphere(volumetric)` | 将当前太阳方向、直射光、天空色和天气影响统一写入 `Volumetric` fog/cloud 对象，避免脚本分别维护不一致参数 |

太阳高度角在正午达到约 `70°`，方位角在一天内转动 360°。

## 天空与环境

| API | 说明 |
|-----|------|
| `setSkyboxEnabled(b)` / `isSkyboxEnabled()` | 开关程序化天空盒（IBL 环境光） |
| `setWeatherInfluence(cloudiness, flash)` | 注入天气影响：云量 `[0,1]` 压低天空、日光与星光，闪电亮度 `[0,1]` 短暂照亮环境 |
| `getWeatherCloudiness()` / `getWeatherFlash()` | 读取当前云量和闪电环境光影响 |
| `getSkyR/G/B()` | 地平线处天空基调色（用于清屏背景） |
| `getAmbientR/G/B()` / `getAmbientBrightness()` | 建议的相机环境光 |

天空使用波长相关的瑞利散射、近似米氏散射、太阳路径消光和多散射底色生成，
因此低太阳高度会自然产生暖色太阳、地平线霞光和更宽的气溶胶光晕。天空盒为
每面 128×128；仅在太阳角变化到新“桶”或大气参数改变时重新生成，避免逐帧
GPU 分配。当前图形后端的环境贴图格式仍为 RGBA8，模块使用 ACES 拟合曲线映射
物理辐亮度；后端升级至浮点环境贴图后可移除此中间映射。

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
- **程序化天空盒**：每张面计算波长相关的大气单次散射、太阳消光及近似多次
  散射，叠加具有真实角尺度的太阳盘，夜间再撒上确定性星光；同一天空写入 IBL，
  使可见天空与材质反射保持一致。
- **夜间光源**：月亮为方向光，篝火为暖色点光，萤火虫为绕锚点正弦漂移的一
  组点光；全部按命名开关启停。

### PR #287 新增绑定

- 反射链、HDR 与探针相关 API：`applyReflectionProbeSky`
