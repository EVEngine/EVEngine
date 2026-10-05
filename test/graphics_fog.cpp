#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/AtmosphereVolume.h"
#include "graphics/fog/AnalyticalVolLight.h"
#include "graphics/fog/ContinuousArt.h"
#include "graphics/fog/FogInteractor.h"
#include "graphics/fog/FogRayMarch.h"
#include "graphics/fog/FogSystem.h"
#include "graphics/fog/ProceduralDust.h"
#include "graphics/fog/RealtimeFog.h"

#include <cmath>
#include <vector>

using eve::graphics::AtmosphereVolume;
using eve::graphics::fog::AnalyticBeam;
using eve::graphics::fog::BeerLightCache;
using eve::graphics::fog::ContinuousArtParams;
using eve::graphics::fog::FogCflReport;
using eve::graphics::fog::FogDensityField;
using eve::graphics::fog::FogInteractor;
using eve::graphics::fog::FogProfile;
using eve::graphics::fog::FogProxyShape;
using eve::graphics::fog::FogQuality;
using eve::graphics::fog::FogRayMarch;
using eve::graphics::fog::FogRayResult;
using eve::graphics::fog::FogSolidProxy;
using eve::graphics::fog::FogSystem;
using eve::graphics::fog::FogVolumeBound;
using eve::graphics::fog::FogWorldBounds;
using eve::graphics::fog::ProceduralDust;
using eve::graphics::fog::RealtimeFog;
using eve::graphics::fog::SceneWind;

namespace {

FogWorldBounds unitBox() {
    FogWorldBounds b;
    b.minimum = {-4.f, 0.f, -4.f};
    b.maximum = {4.f, 4.f, 4.f};
    return b;
}

FogSystem makeSeeded(FogQuality quality = FogQuality::Enhanced) {
    FogSystem system;
    REQUIRE(system.setQuality(quality).ok());
    REQUIRE(system.configureDomain(8, 6, 8, unitBox()).ok());
    REQUIRE(system.seedHeightFog(1.2f, 0.f, 0.4f, 0.25f, 11u).ok());
    return system;
}

}  // namespace

TEST_CASE("graphics_fog.qualityPresetsChangeBudgets") {
    CHECK_EQ(eve::graphics::fog::budgetFor(FogQuality::Fast).raySamples, 12);
    CHECK_EQ(eve::graphics::fog::budgetFor(FogQuality::Enhanced).volLightSegments, 4);
    CHECK_EQ(eve::graphics::fog::budgetFor(FogQuality::PhysicalReference).beerLightCache, false);
    CHECK_EQ(eve::graphics::fog::fogQualityFromName("fast"), FogQuality::Fast);
}

TEST_CASE("graphics_fog.profileRejectsInvalidAlbedoAndPhaseIsForwardPeaked") {
    FogProfile profile;
    CHECK(!profile.configure(0.1f, glm::vec3(1.2f), 0.1f, 1.f, 0.4f, 0.4f).ok());
    REQUIRE(profile.configure(0.1f, glm::vec3(0.9f), 0.1f, 1.f, 0.4f, 0.6f).ok());
    CHECK(profile.phase(1.f) > profile.phase(0.f));
    CHECK(profile.phase(0.f) > profile.phase(-1.f));
}

TEST_CASE("graphics_fog.densityWorldSampleIsStableAcrossUvDrift") {
    FogDensityField field;
    REQUIRE(field.resize(8, 8, 8, unitBox()).ok());
    REQUIRE(field.seedHeightBand(1.f, 0.f, 0.2f, 0.f, 3u).ok());
    const glm::vec3 world(0.4f, 0.8f, -0.3f);
    const float a = field.sampleDensity(world);
    const float b = field.sampleDensity(world);
    CHECK_EQ(a, b);
    CHECK(field.sampleDensity(glm::vec3(0.4f, 3.5f, -0.3f)) < a);
}

TEST_CASE("graphics_fog.sceneWindRateLimitsAndKeepsCurlIndependent") {
    SceneWind wind;
    REQUIRE(wind.setResponseRate(2.f).ok());
    REQUIRE(wind.setMainWind(glm::vec3(10.f, 0.f, 0.f)).ok());
    REQUIRE(wind.setCurlStrength(3.f).ok());
    REQUIRE(wind.tick(0.5f).ok());
    CHECK(wind.mainWind().x > 0.f);
    CHECK(wind.mainWind().x < 10.f);
    CHECK(wind.curlStrength() > 0.f);
    CHECK(wind.curlStrength() < 3.f);
    const glm::vec3 sample = wind.sample(glm::vec3(1.f, 1.f, 1.f), 0.f);
    CHECK(std::fabs(sample.x - wind.mainWind().x) > 0.01f);
}

