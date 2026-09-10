# 天气模块

**脚本入口：** `eve.Weather()`

实时降水 / 闪电 / 风场系统。通过少量逐帧 uniform（时间、风速、强度）在
顶点着色器里驱动一批面向相机的 `Renderable3D` 网格（雨丝、雪花、闪电），
网格与着色器在首次使用时惰性构建并复用，**逐帧不产生 GPU 分配**。

演示场景见 `examples/weather`（`eve run examples/weather`）。

## 基本用法

```squirrel
local weather = eve.Weather();
weather.setPreset("storm");      // clear / drizzle / rain / storm / snow / fog / wind / blizzard
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
| `wind`    | 水平风痕 | 关 | 按风速漂移，静风时隐藏 |
| `blizzard` | 风吹雪 | 关 | 与雪相同的粒子模型，可配合强风、浓雾 |

`setPreset` 只切换类别，降水密度由 `setIntensity` 控制，风速仍由调用方指定。
例如暴风雪使用 `setPreset("blizzard")`、`setIntensity(1.0)`、`setWindSpeed(13.0)`；
轻雪使用 `setPreset("snow")`、`setIntensity(0.75)`、`setWindSpeed(1.2)`。
示例的 1–8 键或面板可切换全部预设。

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
| `setFogColor(r,g,b)` / `setFogDensity(v)` | 天气粒子的雾颜色与密度；场景雾由示例中的 Volumetric 合成 |
| `getAmbientBrightness()` | 建议的相机环境光亮度 |

## 渲染与限制

- 雨雪纹理绑定到实际参与绘制的 Material，避免被默认白纹理覆盖。
- 几何偏移 UV 与纹理 UV 分开，雨丝和雪花纹理完整映射到 `[0,1]`。
- 雨丝对齐风与下落速度；雪花始终面向相机，保持圆形轮廓并独立旋转、飘动。
- 风按 `update(dt, gfx)` 的时间步积分；调整风速不会把整片粒子瞬间移到新位置。
  雨雪粒子在相机周围循环，近镜头和水平边界淡出。独立风效果采用固定的浅弧细丝，
  仅局部亮光沿丝前进并淡出；风丝本身不随时间平移或扭动，使用低覆盖率保持轻淡。
- 自定义天气管线采用覆盖率抖动表达纹理透明度，并保留场景深度遮挡；它不是排序后的
  alpha 混合，低分辨率静帧仍可能看到细小颗粒。雪花数量为 1400，雨/风共用 1600 个粒子。
- `setEnvironmentEnabled(false)` 时 Weather 不写天空和方向光。雪与独立风不再按暴雨
  强度额外压暗；调用方可以通过天空颜色、太阳强度控制阴天氛围。
- `examples/weather` 在 `render3D` 后使用 GBuffer 深度和 `Volumetric.applyFog` 叠加
  场景雾，再绘制 UI。GBuffer 使用片段位置深度，避免三角形透视插值造成的雾量接缝。Weather 本身只对天气粒子应用雾，不自动拥有场景后处理。
- 雪花不模拟屋顶碰撞、地面积雪或植被受风；这些需要对应场景系统提供。

模拟状态仍由 Weather 实例持有，更新在渲染主线程、`render3D` 前执行；没有新增公共
对象所有权、跨域 Link 或持久格式。运动使用调用方提供的 dt，初始化粒子使用固定 seed。
GLSL/WGSL 采用相同公式，但浮点三角函数及覆盖率采样不保证跨后端逐像素一致。

修改 GLSL 后运行 `python scripts/compile_weather_shaders.py` 更新嵌入式 SPIR-V。
WGSL 对应源码位于 `src/modules/weather/shaders/WeatherWgsl.h`，需同步维护并运行后端验证。
