# SilPOM vs SSDM — full planar algorithms

Side-by-side comparison of **classic POM**, **SilPOM**, and **planar SSDM**
on extruded brick cards with hard flat height plateaus.

| Card | Technique | What you should notice |
| --- | --- | --- |
| Left (warm) | Classic **POM** | Internal brick depth; **straight geometric** silhouette |
| Mid (green) | **SilPOM** | POM shading on the front; **jagged brick-cap** side limb |
| Right (cool) | **Planar SSDM** | Geometric heightfield march; **continuous** extruded outline |

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
- Shared GLSL helpers: `src/modules/graphics/shaders/parallax_map.glsl`,
  `ssdm.glsl`
- Demo shader is self-contained (`shaders/compare.frag`):
  - Heightmap is **hard flat brick / mid-depth mortar** (no soft sine pillows)
  - **SilPOM**: steep POM on the front (no horizon trim); side faces march and
    keep brick caps only (jagged limb). Front keeps geometric depth.
  - **SSDM**: heightfield march; `hit.z` is a hit flag (not softCoverage);
    front misses chart-fill, side misses discard; front keeps geometric depth
    (cliff FragDepth opens 镂空 shells)
- Meshes: thin front card for classic POM; extruded slab for SilPOM/SSDM
- `gl_FragDepth` only pulls toward the camera (Vulkan RH_ZO)

## Honesty bound

True screen-space SSDM as a deferred/post pass (pyramid mip + screen warp over
an already-rendered buffer) is **not** shipped as an engine post here. The demo
implements the **geometrically correct planar** form: raymarch the heightfield
inside the extruded volume and write depth from the hit.
