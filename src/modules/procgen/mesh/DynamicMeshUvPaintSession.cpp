#include "procgen/mesh/DynamicMeshUvPaintSession.h"

#include "image/ImageData.h"
#include "procgen/mesh/MeshUvProjection.h"

namespace eve::procgen {
namespace {
Result<void> fail(DiagnosticCode code, const char* message) {
    return Result<void>::failure(Diagnostic::error(code, message, "mesh", {}, "procgen.dynamicMeshUvPaint"));
}
bool validMesh(const MeshBuild& mesh) {
    return !mesh.empty() && mesh.positions().size() % 3u == 0u &&
           mesh.uvs().size() == static_cast<std::size_t>(mesh.getVertexCount()) * 2u;
}
}
Result<void> DynamicMeshUvPaintSession::initializeResult(const MeshBuild& mesh, const image::ImageData& image) {
    if (!validMesh(mesh)) return fail(DiagnosticCode::InvalidArgument, "dynamic UV paint requires mesh UVs");
    auto initialized = paint_.initializeResult(image);
    if (!initialized.ok()) return initialized;
    mesh_ = mesh;
    ++meshRevision_;
    initialized_ = true;
    return Result<void>::success();
}
Result<void> DynamicMeshUvPaintSession::updateMeshResult(const MeshBuild& mesh) {
    if (!initialized_) return fail(DiagnosticCode::PreconditionViolation, "dynamic UV paint is not initialized");
    if (!validMesh(mesh)) return fail(DiagnosticCode::InvalidArgument, "updated dynamic mesh requires UVs");
    mesh_ = mesh; ++meshRevision_; return Result<void>::success();
}
Result<void> DynamicMeshUvPaintSession::paintSurfacePointResult(int triangleIndex, float x, float y, float z,
                                                                float radiusPixels, float r, float g, float b, float a,
                                                                bool wrapU, bool wrapV) {
    if (!initialized_) return fail(DiagnosticCode::PreconditionViolation, "dynamic UV paint is not initialized");
    auto uv = mapMeshSurfacePointToUvResult(mesh_, triangleIndex, x, y, z);
    if (!uv.ok()) return Result<void>::failure(uv.status());
    auto painted = paint_.paintCircleResult(uv.value().u, uv.value().v, radiusPixels, r, g, b, a, wrapU, wrapV);
    if (!painted.ok()) return Result<void>::failure(painted.status());
    return Result<void>::success();
}
Result<std::unique_ptr<image::ImageData>> DynamicMeshUvPaintSession::currentImageResult() const {
    return paint_.currentImageResult();
}
Result<void> DynamicMeshUvPaintSession::undoResult() { return paint_.undoResult(); }
}  // namespace eve::procgen
