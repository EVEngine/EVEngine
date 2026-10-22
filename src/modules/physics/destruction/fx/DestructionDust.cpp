#include "physics/destruction/fx/DestructionDust.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/SquirrelBinding.h"
#include "fluids/VolumeFluid.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "physics/destruction/CollapseAnimation.h"
#include "physics/destruction/fx/CollapseDust.h"
namespace eve::physics {
Module_IMPL(DestructionDust, new DestructionDust());
void DestructionDust::expose(ssq::Table& table) {
    auto module = table.addClass(name, DestructionDust::create, false);
    expose(module);
    auto       cls = table.addClass<CollapseDust>("CollapseDust");
    const auto vm  = table.getHandle();
    cls.addFunc("capture", [vm](CollapseDust* self, fluids::VolumeFluid* source, float time) {
        return script::projectResult(vm, source ? self->capture(*source, time)
                                                : Result<void>::failure(Diagnostic::error(
                                                      DiagnosticCode::InvalidArgument, "source required", "source")));
    });
    cls.addFunc("present", [vm](CollapseDust* self, fluids::VolumeFluid* target, float time) {
        return script::projectResult(vm, target ? self->present(*target, time)
                                                : Result<void>::failure(Diagnostic::error(
                                                      DiagnosticCode::InvalidArgument, "target required", "target")));
    });
    cls.addFunc("save", [vm](CollapseDust* self, const std::string& path) {
        return script::projectResult(vm, self->save(path));
    });
    cls.addFunc("load", [vm](CollapseDust* self, const std::string& path) {
        return script::projectResult(vm, self->load(path));
    });
    cls.addFunc(
        "injectFog", [vm](CollapseDust* self, graphics::Volumetric* fog, float time, float density, float radius) {
            return script::projectResult(
                vm, fog && fog->getAtmosphereVolume()
                        ? self->injectFog(*fog->getAtmosphereVolume(), fog->getInvViewProj(), time, density, radius)
                        : Result<void>::failure(
                              Diagnostic::error(DiagnosticCode::InvalidArgument, "prepared fog required", "fog")));
        });
    cls.addFunc("updateDepthTexture", [vm](CollapseDust* self, graphics::Graphics* graphics, graphics::Texture* texture,
                                           CollapseAnimation* clip, float time, graphics::Camera3D* camera) {
        return script::projectResult(
            vm, graphics && texture && clip && camera
                    ? self->updateDepthTexture(*graphics, *texture, *clip, time, *camera)
                    : Result<void>::failure(
                          Diagnostic::error(DiagnosticCode::InvalidArgument, "depth arguments required", "depth")));
    });
    cls.addFunc(
        "prepareFog", [vm](CollapseDust* self, CollapseAnimation* clip, int dw, int dh, int gw, int gh, int gd) {
            return script::projectResult(vm, clip ? self->prepareFog(*clip, dw, dh, gw, gh, gd)
                                                  : Result<void>::failure(Diagnostic::error(
                                                        DiagnosticCode::InvalidArgument, "clip required", "clip")));
        });
    cls.addFunc("updateFog", [vm](CollapseDust* self, graphics::Volumetric* fog, graphics::Graphics* graphics,
                                  float time, graphics::Camera3D* camera, float density, float radius) {
        if (!fog || !graphics || !camera)
            return script::projectResult(
                vm, Result<void>::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument, "fog update arguments required", "fog")));
        auto result = self->updateFog(*fog, *graphics, time, *camera, density, radius);
        if (!result) return script::projectStatusResult(vm, result.status());
        return script::projectResult(vm, Result<int>::success(static_cast<int>(result.value())),
                                     [](int value) { return Value(value); });
    });
    cls.addFunc("frameCount", &CollapseDust::frameCount);
    cls.addFunc("prepareDepth", [vm](CollapseDust* self, int width, int height) {
        return script::projectResult(vm, self->prepareDepth(width, height));
    });
}
void DestructionDust::expose(ssq::Class&) {}
}  // namespace eve::physics
