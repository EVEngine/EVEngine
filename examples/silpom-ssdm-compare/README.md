# SilPOM vs SSDM — full planar algorithms

Side-by-side comparison of **classic POM**, **full SilPOM**, and **full planar
SSDM** on extruded brick cards.

| Card | Technique | What you should notice |
| --- | --- | --- |
| Left (warm) | Classic **POM** | Internal brick depth; **straight geometric** silhouette |
| Mid (green) | **Full SilPOM** | Steep POM + **chart-border** limb discard + height normals + self-shadow + `FragDepth` → **jagged brick silhouette** (no interior horizon punch-through) |
| Right (cool) | **Full planar SSDM** | Model-space heightfield march; side-face misses discard so brick caps form a **continuous extruded outline** |

Cards are **extruded slabs** (local Z = height axis). That is the correct domain
for silhouette-changing relief. Cylinders are not — a wrapped UV chart cannot
grow the outline, and a constant depth bias is not SSDM.

## Run

```sh
make run/linux-debug GAME=examples/silpom-ssdm-compare
```

Headless smoke (Cloud VM):

```sh
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
export ALSOFT_DRIVERS=null
export XDG_RUNTIME_DIR=/tmp/xdg-runtime
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"
xvfb-run -a scripts/smoke_examples.sh silpom-ssdm-compare
```

## Controls

| Key | Action |
| --- | --- |
| `O` | Toggle auto-orbit |
| `A` / `D` | Yaw |
| `W` / `S` | Pitch |
| `[` / `]` | Relief scale |
| `-` / `=` | Max ray-march layers |
| `1` / `2` / `3` | Focus POM / SilPOM / SSDM |

## Implementation notes

- Shared CPU references: `src/modules/graphics/ParallaxMap.h`
  (offset POM, hard/soft SilPOM coverage, horizon trim, SSDM screen offset)
- Shared GLSL helpers: `src/modules/graphics/shaders/parallax_map.glsl`,
  `ssdm.glsl` (steep POM, soft coverage, horizon trim, height normals,
  self-shadow, planar heightfield march, screen-warp helpers)
- Demo shader is self-contained (`shaders/compare.frag`):
  - **SilPOM**: steep POM + soft chart-**border** limb discard (interior stays
    opaque — full-face horizon trim punched 镂空) + height normals + self-shadow
    + `gl_FragDepth`
  - **SSDM**: model-space heightfield march; **front** misses `chartFill` (solid
    mortar), **side** misses discard so brick caps extrude the silhouette
- Meshes: thin front card for classic POM; extruded slab for SilPOM/SSDM
- `gl_FragDepth` only pulls toward the camera (Vulkan RH_ZO) so relief never
  punches holes in the floor
- Height texture uses moderate soft brick ramps (steep enough for edge pop,
  soft enough to limit cliff-tunnel shells)

## Honesty bound

True screen-space SSDM as a deferred/post pass (pyramid mip + screen warp over
an already-rendered buffer) is **not** shipped as an engine post here. The demo
implements the **geometrically correct planar** form: raymarch the heightfield
inside the extruded volume and write depth from the hit.
