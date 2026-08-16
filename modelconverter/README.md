# Model Converter (EVEngine ↔ Blender)

A plugin feature that drives **Blender's Python package (`bpy`)** to turn a
player's **primitive model** (a simple box, sphere, etc.) into a **richer mesh**
(stone, stone wall, …) automatically. Converters are plain Python scripts or
Blender **Geometry Node** groups, so artists can tweak them without recompiling.

```
primitive box  →  EVEngine native plugin  →  python -m eve_blender_converter (bpy)
                     → converter (script | geometry nodes) → bake → export .glb
                       → model3d loads & renders in EVEngine
```

## Layout

```text
modelconverter/
  plugin/             C++ native plugin (ModelConverter module, EVPlugins)
  python/             Python runtime package (eve_blender_converter, imports bpy)
  converters/         Editable converter catalog (manifest.json + script/.blend)
    stone/            box → low-poly stone (script)
    stone_wall/       box → stone wall (script)
    gn_displace/      geometry-node demo (needs .blend, see scripts)
  scripts/            build_nodegroup_blend.py (generates gn_displace.blend)
examples/model-converter/   end-to-end EVEngine example (main.nut)
docs/dev/模型转换器插件设计.md  design doc
```

## Prerequisites

1. `pip install bpy` into the Python that will run the runtime
   (see `python/requirements.txt`). `bpy` is Blender's official importable Python
   package, including the Geometry Nodes engine. Recent `bpy` (>= 5.x) requires
   CPython **3.13** (e.g. `py -3.13 -m pip install bpy`).
2. (Optional) Build the node-group demo blend once:
   `py -3.13 modelconverter/scripts/build_nodegroup_blend.py`
   (or `blender --background --python ...`)
3. Build the plugin against an EVEngine SDK:
   `cmake -B build -DEVEngine_DIR=<sdk>/cmake && cmake --build build`

## Usage (Squirrel)

```squirrel
plugins.load("build/modelconverter.dll");
mc <- eve.ModelConverter();
mc.configure("modelconverter/converters", "python", "modelconverter/python");

local p = mc.newParams();
p.setInt("seed", 1847);
p.setFloat("size", 1.25);
p.setFloat("detail", 0.6);
if (mc.convert("stone", "box.obj", "out/stone.glb", "glb", p)) {
    local md = model3d.newModelDataFromFile("out/stone.glb");
    print(md.getVertexCount() + " verts\n");
}
```

Point `pythonExe` (2nd arg of `configure`) at a Python with `bpy` installed, e.g.
`py -3.13` if `bpy` 5.x is installed there. Run the interactive demo as
`examples/model-converter`. `mc.check()` reports whether `bpy` is available so
you can degrade gracefully when Blender isn't installed.

## Converters

Each folder under `converters/` is a converter:

- **script** — `convert.py` with `def convert(ctx)`; `ctx` exposes `obj`,
  `params`, and `ctx.mesh_ops` helpers (subdivide, weighted displacement, solidify,
  triangulate, unwrap, …). Deterministic per `seed`.
- **nodegroup** — a `.blend` holding a Geometry Node group; the runtime appends
  it, binds it to the mesh, and applies it. Generate the sample via the script above.

Add a new converter by dropping a folder in `converters/` — no code changes needed.
