# House generator runtime example

Run with `make run/win32-debug GAME=examples/housegen`. The example exercises the Squirrel
generation API and prints a serializable layout. To display it, place the converted CC0 GLB kit
under `assets/`. Native integrations call `HouseLayout::instantiate`; it returns a checked
`Result<vector<ecs::EntityHandle>>`, so the graphics ECS world remains the sole owner of
created entities and callers resolve handles only for the current operation.

Asset provenance and the required conversion record are in `test/assets/housegen/ASSET_SOURCES.md`.

Player kits should use `housegen.loadComponentsFromFile("assets/my-kit/components.json")` and
check its structured Result; relative
`model` paths are resolved beside that manifest. A component may override selected GLB PBR fields
without replacing unspecified material data:

```json
{
  "id": "wall.brick",
  "model": "wall-brick.glb",
  "category": "wall",
  "material": {
    "baseColor": [0.72, 0.48, 0.32, 1.0],
    "baseColorTexture": "textures/brick-albedo.png",
    "normalTexture": "textures/brick-normal-gl.png",
    "heightTexture": "textures/brick-height.png",
    "metallic": 0.0,
    "roughness": 0.88,
    "parallaxScale": 0.025,
    "parallaxMinLayers": 8,
    "parallaxMaxLayers": 24,
    "cellBombScale": 4.0,
    "cellBombStrength": 0.08,
    "cellBombRotation": 0.0
  }
}
```

Texture paths are resolved beside the GLB. Values must be in `[0, 1]`; `parallaxScale` is limited
to `[0, 0.2]`. Omit a property to inherit it from the GLB. Embedded and external glTF base-color,
normal and height textures are supported. The renderer currently uses scalar metallic/roughness;
packed metallic-roughness maps are deliberately not advertised by this manifest.
