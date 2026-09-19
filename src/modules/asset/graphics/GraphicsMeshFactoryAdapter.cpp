#include "asset/graphics/EvpackGraphicsLoader.h"

#include "graphics/IResourceFactory.h"
#include "graphics/Mesh.h"

namespace eve::asset_graphics {
Result<void> GraphicsMeshFactoryAdapter::setMeshTangentFrame(graphics::Mesh* mesh, std::span<const float> tangents,
                                                             std::span<const float> bitangents) {
    if (!mesh) return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "null mesh"));
    return mesh->setTangentFrame(tangents, bitangents);
}
namespace {

template <class T>
Result<T> failure(DiagnosticCode code, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), {}, {},
                                                "asset.graphics.adapter"));
}

}  // namespace

Result<graphics::Mesh*> GraphicsMeshFactoryAdapter::uploadMesh(
    const float* posXYZ, const float* nrmXYZ, const float* uvST, int vertexCount,
    const std::uint32_t* indices, int indexCount) {
    graphics::Mesh* mesh = factory_.newMeshFromArrays(posXYZ, nrmXYZ, uvST, vertexCount,
                                                       indices, indexCount);
    if (!mesh)
        return failure<graphics::Mesh*>(DiagnosticCode::Failed,
                                        "graphics backend rejected canonical mesh upload");
    return Result<graphics::Mesh*>::success(mesh);
}

Result<graphics::Mesh*> GraphicsMeshFactoryAdapter::uploadMeshColored(
    const float* posXYZ, const float* nrmXYZ, const float* uvST, const float* colorRGBA,
    int vertexCount, const std::uint32_t* indices, int indexCount) {
    graphics::Mesh* mesh = factory_.newMeshFromArraysColored(
        posXYZ, nrmXYZ, uvST, colorRGBA, vertexCount, indices, indexCount);
    if (!mesh)
        return failure<graphics::Mesh*>(DiagnosticCode::Failed,
                                        "graphics backend rejected colored canonical mesh upload");
    return Result<graphics::Mesh*>::success(mesh);
}

Result<void> GraphicsMeshFactoryAdapter::releaseMesh(graphics::Mesh* mesh) {
    if (!mesh)
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "cannot release a null graphics mesh");
    if (!factory_.releaseMesh(mesh))
        return failure<void>(DiagnosticCode::Failed,
                             "graphics backend rejected mesh release");
    return Result<void>::success();
}

Result<void> GraphicsMeshFactoryAdapter::setMeshTexcoords(graphics::Mesh* mesh, std::uint32_t set,
                                                          std::span<const float> values) {
    if (!mesh) return failure<void>(DiagnosticCode::InvalidArgument, "null mesh");
    return mesh->setTexcoordSet(set, values);
}

}  // namespace eve::asset_graphics
