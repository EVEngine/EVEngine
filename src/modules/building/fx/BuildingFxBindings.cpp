#include <simplesquirrel/simplesquirrel.hpp>
#include "building/Ghost.h"
#include "building/PlacementSession.h"
#include "building/PlacementWorld.h"
#include "building/fx/BuildingFx.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"

namespace eve::buildingfx {

void BuildingFx::expose(ssq::Table& table) {
    auto cls = table.addClass(name, BuildingFx::create, false);
    expose(cls);
}

void BuildingFx::expose(ssq::Class& cls) {
    cls.addFunc("setMeshResolver", [](BuildingFx* fx, ssq::Table resources) {
        std::unordered_map<std::string, graphics::Mesh*> meshes;
        HSQUIRRELVM                                      vm  = resources.getHandle();
        const SQInteger                                  top = sq_gettop(vm);
        try {
            sq_pushobject(vm, resources.getRaw());
            sq_pushnull(vm);
            while (SQ_SUCCEEDED(sq_next(vm, -2))) {
                const auto    key  = ssq::detail::popValue<std::string>(vm, -2);
                SQUserPointer mesh = nullptr;
                const auto    tag  = reinterpret_cast<SQUserPointer>(ssq::detail::stableTypeHash<graphics::Mesh*>());
                if (SQ_FAILED(sq_getinstanceup(vm, -1, &mesh, tag)) || !mesh)
                    throw ssq::TypeException("mesh resolver values must be non-null Mesh instances");
                meshes.emplace(key, static_cast<graphics::Mesh*>(mesh));
                sq_pop(vm, 2);
            }
        } catch (...) {
            sq_settop(vm, top);
            throw;
        }
        sq_settop(vm, top);
        fx->setMeshResolver([meshes = std::move(meshes)](const std::string& id) {
            const auto found = meshes.find(id);
            return found == meshes.end() ? nullptr : found->second;
        });
    });
    cls.addFunc("clearMeshResolver", &BuildingFx::clearMeshResolver);
    cls.addFunc("attach", &BuildingFx::attach);
    cls.addFunc("detach", &BuildingFx::detach);
    cls.addFunc("isAttached", &BuildingFx::isAttached);
    cls.addFunc("getAttachedCount", &BuildingFx::getAttachedCount);
    cls.addFunc("sync", &BuildingFx::sync);
    cls.addFunc("getVisualCount", &BuildingFx::getVisualCount);
    cls.addFunc("getVisualVariant", &BuildingFx::getVisualVariant);
    cls.addFunc("getVisualResource", &BuildingFx::getVisualResource);
    cls.addFunc("getVisualFallbackReason", &BuildingFx::getVisualFallbackReason);
    cls.addFunc("getCurveGroupCount", &BuildingFx::getCurveGroupCount);
    cls.addFunc("getContinuousCurveVisualCount", &BuildingFx::getContinuousCurveVisualCount);
    cls.addFunc("getCurveVisualFallbackReason", &BuildingFx::getCurveVisualFallbackReason);
    cls.addFunc("updateEdgeCurveSurfacePreview", &BuildingFx::updateEdgeCurveSurfacePreviewStatus);
    cls.addFunc("clearEdgeCurvePreview", &BuildingFx::clearEdgeCurvePreview);
    cls.addFunc("hasEdgeCurvePreview", &BuildingFx::hasEdgeCurvePreview);
    cls.addFunc("getEdgeCurvePreviewFallbackReason", &BuildingFx::getEdgeCurvePreviewFallbackReason);
    cls.addFunc("getEdgeCurvePreviewSurfaceId", &BuildingFx::getEdgeCurvePreviewSurfaceId);
    cls.addFunc("setLevelVisibilityMode", &BuildingFx::setLevelVisibilityMode);
    cls.addFunc("getLevelVisibilityMode", &BuildingFx::getLevelVisibilityMode);
    cls.addFunc("isVisualVisible", &BuildingFx::isVisualVisible);
    cls.addFunc("updateGhost", &BuildingFx::updateGhost);
    cls.addFunc("hideGhost", &BuildingFx::hideGhost);
    cls.addFunc("updateAreaPreview", &BuildingFx::updateAreaPreview);
    cls.addFunc("clearAreaPreview", &BuildingFx::clearAreaPreview);
    cls.addFunc("getAreaPreviewCount", &BuildingFx::getAreaPreviewCount);
    cls.addFunc("getAreaPreviewAccepted", &BuildingFx::getAreaPreviewAccepted);
    cls.addFunc("setGridVisible", &BuildingFx::setGridVisible);
    cls.addFunc("getGridVisible", &BuildingFx::getGridVisible);
    cls.addFunc("drawGrid2D", &BuildingFx::drawGrid2D);
    cls.addFunc("drawGrid3D", &BuildingFx::drawGrid3D);
}

}  // namespace eve::buildingfx