TEST_CASE("graphics_fog.macFixedStepCatchesUpAndReportsCfl") {
    FogSystem system = makeSeeded();
    REQUIRE(system.wind().setMainWind(glm::vec3(0.4f, 0.f, 0.1f)).ok());
    REQUIRE(system.wind().setResponseRate(100.f).ok());
    auto first = system.stepSimulation(1.f / 30.f);
    REQUIRE(first.ok());
    CHECK(system.fluid().stepCount() >= 2);
    CHECK(first.value().cellSize > 0.f);
    CHECK(first.value().cfl >= 0.f);
}

TEST_CASE("graphics_fog.interactorClearsInteriorAndBlocksThinAdvection") {
    FogSystem system = makeSeeded();
    FogSolidProxy sphere;
    sphere.shape = FogProxyShape::Sphere;
    sphere.position = {0.f, 1.f, 0.f};
    sphere.extents = {0.9f, 0.9f, 0.9f};
    sphere.velocity = {2.f, 0.f, 0.f};
    sphere.wakeStrength = 2.f;
    REQUIRE(system.interactor().setProxies({sphere}).ok());
    REQUIRE(system.stepSimulation(1.f / 60.f).ok());
    CHECK(system.densityField().sampleDensity(glm::vec3(0.f, 1.f, 0.f)) < 0.05f);

    FogInteractor clipper;
    FogSolidProxy slab;
    slab.shape = FogProxyShape::Obb;
    slab.position = {0.f, 1.f, 0.f};
    slab.extents = {0.05f, 2.f, 2.f};
    REQUIRE(clipper.setProxies({slab}).ok());
    const glm::vec3 blocked =
        clipper.clipAdvection(glm::vec3(2.f, 2.f, 4.f), glm::vec3(-8.f, 0.f, 0.f), unitBox(),
                              glm::vec3(1.f));
    CHECK(blocked.x > -6.f);
}

TEST_CASE("graphics_fog.rayMarchBeerLambertAndWorldBounds") {
    FogSystem system = makeSeeded(FogQuality::PhysicalReference);
    FogVolumeBound bound;
    bound.kind = FogVolumeBound::Kind::HeightLayer;
    bound.heightMin = 0.f;
    bound.heightMax = 4.f;
    const glm::vec3 origin(0.f, 1.f, 6.f);
    const glm::vec3 dir(0.f, 0.f, -1.f);
    auto farRay = system.marchRay(origin, dir, bound, glm::vec3(0.3f, 1.f, 0.2f), glm::vec3(1.f), 2.f,
                                  glm::vec3(0.4f), glm::vec3(0.1f), 20.f);
    auto nearRay = system.marchRay(origin, dir, bound, glm::vec3(0.3f, 1.f, 0.2f), glm::vec3(1.f), 2.f,
                                   glm::vec3(0.4f), glm::vec3(0.1f), 1.5f);
    REQUIRE(farRay.ok());
    REQUIRE(nearRay.ok());
    CHECK(farRay.value().transmittance < nearRay.value().transmittance);
    CHECK(farRay.value().opticalDepth > nearRay.value().opticalDepth);
    CHECK(farRay.value().samplesUsed > 0);

    auto miss = FogRayMarch::intersectBound(glm::vec3(0.f, 20.f, 0.f), glm::vec3(0.f, 1.f, 0.f), bound);
    CHECK(!miss.has_value());
}

TEST_CASE("graphics_fog.beerCacheInvalidatesOnDensityAndSkipsPhysical") {
    FogSystem enhanced = makeSeeded(FogQuality::Enhanced);
    REQUIRE(enhanced.ensureBeerCache(glm::vec3(0.f, 1.f, 0.f), glm::vec3(1.f), 1.f).ok());
    CHECK(enhanced.beerCache().valid());
    const auto version = enhanced.beerCache().version();
    REQUIRE(enhanced.stepSimulation(1.f / 60.f).ok());
    CHECK(!enhanced.beerCache().valid());
    REQUIRE(enhanced.ensureBeerCache(glm::vec3(0.f, 1.f, 0.f), glm::vec3(1.f), 1.f).ok());
    CHECK(enhanced.beerCache().version() != version);

    FogSystem physical = makeSeeded(FogQuality::PhysicalReference);
    auto noop = physical.ensureBeerCache(glm::vec3(0.f, 1.f, 0.f), glm::vec3(1.f), 1.f);
    REQUIRE(noop.ok());
    CHECK_EQ(noop.code(), eve::StatusCode::NoOp);
    CHECK(!physical.beerCache().valid());
}

