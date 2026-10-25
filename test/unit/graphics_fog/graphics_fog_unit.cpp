#include "graphics/AtmosphereVolume.h"
#include "graphics/fog/AnalyticalVolLight.h"
#include "graphics/fog/ContinuousArt.h"
#include "graphics/fog/FogInteractor.h"
#include "graphics/fog/FogRayMarch.h"
#include "graphics/fog/FogSystem.h"
#include "graphics/fog/ProceduralDust.h"
#include "graphics/fog/RealtimeFog.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>

using namespace eve::graphics;
using namespace eve::graphics::fog;

namespace {
FogWorldBounds domain() {
    FogWorldBounds bounds;
    bounds.minimum = {-4.f, 0.f, -4.f};
    bounds.maximum = {4.f, 4.f, 4.f};
    return bounds;
}

FogSystem seeded(FogQuality quality = FogQuality::Enhanced) {
    FogSystem system;
    REQUIRE(system.setQuality(quality).ok());
    REQUIRE(system.configureDomain(8, 6, 8, domain()).ok());
    REQUIRE(system.seedHeightFog(1.2f, 0.f, .4f, .25f, 11u).ok());
    return system;
}
}  // namespace

TEST_CASE("graphics.fog.spec.configurationAndFields") {
    SUB_CASE("named quality changes ray, froxel, and cache budgets") {
        REQUIRE(fogQualityFromName("fast") == FogQuality::Fast);
        REQUIRE(budgetFor(FogQuality::Fast).raySamples == 12);
        REQUIRE(budgetFor(FogQuality::Enhanced).volLightSegments == 4);
        REQUIRE(!budgetFor(FogQuality::PhysicalReference).beerLightCache);
    };
    SUB_CASE("optical profile validates albedo and has a forward-peaked phase") {
        FogProfile profile;
        REQUIRE(!profile.configure(.1f, glm::vec3(1.2f), .1f, 1.f, .4f, .4f).ok());
        REQUIRE(profile.configure(.1f, glm::vec3(.9f), .1f, 1.f, .4f, .6f).ok());
        REQUIRE(profile.phase(1.f) > profile.phase(0.f));
        REQUIRE(profile.phase(0.f) > profile.phase(-1.f));
    };
    SUB_CASE("world-space density is repeatable and decreases above the height band") {
        FogDensityField field;
        REQUIRE(field.resize(8, 8, 8, domain()).ok());
        REQUIRE(field.seedHeightBand(1.f, 0.f, .2f, 0.f, 3u).ok());
        const glm::vec3 point(.4f, .8f, -.3f);
        const float     density = field.sampleDensity(point);
        REQUIRE(field.sampleDensity(point) == density);
        REQUIRE(field.sampleDensity(glm::vec3(.4f, 3.5f, -.3f)) < density);
    };
    SUB_CASE("wind approaches target at a bounded rate and curl changes sampled flow") {
        SceneWind wind;
        REQUIRE(wind.setResponseRate(2.f).ok());
        REQUIRE(wind.setMainWind(glm::vec3(10.f, 0.f, 0.f)).ok());
        REQUIRE(wind.setCurlStrength(3.f).ok());
        REQUIRE(wind.tick(.5f).ok());
        REQUIRE(wind.mainWind().x > 0.f);
        REQUIRE(wind.mainWind().x < 10.f);
        REQUIRE(wind.curlStrength() > 0.f);
        REQUIRE(wind.curlStrength() < 3.f);
        REQUIRE(std::fabs(wind.sample(glm::vec3(1.f), 0.f).x - wind.mainWind().x) > .01f);
    };
}

