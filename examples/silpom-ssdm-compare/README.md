# SilPOM vs SSDM comparison (cylinders)

Three brick **cylinders** under a grazing camera. A cylinder UV chart wraps once
around the barrel (`u` seam at 0/1) and is open at the top/bottom (`v` limbs):

| Cylinder | Technique | What you should notice |
| --- | --- | --- |
| Left (warm) | Classic **POM** | Depth on the barrel; **geometric** silhouette; seam can wrap |
| Mid (green) | **SilPOM** | POM + discard when displaced UV leaves the chart → **seam gaps** and **bitten top/bottom limbs** |
| Right (cool) | **SSDM-style** | POM shading + `gl_FragDepth` pull → **seam stays filled**, depth wins against the floor |

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
| `1` / `2` / `3` | Focus POM / SilPOM / SSDM cylinder |

## Implementation notes

- Shared CPU references: `src/modules/graphics/ParallaxMap.h`
- Shared GLSL helpers: `src/modules/graphics/shaders/parallax_map.glsl`, `ssdm.glsl`
- Demo shader is self-contained (`shaders/compare.frag`)
- Mesh: `gfx.newMeshCylinder(64, 1, false)` — no caps, so the comparison is the sidewall chart
- POM/SSDM wrap `u` when sampling (continuous seam); SilPOM does **not**, so chart exits open the seam and bite the limbs
- SSDM path never discards on chart exits; FragDepth handles occlusion vs the floor
- Educational planar/TBN approximation of SSDM, not a full-scene post pass

## Floor contact

Cylinders sit slightly above the floor; SSDM fades relief near the chart bottom and
never opens the bottom silhouette, so the floor cannot show through the contact line.
