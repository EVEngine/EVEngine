#include "physics/destruction/graphics/DestructionFx.h"

#include "common/Exception.h"
#include "graphics/Graphics.h"
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
    renderer.addFunc("getInstance", &GeometryCollectionRenderer::getInstance);
    renderer.addFunc("setExteriorColor", &GeometryCollectionRenderer::setExteriorColor);
    renderer.addFunc("setInteriorColor", &GeometryCollectionRenderer::setInteriorColor);
    renderer.addFunc("setSleepColor", &GeometryCollectionRenderer::setSleepColor);
    renderer.addFunc("draw", drawScript);
    renderer.addFunc("lastActiveDrawCount", [](GeometryCollectionRenderer* self) {
        return self->lastActiveDrawCount();
    });
    renderer.addFunc("lastSleepBatchCount", [](GeometryCollectionRenderer* self) {
        return self->lastSleepBatchCount();
    });
}

void DestructionFx::expose(ssq::Class& cls) {
    cls.addFunc("createRenderer", &DestructionFx::createRendererScript);
}

}  // namespace eve::physics