TEST_CASE("graphics.fog.spec.simulationAndCache") {
    SUB_CASE("wall time advances fixed MAC steps and emits CFL diagnostics") {
        auto system = seeded();
        REQUIRE(system.wind().setMainWind(glm::vec3(.4f, 0.f, .1f)).ok());
        REQUIRE(system.wind().setResponseRate(100.f).ok());
        auto report = system.stepSimulation(1.f / 30.f);
        REQUIRE(report.ok());
        REQUIRE(system.fluid().stepCount() >= 2);
        REQUIRE(report.value().cellSize > 0.f);
        REQUIRE(report.value().cfl >= 0.f);
    };
    SUB_CASE("solid sphere clears its interior; thin slab clips advection") {
        auto          system = seeded();
        FogSolidProxy sphere;
        sphere.shape        = FogProxyShape::Sphere;
        sphere.position     = {0.f, 1.f, 0.f};
        sphere.extents      = {.9f, .9f, .9f};
        sphere.velocity     = {2.f, 0.f, 0.f};
        sphere.wakeStrength = 2.f;
        REQUIRE(system.interactor().setProxies({sphere}).ok());
        REQUIRE(system.stepSimulation(1.f / 60.f).ok());
        REQUIRE(system.densityField().sampleDensity(glm::vec3(0.f, 1.f, 0.f)) < .05f);
        FogInteractor clipper;
        FogSolidProxy slab;
        slab.shape    = FogProxyShape::Obb;
        slab.position = {0.f, 1.f, 0.f};
        slab.extents  = {.05f, 2.f, 2.f};
        REQUIRE(clipper.setProxies({slab}).ok());
        // Local x=6 starts at world x=2.5. The proposed -8-cell path crosses
        // the slab at world x=0, so traversal must stop before the far side.
        auto clipped =
            clipper.clipAdvection(glm::vec3(6.f, 2.f, 4.f), glm::vec3(-8.f, 0.f, 0.f), domain(), glm::vec3(1.f));
        REQUIRE(clipped.x < 6.f);
        REQUIRE(clipped.x > 3.f);
    };
    SUB_CASE("density advance invalidates enhanced Beer cache; physical quality skips it") {
        auto enhanced = seeded();
        REQUIRE(enhanced.ensureBeerCache(glm::vec3(0.f, 1.f, 0.f), glm::vec3(1.f), 1.f).ok());
        REQUIRE(enhanced.beerCache().valid());
        auto version = enhanced.beerCache().version();
        REQUIRE(enhanced.stepSimulation(1.f / 60.f).ok());
        REQUIRE(!enhanced.beerCache().valid());
        REQUIRE(enhanced.ensureBeerCache(glm::vec3(0.f, 1.f, 0.f), glm::vec3(1.f), 1.f).ok());
        REQUIRE(enhanced.beerCache().version() != version);
        auto physical = seeded(FogQuality::PhysicalReference);
        auto skipped  = physical.ensureBeerCache(glm::vec3(0.f, 1.f, 0.f), glm::vec3(1.f), 1.f);
        REQUIRE(skipped.ok());
        REQUIRE(skipped.code() == eve::StatusCode::NoOp);
        REQUIRE(!physical.beerCache().valid());
    };
}

