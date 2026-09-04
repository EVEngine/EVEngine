#include "zeroerr/unittest.h"
#include "zeroerr/assert.h"

#include "stylize/MotionEchoEffect.h"

#include <glm/gtc/matrix_transform.hpp>

using namespace eve::stylize;

TEST_CASE("stylize.projectileTrail.resamplesHighSpeedMovement") {
    ProjectileTrailConfig config;
    config.sampleIntervalSeconds = 0.1f;
    config.sampleLifetimeSeconds = 1.f;
    config.minimumDistance = 0.f;
    ProjectileTrailEffect trail(config);
    auto report = trail.update(0.f, {0.f, 0.f, 0.f});
    REQUIRE_EQ(report.captured, 1u);
    report = trail.update(0.35f, {3.5f, 0.f, 0.f});
    REQUIRE_EQ(report.captured, 3u);
    REQUIRE_EQ(trail.size(), 4u);
    const auto mesh = trail.build({0.f, 0.f, -1.f});
    REQUIRE_EQ(mesh.vertices.size(), 8u);
    REQUIRE_EQ(mesh.indices.size(), 18u);
}

TEST_CASE("stylize.projectileTrail.rejectsStationaryDuplicateSamples") {
    ProjectileTrailConfig config;
    config.sampleIntervalSeconds = 0.05f;
    config.minimumDistance = 0.1f;
    ProjectileTrailEffect trail(config);
    (void)trail.update(0.f, {1.f, 2.f, 3.f});
    const auto report = trail.update(0.2f, {1.f, 2.f, 3.f});
    REQUIRE_EQ(report.captured, 0u);
    REQUIRE_EQ(report.distanceRejected, 4u);
    REQUIRE_EQ(trail.size(), 1u);
}

TEST_CASE("stylize.afterimage.capturesExpiresAndPreservesStableIds") {
    AfterimageEffectConfig config;
    config.sampleIntervalSeconds = 0.1f;
    config.lifetimeSeconds = 0.3f;
    config.maximumImages = 4;
    AfterimageEffect afterimage(config);
    const glm::mat4 firstModel = glm::translate(glm::mat4(1.f), {1.f, 0.f, 0.f});
    auto report = afterimage.update(0.25f, firstModel);
    REQUIRE_EQ(report.captured, 2u);
    auto images = afterimage.snapshot();
    REQUIRE_EQ(images.size(), 2u);
    REQUIRE_EQ(images[0].stableId, 1u);
    REQUIRE_EQ(images[1].stableId, 2u);

    const glm::mat4 secondModel = glm::translate(glm::mat4(1.f), {2.f, 0.f, 0.f});
    report = afterimage.update(0.2f, secondModel);
    REQUIRE_EQ(report.expired, 1u);
    images = afterimage.snapshot();
    REQUIRE_EQ(images.size(), 3u);
    REQUIRE_EQ(images[0].stableId, 2u);
    REQUIRE_EQ(images[1].stableId, 3u);
    REQUIRE_EQ(images[2].stableId, 4u);
}
