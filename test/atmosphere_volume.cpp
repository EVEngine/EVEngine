#include "graphics/AtmosphereVolume.h"

#include "zeroerr/unittest.h"

#include <cmath>

using eve::graphics::AtmosphereVolume;

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