TEST_CASE("graphics_fog.froxelInjectsOccupiedMedia") {
    FogSystem system = makeSeeded(FogQuality::Fast);
    AtmosphereVolume volume;
    glm::mat4 inv(1.f);
    auto written = system.renderToFroxel(volume, inv, glm::vec3(0.2f, 1.f, 0.1f), glm::vec3(1.f), 1.f);
    REQUIRE(written.ok());
    CHECK(written.value() > 0);
    CHECK(volume.getWidth() == eve::graphics::fog::budgetFor(FogQuality::Fast).froxelWidth);
    const glm::vec4 sample = volume.sampleIntegrated(0.5f, 0.5f, 8.f);
    CHECK(sample.a <= 1.f);
}

TEST_CASE("graphics_fog.analyticBeamIsOccludedBySceneDepth") {
    FogProfile profile;
    REQUIRE(profile.configure(0.2f, glm::vec3(0.9f), 0.1f, 1.f, 0.5f, 0.4f).ok());
    AnalyticBeam beam;
    beam.apex = {0.f, 2.f, 0.f};
    beam.direction = {0.f, 0.f, 1.f};
    beam.nearDistance = 0.2f;
    beam.farDistance = 8.f;
    beam.nearRadius = 0.2f;
    beam.farRadius = 1.5f;
    beam.intensity = 4.f;
    FogSystem system = makeSeeded();
    auto open = system.integrateBeam(beam, glm::vec3(0.f, 2.f, -1.f), glm::vec3(0.f, 0.f, 1.f), 0.f, 0.4f);
    auto blocked = system.integrateBeam(beam, glm::vec3(0.f, 2.f, -1.f), glm::vec3(0.f, 0.f, 1.f), 0.3f, 0.4f);
    REQUIRE(open.ok());
    REQUIRE(blocked.ok());
    CHECK(open.value().inScatter.r >= blocked.value().inScatter.r);
}

TEST_CASE("graphics_fog.proceduralDustIsWorldStableWithoutScreenTrail") {
    ProceduralDust dust;
    auto a = dust.generate(unitBox(), 32, 0.f, 0.2f, 9u);
    auto b = dust.generate(unitBox(), 32, 0.f, 0.2f, 9u);
    auto c = dust.generate(unitBox(), 32, 1.f, 0.2f, 9u);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE(c.ok());
    CHECK_EQ(a.value()[0].position.x, b.value()[0].position.x);
    CHECK(std::fabs(a.value()[0].position.x - c.value()[0].position.x) > 1e-4f);
    CHECK(a.value()[0].band != a.value()[1].band);
}

TEST_CASE("graphics_fog.artDoesNotMutatePhysicalOptics") {
    FogSystem system = makeSeeded();
    FogRayResult physical;
    physical.inScatter = {0.2f, 0.25f, 0.3f};
    physical.transmittance = 0.4f;
    ContinuousArtParams art;
    art.colorTint = {1.2f, 1.1f, 1.f};
    art.edgeBoost = 0.8f;
    art.internalGlow = 0.5f;
    art.silverDust = 0.7f;
    REQUIRE(system.art().setParams(art).ok());
    const auto styled = system.stylize(physical, 12.f, 0.1f);
    CHECK_EQ(physical.transmittance, 0.4f);
    CHECK(styled.opacity > 0.f);
    CHECK(styled.dustHighlight > 0.f);
    CHECK(styled.color.r > physical.inScatter.r);
}

TEST_CASE("graphics_fog.moduleFactoryTransfersOwnership") {
    RealtimeFog* module = RealtimeFog::create();
    REQUIRE(module != nullptr);
    auto created = module->newSystem();
    REQUIRE(created.ok());
    CHECK(created.value() != nullptr);
    CHECK_EQ(created.value()->quality(), FogQuality::Enhanced);
}
