# SilPOM vs SSDM comparison

Side-by-side demo of the two public techniques people associate with
*Crimson Desert* surface relief (Pearl Abyss has not published their exact path):

| Panel | Technique | What you should notice at grazing angles |
| --- | --- | --- |
| Left (warm) | Classic **POM** | Convincing depth *inside* the face; **flat** geometric silhouette |
| Mid (green) | **SilPOM** | Same POM depth, but fragments whose displaced UV leave the mesh chart are discarded → silhouette follows the height field at UV borders |
| Right (cool) | **SSDM-style** | View-space slab ray-march + `gl_FragDepth` → silhouette can change without being bound to UV clipping |

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
| `1` / `2` / `3` | Focus POM / SilPOM / SSDM panel |

## Implementation notes

- Shared CPU references: `src/modules/graphics/ParallaxMap.h`
  (`silPomCoverage`, `ssdmScreenOffset`, `ssdmCoverage`)
- Shared GLSL helpers: `src/modules/graphics/shaders/parallax_map.glsl`
  (`silPomCoverage`, `parallaxMappedUVSilhouette`) and
  `src/modules/graphics/shaders/ssdm.glsl`
- Demo shader is self-contained (`shaders/compare.frag`) so it does not depend
  on engine include paths at runtime compile time.
- The SSDM panel is an **educational approximation** of classic screen-space
  displacement (planar slab + depth write), not a full-scene post-process SSDM
  pass. It is enough to contrast silhouette behaviour with SilPOM.

## Floor contact / occlusion

The right panel used to look like the floor covered the wall for three separate reasons:

1. **Vulkan ZO depth** — `gl_FragDepth` must be written in `[0,1]` NDC (not OpenGL's `*0.5+0.5`).
2. **No hole punching** — SSDM misses fall back to POM on the card; discarding let the floor show through.
3. **Contact layout** — cards sit a few centimeters above the floor, and SSDM fades relief near the chart bottom so parallax does not look like it continues under the floor plane.
