#include "procgen/mesh/MeshUvProjection.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace eve::procgen {
namespace {
template <class T> Result<T> fail(DiagnosticCode code, const char* message, const char* path) {
    return Result<T>::failure(Diagnostic::error(code, message, path, {}, "procgen.meshUvProjection"));
}
std::array<float, 3> point(const MeshBuild& mesh, std::uint32_t i) {
    const std::size_t b = static_cast<std::size_t>(i) * 3u;
    return {mesh.positions()[b], mesh.positions()[b + 1u], mesh.positions()[b + 2u]};
}
}  // namespace

Result<MeshBuild> projectMeshUvResult(const MeshBuild& input, std::string_view mode, float scale, float offsetU,
                                      float offsetV) {
    if (input.empty() || input.positions().size() % 3u != 0u || input.normals().size() != input.positions().size())
        return fail<MeshBuild>(DiagnosticCode::InvalidArgument, "UV projection requires a valid non-empty mesh", "mesh");
    if ((mode != "planar" && mode != "box" && mode != "spherical") || !std::isfinite(scale) || scale <= 0.f ||
        !std::isfinite(offsetU) || !std::isfinite(offsetV))
        return fail<MeshBuild>(DiagnosticCode::InvalidArgument, "invalid UV projection parameters", "projection");
    MeshBuild output = input;
    output.uvs().assign(static_cast<std::size_t>(output.getVertexCount()) * 2u, 0.f);
    constexpr float pi = 3.14159265358979323846f;
    for (int i = 0; i < output.getVertexCount(); ++i) {
        const std::size_t p = static_cast<std::size_t>(i) * 3u, uv = static_cast<std::size_t>(i) * 2u;
        const float x = output.positions()[p], y = output.positions()[p + 1u], z = output.positions()[p + 2u];
        float u = x, v = z;
        if (mode == "spherical") {
            const float length = std::sqrt(x * x + y * y + z * z);
            if (length > 1e-7f) { u = std::atan2(z, x) / (2.f * pi) + 0.5f; v = std::asin(std::clamp(y / length, -1.f, 1.f)) / pi + 0.5f; }
        } else if (mode == "box") {
            const float nx = std::abs(output.normals()[p]), ny = std::abs(output.normals()[p + 1u]), nz = std::abs(output.normals()[p + 2u]);
            if (nx >= ny && nx >= nz) { u = z; v = y; }
            else if (ny >= nz) { u = x; v = z; }
            else { u = x; v = y; }
        }
        output.uvs()[uv] = u * scale + offsetU;
        output.uvs()[uv + 1u] = v * scale + offsetV;
    }
    output.setMeta("uvProjection", std::string(mode));
    return Result<MeshBuild>::success(std::move(output));
}

Result<MeshSurfaceUv> mapMeshSurfacePointToUvResult(const MeshBuild& mesh, int triangleIndex, float x, float y,
                                                     float z) {
    const std::size_t base = triangleIndex < 0 ? mesh.indices().size() : static_cast<std::size_t>(triangleIndex) * 3u;
    if (base + 2u >= mesh.indices().size() || mesh.uvs().size() != static_cast<std::size_t>(mesh.getVertexCount()) * 2u)
        return fail<MeshSurfaceUv>(DiagnosticCode::InvalidArgument, "invalid dynamic mesh triangle or UV stream", "triangle");
    const auto ia = mesh.indices()[base], ib = mesh.indices()[base + 1u], ic = mesh.indices()[base + 2u];
    if (ia >= static_cast<std::uint32_t>(mesh.getVertexCount()) || ib >= static_cast<std::uint32_t>(mesh.getVertexCount()) || ic >= static_cast<std::uint32_t>(mesh.getVertexCount()))
        return fail<MeshSurfaceUv>(DiagnosticCode::InvalidArgument, "triangle index exceeds dynamic mesh", "triangle");
    const auto a = point(mesh, ia), b = point(mesh, ib), c = point(mesh, ic);
    const std::array<float,3> v0{b[0]-a[0],b[1]-a[1],b[2]-a[2]}, v1{c[0]-a[0],c[1]-a[1],c[2]-a[2]}, q{x-a[0],y-a[1],z-a[2]};
    const float d00=v0[0]*v0[0]+v0[1]*v0[1]+v0[2]*v0[2], d01=v0[0]*v1[0]+v0[1]*v1[1]+v0[2]*v1[2];
    const float d11=v1[0]*v1[0]+v1[1]*v1[1]+v1[2]*v1[2], d20=q[0]*v0[0]+q[1]*v0[1]+q[2]*v0[2], d21=q[0]*v1[0]+q[1]*v1[1]+q[2]*v1[2];
    const float denominator=d00*d11-d01*d01;
    if (std::abs(denominator) < 1e-12f) return fail<MeshSurfaceUv>(DiagnosticCode::InvalidArgument, "degenerate dynamic mesh triangle", "triangle");
    const float wb=(d11*d20-d01*d21)/denominator, wc=(d00*d21-d01*d20)/denominator, wa=1.f-wb-wc;
    const auto uvAt=[&mesh](std::uint32_t i,int c0){return mesh.uvs()[static_cast<std::size_t>(i)*2u+static_cast<std::size_t>(c0)];};
    return Result<MeshSurfaceUv>::success({wa*uvAt(ia,0)+wb*uvAt(ib,0)+wc*uvAt(ic,0), wa*uvAt(ia,1)+wb*uvAt(ib,1)+wc*uvAt(ic,1)});
}
}  // namespace eve::procgen
