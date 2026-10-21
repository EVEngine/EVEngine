#include "physics/destruction/cook/DestructionCook.h"
#include <functional>
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "physics/World3D.h"
#include "physics/destruction/FractureRecipe.h"
#include "physics/destruction/GeometryCollectionInstance.h"
#include "physics/destruction/cook/AssemblyFractureSettings.h"
#include "physics/destruction/cook/VoronoiCook.h"
namespace eve::physics::destruction_cook {
Module_IMPL(DestructionCook, new DestructionCook());
namespace {
template <class T>
ssq::Table owned(HSQUIRRELVM vm, Result<T> result) {
    if (!result) return script::projectStatusResult(vm, result.status());
    auto object = script::makeOwnedSquirrelInstance<T>(vm, std::make_unique<T>(std::move(result).takeValue()));
    if (!object) return script::projectStatusResult(vm, object.status());
    return script::projectStatusResult(vm, Status::success(), std::move(object).takeValue());
}
}  // namespace
void DestructionCook::expose(ssq::Table& table) {
    auto cls = table.addClass(name, DestructionCook::create, false);
    expose(cls);
    auto asset = table.addClass<FracturedCollection>(
        "FracturedCollection",
        std::function<FracturedCollection*()>([] { return static_cast<FracturedCollection*>(nullptr); }), true);
    const auto vm       = table.getHandle();
    auto       settings = table.addClass<AssemblyFractureSettings>("AssemblyFractureSettings");
    settings.addFunc("setGroupPolicy", [vm](AssemblyFractureSettings* self, int group, int fragments, float cohesion,
                                            float connection) {
        return script::projectResult(vm, self->setGroupPolicy(group, {fragments, cohesion, connection}));
    });
    settings.addFunc("setComponentPolicy", [vm](AssemblyFractureSettings* self, int component, int fragments,
                                                float cohesion, float connection) {
        return script::projectResult(vm, self->setComponentPolicy(component, {fragments, cohesion, connection}));
    });
    settings.addFunc("setSeed", [vm](AssemblyFractureSettings* self, int value) {
        if (value < 0)
            return script::projectResult(vm, Result<void>::failure(Diagnostic::error(
                                                 DiagnosticCode::InvalidArgument, "seed must be nonnegative", "seed")));
        self->recipe.seed = static_cast<std::uint64_t>(value);
        return script::projectResult(vm, Result<void>::success());
    });
    settings.addFunc("setContactTolerance", [vm](AssemblyFractureSettings* self, float value) {
        auto copy             = *self;
        copy.contactTolerance = value;
        auto checked          = copy.validate();
        if (checked) *self = std::move(copy);
        return script::projectResult(vm, std::move(checked));
    });
    settings.addFunc("setMaximumBones", [vm](AssemblyFractureSettings* self, int value) {
        auto copy                = *self;
        copy.recipe.maximumBones = value;
        auto checked             = copy.validate();
        if (checked) *self = std::move(copy);
        return script::projectResult(vm, std::move(checked));
    });

    asset.addFunc("boneCount",
                  [](FracturedCollection* self) { return static_cast<int>(self->collection.bones.size()); });
    asset.addFunc("createInstance", [vm](FracturedCollection* self, World3D* world, float x, float y, float z) {
        if (!world)
            return script::projectResult(vm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                                     "world required", "world")));
        auto instance = GeometryCollectionInstance::createFractured(*world, self->collection, self->geometry, x, y, z);
        if (!instance) return script::projectStatusResult(vm, instance.status());
        auto object =
            script::makeOwnedSquirrelInstance<GeometryCollectionInstance>(vm, std::move(instance).takeValue());
        if (!object) return script::projectStatusResult(vm, object.status());
        return script::projectStatusResult(vm, Status::success(), std::move(object).takeValue());
    });
}
void DestructionCook::expose(ssq::Class& cls) {
    cls.addFunc("fractureBoxes", [vm = cls.getHandle()](DestructionCook*, GeometryCollectionAsset* source,
                                                        AssemblyFractureSettings* settings) {
        if (!source || !settings)
            return script::projectResult(
                vm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "source and settings required", "settings")));
        return owned(vm, cookVoronoiAssembly(*source, *settings));
    });
}
}  // namespace eve::physics::destruction_cook
