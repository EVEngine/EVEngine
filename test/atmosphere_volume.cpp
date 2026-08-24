#include "graphics/AtmosphereVolume.h"
#include "graphics/AtmosphereClipmap.h"
#include "graphics/FogVolume.h"

#include "zeroerr/unittest.h"

#include <cmath>

using eve::graphics::AtmosphereVolume;
using eve::graphics::AtmosphereClipmap;
using eve::graphics::FogVolume;
using eve::graphics::VolumetricLight;

TEST_CASE("atmosphereVolume.logarithmicSlicesRoundTrip") {
    AtmosphereVolume volume;
    volume.resize(4, 3, 32);
    volume.setDepthRange(0.1f, 1000.f);
    for (int z = 0; z < volume.getDepth(); ++z)
        CHECK(volume.sliceForDistance(volume.sliceDistance(z)) == z);
}

TEST_CASE("atmosphereVolume.uniformBeerLambert") {
    AtmosphereVolume volume;
    volume.resize(1, 1, 64);
    volume.setDepthRange(1.f, 101.f);
    for (int z = 0; z < volume.getDepth(); ++z) {
        volume.at(0, 0, z).extinction = 0.02f;
        volume.at(0, 0, z).scattering = glm::vec3(0.02f);
    }
    volume.integrate(glm::vec3(1.f));
    const float actual = volume.integratedAt(0, 0, 63).a;
    const float expected = std::exp(-0.02f * (volume.sliceDistance(63) - 1.f));
    CHECK(std::fabs(actual - expected) < 1e-5f);
}

TEST_CASE("atmosphereVolume.heightFogFallsOffUpward") {
    AtmosphereVolume volume;
    volume.resize(2, 8, 4);
    volume.injectHeightFog(0.2f, glm::vec3(0.8f), 0.f, 0.5f, 0.f, 8.f);
    CHECK(volume.at(0, 0, 0).extinction > volume.at(0, 7, 0).extinction);
}

TEST_CASE("atmosphereVolume.directionalShadowReducesScattering") {
    AtmosphereVolume lit;
    lit.resize(1, 1, 8);
    lit.setDepthRange(1.f, 20.f);
    for (int z = 0; z < 8; ++z) {
        lit.at(0, 0, z).extinction = 0.1f;
        lit.at(0, 0, z).scattering = glm::vec3(0.08f);
    }
    AtmosphereVolume shadowed = lit;
    for (int z = 0; z < 8; ++z) shadowed.setLightVisibility(0, 0, z, 0.f);
    lit.integrate(glm::vec3(1.f));
    shadowed.integrate(glm::vec3(1.f));
    CHECK(lit.integratedAt(0, 0, 7).r > shadowed.integratedAt(0, 0, 7).r);
}

TEST_CASE("atmosphereVolume.temporalHistoryRejectsLightingDiscontinuity") {
    AtmosphereVolume current;
    current.resize(2, 1, 2);
    current.setDepthRange(1.f, 4.f);
    for (int z = 0; z < 2; ++z) {
        current.at(0, 0, z).emissive = glm::vec3(2.f);
        current.at(1, 0, z).emissive = glm::vec3(0.2f);
    }
    current.integrate(glm::vec3(0.f));
    AtmosphereVolume history = current;
    history.clear();
    history.integrate(glm::vec3(0.f));
    CHECK(current.blendHistory(history, 0.9f, 0.2f) == 4);
    CHECK(current.integratedAt(0, 0, 1).r > 0.f);
}

TEST_CASE("atmosphereVolume.localVolumesAddAndCarveMedia") {
    AtmosphereVolume volume;
    volume.resize(8, 8, 8);
    volume.injectHeightFog(0.1f, glm::vec3(1.f), 0.f, 0.f, -4.f, 4.f);
    FogVolume add;
    add.setShape("sphere");
    add.setSize(4.f, 4.f, 4.f);
    add.setExtinction(0.2f);
    add.setEdgeFalloff(0.f);
    volume.injectLocalVolume(add, glm::vec3(-4.f), glm::vec3(4.f));
    CHECK(volume.at(4, 4, 4).extinction > volume.at(0, 0, 0).extinction);

    FogVolume carve = add;
    carve.setExtinction(-1.f);
    volume.injectLocalVolume(carve, glm::vec3(-4.f), glm::vec3(4.f));
    CHECK(volume.at(4, 4, 4).extinction == 0.f);
    CHECK(volume.at(0, 0, 0).extinction > 0.f);
}

TEST_CASE("atmosphereVolume.localLightAndTransparentDepthQuery") {
    AtmosphereVolume volume;
    volume.resize(4, 4, 16);
    volume.setDepthRange(0.5f, 30.f);
    volume.injectHeightFog(0.04f, glm::vec3(0.8f), 0.f, 0.f, -4.f, 4.f);
    VolumetricLight light;
    light.position = glm::vec3(0.f);
    light.radius = 6.f;
    light.color = glm::vec3(1.f, 0.2f, 0.1f);
    volume.integrateLocalLights({light}, glm::vec3(-8.f), glm::vec3(8.f), glm::vec3(0.02f));
    const glm::vec4 nearFog = volume.sampleIntegrated(0.5f, 0.5f, 1.f);
    const glm::vec4 farFog = volume.sampleIntegrated(0.5f, 0.5f, 20.f);
    CHECK(farFog.a < nearFog.a);
    CHECK(farFog.r > farFog.b);
}

TEST_CASE("atmosphereClipmap.snapsCentersAndChoosesFinestLevel") {
    AtmosphereClipmap clipmap;
    clipmap.configure(3, 8, 8.f);
    clipmap.updateCenter(glm::vec3(1.1f, 0.f, 0.f));
    const glm::vec3 firstCenter = clipmap.getLevel(0).center;
    clipmap.updateCenter(glm::vec3(1.9f, 0.f, 0.f));
    CHECK(clipmap.getLevel(0).center == firstCenter);
    CHECK(clipmap.levelForPosition(glm::vec3(4.f, 0.f, 0.f)) == 0);
    CHECK(clipmap.levelForPosition(glm::vec3(12.f, 0.f, 0.f)) == 1);
    CHECK(clipmap.levelForPosition(glm::vec3(28.f, 0.f, 0.f)) == 2);
    CHECK(clipmap.levelForPosition(glm::vec3(80.f, 0.f, 0.f)) == -1);
}
