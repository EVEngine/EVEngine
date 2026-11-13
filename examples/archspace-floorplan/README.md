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

## 2026-11-13 画面复查

保留 ArchSpace 文档烘焙、开口和目录家具；按完整几何块分材质，展开三角面法线以明确硬边，增加分区地板、板缝、展示底座和 W 墙体显隐。

[前后截图与运行验证](../../docs/images/examples/review-2026-11-13/nine-improved/README.md)。
