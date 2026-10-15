#include <cmath>
#include <vector>
#include "GraphicsParitySupport.h"
#include "graphics/AtmosphereVolume.h"
#include "graphics/Volumetric.h"
#include "graphics/fog/AnalyticalVolLight.h"
#include "graphics/fog/ContinuousArt.h"
#include "graphics/fog/FogInteractor.h"
#include "graphics/fog/FogRayMarch.h"
#include "graphics/fog/FogSystem.h"
#include "graphics/fog/ProceduralDust.h"
#include "graphics/fog/RealtimeFog.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// graphics_fog.cpp
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

[[maybe_unused]] FogWorldBounds unitBox() {
    FogWorldBounds b;
    b.minimum = {-4.f, 0.f, -4.f};
    b.maximum = {4.f, 4.f, 4.f};
    return b;
}

[[maybe_unused]] FogSystem makeSeeded(FogQuality quality = FogQuality::Enhanced) {
    FogSystem system;
    REQUIRE(system.setQuality(quality).ok());
    REQUIRE(system.configureDomain(8, 6, 8, unitBox()).ok());
    REQUIRE(system.seedHeightFog(1.2f, 0.f, 0.4f, 0.25f, 11u).ok());
    return system;
}

}  // namespace


// graphics_fog_volumetric.cpp
using eve::graphics::Volumetric;
using eve::graphics::fog::FogQuality;
using eve::graphics::fog::FogSystem;
using eve::graphics::fog::FogWorldBounds;
using eve::graphics::parity_test::headlessGraphics;

TEST_CASE("graphics_fog.syncToVolumetricInjectsMedia") {
    auto* graphics = headlessGraphics();
    REQUIRE(graphics != nullptr);

    FogSystem system;
    REQUIRE(system.setQuality(FogQuality::Fast).ok());
    FogWorldBounds bounds;
    bounds.minimum = {-4.f, 0.f, -4.f};
    bounds.maximum = {4.f, 4.f, 4.f};
    REQUIRE(system.configureDomain(8, 6, 8, bounds).ok());
    REQUIRE(system.seedHeightFog(1.0f, 0.f, 0.3f, 0.2f).ok());

    Volumetric volume(graphics);
    volume.setMode("froxel");
    volume.setCamera(0.f, 3.f, 8.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 55.f, 1.777f, 0.1f, 40.f);
    volume.configureFroxelGrid(16, 9, 16, 0.1f, 40.f);

    auto written = system.syncToVolumetric(&volume, glm::vec3(0.2f, 1.f, 0.1f), glm::vec3(1.f), 1.2f);
    REQUIRE(written.ok());
    CHECK(written.value() > 0);
    CHECK(volume.getAtmosphereVolume() != nullptr);
    CHECK(volume.getAtmosphereVolume()->getFroxelCount() > 0u);
    // upload must succeed after sync (atlas cols/rows follow configureFroxelGrid).
    volume.uploadFroxel(graphics);

    CHECK(!system.syncToVolumetric(nullptr, glm::vec3(0.f, 1.f, 0.f), glm::vec3(1.f), 1.f).ok());
}

TEST_CASE("graphics_fog.injectToVolumetricKeepsHeightFog") {
    auto* graphics = headlessGraphics();
    REQUIRE(graphics != nullptr);

    FogSystem system;
    REQUIRE(system.setQuality(FogQuality::Fast).ok());
    FogWorldBounds bounds;
    bounds.minimum = {-4.f, 0.f, -4.f};
    bounds.maximum = {4.f, 4.f, 4.f};
    REQUIRE(system.configureDomain(8, 6, 8, bounds).ok());
    REQUIRE(system.seedHeightFog(1.0f, 0.f, 0.3f, 0.2f).ok());

    Volumetric volume(graphics);
    volume.setMode("froxel");
    volume.setCamera(0.f, 3.f, 8.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 55.f, 1.777f, 0.1f, 40.f);
    volume.configureFroxelGrid(16, 9, 16, 0.1f, 40.f);
    volume.injectFroxelHeightFog(0.035f, 0.72f, 0.82f, 0.98f, 0.f, 0.16f, -2.f, 10.f);
    const float before = volume.getAtmosphereVolume()->at(8, 4, 8).extinction;
    CHECK(before > 0.f);

    auto written = system.injectToVolumetric(&volume);
    REQUIRE(written.ok());
    CHECK(volume.getAtmosphereVolume()->at(8, 4, 8).extinction >= before);
    volume.integrateFroxel(0.55f, 0.62f, 0.78f, 1.f);
    volume.uploadFroxel(graphics);

    CHECK(!system.injectToVolumetric(nullptr).ok());
}
