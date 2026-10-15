#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ui/UIHost.h"
#include "ui/WorldAnchorProjection.h"

#include <glm/mat4x4.hpp>

#include <cmath>

using namespace eve::ui;

TEST_CASE("UI.worldAnchor.projectsCenterAndDistanceScale") {
    UIHost::WorldAnchor anchor;
    anchor.enabled = true;
    anchor.worldZ = 0.5f;
    anchor.distanceScale = true;
    anchor.referenceDistance = 2.f;
    anchor.minScale = 0.5f;
    anchor.maxScale = 1.5f;

    const auto projected = projectWorldAnchor(anchor, glm::mat4(1.f), 0.f, 0.f, 2.5f,
                                               800.f, 600.f);
    CHECK(projected.render);
    CHECK(static_cast<int>(projected.state) == static_cast<int>(WorldAnchorState::Visible));
    CHECK(std::abs(projected.screenX - 400.f) < 0.001f);
    CHECK(std::abs(projected.screenY - 300.f) < 0.001f);
    CHECK(std::abs(projected.scale - 1.f) < 0.001f);
}

TEST_CASE("UI.worldAnchor.hidesOrClampsOutsideViewport") {
    UIHost::WorldAnchor anchor;
    anchor.enabled = true;
    anchor.worldX = 2.f;
    anchor.worldZ = 0.5f;

    auto projected = projectWorldAnchor(anchor, glm::mat4(1.f), 0.f, 0.f, 0.f,
                                        100.f, 80.f);
    CHECK(!projected.render);
    CHECK(static_cast<int>(projected.state) == static_cast<int>(WorldAnchorState::OutsideViewport));

    anchor.edgePolicy = WorldAnchorEdgePolicy::Clamp;
    anchor.safeMargin = 7.f;
    projected = projectWorldAnchor(anchor, glm::mat4(1.f), 0.f, 0.f, 0.f, 100.f, 80.f);
    CHECK(projected.render);
    CHECK(static_cast<int>(projected.state) == static_cast<int>(WorldAnchorState::OutsideViewport));
    CHECK(std::abs(projected.screenX - 93.f) < 0.001f);
}

TEST_CASE("UI.worldAnchor.detectsBehindCameraAndHostCanDetach") {
    UIHost::WorldAnchor anchor;
    anchor.enabled = true;
    anchor.worldZ = 1.f;
    glm::mat4 behind(0.f);
    behind[0][0] = 1.f;
    behind[1][1] = 1.f;
    behind[2][2] = 1.f;
    behind[2][3] = -1.f;
    const auto projected = projectWorldAnchor(anchor, behind, 0.f, 0.f, 0.f, 100.f, 80.f);
    CHECK(!projected.render);
    CHECK(static_cast<int>(projected.state) == static_cast<int>(WorldAnchorState::BehindCamera));

    const UIHostHandle handle = UIHost::createHost("world-anchor-lifecycle");
    auto host = UIHost::resolve(handle);
    REQUIRE(host.has_value());
    host->get().setWorldAnchor(1.f, 2.f, 3.f);
    CHECK(host->get().worldAnchor()->enabled);
    host->get().clearWorldAnchor();
    CHECK(!host->get().worldAnchor()->enabled);
    CHECK(static_cast<int>(host->get().worldAnchor()->state) ==
          static_cast<int>(WorldAnchorState::Disabled));
}

TEST_CASE("UI.worldAnchor.avoidsOverlapByPriorityWithStableResults") {
    WorldAnchorLayoutItem lower;
    lower.stableIndex = 2;
    lower.screenX = 50.f;
    lower.screenY = 50.f;
    lower.width = 20.f;
    lower.height = 10.f;
    lower.pivotX = 0.5f;
    lower.pivotY = 0.5f;
    lower.padding = 2.f;
    lower.maxDisplacement = 30.f;
    lower.priority = 1;
    lower.avoidOverlap = true;

    auto higher = lower;
    higher.stableIndex = 7;
    higher.priority = 10;

    const auto resolved = resolveWorldAnchorOverlaps({lower, higher}, 100.f, 100.f);
    REQUIRE(resolved.size() == 2);
    CHECK(resolved[0].stableIndex == 2);
    CHECK(resolved[0].render);
    CHECK(std::abs(resolved[0].displacementY + 12.f) < 0.001f);
    CHECK(resolved[1].stableIndex == 7);
    CHECK(resolved[1].render);
    CHECK(std::abs(resolved[1].displacementY) < 0.001f);
}

TEST_CASE("UI.worldAnchor.suppressesCrowdedAnchorWhenDisplacementIsBounded") {
    WorldAnchorLayoutItem fixed;
    fixed.stableIndex = 0;
    fixed.screenX = 50.f;
    fixed.screenY = 50.f;
    fixed.width = 20.f;
    fixed.height = 10.f;

    auto crowded = fixed;
    crowded.stableIndex = 1;
    crowded.avoidOverlap = true;
    crowded.maxDisplacement = 0.f;

    const auto resolved = resolveWorldAnchorOverlaps({fixed, crowded}, 100.f, 100.f);
    REQUIRE(resolved.size() == 2);
    CHECK(resolved[0].render);
    CHECK(!resolved[1].render);
}
