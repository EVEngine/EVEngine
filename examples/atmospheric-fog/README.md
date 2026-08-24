# Atmospheric Froxel Fog

This example is the smallest complete rendering path for EVEngine's atmospheric
froxel fog. It renders opaque geometry into the GBuffer, builds and uploads an
integrated participating-media volume, then composites that volume at each
pixel's scene depth.

```powershell
make run/win32-debug GAME=examples/atmospheric-fog
```

With an SDK build:

```powershell
eve run examples/atmospheric-fog
```

Controls:

- `1`: thin morning haze
- `2`: medium fog
- `3`: dense fog
- `Space`: toggle fog for an immediate before/after comparison

The important calls in `main.nut` are:

1. Enable `gbuffer` and `volumetricFog` on `RenderControl`.
2. Create a `Volumetric`, select `froxel`, and set the camera.
3. Configure and clear the froxel grid.
4. Inject height fog, integrate incident lighting, and upload the slice atlas.
5. After `gfx.render3D()`, obtain the GBuffer depth and call `applyFroxel()`.

`rebuildFog()` only runs when the preset changes. Dynamic fog, local volumes,
clipmaps, density graphs, and sparse volume textures use the same principle:
inject changed media, integrate, upload, then reuse the atlas during rendering.
