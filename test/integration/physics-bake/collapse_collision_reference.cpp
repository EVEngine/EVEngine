// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "role": "tests",
  "default": "module-composition",
  "module": "physics_destruction"
}
*/
// clang-format on

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>
#include "physics/Body3D.h"
#include "physics/World3D.h"
#include "physics/backend/bake/collapse/CollapseAnimation.h"
#include "physics/destruction/GeometryCollectionInstance.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::physics;
TEST_CASE("physics.bake.collapseCollisionReplayMatchesLiveReference") {
    constexpr int                                Steps = 480;
    constexpr float                              Dt    = 1.f / 240.f;
    constexpr std::array<int, 4>                 Strides{1, 2, 4, 8};
    std::array<CollapseAnimation, 4>             clips;
    std::vector<std::array<BonePresentation, 2>> reference;
    {
        World3D world(0.f, -9.81f, 0.f, false);
        auto*   ground = world.newBody("static", 0.f, -.5f, 0.f);
        REQUIRE(ground != nullptr);
        REQUIRE(ground->newBoxShape(30.f, 1.f, 30.f, 0.f, .6f, 0.f) != nullptr);
        auto             asset    = GeometryCollectionAsset::makeWeldedBoxesFixture(1.f).expect("fixture");
        auto             instance = GeometryCollectionInstance::create(world, asset, 0.f, 2.f, 0.f).expect("fragments");
        DestructionField strain;
        strain.kind      = DestructionFieldKind::Strain;
        strain.radius    = 100;
        strain.magnitude = 100;
        instance->applyField(strain).expect("break connection");
        instance->step({eve::SimulationTick{1}, eve::Duration::fromSeconds(Dt).expect("dt")}).expect("detach");
        for (int bone = 0; bone < 2; ++bone) {
            REQUIRE(instance->boneState(bone) == BoneRuntimeState::Detached);
            auto  link = instance->boneLink(bone).expect("link");
            auto* body = link.resolve(world).expect("body");
            body->setAngularVelocity(0.f, 8.f, 0.f);
            body->setAngularDamping(0.f);
            body->setLinearDamping(0.f);
        }
        for (int frame = 0; frame <= Steps; ++frame) {
            reference.push_back({instance->bonePresentation(0).expect("live pose 0"),
                                 instance->bonePresentation(1).expect("live pose 1")});
            for (std::size_t track = 0; track < clips.size(); ++track)
                if (frame % Strides[track] == 0)
                    clips[track].capture(*instance, frame * Dt).expect("capture live fragments");
            if (frame != Steps) world.update(Dt);
        }
    }
    // Verify actual falling and ground response, not merely free-flight or static poses.
    for (int bone = 0; bone < 2; ++bone) {
        REQUIRE(reference.front()[bone].worldY > 2.9f);
        REQUIRE(reference[120][bone].worldY < 2.f);
        REQUIRE(reference.back()[bone].worldY > .3f);
        REQUIRE(reference.back()[bone].worldY < .8f);
    }
    // All simulation owners are gone. Sample every original physics tick independently.
    for (std::size_t track = 0; track < clips.size(); ++track) {
        double maximumPosition = 0, maximumAngle = 0, squares = 0;
        for (int frame = 0; frame <= Steps; ++frame) {
            for (int bone = 0; bone < 2; ++bone) {
                const auto   a = clips[track].sample(frame * Dt, bone).expect("replay");
                const auto   b = reference[frame][bone];
                const double error =
                    std::hypot(double(a.worldX) - b.worldX, double(a.worldY) - b.worldY, double(a.worldZ) - b.worldZ);
                REQUIRE(std::isfinite(error));
                maximumPosition = std::max(maximumPosition, error);
                squares += error * error;
                const double dot = double(a.rotX) * b.rotX + double(a.rotY) * b.rotY + double(a.rotZ) * b.rotZ +
                                   double(a.rotW) * b.rotW;
                const double aa = double(a.rotX) * a.rotX + double(a.rotY) * a.rotY + double(a.rotZ) * a.rotZ +
                                  double(a.rotW) * a.rotW;
                const double bb = double(b.rotX) * b.rotX + double(b.rotY) * b.rotY + double(b.rotZ) * b.rotZ +
                                  double(b.rotW) * b.rotW;
                const double angle = 2 * std::acos(std::clamp(std::abs(dot) / std::sqrt(aa * bb), 0., 1.));
                REQUIRE(std::isfinite(angle));
                maximumAngle = std::max(maximumAngle, angle);
                REQUIRE(a.state == b.state);
                if (frame % Strides[track] == 0) {
                    REQUIRE(error < 1e-5);
                    REQUIRE(angle < 1e-6);
                }
            }
        }
        std::cout << "collapse_live_error stride=" << Strides[track] << " position_max_m=" << maximumPosition
                  << " position_rms_m=" << std::sqrt(squares / ((Steps + 1) * 2)) << " angle_max_rad=" << maximumAngle
                  << '\n';
    }
}
