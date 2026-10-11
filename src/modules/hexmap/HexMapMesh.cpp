#include "hexmap/HexMapMesh.h"

namespace eve::hexmap {

void buildChunkSurfaceMesh(const HexMap& map, std::int32_t chunkIndex, HexSurface surface, HexMeshData& out) {
    switch (surface) {
        case HexSurface::Terrain: buildTerrainMesh(map, chunkIndex, out); return;
        case HexSurface::Water: buildWaterMesh(map, chunkIndex, out); return;
        case HexSurface::River: buildRiverMesh(map, chunkIndex, out); return;
        case HexSurface::Road: buildRoadMesh(map, chunkIndex, out); return;
        case HexSurface::Fog:
            // The fog overlay also depends on the visibility counters, which a grid-only
            // helper cannot see; `HexMapModule::rebuildChunk` dispatches it to `buildFogMesh`.
            break;
        case HexSurface::Wall:
        case HexSurface::Feature:
            // Both are declared by `HexFeatures.h` and dispatched by
            // `HexMapModule::rebuildChunk`; `HexMapMesh` stays free of that header.
            break;
    }
    out.clear();
    out.finalize();
}

}  // namespace eve::hexmap
