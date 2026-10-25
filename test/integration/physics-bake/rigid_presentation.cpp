#include <cmath>
#include <limits>
#include "graphics/RenderSystem.h"
#include "graphics/RenderTransformProjection.h"
#include "physics/backend/bake/RigidPlayback.h"
#include "physics/bake_graphics/RigidBakePresentation.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve;
using namespace eve::physics;
TEST_CASE("physics.bake.rigidPresentationUsesRenderMatrixWithoutWorld") {
    PersistentId::Bytes bytes{};
    bytes.back() = 1;
    const PersistentId identity(bytes);
    auto track = RigidPoseTrack::prepare(RigidTrackSpace::Metres3D, {{0, {{identity, {0, 0, 0, 0, 0, 0, 1}}}},
                                                                     {2, {{identity, {10, 0, 0, 0, 0, 1, 0}}}}})
                     .expect("track");
    auto* target = graphics::Renderable3D::create();
    REQUIRE(target != nullptr);
    target->setScale(2, 3, 4);
    {
        RigidPlayback player(track);
        REQUIRE(player.seek(1).ok());
        REQUIRE(player.setPlacement({1, 2, 3}).ok());
        REQUIRE(submitRigidPlayback(player, identity, *target).ok());
        const auto matrix = graphics::detail::modelFromTransform(*target->transform());
        const auto point  = matrix * glm::vec4(1, 0, 0, 1);
        REQUIRE(std::abs(point.x - 6.f) < 0.001f);
        REQUIRE(std::abs(point.y - 4.f) < 0.001f);
        REQUIRE(std::abs(point.z - 3.f) < 0.001f);
        REQUIRE(!submitRigidPlayback(player, PersistentId::nil(), *target).ok());
        REQUIRE(graphics::detail::modelFromTransform(*target->transform()) == matrix);
        auto planar = RigidPoseTrack::prepare(RigidTrackSpace::PlanarPixels, {{0, {{identity, {}}}}}).expect("planar");
        REQUIRE(!submitRigidPlayback(RigidPlayback(planar), identity, *target).ok());
        REQUIRE(graphics::detail::modelFromTransform(*target->transform()) == matrix);
        target->transform()->sx = std::numeric_limits<float>::infinity();
        REQUIRE(!submitRigidPlayback(player, identity, *target).ok());
        REQUIRE(graphics::detail::modelFromTransform(*target->transform()) == matrix);
        target->transform()->sx = 2;
    }
    REQUIRE(graphics::detail::modelFromTransform(*target->transform())[3].x == 6.f);
    target->setPosition(11, 12, 13);
    REQUIRE(!target->transform()->hasLinkedWorld);
    REQUIRE(graphics::detail::modelFromTransform(*target->transform())[3].x == 11.f);
    ecs::DestroyEntity(target);
    REQUIRE(track.sample(1, identity).expect("target destroyed").x == 5.f);
}

TEST_CASE("physics.bake.planarPresentationPreservesPixelsAndScale") {
    PersistentId::Bytes bytes{};
    bytes.back() = 2;
    const PersistentId identity(bytes);
    auto track = RigidPoseTrack::prepare(RigidTrackSpace::PlanarPixels, {{0, {{identity, {20, 30, 0, 0, 0, 0, 1}}}},
                                                                         {2, {{identity, {40, 50, 0, 0, 0, 1, 0}}}}})
                     .expect("planar");
    auto* target = graphics::Renderable2D::create();
    REQUIRE(target != nullptr);
    target->setScale(2, 3);
    target->setSize(10, 15);
    {
        RigidPlayback player(track);
        REQUIRE(player.seek(1).ok());
        REQUIRE(submitRigidPlayback(player, identity, *target).ok());
        REQUIRE(target->getX() == 30);
        REQUIRE(target->getY() == 40);
        REQUIRE(std::abs(target->getRotation() - 90.f) < 0.001f);
        REQUIRE(target->getScaleX() == 2);
        REQUIRE(target->getScaleY() == 3);
        REQUIRE(target->getWidth() == 10);
        REQUIRE(!submitRigidPlayback(player, PersistentId::nil(), *target).ok());
        auto spatial = RigidPoseTrack::prepare(RigidTrackSpace::Metres3D, {{0, {{identity, {}}}}}).expect("spatial");
        REQUIRE(!submitRigidPlayback(RigidPlayback(spatial), identity, *target).ok());
        REQUIRE(target->getX() == 30);
        REQUIRE(std::abs(target->getRotation() - 90.f) < 0.001f);
    }
    REQUIRE(target->getY() == 40);
    ecs::DestroyEntity(target);
    REQUIRE(track.sample(1, identity).expect("after target destruction").x == 30);
}
