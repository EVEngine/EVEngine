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
