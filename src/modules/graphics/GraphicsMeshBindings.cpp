#include "graphics/GraphicsMeshBindings.h"
#include "graphics/SceneVisibility.h"
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include <typeindex>
#include "common/SquirrelBinding.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"

namespace eve::graphics {
void exposeGraphicsMeshes(ssq::Table& table) {
    exposeSceneVisibility(table);
    auto meshCls = table.addClass<Mesh>("Mesh", std::function<Mesh*()>([]() -> Mesh* { return nullptr; }), true);
    meshCls.addFunc("getVertexCount", &Mesh::getVertexCount);
    meshCls.addFunc("getIndexCount", std::function<int(Mesh*)>([](Mesh* mesh) { return mesh->indexCount; }));
    meshCls.addFunc("getMorphCount", &Mesh::getMorphCount);
    meshCls.addFunc("getMorphName", &Mesh::getMorphName);
    meshCls.addFunc("hasMorph", &Mesh::hasMorph);
    meshCls.addFunc("setMorphWeight", &Mesh::setMorphWeight);
    meshCls.addFunc("getMorphWeight", &Mesh::getMorphWeight);
    meshCls.addFunc("clearMorphWeights", &Mesh::clearMorphWeights);
    meshCls.addFunc("hasMorphData", &Mesh::hasMorphData);
    meshCls.addFunc("isMorphDirty", &Mesh::isMorphDirty);
    meshCls.addFunc("setCastOcclusion", &Mesh::setCastOcclusion);
    meshCls.addFunc("getCastOcclusion", &Mesh::getCastOcclusion);
    const auto vm = table.getHandle();
    table.addFunc("destroyRenderable3D", [vm](std::int64_t id, std::int64_t generation) {
        if (id < 0 || generation < 0 || id > UINT32_MAX || generation > UINT32_MAX)
            return script::projectStatusResult(
                vm, Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "Entity id and generation must fit uint32")));
        ecs::EntityHandle handle{ecs::current(), std::type_index(typeid(Renderable3D)), static_cast<std::uint32_t>(id),
                                 static_cast<std::uint32_t>(generation)};
        auto*             entity = dynamic_cast<Renderable3D*>(ecs::try_get(handle));
        if (!entity)
            return script::projectStatusResult(
                vm, Status::failure(
                        Diagnostic::error(DiagnosticCode::NotFound, "Renderable no longer exists at this generation")));
        ecs::DestroyEntity(entity);
        return script::projectStatusResult(vm, Status::success(StatusCode::Applied));
    });
    meshCls.addFunc("getLocalBounds", [vm](Mesh* mesh) {
        auto bounds = mesh->localBounds();
        if (!bounds.ok()) return script::projectStatusResult(vm, bounds.status());
        Value::Array values;
        for (float value : bounds.value()) values.emplace_back(static_cast<double>(value));
        return script::projectStatusResult(vm, bounds.status(), Value(std::move(values)));
    });
}
}  // namespace eve::graphics
