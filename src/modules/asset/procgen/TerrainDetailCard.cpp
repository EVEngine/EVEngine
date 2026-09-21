#include "asset/procgen/TerrainDetailCard.h"

#include <new>

namespace eve::asset_procgen {
namespace {

void appendQuad(TerrainDetailCardGeometry& geometry, float xx, float xz, float normalX, float normalZ) {
    const std::uint32_t base = static_cast<std::uint32_t>(geometry.positions.size() / 3);
    geometry.positions.insert(geometry.positions.end(), {-0.5f * xx, 0.f, -0.5f * xz, 0.5f * xx, 0.f, 0.5f * xz,
                                                         0.5f * xx, 1.f, 0.5f * xz, -0.5f * xx, 1.f, -0.5f * xz});
    for (int vertex = 0; vertex < 4; ++vertex) geometry.normals.insert(geometry.normals.end(), {normalX, 0.f, normalZ});
    geometry.texcoords.insert(geometry.texcoords.end(), {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f});
    geometry.indices.insert(geometry.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
}

}  // namespace

Result<TerrainDetailCardGeometry> buildTerrainDetailCard(const RuntimeInstancePrototype& prototype) {
    if (prototype.prototype.empty())
        return Result<TerrainDetailCardGeometry>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain Detail prototype identity is empty", {}, {},
                              "asset.procgen.terrainDetailCard"));
    if (prototype.usePrototypeMesh || prototype.renderMode == "VertexLit")
        return Result<TerrainDetailCardGeometry>::failure(
            Diagnostic::error(DiagnosticCode::Unsupported, "mesh-backed terrain Detail requires its authored mesh",
                              prototype.prototype, {}, "asset.procgen.terrainDetailCard"));
    if (!prototype.prototype.starts_with("unity-texture-guid:"))
        return Result<TerrainDetailCardGeometry>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "texture terrain Detail identity is invalid",
                              prototype.prototype, {}, "asset.procgen.terrainDetailCard"));
    try {
        TerrainDetailCardGeometry geometry;
        if (prototype.renderMode == "GrassBillboard") {
            geometry.cameraFacing = true;
            appendQuad(geometry, 1.f, 0.f, 0.f, 1.f);
        } else if (prototype.renderMode == "Grass") {
            appendQuad(geometry, 1.f, 0.f, 0.f, 1.f);
            appendQuad(geometry, 0.f, 1.f, 1.f, 0.f);
        } else {
            return Result<TerrainDetailCardGeometry>::failure(
                Diagnostic::error(DiagnosticCode::Unsupported, "terrain Detail render mode has no card representation",
                                  prototype.renderMode, {}, "asset.procgen.terrainDetailCard"));
        }
        return Result<TerrainDetailCardGeometry>::success(std::move(geometry));
    } catch (const std::bad_alloc&) {
        return Result<TerrainDetailCardGeometry>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "terrain Detail card allocation failed", prototype.prototype, {},
                              "asset.procgen.terrainDetailCard"));
    }
}

}  // namespace eve::asset_procgen
