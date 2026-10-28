#include "procgen/Procgen.h"
#include "procgen/ProcgenOwnership.h"

#include "procgen/GridGraph.h"
#include "procgen/MeshGraph.h"
#include "procgen/PointGraph.h"
#include "procgen/mesh/DynamicMeshUvPaintSession.h"
#include "procgen/mesh/MeshDeformationSession.h"
#include "procgen/mesh/MeshModifierGraph.h"
#include "procgen/spline/SplinePath.h"

namespace eve::procgen {

eve::Result<ProcgenPointGraphHandleRef> Procgen::newPointGraphHandle() {
    return ownership_->pointGraphs.emplace(std::make_unique<PointGraph>());
}

eve::Result<ProcgenMeshModifierGraphHandleRef> Procgen::newMeshModifierGraphHandle() {
    return ownership_->meshModifierGraphs.emplace(std::make_unique<MeshModifierGraph>());
}

eve::Result<ProcgenMeshDeformationSessionHandleRef> Procgen::newMeshDeformationSessionHandle() {
    return ownership_->meshDeformationSessions.emplace(std::make_unique<MeshDeformationSession>());
}

eve::Result<ProcgenDynamicMeshUvPaintSessionHandleRef> Procgen::newDynamicMeshUvPaintSessionHandle() {
    return ownership_->dynamicMeshUvPaintSessions.emplace(std::make_unique<DynamicMeshUvPaintSession>());
}

eve::Result<ProcgenSplinePathHandleRef> Procgen::newSplinePathHandle() {
    return ownership_->splinePaths.emplace(std::make_unique<SplinePath>());
}

eve::Result<ProcgenGridGraphHandleRef> Procgen::newGridGraphHandle() {
    return ownership_->gridGraphs.emplace(std::make_unique<GridGraph>());
}

eve::Result<ProcgenMeshGraphHandleRef> Procgen::newMeshGraphHandle() {
    return ownership_->meshGraphs.emplace(std::make_unique<MeshGraph>());
}

}  // namespace eve::procgen
