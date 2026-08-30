# 天气模块

**脚本入口：** `eve.Weather()`

实时降水 / 闪电 / 风场系统。通过少量逐帧 uniform（时间、风速、强度）在
顶点着色器里驱动一批面向相机的 `Renderable3D` 网格（雨丝、雪花、闪电），
网格与着色器在首次使用时惰性构建并复用，**逐帧不产生 GPU 分配**。

演示场景见 `examples/weather`（`eve run examples/weather`）。

## 基本用法

```squirrel
local weather = eve.Weather();
weather.setPreset("storm");      // clear / drizzle / rain / storm / snow / fog
weather.setIntensity(0.8);
weather.setWindSpeed(12.0);
weather.setWindDirection(40.0);  // 度；0 = 吹向 +Z，90 = 吹向 -X

// 每帧、在 gfx.render3D() 之前：
weather.update(dt, gfx);
// 用天气的亮度给相机环境光调色：
camera.setAmbient(weather.getAmbientBrightness(),
                  weather.getAmbientBrightness(),
                  weather.getAmbientBrightness());
gfx.render3D();
```

`weather.update` 会同步更新背景色与方向光（暗化、去饱和），并让相机环境光
通过 `getAmbientBrightness()` 随之变化，形成风暴氛围。

## 预置（Preset）

| 预置 | 降水 | 闪电 | 氛围 |
|------|------|------|------|
| `clear`   | —    | 关 | 晴朗、亮 |
| `drizzle` | 细雨 | 关 | 微灰 |
| `rain`    | 大雨 | 关 | 变暗、起雾 |
| `storm`   | 暴雨 | 开 | 最暗、雷暴 |
| `snow`    | 降雪 | 关 | 冷白、柔和 |
| `fog`     | —    | 关 | 浓雾、低对比 |

`setPreset` 只切换类别，降水密度由 `setIntensity` 控制。

## 参数

| API | 说明 |
|-----|------|
| `setIntensity(v)` / `getIntensity()` | 降水密度 `[0,1]`，平滑过渡 |
| `setWindSpeed(v)` / `getWindSpeed()` | 风速（米/秒） |
| `setWindDirection(deg)` | 风向（度），0 = +Z |
| `setLightningEnabled(b)` | 风暴是否自动闪 |
| `setEnvironmentEnabled(b)` / `isEnvironmentEnabled()` | 是否由 Weather 写入背景色和方向光；与 DayNight 联用时关闭，由 DayNight 统一合成天空和光照 |
| `strike()` | 手动触发一道闪电 |
| `getFlash()` | 当前闪光的 0..1，可用来驱动场景补光 |
| `setSkyColor(r,g,b)` | 天空基调色（暴风雨时被暗化） |
| `setSunIntensity(v)` | 太阳强度 |
| `setFogColor(r,g,b)` / `setFogDensity(v)` | 场景雾颜色与密度 |
| `getAmbientBrightness()` | 建议的相机环境光亮度 |

## 实现方式与成熟渲染方案

设计参考了主流引擎的天气渲染取舍（Unreal 的 Volumetric Cloud / Sky Atmosphere +
Niagara 降水、Frostbite 的指数高度雾、GPU Gems 的降水技术）：

- **雨**：面向相机的细长粒子条纹，按速度/风向拉伸；纹理为带模糊尾迹的亮条，
  顶点着色器按时间下落并回卷（wrap）。近处靠纹理 alpha 测试裁出软边。
- **雪**：面向相机的方形 billboard，叠加缓慢下落、风致摇摆与随机相位；
  圆形软雪片纹理 + alpha 测试。
- **风**：作为全局向量场（基础方向 + 阵风摇摆）统一喂给降水着色器，形成倾斜/漂移。
- **闪电**：中位点置换生成的分叉折线，挤出成变细的三角带；通过 `uFlash` 快速闪烁
  衰减。闪电本身即“关键光”，配合 `getFlash()` 可让场景补光同步闪亮。
- **氛围**：指数距离雾 + 天空/太阳暗化 + 环境光衰减，一次性奠定风暴情绪
  （“先做情绪，再做粒子”的原则）。
- **雾**：逐片段的指数距离雾，作用于降水着色器与整个场景基调。

> 更重的方案（体素化体云 `ray marching`、基于曲率的湿表面高光、雪花累积高度图）
> 可作为后续扩展；本模块保持轻量、逐帧零分配、可热重载。
