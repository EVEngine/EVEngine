#pragma once
#include "common/Result.h"
#include "procgen/MeshBuild.h"
namespace eve::procgen {
/** @brief GTS mesh simplification controls supported by MeshBuild streams. */
struct GtsMeshSimplificationOptions {
    bool preserveBorderEdges=true,preserveUvSeamEdges=false,preserveUvFoldoverEdges=false;
    bool preserveSurfaceCurvature=false,enableSmartLink=true;
    double vertexLinkDistance=0.0001,aggressiveness=7.0;
    int maxIterationCount=100;
};
/**
 * @brief Simplify a mesh with deterministic quadric edge collapse.
 * @param output Replaced atomically with the simplified mesh.
 * @param source Immutable source; it may alias output.
 * @param quality Target triangle ratio in [0,1], rounded like GTS.
 * @param options Validated GTS simplification controls.
 */
[[nodiscard]] Result<int> simplifyGtsMesh(MeshBuild& output,const MeshBuild& source,float quality,
                                          const GtsMeshSimplificationOptions& options={});
}
