# Terrain gallery

Renders three fixed terrain seeds under identical generation parameters, lighting, material,
camera, and chunk layout. The gallery is a visual regression aid for drainage morphology,
biome continuity, river-water width, and chunk seams.

```sh
make run/macosx-debug GAME=examples/terrain-gallery
```

After eight rendered frames it writes `/private/tmp/evengine-terrain-gallery.png`.
It also writes a closer terrain view to
`/private/tmp/evengine-terrain-gallery-closeup.png`. This example contains
procedural terrain and water only; the closeup is not Unity Terrain Detail
evidence. Terrain Detail validation requires an imported
`*.eve-details.json` sidecar and the instance-set/GPU resolver path.
