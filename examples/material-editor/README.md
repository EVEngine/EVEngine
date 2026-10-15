# Material Editor

Project-composed material studio: a UE5-style Material Sphere sits in the center
viewport while the right inspector edits a live `gfx.Material`. There is no fixed
C++ material-editor window — the Squirrel host owns layout, widgets, and preview.

Run:

```sh
# Linux (headless Cloud VM)
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
export ALSOFT_DRIVERS=null
export XDG_RUNTIME_DIR=/tmp/xdg-runtime && mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"
cd examples/material-editor && xvfb-run -a ../../build/linux-debug/src/engine/eve run

# Windows
make run/win32-debug GAME=examples/material-editor
```

## Layout

- **Top** — material presets (Plastic / Rubber / Brushed / Chrome / Gold / Copper / Glass) and turntable pause
- **Center** — embedded 3D viewport with the UE5 Material Sphere (`gfx.newMeshSphere(64, 32)`), studio IBL cubemap (so metallic presets stay readable), key/fill/rim lights, and a neutral shadow floor
- **Right** — schema-aligned knobs mirroring `MaterialDocumentTarget`: shading model, tint, metallic, roughness, surface/blend mode, alpha cutoff, parallax, lighting and shadow flags, plus preview mesh switch (Sphere / Cube / Cylinder)

## Controls

- LMB drag in the viewport to orbit
- Mouse wheel to zoom
- Auto turntable (Pause spin in the toolbar)
- Inspector edits apply immediately to the shared `Material` on the preview ball

Metallic / chrome / gold need image-based lighting: ambient alone is scaled by `(1 - metallic)`. The example bakes a six-face studio sky via `gfx.newReflectionProbeCapture` and attaches it with `camera.setEnvMap`.

## Files

| File | Role |
|---|---|
| `config.nut` | Window title / size / hot reload |
| `main.nut` | Workspace panels, UE5 sphere preview, live Material binding |
| `material-editor.png` | Auto-captured frame after a few frames (`gfx.saveFramePng`) |

Knobs write one authoritative `Material` attached via `Renderable3D.setMaterial`; legacy tint/metallic/roughness fields stay synchronized for paths that still read MeshRenderer state.
