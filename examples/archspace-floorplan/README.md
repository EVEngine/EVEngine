# ArchSpace Floor Plan

Builds a compact apartment document — living, bedroom, bath, kitchen, foyer —
with several doors and windows, bakes a renderer-neutral mesh (opening cutouts),
uploads it through `gfx.newMeshFromArrays`, and writes `archspace-floorplan.png`.

```sh
cd examples/archspace-floorplan
../../build/linux-debug/src/engine/eve run
```

Headless smoke (Linux VM):

```sh
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
export ALSOFT_DRIVERS=null
export XDG_RUNTIME_DIR=/tmp/xdg-runtime && mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"
xvfb-run -a ../../build/linux-debug/src/engine/eve run
```

Success markers:

- stdout contains `ARCHSPACE_FLOORPLAN_PASS`
- `archspace-floorplan.png` is written after a few frames

Design notes: [`docs/dev/2026-10-15-archspace-multi-topology-projection.md`](../../docs/dev/2026-10-15-archspace-multi-topology-projection.md).
