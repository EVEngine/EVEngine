#include "asset_graphics/EvpackGraphicsLoader.h"

#include "graphics/IResourceFactory.h"

namespace eve::asset_graphics {
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

Result<void> GraphicsMeshFactoryAdapter::releaseMesh(graphics::Mesh* mesh) {
    if (!mesh)
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "cannot release a null graphics mesh");
    if (!factory_.releaseMesh(mesh))
        return failure<void>(DiagnosticCode::Failed,
                             "graphics backend rejected mesh release");
    return Result<void>::success();
}

}  // namespace eve::asset_graphics
