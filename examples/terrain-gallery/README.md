# Terrain gallery

Renders three fixed terrain seeds under identical generation parameters, lighting, material,
camera, and chunk layout. The gallery is a visual regression aid for drainage morphology,
biome continuity, river-water width, and chunk seams.

```sh
make run/macosx-debug GAME=examples/terrain-gallery
```

After eight rendered frames it writes `/private/tmp/evengine-terrain-gallery.png`.
