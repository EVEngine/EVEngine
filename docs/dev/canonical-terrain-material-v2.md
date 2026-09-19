# Canonical terrain material schema v2

This schema is retained as the sole compatibility input for v3. New imports emit v3; see
[canonical-terrain-material-v3.md](canonical-terrain-material-v3.md).

Schema v2 extends each terrain layer with the state consumed by TVE 12.6's
`TVETerrain.CopyLayerSettings`: `maskSource`, two-axis `tileScaleMeters` and
`tileOffsetMeters`, RGBA `maskRemapMinimum`/`maskRemapMaximum`, RGBA
`specular`, `metallic`, signed `normalScale`, and `smoothness`. The existing
diffuse, normal, weight, normal convention, and scalar tile-size fields remain
available to older native terrain consumers.

The root owns `holesSource`, exactly four `controlSources`, and a positive
`boundsMultiplier`. Empty source strings mean that no source texture was
assigned. These are stable source identities; runtime texture resolution and
GPU ownership stay with the terrain renderer rather than this metadata loader.

All numbers must be finite. Tile scales and bounds are positive; metallic and
smoothness are in `[0,1]`; normal scale is in `[-8,8]`. Import and runtime load
build an owning candidate and publish it only after every layer passes.

Unity TerrainData maps its holes and alphamap textures directly, rejects more
than TVE's four control textures, and preserves TerrainLayer mask/remap,
specular, material and independent X/Y coordinate values. The canonical Z-axis
reflection applies to geometry and instances; texture coordinate values remain
in their authored terrain units.

`eve.terrain-material/1` is the only migration input. Migration preserves all
legacy and vendor fields, derives the two-axis scale from `tileSizeMeters`, and
writes explicit neutral defaults for the newly versioned TVE state. It rejects
v1 data that already contains any v2 member.