TEST_CASE("graphics.fog.spec.opticalAndArtOutputs") {
    SUB_CASE("longer ray increases optical depth and loses transmittance") {
        auto           system = seeded(FogQuality::PhysicalReference);
        FogVolumeBound bound;
        bound.kind      = FogVolumeBound::Kind::HeightLayer;
        bound.heightMin = 0.f;
        bound.heightMax = 4.f;
        auto march      = [&](float maximumDistance) {
            return system.marchRay(glm::vec3(0.f, 1.f, 6.f), glm::vec3(0.f, 0.f, -1.f), bound, glm::vec3(.3f, 1.f, .2f),
                                        glm::vec3(1.f), 2.f, glm::vec3(.4f), glm::vec3(.1f), maximumDistance);
        };
        auto near = march(1.5f);
        auto far  = march(20.f);
        REQUIRE(near.ok());
        REQUIRE(far.ok());
        REQUIRE(far.value().opticalDepth > near.value().opticalDepth);
        REQUIRE(far.value().transmittance < near.value().transmittance);
        REQUIRE(far.value().samplesUsed > 0);
        REQUIRE(!FogRayMarch::intersectBound(glm::vec3(0.f, 20.f, 0.f), glm::vec3(0.f, 1.f, 0.f), bound).has_value());
    };
    SUB_CASE("scene depth occludes an analytic beam") {
        auto         system = seeded();
        AnalyticBeam beam;
        beam.apex         = {0.f, 2.f, 0.f};
        beam.direction    = {0.f, 0.f, 1.f};
        beam.nearDistance = .2f;
        beam.farDistance  = 8.f;
        beam.nearRadius   = .2f;
        beam.farRadius    = 1.5f;
        beam.intensity    = 4.f;
        auto integrate    = [&](float depth) {
            return system.integrateBeam(beam, glm::vec3(0.f, 2.f, -1.f), glm::vec3(0.f, 0.f, 1.f), depth, .4f);
        };
        auto open    = integrate(0.f);
        auto blocked = integrate(.3f);
        REQUIRE(open.ok());
        REQUIRE(blocked.ok());
        REQUIRE(open.value().inScatter.r >= blocked.value().inScatter.r);
    };
    SUB_CASE("dust is repeatable at fixed seed/time; changing time moves particles") {
        ProceduralDust dust;
        auto           first  = dust.generate(domain(), 32, 0.f, .2f, 9u);
        auto           repeat = dust.generate(domain(), 32, 0.f, .2f, 9u);
        auto           later  = dust.generate(domain(), 32, 1.f, .2f, 9u);
        REQUIRE(first.ok());
        REQUIRE(repeat.ok());
        REQUIRE(later.ok());
        REQUIRE(first.value()[0].position.x == repeat.value()[0].position.x);
        REQUIRE(std::fabs(first.value()[0].position.x - later.value()[0].position.x) > 1e-4f);
        REQUIRE(first.value()[0].band != first.value()[1].band);
    };
    SUB_CASE("art changes presentation without mutating optical input") {
        auto         system = seeded();
        FogRayResult physical;
        physical.inScatter     = {.2f, .25f, .3f};
        physical.transmittance = .4f;
        ContinuousArtParams params;
        params.colorTint    = {1.2f, 1.1f, 1.f};
        params.edgeBoost    = .8f;
        params.internalGlow = .5f;
        params.silverDust   = .7f;
        REQUIRE(system.art().setParams(params).ok());
        auto styled = system.stylize(physical, 12.f, .1f);
        REQUIRE(physical.transmittance == .4f);
        REQUIRE(styled.opacity > 0.f);
        REQUIRE(styled.dustHighlight > 0.f);
        REQUIRE(styled.color.r > physical.inScatter.r);
    };
}

TEST_CASE("graphics.fog.spec.renderInjectionAndFactory") {
    SUB_CASE("render creates froxels with quality-specific resolution") {
        auto             system = seeded(FogQuality::Fast);
        AtmosphereVolume volume;
        auto written = system.renderToFroxel(volume, glm::mat4(1.f), glm::vec3(.2f, 1.f, .1f), glm::vec3(1.f), 1.f);
        REQUIRE(written.ok());
        REQUIRE(written.value() > 0);
        REQUIRE(volume.getWidth() == budgetFor(FogQuality::Fast).froxelWidth);
        REQUIRE(volume.sampleIntegrated(.5f, .5f, 8.f).a <= 1.f);
    };
    SUB_CASE("injection adds to existing height fog rather than clearing it") {
        auto             system = seeded(FogQuality::Fast);
        AtmosphereVolume volume;
        volume.resize(16, 9, 16);
        volume.setDepthRange(.1f, 40.f);
        glm::mat4 inverse(1.f);
        volume.injectHeightFogFrustum(.035f, glm::vec3(.72f, .82f, .98f), 0.f, .16f, -2.f, 10.f, inverse);
        const float before = volume.at(8, 4, 4).extinction;
        REQUIRE(before > 0.f);
        REQUIRE(system.injectToFroxel(volume, inverse).ok());
        REQUIRE(volume.at(8, 4, 4).extinction >= before);
    };
    SUB_CASE("module factory returns an owned enhanced system") {
        RealtimeFog* module = RealtimeFog::create();
        REQUIRE(module != nullptr);
        auto created = module->newSystem();
        REQUIRE(created.ok());
        REQUIRE(created.value() != nullptr);
        REQUIRE(created.value()->quality() == FogQuality::Enhanced);
    };
}
