#include "graphics/Renderable3DBindings.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/SquirrelBinding.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"

namespace eve::graphics::detail {
void exposeRenderable3DBindings(ssq::Table& table) {
    auto ent = table.addClass<Renderable3D>(
        "Renderable3D", std::function<Renderable3D*()>([]() { return Renderable3D::create(); }), false);
    ent.addFunc("getEntityId", &Renderable3D::getEntityId);
    ent.addFunc("getEntityGeneration", &Renderable3D::getEntityGeneration);
    ent.addFunc("setPosition", &Renderable3D::setPosition);
    ent.addFunc("setRotation", &Renderable3D::setRotation);
    ent.addFunc("setYaw", &Renderable3D::setYaw);
    ent.addFunc("getYaw", &Renderable3D::getYaw);
    ent.addFunc("setScale", &Renderable3D::setScale);
    ent.addFunc("setMesh", &Renderable3D::setMesh);
    ent.addFunc("getMesh", &Renderable3D::getMesh);
    const auto vm = table.getHandle();
    ent.addFunc("setInstanceRange", [vm](Renderable3D* self, int64_t first, int64_t count, ssq::Array minimum,
                                         ssq::Array maximum, float distance) {
        try {
            if (!self || first < 0 || count < 0 || uint64_t(first) > UINT32_MAX || uint64_t(count) > UINT32_MAX ||
                minimum.size() != 3 || maximum.size() != 3)
                throw std::runtime_error("Expected nonnegative instance range and two float3 bounds");
            MeshInstanceRange range;
            range.first                     = uint32_t(first);
            range.count                     = uint32_t(count);
            range.maximumHorizontalDistance = distance;
            for (int i = 0; i < 3; ++i) {
                range.minimum[i] = minimum.get<float>(i);
                range.maximum[i] = maximum.get<float>(i);
            }
            return script::projectResult(vm, self->setInstanceRange(range));
        } catch (const std::exception& error) {
            return script::projectResult(vm, Result<void>::failure(Diagnostic::error(
                                                 DiagnosticCode::InvalidArgument, error.what(), "graphics.instances")));
        }
    });
    ent.addFunc("clearInstanceRange", &Renderable3D::clearInstanceRange);
    ent.addFunc("setTexture", &Renderable3D::setTexture);
    ent.addFunc("setNormalTexture", &Renderable3D::setNormalTexture);
    ent.addFunc("setHeightTexture", &Renderable3D::setHeightTexture);
    ent.addFunc("setShader", &Renderable3D::setShader);
    ent.addFunc("setMaterial", &Renderable3D::setMaterial);
    ent.addFunc("getMaterial", &Renderable3D::getMaterial);
    ent.addFunc("setPart", &Renderable3D::setPart);
    ent.addFunc("setPartSortPriority", &Renderable3D::setPartSortPriority);
    ent.addFunc("clearPartSortPriority", &Renderable3D::clearPartSortPriority);
    ent.addFunc("getPartSortPriority", &Renderable3D::getPartSortPriority);
    ent.addFunc("clearParts", &Renderable3D::clearParts);
    ent.addFunc("getPartCount", &Renderable3D::getPartCount);
    ent.addFunc("getPartName", &Renderable3D::getPartName);
    ent.addFunc("getPartMesh", &Renderable3D::getPartMesh);
    ent.addFunc("getPartMaterial", &Renderable3D::getPartMaterial);
    ent.addFunc("setHair", &Renderable3D::setHair);
    ent.addFunc("getHair", &Renderable3D::getHair);
    ent.addFunc("setTint", &Renderable3D::setTint);
    ent.addFunc("getTintR", &Renderable3D::getTintR);
    ent.addFunc("getTintG", &Renderable3D::getTintG);
    ent.addFunc("getTintB", &Renderable3D::getTintB);
    ent.addFunc("getRoughness", &Renderable3D::getRoughness);
    ent.addFunc("setMetallic", &Renderable3D::setMetallic);
    ent.addFunc("setRoughness", &Renderable3D::setRoughness);
    ent.addFunc("setTexCellBomb", &Renderable3D::setTexCellBomb);
    ent.addFunc("getTexCellBombScale", &Renderable3D::getTexCellBombScale);
    ent.addFunc("getTexCellBombStrength", &Renderable3D::getTexCellBombStrength);
    ent.addFunc("getTexCellBombRotation", &Renderable3D::getTexCellBombRotation);
    ent.addFunc("setParallax", &Renderable3D::setParallax);
    ent.addFunc("getParallaxScale", &Renderable3D::getParallaxScale);
    ent.addFunc("getParallaxMinLayers", &Renderable3D::getParallaxMinLayers);
    ent.addFunc("getParallaxMaxLayers", &Renderable3D::getParallaxMaxLayers);
    ent.addFunc("setVisible", &Renderable3D::setVisible);
    ent.addFunc("setReflectionCaptureMask", &Renderable3D::setReflectionCaptureMask);
    ent.addFunc("getReflectionCaptureMask", &Renderable3D::getReflectionCaptureMask);
    ent.addFunc("setReceiveLight", &Renderable3D::setReceiveLight);
    ent.addFunc("setCastShadow", &Renderable3D::setCastShadow);
    ent.addFunc("setReceiveShadow", &Renderable3D::setReceiveShadow);
    ent.addFunc("setCastOcclusion", &Renderable3D::setCastOcclusion);
    ent.addFunc("getCastOcclusion", &Renderable3D::getCastOcclusion);
    ent.addFunc("setCamera", &Renderable3D::setCamera);
    ent.addFunc("setMeshLod", &Renderable3D::setMeshLod);
    ent.addFunc("clearMeshLod", &Renderable3D::clearMeshLod);
    ent.addFunc("getMeshLodCount", &Renderable3D::getMeshLodCount);
    ent.addFunc("getMeshLodLevelAtDistance", &Renderable3D::getMeshLodLevelAtDistance);
}
}  // namespace eve::graphics::detail
