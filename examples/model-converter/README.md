# Model Converter demo

End-to-end EVEngine example for the `modelconverter` plugin: it turns a primitive
`box.obj` into a **stone** or **stone wall** by driving Blender's Python package
(`bpy`), then reloads the produced `.glb` back into EVEngine with `model3d`.

## Prerequisites

1. `pip install bpy`
2. Build the plugin against an EVEngine SDK (see `modelconverter/README.md`),
   place the resulting `modelconverter.dll` at `build/modelconverter.dll`
   relative to this example, or set `modelconverter_plugin` in `config.nut`.
3. If `python` on your PATH has `bpy`, leave `modelconverter_python = "python"`;
   otherwise point it at the interpreter that has `bpy` installed.

## Run

Launch it like the other examples, selecting `examples/model-converter` as the game.

If `bpy` is unavailable the example still runs; the UI will show the clear
`pip install bpy` error instead of a converted model.

## Controls

- **Next converter** — switch `stone` / `stone_wall` / `gn_displace`.
- **Re-convert (new seed)** — advance the deterministic seed and rebuild.
- **Re-convert (same seed)** — rebuild with the current seed.
