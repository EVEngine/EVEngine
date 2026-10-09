# Unreal uasset converter

This tool asks the user's installed Unreal Editor to load project assets and export
them through Epic's glTF Exporter. It does not parse or reverse engineer `.uasset`
files. EVEngine can load the resulting `.glb`/`.gltf` through its existing Model3D
path.

The source UE 5 project must enable **Python Editor Script Plugin** and **GLTF Exporter**.
Static Meshes, Animation Sequences and Skeletal Meshes are supported. For animation sequences,
the exporter includes the preview skeletal mesh and vertex skin weights so the
result is self-contained for runtime animation loading.

Material baking uses the installed GPU through `-AllowCommandletRendering` and
exports PNG images. Shader compilation may take time on the first run; NullRHI
must not be used for material export. GLB normalized integer skin weights are
expanded to FLOAT before publication for the current Model3D reader. Conversion
diagnostics and final artifact hashes are recorded in the manifest. This weight
compatibility conversion does not currently apply to sidecar `.gltf` output.
Use `--texture-size 512` for compact example content or keep the 1024 default
for higher-resolution production assets.

```powershell
python tools/unreal-uasset-converter/unreal_uasset_converter.py `
  --project C:\Projects\OwnedAssets\OwnedAssets.uproject `
  --uasset C:\Projects\OwnedAssets\Content\Animations\Vault.uasset `
  --output C:\Projects\MyGame\assets\vault `
  --texture-size 1024 `
  --rights-confirmed
```

An Unreal content reference works as well and is required for plugin-mounted assets:

```powershell
python tools/unreal-uasset-converter/unreal_uasset_converter.py `
  --project C:\Projects\OwnedAssets\OwnedAssets.uproject `
  --asset /Game/Animations/Vault `
  --asset /Game/Animations/Mantle `
  --output C:\Projects\MyGame\assets\climbing `
  --rights-confirmed
```

For large libraries, `--asset-list assets.txt` accepts one UTF-8 reference per
line, while `--asset-audit audit.json` converts every `StaticMesh` recorded by
the repository's `ue_audit_assets.py`. Blank lines and `#` comments are ignored
in list files. Add `--preserve-path-names` when packages contain duplicate
basenames; mounted package paths are encoded into deterministic output names.
An asset proven by Unreal's own export log to be unloadable in isolation may be
documented and omitted with `--exclude-asset /Game/Path/Asset`; exclusions are
applied after list/audit expansion and are never implicit.

Use `--unreal-editor` if automatic discovery does not find the desired engine.
`--dry-run` prints the exact Unreal command and versioned request without loading
or exporting content.

## Rights and publication boundary

`--rights-confirmed` is required for a real conversion. It confirms that the user
has permission to convert every requested asset for use outside Unreal Engine.
The converter does not download assets, copy source `.uasset` files, or bypass
content labels. UE-Only content is not an appropriate input for an EVEngine asset
pipeline unless the user has separate permission covering that use.

## Output contract

The output directory must not already exist. Unreal writes into a sibling staging
directory; the converter validates every glTF/GLB and publishes the whole directory
only after all requested assets succeed. Failures therefore leave no partial output.

`conversion.manifest.json` uses schema `eve.unreal-animation-conversion/1` for
animation-only batches and `eve.unreal-asset-conversion/1` for batches containing
static meshes. Readers
must reject unknown schema versions and may ignore unknown fields within version 1.
Every artifact records its source content reference, Unreal asset class, byte size,
and SHA-256 digest. The source `.uproject` name is recorded, but machine-local source
paths are deliberately omitted.

Version 1 has no predecessor and therefore no migration. A future version must use a
new schema id suffix and provide an explicit offline migration if compatibility is
needed.

## Static Mesh parity audit

After an Unreal-side audit and conversion, cross-check every Static Mesh artifact:

The runnable sample lives in the separate [EVEngine/medieval-docks](https://github.com/EVEngine/medieval-docks)
repository. Paths below refer to its adjacent local checkout; licensed assets and audits remain local.

```powershell
python tools/unreal-uasset-converter/audit_static_mesh_parity.py `
  --ue-audit ../medieval-docks/ue-asset-audit.json `
  --manifest ../medieval-docks/assets/medieval-docks-complete/conversion.manifest.json `
  --output tools/unreal-uasset-converter/reports/medieval-docks-static-mesh-parity.json
```

The audit verifies file size and SHA-256, non-empty meshes and primitives, material
assignment on every primitive, and that referenced GLB material names trace back to
materials assigned in UE. Extra UE material slots that are not referenced by exported
geometry are reported as unused. Assets using water, GodRay, or backdrop shader models
without an ordinary base-color texture are reported separately for renderer review.

Generate deterministic EV vegetation-wind inputs from the Unreal mesh/material
audits (this does not claim shader-graph parity):

```powershell
python tools/unreal-uasset-converter/build_foliage_wind_profiles.py `
  --asset-audit ../medieval-docks/ue-asset-audit.json `
  --material-audit ../medieval-docks/ue-material-audit.json `
  --manifest ../medieval-docks/assets/medieval-docks-complete/conversion.manifest.json `
  --output ../medieval-docks/foliage-wind-profiles.nut
```

The generated table converts audited UE bounds from centimetres to metres and
retains the effective wind direction, distance, branch/leaf controls, and source
material choice for every eligible Static Mesh. Runtime code must still select a
fragment path that preserves the imported foliage PBR and translucency model.

Generate the native-water projection from the audited effective material:

```powershell
python tools/unreal-uasset-converter/build_water_material_profile.py `
  --material-audit ../medieval-docks/ue-material-audit.json `
  --output ../medieval-docks/water-material-profile.nut
```

The output records otherwise unsupported roughness, specular, absorption, and
scattering values as audit evidence. Compatible spatial controls are converted
from centimetres to metres; cross-model foam and caustics energy adapters are
explicit in the generator instead of being hidden scene-tuning constants.

## Tests

```powershell
python -m unittest scripts.tests.test_unreal_uasset_converter -v
```

The automated suite uses synthetic GLB bytes and a fake Unreal process. A local
integration smoke can use any self-authored or otherwise cross-engine-licensed UE
animation project.
