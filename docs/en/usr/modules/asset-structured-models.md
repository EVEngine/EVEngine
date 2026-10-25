# SCAD and LDraw model import

[中文](../../../usr/modules/asset-structured-models.md) · [Module handbook](../MODULES.md)

These are offline asset importers: source → `.eva` → cook → `.evpack` → native static prefab.
They do not add direct `Model3D.load("model.scad")` or `Model3D.load("model.ldr")` script methods.
Shipped games need neither OpenSCAD nor the LDraw parts library.

## SCAD

Install [OpenSCAD](https://openscad.org/downloads.html), or unpack its portable executable.
The selected compiler provides the SCAD language, CSG and include/use/import support.
EVEngine launches an argument vector without a shell, requests plain binary STL, and validates
the complete result before publishing. Missing compiler/output, timeout, nonzero exit,
nonfinite coordinates and malformed meshes fail explicitly.
`--hardwarnings` also rejects compiler warnings such as missing include files.

```sh
eve asset import test/fixtures/structured_models/cup.scad --from scad \
  --openscad /absolute/path/to/openscad --out cup.eva \
  --package-id 018f6f22-2490-7ad2-bf58-4f1dbca31040 --name demo.cup
eve asset validate cup.eva
eve asset cook cup.eva --target windows-x86_64-vulkan --out cup.evpack
```

On Windows, quote a path such as `"C:/Program Files/OpenSCAD/openscad.exe"`.
`--scad-timeout` defaults to 120 seconds and accepts 1–3600.
Millimetres/Z-up become metres/Y-up, with flat geometric normals. STL carries geometry only:
SCAD colours, UVs, rigs and animation are not translated. The root SCAD text is retained;
external dependencies are not archived automatically. Keep those files for reimport.
OpenSCAD can read its ordinary local dependencies; this authoring operation is not a sandbox.

## LDraw

The native importer accepts LDR/DAT and MPD FILE/NOFILE sections. It reads LDraw text files
under the input directory. An optional library root supplies `parts/`, `p/` and `LDConfig.ldr`:

```sh
eve asset import test/fixtures/structured_models/blocks.mpd --from ldraw \
  --out blocks.eva --package-id 018f6f22-2490-7ad2-bf58-4f1dbca31041 --name demo.blocks
eve asset cook blocks.eva --target windows-x86_64-vulkan --out blocks.evpack

eve asset import my-model/model.ldr --from ldraw --ldraw-library /absolute/path/to/ldraw \
  --out model.eva --package-id 018f6f22-2490-7ad2-bf58-4f1dbca31042
```

Type-1 references expand recursively with their affine transforms. Type-3 triangles and
type-4 quads produce colour-separated meshes/materials; BFC winding and INVERTNEXT apply.
Colour 16 inherits its parent (root defaults to 7). Basic colours 0–15 are built in;
additional colours use `!COLOUR`, including alpha, or direct RGB codes. Base colours become
linear PBR values. One LDU is 0.4 mm; `(x,y,z)` becomes `(x,-y,-z)` in metres.
Local `!COLOUR` definitions apply from their declaration, propagate to subfiles and expire on return.
Unreferenced files and the alternative `LDCfgalt.ldr` palette do not override the selected default palette.

Case-insensitive ASCII part paths, backslashes and relative/parts/p/models resolution are supported.
Missing/cyclic references, singular transforms, unsafe paths, ambiguous names, bad geometry
and exceeded budgets fail. Lines and conditional edges (types 2/5), special finishes and
luminance remain source-only with report findings. TEXMAP/embedded geometry is rejected.
Transforms are baked; editable part trees, LEGO connectivity and physics are not generated.
Used source files are retained; libraries are not downloaded. Check their redistribution permissions.
The bundled MPD uses original box parts and needs no external library.

## Runtime and identity

The output contains `eve.mesh/2`, `eve.material/15` and `eve.scene-template/3`.
`default` identifies the static prefab; `mesh:<part>` identifies individual meshes.
Use the existing native `EvpackResourceReader` and `EvpackStaticPrefab::load` APIs; there is
no generic Asset script loader. Consult Doxygen for lifetime and graphics-thread requirements.
Reuse the package UUID on reimport for stable part identities. Preparation never mutates live
ECS/GPU/editor state. Encoding is deterministic for identical inputs; OpenSCAD output depends
on its compiler version and dependencies. Conversion findings live in `reports/import.json`.
