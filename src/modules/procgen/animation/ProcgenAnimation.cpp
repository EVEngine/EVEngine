#include "procgen/animation/ProcgenAnimation.h"

#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "procgen/animation/PcgSkinnedMesh.h"

#include <functional>
#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::procgen_animation {
Module_IMPL(ProcgenAnimation, new ProcgenAnimation());

void ProcgenAnimation::expose(ssq::Table& table) {
    auto module = table.addClass(name, ProcgenAnimation::create, false);
    expose(module);
    auto plan = table.addClass("PcgSkinnedMeshPlan", ssq::Class::Ctor<PcgSkinnedMeshPlan()>());
    plan.addFunc("appendSource", [vm = table.getHandle()](PcgSkinnedMeshPlan* self,
                                                           const procgen::MeshBuild* mesh,
                                                           const animation::AnimSkin* skin,
                                                           const procgen::PcgMeshTransform* transform,
                                                           const std::string& material) {
        if (!self || !mesh || !skin || !transform)
            return eve::script::projectResult(vm, Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "Pcg skinned plan arguments are required",
                "procgen.animation.pcgSkinnedMesh")));
        return eve::script::projectResult(vm, self->appendSource(*mesh, *skin, *transform, material));
    });
    plan.addFunc("clear", [](PcgSkinnedMeshPlan* self) { if (self) self->clear(); });
    plan.addFunc("getSourceCount", [](const PcgSkinnedMeshPlan* self) {
        return self ? self->getSourceCount() : 0;
    });
    auto result = table.addClass("PcgSkinnedMeshResult", ssq::Class::Ctor<PcgSkinnedMeshResult()>());
    result.addFunc("isValid", [](const PcgSkinnedMeshResult* self) { return self && self->valid(); });
    result.addFunc("getSkin", [](PcgSkinnedMeshResult* self) { return self ? self->skin() : nullptr; });
    result.addFunc("copyMesh", [vm = table.getHandle()](const PcgSkinnedMeshResult* self) -> ssq::Object {
        const auto* mesh = self ? self->mesh() : nullptr;
        if (!mesh) return ssq::Object(vm);
        auto object = eve::script::makeOwnedSquirrelInstance<procgen::MeshBuild>(
            vm, std::make_unique<procgen::MeshBuild>(*mesh));
        if (!object) {
            object.ignore("failed to create Pcg combined skinned mesh Squirrel instance");
            return ssq::Object(vm);
        }
        return std::move(object).takeValue();
    });
}

void ProcgenAnimation::expose(ssq::Class& cls) {
    cls.addFunc("combinePcgSkinnedMeshes", [vm = cls.getHandle()](ProcgenAnimation*,
                                                                    PcgSkinnedMeshResult* output,
                                                                    const PcgSkinnedMeshPlan* plan) {
        if (!output || !plan)
            return eve::script::projectResult(vm, Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "Pcg skinned output and plan are required",
                "procgen.animation.pcgSkinnedMesh")));
        return eve::script::projectResult(vm, combinePcgSkinnedMeshesInto(*output, *plan));
    });
}

}  // namespace eve::procgen_animation
