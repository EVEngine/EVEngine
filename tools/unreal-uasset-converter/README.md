# Unreal uasset animation converter

This tool asks the user's installed Unreal Editor to load project assets and export
them through Epic's glTF Exporter. It does not parse or reverse engineer `.uasset`
files. EVEngine can load the resulting `.glb`/`.gltf` through its existing Model3D
animation path.

The source UE 5 project must enable **Python Editor Script Plugin** and **GLTF Exporter**.
Animation Sequences and Skeletal Meshes are supported. For animation sequences,
the exporter includes the preview skeletal mesh and vertex skin weights so the
result is self-contained for runtime animation loading.

```powershell
python tools/unreal-uasset-converter/unreal_uasset_converter.py `
  --project C:\Projects\OwnedAssets\OwnedAssets.uproject `
  --uasset C:\Projects\OwnedAssets\Content\Animations\Vault.uasset `
  --output C:\Projects\MyGame\assets\vault `
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

`conversion.manifest.json` uses schema `eve.unreal-animation-conversion/1`. Readers
must reject unknown schema versions and may ignore unknown fields within version 1.
Every artifact records its source content reference, Unreal asset class, byte size,
and SHA-256 digest. The source `.uproject` name is recorded, but machine-local source
paths are deliberately omitted.

Version 1 has no predecessor and therefore no migration. A future version must use a
new schema id suffix and provide an explicit offline migration if compatibility is
needed.

## Tests

```powershell
python -m unittest scripts.tests.test_unreal_uasset_converter -v
```

The automated suite uses synthetic GLB bytes and a fake Unreal process. A local
integration smoke can use any self-authored or otherwise cross-engine-licensed UE
animation project.
