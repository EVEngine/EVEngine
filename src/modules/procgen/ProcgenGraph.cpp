#include "procgen/Procgen.h"

#include "procgen/GridGraph.h"
#include "procgen/MeshGraph.h"
#include "procgen/PointGraph.h"
#include "procgen/mesh/MeshModifierGraph.h"

namespace eve::procgen {

eve::Result<ProcgenPointGraphHandleRef> Procgen::newPointGraphHandle() {
    return pointGraphs_.emplace(std::make_unique<PointGraph>());
}

eve::Result<ProcgenMeshModifierGraphHandleRef> Procgen::newMeshModifierGraphHandle() {
    return meshModifierGraphs_.emplace(std::make_unique<MeshModifierGraph>());
}

eve::Result<ProcgenMeshDeformationSessionHandleRef> Procgen::newMeshDeformationSessionHandle() {
    return meshDeformationSessions_.emplace(std::make_unique<MeshDeformationSession>());
}

eve::Result<ProcgenDynamicMeshUvPaintSessionHandleRef> Procgen::newDynamicMeshUvPaintSessionHandle() {
    return dynamicMeshUvPaintSessions_.emplace(std::make_unique<DynamicMeshUvPaintSession>());
}

eve::Result<ProcgenSplinePathHandleRef> Procgen::newSplinePathHandle() {
    return splinePaths_.emplace(std::make_unique<SplinePath>());
}

eve::Result<ProcgenGridGraphHandleRef> Procgen::newGridGraphHandle() {
    return gridGraphs_.emplace(std::make_unique<GridGraph>());
}

eve::Result<ProcgenMeshGraphHandleRef> Procgen::newMeshGraphHandle() {
    return meshGraphs_.emplace(std::make_unique<MeshGraph>());
}

}  // namespace eve::procgen
