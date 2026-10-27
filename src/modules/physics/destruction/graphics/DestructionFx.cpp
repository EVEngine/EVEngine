#include "physics/destruction/graphics/DestructionFx.h"

#include "common/Exception.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "graphics/Graphics.h"
#include "physics/destruction/CollapseAnimation.h"
#include "physics/destruction/CollapsePlayback.h"
#include "physics/destruction/CollapseScriptValue.h"
#include "physics/destruction/GeometryCollectionInstance.h"
#include "physics/destruction/graphics/GeometryCollectionRenderer.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>

namespace eve::physics {

Module_IMPL(DestructionFx, new DestructionFx());

eve::Result<GeometryCollectionRenderer*> DestructionFx::createRenderer(GeometryCollectionInstance* instance) {
    if (!instance)
        return eve::Result<GeometryCollectionRenderer*>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "DestructionFx.createRenderer requires an instance", "instance"));
    return eve::Result<GeometryCollectionRenderer*>::success(new GeometryCollectionRenderer(instance));
}

eve::Result<std::unique_ptr<GeometryCollectionRenderer>> DestructionFx::createAnimationRenderer() {
    return eve::Result<std::unique_ptr<GeometryCollectionRenderer>>::success(
        std::make_unique<GeometryCollectionRenderer>(nullptr));
}
GeometryCollectionRenderer* DestructionFx::createRendererScript(GeometryCollectionInstance* instance) {
    auto created = createRenderer(instance);
    if (!created.ok()) {
        const auto* diagnostic = created.status().primaryDiagnostic();
        throw Exception("DestructionFx.createRenderer: %s",
                        diagnostic ? diagnostic->message().c_str() : "creation failed");
    }
    return created.value();
}

namespace {

void drawScript(GeometryCollectionRenderer* self, graphics::Graphics* graphics) {
    if (!self) throw Exception("GeometryCollectionRenderer.draw: null");
    auto result = self->draw(graphics);
    if (!result.ok()) {
        const auto* diagnostic = result.status().primaryDiagnostic();
        throw Exception("GeometryCollectionRenderer.draw: %s",
                        diagnostic ? diagnostic->message().c_str() : "draw failed");
    }
}

}  // namespace

void DestructionFx::expose(ssq::Table& table) {
    auto cls = table.addClass(name, DestructionFx::create, false);
    expose(cls);

    auto renderer = table.addClass<GeometryCollectionRenderer>(
        "GeometryCollectionRenderer",
        std::function<GeometryCollectionRenderer*()>([]() -> GeometryCollectionRenderer* { return nullptr; }), true);
    renderer.addFunc("setInstance", &GeometryCollectionRenderer::setInstance);
    renderer.addFunc("clearInstance", [](GeometryCollectionRenderer* self) { self->setInstance(nullptr); });
    // Lambda keeps BindingContracts from needing to parse the inline noexcept getter.
    renderer.addFunc("getInstance",
                     [](GeometryCollectionRenderer* self) -> GeometryCollectionInstance* {
                         return self ? self->getInstance() : nullptr;
                     });
    renderer.addFunc("setExteriorColor", &GeometryCollectionRenderer::setExteriorColor);
    renderer.addFunc("setInteriorColor", &GeometryCollectionRenderer::setInteriorColor);
    renderer.addFunc("setGroupExteriorColor", [vm = table.getHandle()](GeometryCollectionRenderer* self, int groupId,
                                                                       float r, float g, float b, float a) {
        return script::projectResult(vm, self->setGroupExteriorColor(groupId, r, g, b, a));
    });
    renderer.addFunc("setSleepColor", &GeometryCollectionRenderer::setSleepColor);
    renderer.addFunc("draw", drawScript);
    renderer.addFunc("preparePlayback",
                     [vm = table.getHandle()](GeometryCollectionRenderer* self, const ssq::Object& playbackObject,
                                              graphics::Graphics* graphics) {
                         auto player = collapse_detail::scriptValue<CollapsePlayback>(playbackObject);
                         if (!player) return script::projectResult(vm, Result<void>::failure(player.status()));
                         return script::projectResult(vm, self->preparePlayback(player.value().get(), graphics));
                     });
    renderer.addFunc("submitPlayback",
                     [vm = table.getHandle()](GeometryCollectionRenderer* self, const ssq::Object& playbackObject) {
                         auto player = collapse_detail::scriptValue<CollapsePlayback>(playbackObject);
                         if (!player) return script::projectResult(vm, Result<void>::failure(player.status()));
                         return script::projectResult(vm, self->submitPlayback(player.value().get()));
                     });
    renderer.addFunc("prepareAnimation", [vm = table.getHandle()](GeometryCollectionRenderer* self,
                                                                  CollapseAnimation*          animation,
                                                                  graphics::Graphics*         graphics) {
        if (!animation)
            return script::projectResult(vm, Result<void>::failure(Diagnostic::error(
                                                 DiagnosticCode::InvalidArgument, "animation required", "animation")));
        return script::projectResult(vm, self->prepareAnimation(*animation, graphics));
    });
    renderer.addFunc("prepareInstance",
                     [vm = table.getHandle()](GeometryCollectionRenderer* self, graphics::Graphics* graphics) {
                         return script::projectResult(vm, self->prepareInstance(graphics));
                     });
    renderer.addFunc("submitAnimation", [vm = table.getHandle()](GeometryCollectionRenderer* self,
                                                                 CollapseAnimation* animation, float seconds, float x,
                                                                 float y, float z) {
        if (!animation)
            return script::projectResult(vm, Result<void>::failure(Diagnostic::error(
                                                 DiagnosticCode::InvalidArgument, "animation required", "animation")));
        return script::projectResult(vm, self->submitAnimation(*animation, seconds, x, y, z));
    });
    renderer.addFunc("drawAnimation", [vm = table.getHandle()](
                                          GeometryCollectionRenderer* self, CollapseAnimation* animation,
                                          graphics::Graphics* graphics, float seconds, float x, float y, float z) {
        if (!animation)
            return script::projectResult(vm, Result<void>::failure(Diagnostic::error(
                                                 DiagnosticCode::InvalidArgument, "animation required", "animation")));
        return script::projectResult(vm, self->drawAnimation(*animation, graphics, seconds, x, y, z));
    });
    renderer.addFunc("lastActiveDrawCount", [](GeometryCollectionRenderer* self) {
        return self->lastActiveDrawCount();
    });
    renderer.addFunc("lastSleepBatchCount", [](GeometryCollectionRenderer* self) {
        return self->lastSleepBatchCount();
    });
}

void DestructionFx::expose(ssq::Class& cls) {
    cls.addFunc("createRenderer", &DestructionFx::createRendererScript);
    cls.addFunc("createAnimationRenderer", [vm = cls.getHandle()](DestructionFx* self) {
        auto created = self->createAnimationRenderer();
        if (!created) return script::projectStatusResult(vm, created.status());
        auto instance =
            script::makeOwnedSquirrelInstance<GeometryCollectionRenderer>(vm, std::move(created).takeValue());
        if (!instance) return script::projectStatusResult(vm, instance.status());
        return script::projectStatusResult(vm, Status::success(), std::move(instance).takeValue());
    });
}

}  // namespace eve::physics
