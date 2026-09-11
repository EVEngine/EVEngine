# Venom Transform Demo

Interactive approximation of the Bilibili / Unity "毒液变装" look
([BV12H4y1D7qf](https://www.bilibili.com/video/BV12H4y1D7qf)): glossy black
symbiote coverage crawling up a textured character with an emissive frontier,
tendril ribbons, and splash droplets.

## What this proves

EVEngine can assemble that effect from existing pieces:

| Layer | Implementation here |
| --- | --- |
| Body coverage | Custom mesh fragment shader (`shaders/venom.frag`) — height + fbm noise mask |
| Black goo look | Dark albedo + high gloss specular + cool fresnel |
| Frontier glow | Magenta/purple emissive band along the coverage edge |
| Tendrils | Animated thin cylinders around the active frontier |
| Droplets | Small spheres emitted near the front |
| Host mesh | Quaternius Superhero Female (CC0) via `model3d` + custom material shader |

This is intentionally a **shader + props** demo, not a volumetric metaball
solver. That matches the Unity reference technique more closely than a full
fluid simulation would.

## Character asset

`assets/quaternius/` vendors the CC0 Quaternius Universal Base Characters
female FullBody glTF (same pack as `examples/character-motion-lab`). See
`assets/quaternius/LICENSE-CHARACTERS.txt` and
[quaternius.com](https://quaternius.com).

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

- `main.nut` — scene, character load, tendrils, auto-capture
- `shaders/venom.frag` — coverage / goo / edge shader (samples albedo)
- `config.nut` — window + modules (`model3d`)
- `assets/quaternius/` — CC0 character glTF + textures
- `captures/` — PNG outputs from the auto demo

## Captures

Auto-demo frames (Lavapipe + Xvfb):

| coverage | file |
| --- | --- |
| 0% | `captures/venom-00-c0.png` |
| 28% | `captures/venom-01-c28.png` |
| 55% | `captures/venom-02-c55.png` |
| 82% | `captures/venom-03-c82.png` |
| 100% | `captures/venom-04-c100.png` |
