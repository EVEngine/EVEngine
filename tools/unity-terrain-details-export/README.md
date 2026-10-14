# Unity Terrain detail export

Copy `Editor/ExportEvEngineTerrainDetails.cs` into an `Editor` directory in the
source Unity project. Select the Terrain GameObject and run **Tools > EVEngine >
Export Selected Terrain Details**. Keep the default adjacent name
`<TerrainData path>.eve-details.json`; EVEngine discovers it automatically when
`--terrain` selects that TerrainData. A different location can be selected with
`eve asset import --from unity --terrain-details <project-relative path>`.

The exporter requires Unity 2021.2 or newer. It calls the public
`TerrainData.ComputeDetailInstanceTransforms` API for every prototype and patch,
so Unity remains authoritative for density, noise, random scale, ground alignment,
position jitter, and scatter-mode behavior. The sidecar stores canonical EVEngine
coordinates and schema `eve.unity-terrain-details/3`, including prototype
appearance and Terrain waving-grass globals. The importer validates the whole
file before adding any instances and continues to read schema versions 1 and 2.
