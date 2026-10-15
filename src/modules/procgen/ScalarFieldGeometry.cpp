#include "procgen/ScalarField3D.h"

#include "procgen/algorithms/MarchingCubes.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen {
Result<void> ScalarField3D::carveEllipsoid(float cx, float cy, float cz, float rx, float ry, float rz) {
    if (samples_.empty() || !std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(cz) || !std::isfinite(rx) ||
        !std::isfinite(ry) || !std::isfinite(rz) || std::abs(cx) > 1.f || std::abs(cy) > 1.f || std::abs(cz) > 1.f ||
        rx < 0.0001f || ry < 0.0001f || rz < 0.0001f || rx > 2.f || ry > 2.f || rz > 2.f)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "ellipsoid requires an allocated field, normalized center and radii in [0.0001,2]", {},
                              {}, "procgen.field"));
    const float scale = std::min({rx, ry, rz});
    for (int z = 0; z < nz_; ++z)
        for (int y = 0; y < ny_; ++y)
            for (int x = 0; x < nx_; ++x) {
                const float px     = (float(x) / float(nx_ - 1) * 2.f - 1.f - cx) / rx;
                const float py     = (float(y) / float(ny_ - 1) * 2.f - 1.f - cy) / ry;
                const float pz     = (float(z) / float(nz_ - 1) * 2.f - 1.f - cz) / rz;
                auto&       sample = samples_[size_t(x) + size_t(nx_) * (size_t(y) + size_t(ny_) * size_t(z))];
                sample             = std::min(sample, (std::sqrt(px * px + py * py + pz * pz) - 1.f) * scale);
            }
    return Result<void>::success();
}

Result<MeshBuild> ScalarField3D::extractMesh(float isolevel) const {
    if (samples_.empty() || !std::isfinite(isolevel))
        return Result<MeshBuild>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "isosurface requires an allocated field and a finite level",
                                                            {}, {}, "procgen.field"));
    MeshBuild   mesh;
    std::string error;
    if (!marchingCubes(samples_.data(), nx_, ny_, nz_, isolevel, mesh, &error) || mesh.empty()) {
        if (error.empty()) error = "scalar field has no isosurface at the requested level";
        return Result<MeshBuild>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, error, {}, {}, "procgen.field"));
    }
    return Result<MeshBuild>::success(std::move(mesh));
}
}  // namespace eve::procgen
