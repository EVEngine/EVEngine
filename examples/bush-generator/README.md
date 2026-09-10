# Bush Lab — procedural small 3D bush (`mesh.bush`)

`mesh.bush` is a native EVEngine procedural mesh recipe that generates a small,
low-poly 3D bush deterministically. The same seed and parameters always produce
the identical mesh (positions, normals, UVs and indices).

Run from the repository root:

```sh
make run/win32-debug GAME=examples/bush-generator   # Windows Debug
make run GAME=examples/bush-generator               # current host debug
```

## How it works

`procgen.generateMesh("mesh.bush", p, gfx)` returns a Result projection whose
`value` is a GPU `Mesh`; use `procgen.buildMesh("mesh.bush", p)` for the CPU
`MeshBuild`. Check `ok` before reading either `value`. The generator
clusters a set of irregular foliage lobes under a dome silhouette, adds a few
woody twigs, and grows loose leaf cards over each lobe's outer surface.

The mesh uses a two-region atlas, matching the `mesh.tree` convention:

| Region | UV.u  | Content |
|--------|-------|---------|
| Twigs / bark | `[0.0, 0.45]` | woody emergent stems |
| Foliage | `[0.55, 1.0]`  | lobes + leaf cards |

## Parameters

| Key | Default | Meaning |
|-----|---------|---------|
| `seed` | (from `newParams`) | deterministic source |
| `style` | `mound` | `mound` (flattened dome) or `sphere` (round ball) |
| `leafMode` | `mixed` | `blobs` \| `cards` \| `mixed` \| `none` |
| `height` | `1.4` | overall height |
| `width` | `2.2` | overall width (diameter) |
| `blobs` | `9` | number of foliage lobes |
| `rings` | `3` | lobe ring resolution |
| `radialSegments` | `7` | lobe radial resolution |
| `leafDensity` | `0.62` | fraction of loose leaf cards |
| `lobeScale` | `0.68` | relative size of each foliage lobe |
| `irregularity` | `0.62` | strength of low-frequency silhouette breakup |
| `leafSize` | `0.16·height` | leaf-card size |
| `twigs` | `4` | emergent woody twigs (0 disables) |
| `twigLength` | `0.30·height` | twig length |

Squirrel example:

```squirrel
local paramsResult = procgen.newParams();
if (!paramsResult.ok) throw paramsResult.status.summary;
local p = paramsResult.value;
p.setSeed(20260815);
p.setString("style", "mound");
p.setString("leafMode", "mixed");
p.setFloat("height", 1.7);
local meshResult = procgen.generateMesh("mesh.bush", p, gfx);
if (!meshResult.ok) throw meshResult.status.summary;
local mesh = meshResult.value;
```

## Live controls

The example exposes silhouette and foliage modes plus sliders for width,
height, lobe count/scale, irregularity, leaf density/size, and twig count/length.
Every edit rebuilds the native deterministic mesh immediately. **New seed**
changes the variation; **Reset** restores the authored showcase values.

## Assets & attribution

The default texture `assets/bush_atlas.png` is a two-tone atlas (brown twig
half + green foliage half) **derived from the color palette** of the
[Kenney "Mini Forest"](https://kenney.nl/assets/mini-forest) pack. The original
`assets/kenney/kenney_mini-forest_colormap.png` and its
`assets/kenney/LICENSE.txt` are included.

> Kenney assets are released under **CC0 1.0 (public domain)** — no
> attribution required, but appreciated. See `assets/kenney/LICENSE.txt` for the
> full license text.
