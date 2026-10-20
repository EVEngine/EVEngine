# Procedural Render Gallery

The five former screenshot-only tests now run continuously as an example:

- `1`: tree (`procgen.mesh.tree.renderDump`), seed 31415, canopy and space colonization.
- `2`: bush (`procgen.mesh.bush.renderDump`), seed 20260815, mixed foliage.
- `3`: L-system (`procgen.lsystem.renderDump`), seed 20260826, five iterations.
- `4`: hex planet (`procgen.render.hexplanetPng`), radius 1, three subdivisions.
- `5`: skyscraper (`procgen.render.skyscraperPng`), seed 99, six tiers.
- `Space`: pause rotation.
- `F`: switch between live models and the six foliage construction frames;
  in the construction view, `1`-`6` select points, leaves, angled card,
  plane counts, spherical normals and the final tree.

```sh
make run/win32-debug GAME=examples/procedural-render-gallery
```

Original default geometry recipes and palettes are retained; lighting is shared
across the gallery and the skyscraper camera is pulled back to frame the whole
tower. Edit the preset functions in `main.nut`
to compare parameters previously controlled through test environment variables.
Use engine MCP `eve_screenshot` for capture. The dedicated tree-generator,
bush-generator, hex-planet and skyscraper-generator examples remain available
for deeper interactive editing.

The former `procgen.mesh.foliageCluster.stages` screenshot walk lives in
`foliage_stages.cpp`. Build the opt-in `foliage_stage_render` target in a
configured host build and run it with this directory as the working directory
to regenerate `stages/*.png`. Those six images are engine-rendered frames.
