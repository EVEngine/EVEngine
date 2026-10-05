#include "graphics/fog/RealtimeFog.h"

#include "common/Exception.h"
#include "graphics/Volumetric.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

namespace eve::graphics::fog {
namespace {

void throwIfFailed(const Result<void>& result, const char* where) {
    if (result.ok()) return;
    const auto* diagnostic = result.status().primaryDiagnostic();
    throw Exception("%s: %s", where, diagnostic ? diagnostic->message().c_str() : "failed");
}

template <typename T>
T throwOrTake(Result<T>&& result, const char* where) {
    if (!result.ok()) {
        const auto* diagnostic = result.status().primaryDiagnostic();
        throw Exception("%s: %s", where, diagnostic ? diagnostic->message().c_str() : "failed");
    }
    return std::move(result).value();
}

}  // namespace

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
        throwIfFailed(self->setQuality(fogQualityFromName(quality)), "FogSystem.setQuality");
    });
    system.addFunc("simulationTime", [](FogSystem* self) { return self->simulationTime(); });
    system.addFunc("configureDomain",
                   [](FogSystem* self, int w, int h, int d, float minX, float minY, float minZ, float maxX,
                      float maxY, float maxZ) {
                       FogWorldBounds bounds;
                       bounds.minimum = {minX, minY, minZ};
                       bounds.maximum = {maxX, maxY, maxZ};
                       throwIfFailed(self->configureDomain(w, h, d, bounds), "FogSystem.configureDomain");
                   });
    system.addFunc("seedHeightFog",
                   [](FogSystem* self, float density, float height, float falloff, float curl) {
                       throwIfFailed(self->seedHeightFog(density, height, falloff, curl),
                                     "FogSystem.seedHeightFog");
                   });
    system.addFunc("setMainWind", [](FogSystem* self, float x, float y, float z) {
        throwIfFailed(self->wind().setMainWind(glm::vec3(x, y, z)), "FogSystem.setMainWind");
    });
    system.addFunc("setCurlStrength", [](FogSystem* self, float strength) {
        throwIfFailed(self->wind().setCurlStrength(strength), "FogSystem.setCurlStrength");
    });
    system.addFunc("setCurlTimeScale", [](FogSystem* self, float scale) {
        throwIfFailed(self->wind().setCurlTimeScale(scale), "FogSystem.setCurlTimeScale");
    });
    system.addFunc("setWindResponseRate", [](FogSystem* self, float rate) {
        throwIfFailed(self->wind().setResponseRate(rate), "FogSystem.setWindResponseRate");
    });
    system.addFunc("setSphereInteractor",
                   [](FogSystem* self, float x, float y, float z, float radius, float vx, float vy,
                      float vz) {
                       FogSolidProxy proxy;
                       proxy.shape = FogProxyShape::Sphere;
                       proxy.position = {x, y, z};
                       proxy.extents = {radius, radius, radius};
                       proxy.velocity = {vx, vy, vz};
                       proxy.drag = 0.4f;
                       proxy.wakeStrength = 1.5f;
                       throwIfFailed(self->interactor().setProxies({proxy}),
                                     "FogSystem.setSphereInteractor");
                   });
    system.addFunc("clearInteractors", [](FogSystem* self) {
        throwIfFailed(self->interactor().setProxies({}), "FogSystem.clearInteractors");
    });
    system.addFunc("stepSimulation", [](FogSystem* self, float dt) {
        return throwOrTake(self->stepSimulation(dt), "FogSystem.stepSimulation").cfl;
    });
    system.addFunc("syncToVolumetric",
                   [](FogSystem* self, Volumetric* volumetric, float lx, float ly, float lz, float r,
                      float g, float b, float intensity) {
                       return throwOrTake(
                           self->syncToVolumetric(volumetric, glm::vec3(lx, ly, lz),
                                                  glm::vec3(r, g, b), intensity),
                           "FogSystem.syncToVolumetric");
                   });
}

}  // namespace eve::graphics::fog
