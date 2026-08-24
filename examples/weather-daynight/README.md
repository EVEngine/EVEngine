# Living Sky

Unified `daynight` + `weather` showcase inspired by stylized Unity environment
lighting demos. It couples the solar/lunar cycle, procedural atmosphere,
volumetric clouds, height fog, precipitation, lightning exposure and scene
ambient light instead of letting independent modules overwrite one another.
The scene combines EVEngine's generated heightfield and curved waterfall
shader with a small selection of low-poly pine trees from
[Kenney Nature Kit](https://kenney.nl/assets/nature-kit) (CC0; license copied
to `assets/KENNEY_LICENSE.txt`).

```sh
eve run examples/weather-daynight
```

- `1`–`5`: clear, cloudy, rain, storm, snow
- `Space`: pause the clock
- `Left` / `Right`: scrub time
- `S`: force lightning
- `C`: toggle orbit camera
