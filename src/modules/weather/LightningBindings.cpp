#include <functional>
#include <stdexcept>
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "graphics/Graphics.h"
#include "weather/Lightning.h"
#include "weather/Weather.h"
namespace eve::weather {
void exposeLightning(ssq::Table& table, ssq::Class& weather) {
    const auto vm       = table.getHandle();
    auto       settings = table.addClass("LightningSettings", ssq::Class::Ctor<LightningSettings()>());
    settings.addVar("width", &LightningSettings::width);
    settings.addVar("displacement", &LightningSettings::displacement);
    settings.addVar("duration", &LightningSettings::duration);
    settings.addVar("subdivisions", &LightningSettings::subdivisions);
    settings.addVar("branches", &LightningSettings::branches);
    settings.addVar("variants", &LightningSettings::variants);
    settings.addVar("seed", &LightningSettings::seed);
    auto cls = table.addClass<Lightning>("Lightning", std::function<Lightning*()>([]() -> Lightning* {
                                             throw std::runtime_error("Use weather.prepareLightning(gfx, settings)");
                                         }),
                                         false);
    weather.addFunc("prepareLightning", [vm](Weather*, graphics::Graphics* gfx, LightningSettings* s) {
        if (!gfx || !s)
            return script::projectStatusResult(
                vm, Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Graphics and settings required",
                                                      "weather.lightning")));
        auto prepared = Lightning::prepare(*gfx, *s);
        if (!prepared) return script::projectStatusResult(vm, prepared.status());
        auto owned = script::makeOwnedSquirrelInstance(vm, std::move(prepared).takeValue());
        if (!owned) return script::projectStatusResult(vm, owned.status());
        return script::projectStatusResult(vm, Status::success(), std::move(owned).takeValue());
    });
    cls.addFunc("setEndpoints", [vm](Lightning* s, float ax, float ay, float az, float bx, float by, float bz) {
        return script::projectResult(vm, s->setEndpoints(ax, ay, az, bx, by, bz));
    });
    cls.addFunc("setColor", [vm](Lightning* s, float r, float g, float b) {
        return script::projectResult(vm, s->setColor(r, g, b));
    });
    cls.addFunc("trigger", [vm](Lightning* s) { return script::projectResult(vm, s->trigger()); });
    cls.addFunc("update", [vm](Lightning* s, float dt) { return script::projectResult(vm, s->update(dt)); });
    cls.addFunc("stop", &Lightning::stop);
    cls.addFunc("amplitude", &Lightning::amplitude);
}
}  // namespace eve::weather
