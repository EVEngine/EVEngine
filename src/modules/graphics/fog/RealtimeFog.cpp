#include "graphics/fog/RealtimeFog.h"

#include "common/Exception.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>
#include <string>

#include <glm/vec3.hpp>

namespace eve::graphics::fog {

Module_IMPL(RealtimeFog, new RealtimeFog());

Result<std::unique_ptr<FogSystem>> RealtimeFog::newSystem() {
    auto system = std::make_unique<FogSystem>();
    auto quality = system->setQuality(FogQuality::Enhanced);
    if (!quality.ok()) return Result<std::unique_ptr<FogSystem>>::failure(quality.status());
    return Result<std::unique_ptr<FogSystem>>::success(std::move(system));
}

FogSystem* RealtimeFog::newSystemScript() {
    auto created = newSystem();
    if (!created.ok()) {
        const auto* diagnostic = created.status().primaryDiagnostic();
        throw Exception("RealtimeFog.newSystem: %s",
                        diagnostic ? diagnostic->message().c_str() : "creation failed");
    }
    return created.value().release();
}

std::string RealtimeFog::qualityName(FogQuality quality) {
    return std::string(fogQualityName(quality));
}

void RealtimeFog::expose(ssq::Class& cls) {
    cls.addFunc("newSystem", &RealtimeFog::newSystemScript);
    cls.addFunc("qualityName", [](RealtimeFog*, const std::string& quality) -> std::string {
        return std::string(fogQualityName(fogQualityFromName(quality)));
    });
}

void RealtimeFog::expose(ssq::Table& table) {
    auto cls = table.addClass(name, RealtimeFog::create, false);
    expose(cls);

    auto system = table.addClass<FogSystem>(
        "FogSystem", std::function<FogSystem*()>([]() -> FogSystem* { return nullptr; }), true);
    system.addFunc("quality", [](FogSystem* self) {
        return std::string(fogQualityName(self->quality()));
    });
    system.addFunc("setQuality", [](FogSystem* self, const std::string& quality) {
        auto result = self->setQuality(fogQualityFromName(quality));
        if (!result.ok()) {
            const auto* diagnostic = result.status().primaryDiagnostic();
            throw Exception("FogSystem.setQuality: %s",
                            diagnostic ? diagnostic->message().c_str() : "failed");
        }
    });
    system.addFunc("simulationTime", [](FogSystem* self) { return self->simulationTime(); });
    system.addFunc("configureDomain",
                   [](FogSystem* self, int w, int h, int d, float minX, float minY, float minZ, float maxX,
                      float maxY, float maxZ) {
                       FogWorldBounds bounds;
                       bounds.minimum = {minX, minY, minZ};
                       bounds.maximum = {maxX, maxY, maxZ};
                       auto result = self->configureDomain(w, h, d, bounds);
                       if (!result.ok()) {
                           const auto* diagnostic = result.status().primaryDiagnostic();
                           throw Exception("FogSystem.configureDomain: %s",
                                           diagnostic ? diagnostic->message().c_str() : "failed");
                       }
                   });
    system.addFunc("seedHeightFog",
                   [](FogSystem* self, float density, float height, float falloff, float curl) {
                       auto result = self->seedHeightFog(density, height, falloff, curl);
                       if (!result.ok()) {
                           const auto* diagnostic = result.status().primaryDiagnostic();
                           throw Exception("FogSystem.seedHeightFog: %s",
                                           diagnostic ? diagnostic->message().c_str() : "failed");
                       }
                   });
    system.addFunc("stepSimulation", [](FogSystem* self, float dt) {
        auto result = self->stepSimulation(dt);
        if (!result.ok()) {
            const auto* diagnostic = result.status().primaryDiagnostic();
            throw Exception("FogSystem.stepSimulation: %s",
                            diagnostic ? diagnostic->message().c_str() : "failed");
        }
        return result.value().cfl;
    });
}

}  // namespace eve::graphics::fog
