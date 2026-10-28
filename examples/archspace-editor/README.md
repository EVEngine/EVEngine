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
