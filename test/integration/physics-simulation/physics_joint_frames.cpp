#include "physics/Body3D.h"
#include "physics/Joint3D.h"
#include "physics/World3D.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace eve::physics;

TEST_CASE("physics.joints.localFramesAreOwnedAndBodyRelative") {
    World3D                 world(0.f, 0.f, 0.f, false);
    std::unique_ptr<Body3D> a(world.newBody("static", 1.f, 2.f, 3.f));
    std::unique_ptr<Body3D> b(world.newBody("dynamic", 4.f, 2.f, 3.f));
    const float             half = std::sqrt(0.5f);
    a->setRotation(0.f, 0.f, half, half);
    std::unique_ptr<Joint3D> joint(world.newRevoluteJoint(a.get(), b.get(), 1.f, 4.f, 3.f, 0.f, 0.f, 1.f));
    auto                     frames = joint->localFrames();
    REQUIRE(frames.ok());
    CHECK(std::fabs(frames.value().bodyA[0] - 2.f) < 0.0001f);
    CHECK(std::fabs(frames.value().bodyA[1]) < 0.0001f);
    CHECK(std::fabs(frames.value().bodyB[0] + 3.f) < 0.0001f);
    CHECK(std::fabs(frames.value().bodyB[1] - 2.f) < 0.0001f);
    joint->destroy();
    auto stale = joint->localFrames();
    CHECK(!stale.ok());
    CHECK(std::fabs(frames.value().bodyA[0] - 2.f) < 0.0001f);
}

TEST_CASE("physics.joints.rotatedFramesSurviveSnapshotRestore") {
    const eve::SnapshotHashProvider hash = [](std::string_view input) {
        eve::ContentId::Bytes bytes{};
        std::uint64_t         value = 14695981039346656037ull;
        for (unsigned char byte : input) value = (value ^ byte) * 1099511628211ull;
        for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(value >> ((i % 8) * 8));
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
    World3D                 world(0.f, 0.f, 0.f, false);
    std::unique_ptr<Body3D> a(world.newBody("static", 1.f, 2.f, 3.f));
    std::unique_ptr<Body3D> b(world.newBody("dynamic", 4.f, 5.f, 6.f));
    a->setRotation(0.1f, 0.2f, 0.3f, 0.9f);
    b->setRotation(0.3f, -0.2f, 0.1f, 0.9f);
    std::vector<std::unique_ptr<Joint3D>> joints;
    joints.emplace_back(world.newDistanceJoint(a.get(), b.get(), 2.f, 3.f, 4.f, 4.f, 5.f, 6.f, 3.f));
    joints.emplace_back(world.newRevoluteJoint(a.get(), b.get(), 2.f, 3.f, 4.f, 1.f, 2.f, 3.f));
    joints.emplace_back(world.newPrismaticJoint(a.get(), b.get(), 2.f, 3.f, 4.f, 1.f, 2.f, 3.f));
    joints.emplace_back(world.newSphericalJoint(a.get(), b.get(), 2.f, 3.f, 4.f, 1.f, 2.f, 3.f));
    joints.emplace_back(world.newWheelJoint(a.get(), b.get(), 2.f, 3.f, 4.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f));
    joints.emplace_back(world.newWeldJoint(a.get(), b.get(), 2.f, 3.f, 4.f));
    joints.emplace_back(world.newMotorJoint(a.get(), b.get()));
    joints.emplace_back(world.newParallelJoint(a.get(), b.get(), 1.f, 2.f, 3.f));
    joints.emplace_back(world.newFilterJoint(a.get(), b.get()));
    auto before = world.snapshot(hash);
    REQUIRE(before.ok());
    auto restored = world.restore(before.value(), hash);
    REQUIRE(restored.ok());
    auto after = world.snapshot(hash);
    REQUIRE(after.ok());
    const auto* left  = before.value().payload.find("joints");
    const auto* right = after.value().payload.find("joints");
    REQUIRE(left != nullptr);
    REQUIRE(right != nullptr);
    REQUIRE_EQ(left->arraySize(), 9u);
    REQUIRE_EQ(right->arraySize(), left->arraySize());
    for (std::size_t i = 0; i < left->arraySize(); ++i) {
        const auto* l = left->at(i).find("localFrames");
        const auto* r = right->at(i).find("localFrames");
        REQUIRE(l != nullptr);
        REQUIRE(r != nullptr);
        for (std::size_t component = 0; component < 14; ++component)
            CHECK(std::fabs(l->at(component).asDouble() - r->at(component).asDouble()) < 0.0001);
    }
}
