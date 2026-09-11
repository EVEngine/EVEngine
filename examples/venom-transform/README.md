# Venom Transform Demo

Interactive approximation of the Bilibili / Unity "毒液变装" look
([BV12H4y1D7qf](https://www.bilibili.com/video/BV12H4y1D7qf)): glossy black
symbiote coverage crawling up a mannequin with an emissive frontier, tendril
ribbons, and splash droplets.

## What this proves

EVEngine can assemble that effect from existing pieces:

| Layer | Implementation here |
| --- | --- |
| Body coverage | Custom mesh fragment shader (`shaders/venom.frag`) — height + fbm noise mask |
| Black goo look | Dark albedo + high gloss specular + cool fresnel |
| Frontier glow | Magenta/purple emissive band along the coverage edge |
| Tendrils | Animated thin cylinders around the active frontier |
| Droplets | Small spheres emitted near the front |
| Host mesh | Primitive mannequin (sphere / cylinder), same path as any `Renderable3D` |

This is intentionally a **shader + props** demo, not a volumetric metaball
solver. That matches the Unity reference technique more closely than a full
fluid simulation would.

## Run

```bash
# from repo root, after build/linux-debug is ready
make run/linux-debug GAME=examples/venom-transform
```

Headless / CI-style capture (Lavapipe + Xvfb):

```bash
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
export XDG_RUNTIME_DIR=/tmp/xdg-runtime
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"
export ALSOFT_DRIVERS=null
xvfb-run -a make run/linux-debug GAME=examples/venom-transform
```

On launch the demo auto-plays coverage `0 → 1`, writes PNGs under
`captures/`, then closes the window.

## Controls

| Key | Action |
| --- | --- |
| Space | Play / pause coverage |
| Left / Right | Scrub coverage ±0.05 |
| R | Reset to 0 and play |
| P | Save `captures/venom-manual-*.png` |

## Files

- `main.nut` — scene, mannequin, tendrils, auto-capture
- `shaders/venom.frag` — coverage / goo / edge shader
- `config.nut` — window + modules
- `captures/` — PNG outputs from the auto demo
