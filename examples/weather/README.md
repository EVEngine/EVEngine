# Weather Lab

A small 3D diorama that demonstrates the EVEngine **weather module**:
rain, snow, lightning, wind and the storm mood (sky / fog / sun).

## Run

```sh
eve run examples/weather
```

## Controls

- Left panel buttons (or keys `1`..`6`) switch presets:
  `Clear`, `Drizzle`, `Rain`, `Storm`, `Snow`, `Fog`.
- Sliders tune precipitation intensity, wind speed and wind direction live.
- `S` / the *Strike lightning* button forces a bolt (also automatic in `Storm`).
- Camera auto-orbits the diorama.

## Scene

Procedural props (trees, pillars, rocks) on a disc ground plane, lit by a
single directional light + camera ambient. The weather module drives the
background color, light intensity, fog and ambient brightness so each preset
reads as a distinct mood.

## 2026-11-11 视觉改善

默认显示雨天，按预设切换地面粗糙度、雪地颜色和水洼显示；增加石径及固定取景，扩展控件布局。晴、雨、雪、暴风使用同机位记录；水洼/雪地颜色为状态展示，不是积水或积雪物理。

[当前实帧](../../docs/images/examples/review-2026-11-11/next10/weather.png) · [前后对照与验证](../../docs/images/examples/QUALITY_REVIEW.md)
