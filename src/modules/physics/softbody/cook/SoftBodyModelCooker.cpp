#include "physics/softbody/cook/SoftBodyModelCooker.h"

#include "asset/CanonicalMesh.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace eve::physics::softbody_cook {
namespace {
template <typename T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
}
}  // namespace

eve::Result<SoftBodyModel> cookSoftBodyModel(const eve::asset::CanonicalMeshData& mesh,
                                             const SoftBodyModelDefinition&       definition) {
    auto recipeValid = definition.validate();
    if (!recipeValid) return eve::Result<SoftBodyModel>::failure(recipeValid.status());
    if (definition.surfaceSampling != SoftBodySurfaceSampling::Vertices ||
        definition.volumeSampling != SoftBodyVolumeSampling::None)
        return failure<SoftBodyModel>(eve::DiagnosticCode::Unsupported,
                                      "only vertex surface sampling without volume voxels is implemented",
                                      "sampling");
    if (mesh.positions.empty() || mesh.positions.size() % 3 != 0)
        return failure<SoftBodyModel>(eve::DiagnosticCode::InvalidArgument,
                                      "mesh positions must contain complete xyz vertices", "mesh.positions");
    const std::size_t vertexCount = mesh.positions.size() / 3;
    if (mesh.indices.empty() || mesh.indices.size() % 3 != 0)
        return failure<SoftBodyModel>(eve::DiagnosticCode::InvalidArgument,
                                      "mesh indices must contain complete triangles", "mesh.indices");

    SoftBodyModel model;
    model.sourceMesh = definition.sourceMesh;
    model.particles.reserve(vertexCount);
    model.surfaceBindings.reserve(vertexCount);
    for (std::size_t index = 0; index < vertexCount; ++index) {
        const float x = mesh.positions[index * 3];
        const float y = mesh.positions[index * 3 + 1];
        const float z = mesh.positions[index * 3 + 2];
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            return failure<SoftBodyModel>(eve::DiagnosticCode::InvalidArgument,
                                          "mesh position must be finite", "mesh.positions");
        model.particles.push_back({x, y, z});
        model.surfaceBindings.push_back({static_cast<std::uint32_t>(index)});
    }
    model.surfaceIndices = mesh.indices;

    std::vector<std::set<std::uint32_t>> neighborhoods(vertexCount);
    for (std::size_t triangle = 0; triangle < mesh.indices.size(); triangle += 3) {
        const auto a = mesh.indices[triangle];
        const auto b = mesh.indices[triangle + 1];
        const auto c = mesh.indices[triangle + 2];
        if (a >= vertexCount || b >= vertexCount || c >= vertexCount)
            return failure<SoftBodyModel>(eve::DiagnosticCode::InvalidArgument,
                                          "mesh triangle index exceeds vertex count", "mesh.indices");
        if (a == b || b == c || a == c)
            return failure<SoftBodyModel>(eve::DiagnosticCode::InvalidArgument,
                                          "mesh contains a degenerate index triangle", "mesh.indices");
        for (const auto center : {a, b, c}) {
            neighborhoods[center].insert(a);
            neighborhoods[center].insert(b);
            neighborhoods[center].insert(c);
        }
    }
    for (const auto& neighborhood : neighborhoods) {
        if (neighborhood.size() < 3) continue;
        SoftBodyModelCluster cluster;
        cluster.particleIndices.assign(neighborhood.begin(), neighborhood.end());
        model.clusters.push_back(std::move(cluster));
    }
    auto modelValid = model.validate();
    if (!modelValid) return eve::Result<SoftBodyModel>::failure(modelValid.status());
    return eve::Result<SoftBodyModel>::success(std::move(model));
}

}  // namespace eve::physics::softbody_cook
