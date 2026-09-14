# SilPOM vs SSDM comparison (dihedral corners)

Three brick **corners** (two faces meeting at 90°) under a grazing camera.
A crease is where the public techniques diverge most clearly:

| Corner | Technique | What you should notice |
| --- | --- | --- |
| Left (warm) | Classic **POM** | Depth inside each face; **flat** geometric crease |
| Mid (green) | **SilPOM** | POM + discard when displaced UV leaves the chart → **gaps / bitten silhouette along the crease** |
| Right (cool) | **SSDM-style** | POM shading + `gl_FragDepth` pull → **crease stays filled**, depth wins against the floor |

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
| `1` / `2` / `3` | Focus POM / SilPOM / SSDM corner |

## Implementation notes

- Shared CPU references: `src/modules/graphics/ParallaxMap.h`
- Shared GLSL helpers: `src/modules/graphics/shaders/parallax_map.glsl`, `ssdm.glsl`
- Demo shader is self-contained (`shaders/compare.frag`)
- Each corner face is its own UV chart `[0,1]^2` (crease is `u=0` on both) so SilPOM can clip there
- SSDM path uses per-face TBN + POM for UVs (works on both +Z and +X walls) and FragDepth for occlusion — it does **not** discard on chart exits, which is the point of the crease comparison
- Educational planar approximation of SSDM, not a full-scene post pass

## Floor contact

Corners sit slightly above the floor; SSDM fades relief near the chart bottom and
never opens the bottom silhouette, so the floor cannot show through the contact line.
