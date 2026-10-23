#include "daynight/sky/SkyScriptBindings.h"
#include <functional>
#include <optional>
#include <stdexcept>
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "daynight/DayNight.h"
#include "daynight/sky/SkyProfile.h"
#include "daynight/sky/SkyRuntime.h"
#include "graphics/Graphics.h"
#include "graphics/sky/SkyWispsAsset.h"

namespace eve::daynight {
namespace {
Result<void> invalid(const char* message) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, "sky.script"));
}
Value frameValue(const SkyRuntimeFrame& frame) {
    const auto& weather = frame.simulation.weather;
    return Value::object(
        {{"hour", frame.simulation.hour},
         {"elapsedSeconds", frame.simulation.elapsedSeconds},
         {"cloudTime", frame.cloudTime},
         {"wispsMorphPhase", double(frame.wispsMorphPhase)},
         {"cloudTravelXZ", Value::array({frame.simulation.cloudTravelXZ[0], frame.simulation.cloudTravelXZ[1]})},
         {"weather", Value::object({{"cloudCoverage", weather.cloudCoverage},
                                    {"fog", weather.fog},
                                    {"rain", weather.rain},
                                    {"snow", weather.snow},
                                    {"windXZ", Value::array({weather.windXZ[0], weather.windXZ[1]})}})},
         {"sunDirection", Value::array({frame.sunDirection[0], frame.sunDirection[1], frame.sunDirection[2]})},
         {"sunIrradiance", Value::array({frame.sunIrradiance[0], frame.sunIrradiance[1], frame.sunIrradiance[2]})}});
}
}  // namespace
void exposeSkyScriptBindings(ssq::Table& table, ssq::Class& daynightClass) {
    const auto vm = table.getHandle();
    auto       cls =
        table.addClass<SkyRuntime>("SkyRuntime", std::function<SkyRuntime*()>([]() -> SkyRuntime* {
                                       throw std::runtime_error("Use daynight.prepareSky(graphics, profileJson)");
                                   }),
                                   false);
    daynightClass.addFunc("prepareSky", [vm](DayNight*, graphics::Graphics* graphics, const std::string& profileJson) {
        if (!graphics || profileJson.size() > 65536)
            return script::projectResult(
                vm, invalid("Sky preparation requires graphics and at most 64 KiB of profile JSON"));
        auto document = Value::fromJson(profileJson);
        if (!document) return script::projectStatusResult(vm, document.status());
        auto profile = SkyProfile::decode(document.value());
        if (!profile) return script::projectStatusResult(vm, profile.status());
        std::optional<graphics::SkyWispsAsset> wisps;
        if (profile.value().proceduralSeed() || !profile.value().wispsAsset().empty()) {
            auto loaded = profile.value().proceduralSeed()
                              ? graphics::SkyWispsAsset::generate(*profile.value().proceduralSeed())
                              : graphics::SkyWispsAsset::load(profile.value().wispsAsset());
            if (!loaded) return script::projectStatusResult(vm, loaded.status());
            wisps.emplace(std::move(loaded).takeValue());
        }
        auto prepared = SkyRuntime::prepare(*graphics, profile.value().settings(), profile.value().atmosphere(),
                                            wisps ? &wisps->layer() : nullptr);
        if (!prepared) return script::projectStatusResult(vm, prepared.status());
        auto owned = script::makeOwnedSquirrelInstance(vm, std::move(prepared).takeValue());
        if (!owned) return script::projectStatusResult(vm, owned.status());
        auto result = script::projectStatusResult(vm, Status::success(), std::move(owned).takeValue());
        result.set("ownership", std::string("owned"));
        result.set("assetSource",
                   std::string(profile.value().proceduralSeed() ? "procedural-v1"
                                                                : (wisps ? "external-pack" : "atmosphere-only")));
        return result;
    });
    cls.addFunc("attach", [vm](SkyRuntime* sky) {
        return script::projectResult(vm, sky ? sky->attach() : invalid("Sky runtime must not be null"));
    });
    cls.addFunc("detach", [vm](SkyRuntime* sky) {
        if (!sky) return script::projectResult(vm, invalid("Sky runtime must not be null"));
        sky->detach();
        return script::projectResult(vm, Result<void>::success());
    });
    cls.addFunc("advanceSeconds", [vm](SkyRuntime* sky, float seconds) {
        if (!sky) return script::projectResult(vm, invalid("Sky runtime must not be null"));
        auto duration = Duration::fromSeconds(seconds);
        if (!duration) return script::projectStatusResult(vm, duration.status());
        return script::projectResult(vm, sky->advance(duration.value()));
    });
    cls.addFunc("frame", [vm](SkyRuntime* sky) {
        if (!sky) return script::projectResult(vm, invalid("Sky runtime must not be null"));
        return script::projectStatusResult(vm, Status::success(), frameValue(sky->frame()));
    });
}
}  // namespace eve::daynight
