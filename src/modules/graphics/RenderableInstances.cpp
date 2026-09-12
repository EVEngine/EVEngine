#include "graphics/RenderableInstances.h"
#include "graphics/Mesh.h"
#include "graphics/Shader.h"

namespace eve::graphics {
namespace detail {
Result<void> validateInstancedRenderable(const Renderable3D::MeshRenderer& mr) {
    auto* shader = mr.material ? mr.material->effectiveShader() : mr.shader;
    if (!mr.mesh || !shader || shader->getKind() != Shader::Kind::eMesh3D || shader->isXray() || mr.usesParts() ||
        mr.effectiveHair() || mr.xrayHighlight || mr.lodCount || mr.mesh->hasGpuSkinning() ||
        mr.effectiveCastShadow() || mr.castOcclusion)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported,
            "Instanced renderables require one custom static mesh without shadow/occlusion casting, LOD, hair or Xray",
            "graphics.instances"));
    return Result<void>::success();
}
}  // namespace detail
Result<void> Renderable3D::setInstanceRange(const MeshInstanceRange& range) {
    auto valid = validateMeshInstanceRange(range);
    if (!valid) return valid;
    auto mr        = meshRenderer();
    auto supported = detail::validateInstancedRenderable(*mr);
    if (!supported) return supported;
    mr->instances = range;
    return Result<void>::success();
}
void Renderable3D::clearInstanceRange() { meshRenderer()->instances.reset(); }
}  // namespace eve::graphics
