#include "procgen/algorithms/CaveGeneration.h"
#include "procgen/algorithms/CaveMeshInternal.h"

namespace eve::procgen {
MeshRecipeFn caveMeshGenerator() {
    // Compatibility recipe delegates to the stages used by script-authored builds.
    return [](const Params& params, MeshBuild& out, std::string& error) {
        CaveGeneration generation;
        auto           status = generation.configure(params);
        if (status.ok()) status = generation.buildSkeleton();
        if (status.ok()) status = generation.sampleField();
        for (auto operation : {CaveSurfaceOperation::Reactivity, CaveSurfaceOperation::ReactivePatches,
                               CaveSurfaceOperation::Condensation, CaveSurfaceOperation::Curvature}) {
            if (status.ok()) status = generation.evolveSurface(operation);
        }
        if (status.ok()) status = generation.deposit();
        if (!status.ok()) {
            error = status.status().diagnostics().front().message();
            return false;
        }
        auto mesh = generation.buildMesh();
        if (!mesh.ok()) {
            error = mesh.status().diagnostics().front().message();
            return false;
        }
        out = std::move(mesh).takeValue();
        return true;
    };
}
}  // namespace eve::procgen
