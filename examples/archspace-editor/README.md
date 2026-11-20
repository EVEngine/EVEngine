# ArchSpace Editor example

Builds a small apartment document (rooms, corridor wall, door, window, desk),
bakes a renderer-neutral mesh with opening cutouts, uploads it through
`gfx.newMeshFromArrays`, and saves `archspace-editor.png`.

```sh
cd examples/archspace-editor
../../build/linux-debug/src/engine/eve run
```

Success markers:

- stdout contains `ARCHSPACE_PASS`
- `archspace-editor.png` is written after a few frames

C++/automation coverage: `test/editor_archspace_target.cpp`.
Design notes: `docs/dev/2026-09-14-archspace-editor.md`.

## 2026-11-13 画面复查

保留文档创建、门窗开口和目录家具验证；固定可读视角，补地板、家具配色和底座，修正面法线；W 可隐藏整块墙体以检查室内。

[前后截图与运行验证](../../docs/images/examples/review-2026-11-13/nine-improved/README.md)。
