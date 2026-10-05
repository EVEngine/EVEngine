#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "GraphicsParitySupport.h"
#include "graphics/Volumetric.h"
#include "graphics/fog/FogSystem.h"

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

    auto written =
        system.syncToVolumetric(&volume, glm::vec3(0.2f, 1.f, 0.1f), glm::vec3(1.f), 1.2f);
    REQUIRE(written.ok());
    CHECK(written.value() > 0);
    CHECK(volume.getAtmosphereVolume() != nullptr);
    CHECK(volume.getAtmosphereVolume()->getFroxelCount() > 0u);
    // upload must succeed after sync (atlas cols/rows follow configureFroxelGrid).
    volume.uploadFroxel(graphics);

    CHECK(!system.syncToVolumetric(nullptr, glm::vec3(0.f, 1.f, 0.f), glm::vec3(1.f), 1.f).ok());
}
