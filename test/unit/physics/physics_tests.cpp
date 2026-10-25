// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "box3d.cpp",
    "physics_jolt.cpp",
    "physics_jolt_contacts.cpp",
    "physics_jolt_cooking.cpp",
    "physics_rigid_backend.cpp",
    "targeting.cpp",
    "water_mesh.cpp",
    "physics_force_models.cpp",
    "physics_tire_stability.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_box3d)
#include <box3d/box3d.h>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>
#include "PrivateTestScope.h"
#include "common/SdfField3D.h"
#include "physics/Body3D.h"
#include "physics/DistanceField3D.h"
#include "physics/DistanceField3DSdf.h"
#include "physics/Joint3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/TerrainLoadGravity.h"
#include "physics/World3D.h"
#include "physics/box3d/Box3DBody.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// box3d.cpp
namespace test_box3d_unit {
using namespace eve::physics;


TEST_CASE("box3d.sdf.sharedInterfaceAndBlobRoundTrip") {
    DistanceField3D field(5, 5, 5, 1.f, -2.f, -2.f, -2.f, 100.f);
    for (int z = 0; z < 5; ++z)
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 5; ++x) field.setDistance(x, y, z, static_cast<float>(y - 2));
    DistanceField3DSdf adapter;
    adapter.bind(field);
    REQUIRE(std::fabs(adapter.sample(0.f, 0.5f, 0.f) - 0.5f) < 1e-5f);
    float nx = 0, ny = 0, nz = 0;
    REQUIRE(adapter.gradient(0.f, 0.f, 0.f, nx, ny, nz) == eve::SdfField3DGradientStatus::Defined);
    REQUIRE(ny > 0.9f);
    REQUIRE(adapter.revision() == field.getRevision());
    float minX, minY, minZ, maxX, maxY, maxZ;
    adapter.bounds(minX, minY, minZ, maxX, maxY, maxZ);
    REQUIRE(std::fabs(minX + 2.f) < 1e-5f);
    REQUIRE(std::fabs(maxY - 2.f) < 1e-5f);

    auto blob = sdfField3DBlobFromDistanceField(field);
    REQUIRE(blob.ok());
    auto restored = distanceFieldFromSdfField3DBlob(blob.value());
    REQUIRE(restored.ok());
    REQUIRE(std::fabs(restored.value().sample(0.25f, 0.5f, -0.25f) - 0.5f) < 1e-5f);
}

TEST_CASE("box3d.sdf.interpolationNormalAndSphere") {
    DistanceField3D field(5, 5, 5, 1.f, -2.f, -2.f, -2.f, 100.f);
    // Exact SDF for the plane y=0. Trilinear interpolation reproduces it exactly.
    for (int z = 0; z < 5; ++z)
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 5; ++x) field.setDistance(x, y, z, static_cast<float>(y - 2));

    CHECK(std::fabs(field.sample(0.25f, 0.5f, -0.25f) - 0.5f) < 1e-5f);
    field.sampleNormal(0.f, 0.f, 0.f);
    CHECK(std::fabs(field.getNormalX()) < 1e-5f);
    CHECK(std::fabs(field.getNormalY() - 1.f) < 1e-5f);
    CHECK(std::fabs(field.getNormalZ()) < 1e-5f);

    CHECK(field.checkSphere(0.f, 0.4f, 0.f, 0.5f));
    CHECK_LT(field.getCollisionDistance(), 0.f);
    CHECK(std::fabs(field.getPenetrationDepth() - 0.1f) < 1e-5f);
    CHECK(std::fabs(field.getSurfaceY()) < 1e-5f);
    CHECK(std::fabs(field.getShapeContactY() + 0.1f) < 1e-5f);
    CHECK(!field.checkSphere(0.f, 1.f, 0.f, 0.25f));
}

TEST_CASE("box3d.sdf.boundaryNormalsUseOneSidedGradient") {
    DistanceField3D field(5, 5, 5, 1.f, -2.f, -2.f, -2.f, 100.f);
    for (int z = 0; z < 5; ++z)
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 5; ++x) field.setDistance(x, y, z, static_cast<float>(y - 2));

    field.sampleNormal(0.f, -2.f, 0.f);
    CHECK(std::fabs(field.getNormalX()) < 1e-5f);
    CHECK(field.getNormalY() > 0.999f);
    CHECK(std::fabs(field.getNormalZ()) < 1e-5f);
    field.sampleNormal(0.f, 2.f, 0.f);
    CHECK(field.getNormalY() > 0.999f);

    // Outside points have no meaningful field gradient and use the documented safe fallback.
    field.sampleNormal(0.f, 2.01f, 0.f);
    CHECK(std::fabs(field.getNormalX()) < 1e-5f);
    CHECK(field.getNormalY() > 0.999f);
    CHECK(std::fabs(field.getNormalZ()) < 1e-5f);
}

TEST_CASE("box3d.sdf.bulkDataMetadataAndAtomicValidation") {
    DistanceField3D field(3, 2, 2, 0.5f, -1.f, 2.f, 4.f, 99.f);
    CHECK_EQ(field.getRevision(), 0u);
    CHECK_EQ(field.getWidth(), 3);
    CHECK_EQ(field.getHeight(), 2);
    CHECK_EQ(field.getDepth(), 2);
    CHECK_EQ(field.getSampleCount(), 12);
    CHECK(std::fabs(field.getCellSize() - 0.5f) < 1e-6f);
    CHECK(std::fabs(field.getOriginX() + 1.f) < 1e-6f);
    CHECK(std::fabs(field.getOriginY() - 2.f) < 1e-6f);
    CHECK(std::fabs(field.getOriginZ() - 4.f) < 1e-6f);
    CHECK(std::fabs(field.getOutsideDistance() - 99.f) < 1e-6f);

    std::vector<float> samples(12);
    for (size_t i = 0; i < samples.size(); ++i) samples[i] = static_cast<float>(i);
    field.setDistances(samples);
    CHECK_EQ(field.getRevision(), 1u);
    CHECK(std::fabs(field.getDistance(2, 0, 0) - 2.f) < 1e-6f);
    CHECK(std::fabs(field.getDistance(0, 1, 0) - 3.f) < 1e-6f);
    CHECK(std::fabs(field.getDistance(0, 0, 1) - 6.f) < 1e-6f);

    auto invalid = samples;
    invalid[4]   = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS((field.setDistances(invalid), false));
    CHECK_EQ(field.getRevision(), 1u);
    CHECK(std::fabs(field.getDistance(1, 1, 0) - 4.f) < 1e-6f);
    CHECK_THROWS((field.setDistances(std::vector<float>(11, 0.f)), false));

    field.fill(-2.f);
    CHECK_EQ(field.getRevision(), 2u);
    CHECK(std::fabs(field.getDistance(2, 1, 1) + 2.f) < 1e-6f);
    CHECK_THROWS((field.fill(std::numeric_limits<float>::infinity()), false));
    CHECK(std::fabs(field.getDistance(2, 1, 1) + 2.f) < 1e-6f);

    const std::vector<float> region{10.f, 11.f, 12.f, 13.f, 14.f, 15.f, 16.f, 17.f};
    field.setDistanceRegion(1, 0, 0, 2, 2, 2, region);
    CHECK_EQ(field.getRevision(), 3u);
    CHECK(std::fabs(field.getDistance(1, 0, 0) - 10.f) < 1e-6f);
    CHECK(std::fabs(field.getDistance(2, 0, 0) - 11.f) < 1e-6f);
    CHECK(std::fabs(field.getDistance(1, 1, 0) - 12.f) < 1e-6f);
    CHECK(std::fabs(field.getDistance(1, 0, 1) - 14.f) < 1e-6f);
    CHECK(std::fabs(field.getDistance(2, 1, 1) - 17.f) < 1e-6f);
    CHECK(std::fabs(field.getDistance(0, 0, 0) + 2.f) < 1e-6f);

    auto invalidRegion = region;
    invalidRegion[2]   = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS((field.setDistanceRegion(1, 0, 0, 2, 2, 2, invalidRegion), false));
    CHECK_THROWS((field.setDistanceRegion(2, 0, 0, 2, 2, 2, region), false));
    CHECK_EQ(field.getRevision(), 3u);
    CHECK(std::fabs(field.getDistance(1, 1, 0) - 12.f) < 1e-6f);

    field.fillRegion(0, 1, 0, 1, 1, 2, 7.f);
    CHECK_EQ(field.getRevision(), 4u);
    CHECK(std::fabs(field.getDistance(0, 1, 0) - 7.f) < 1e-6f);
    CHECK(std::fabs(field.getDistance(0, 1, 1) - 7.f) < 1e-6f);
    CHECK_THROWS((field.fillRegion(0, 0, 0, 0, 1, 1, 3.f), false));
    CHECK_EQ(field.getRevision(), 4u);
}

TEST_CASE("box3d.sdf.capsuleFindsInteriorCollision") {
    DistanceField3D field(7, 7, 7, 1.f, -3.f, -3.f, -3.f, 100.f);
    // Sphere SDF centered at the origin.
    for (int z = 0; z < 7; ++z) {
        for (int y = 0; y < 7; ++y) {
            for (int x = 0; x < 7; ++x) {
                float px = static_cast<float>(x - 3);
                float py = static_cast<float>(y - 3);
                float pz = static_cast<float>(z - 3);
                field.setDistance(x, y, z, std::sqrt(px * px + py * py + pz * pz) - 1.f);
            }
        }
    }
    // Endpoints are outside, but the capsule segment crosses the solid center.
    CHECK(field.checkCapsule(-2.f, 0.f, 0.f, 2.f, 0.f, 0.f, 0.1f));
    CHECK(std::fabs(field.getCollisionX()) < 0.51f);
    CHECK_LT(field.getCollisionDistance(), 0.f);
    CHECK(!field.checkCapsule(-2.f, 2.f, 0.f, 2.f, 2.f, 0.f, 0.1f));
}

TEST_CASE("box3d.sdf.sphereCastPreventsHighSpeedTunneling") {
    DistanceField3D field(5, 5, 5, 1.f, -2.f, -2.f, -2.f, 100.f);
    for (int z = 0; z < 5; ++z)
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 5; ++x) field.setDistance(x, y, z, static_cast<float>(y - 2));

    CHECK(field.castSphere(0.f, 1.75f, 0.f, 0.25f, 0.f, -3.5f, 0.f));
    CHECK(!field.didCastStartInside());
    CHECK(std::fabs(field.getCastFraction() - (1.5f / 3.5f)) < 1e-4f);
    CHECK(std::fabs(field.getCastDistance() - 1.5f) < 1e-4f);
    CHECK(std::fabs(field.getCollisionY() - 0.25f) < 1e-4f);
    CHECK(field.getNormalY() > 0.999f);

    CHECK(!field.castSphere(0.f, 1.75f, 0.f, 0.25f, 3.f, 0.f, 0.f));
    CHECK(std::fabs(field.getCastFraction() - 1.f) < 1e-6f);
    CHECK(std::fabs(field.getCastDistance() - 3.f) < 1e-6f);
}

TEST_CASE("box3d.sdf.capsuleCastAndInitialOverlap") {
    DistanceField3D field(7, 7, 7, 1.f, -3.f, -3.f, -3.f, 100.f);
    for (int z = 0; z < 7; ++z)
        for (int y = 0; y < 7; ++y)
            for (int x = 0; x < 7; ++x) field.setDistance(x, y, z, static_cast<float>(y - 3));

    CHECK(field.castCapsule(-1.f, 2.f, 0.f, 1.f, 2.f, 0.f, 0.5f, 0.f, -4.f, 0.f));
    CHECK(!field.didCastStartInside());
    CHECK(std::fabs(field.getCastFraction() - 0.375f) < 1e-4f);
    CHECK(std::fabs(field.getCollisionY() - 0.5f) < 1e-4f);
    CHECK(field.getNormalY() > 0.999f);

    CHECK(field.castCapsule(-1.f, 0.25f, 0.f, 1.f, 0.25f, 0.f, 0.5f, 0.f, 2.f, 0.f));
    CHECK(field.didCastStartInside());
    CHECK(std::fabs(field.getCastFraction()) < 1e-6f);
    CHECK_LT(field.getCollisionDistance(), 0.f);
}

TEST_CASE("box3d.sdf.moverSlidesAlongWall") {
    DistanceField3D field(7, 7, 7, 1.f, -3.f, -3.f, -3.f, 100.f);
    for (int z = 0; z < 7; ++z)
        for (int y = 0; y < 7; ++y)
            for (int x = 0; x < 7; ++x) field.setDistance(x, y, z, static_cast<float>(x - 3));

    CHECK(field.moveCapsule(1.5f, -1.f, 0.f, 1.5f, 1.f, 0.f, 0.25f, -2.f, 0.f, 1.f));
    CHECK(field.getMoverDeltaX() < -1.24f);
    CHECK(field.getMoverDeltaX() > -1.26f);
    CHECK(field.getMoverDeltaZ() > 0.99f);
    CHECK(field.getMoverNormalX() > 0.999f);
    CHECK_GT(field.getMoverIterations(), 1);

    CHECK(!field.moveCapsule(1.5f, -1.f, 0.f, 1.5f, 1.f, 0.f, 0.25f, 0.f, 0.f, 1.f));
    CHECK(std::fabs(field.getMoverDeltaZ() - 1.f) < 1e-5f);
}

TEST_CASE("box3d.sdf.moverRecoversInitialPenetration") {
    DistanceField3D field(7, 7, 7, 1.f, -3.f, -3.f, -3.f, 100.f);
    for (int z = 0; z < 7; ++z)
        for (int y = 0; y < 7; ++y)
            for (int x = 0; x < 7; ++x) field.setDistance(x, y, z, static_cast<float>(x - 3));

    CHECK(field.moveCapsule(0.1f, -1.f, 0.f, 0.1f, 1.f, 0.f, 0.25f, 0.f, 0.f, 0.f));
    CHECK(field.getMoverDeltaX() > 0.15f);
    CHECK(field.getMoverDeltaX() < 0.152f);
    CHECK(field.getMoverNormalX() > 0.999f);
}

TEST_CASE("box3d.sdf.moverStopsAtTwoPlaneCorner") {
    // Resolve the corner below the 1cm positional tolerance. A 0.5m grid
    // interpolates min(x,z-0.5) into a rounded corner wider than this capsule.
    DistanceField3D field(201, 61, 201, 0.02f, -2.f, -0.6f, -2.f, 100.f);
    for (int z = 0; z < 201; ++z) {
        for (int y = 0; y < 61; ++y) {
            for (int x = 0; x < 201; ++x) {
                const float worldX = -2.f + 0.02f * static_cast<float>(x);
                const float worldZ = -2.f + 0.02f * static_cast<float>(z);
                // Union of x<=0 and z<=0.5 half spaces.
                field.setDistance(x, y, z, std::min(worldX, worldZ - 0.5f));
            }
        }
    }

    CHECK(field.moveCapsule(1.5f, -0.5f, 2.f, 1.5f, 0.5f, 2.f, 0.25f, -2.f, 0.f, -3.f));
    CHECK(field.getMoverDeltaX() < -1.24f);
    CHECK(field.getMoverDeltaX() > -1.26f);
    CHECK(field.getMoverDeltaZ() < -1.24f);
    CHECK(field.getMoverDeltaZ() > -1.26f);
    CHECK_GT(field.getMoverIterations(), 2);
}

TEST_CASE("box3d.sdf.moverGroundSlopeAndSkinConfiguration") {
    DistanceField3D field(7, 7, 7, 1.f, -3.f, -3.f, -3.f, 100.f);
    for (int z = 0; z < 7; ++z)
        for (int y = 0; y < 7; ++y)
            for (int x = 0; x < 7; ++x) field.setDistance(x, y, z, static_cast<float>(y - 3));

    field.setMoverSkinWidth(0.02f);
    CHECK(std::fabs(field.getMoverSkinWidth() - 0.02f) < 1e-6f);
    CHECK_THROWS((field.setMoverSkinWidth(-0.01f), false));
    CHECK_THROWS((field.setMoverUp(0.f, 0.f, 0.f), false));

    field.setMoverUp(0.f, 1.f, 1.f);  // Normalized internally; floor dot is sqrt(1/2).
    field.setMoverSlopeLimit(40.f);
    CHECK(field.moveCapsule(0.f, 0.5f, 0.f, 0.f, 1.5f, 0.f, 0.25f, 0.f, -1.f, 0.f));
    CHECK(!field.isMoverGrounded());
    CHECK(field.getMoverGroundDot() > 0.70f);
    CHECK(field.getMoverGroundDot() < 0.72f);
    CHECK(field.getMoverDeltaY() > -0.24f);
    CHECK(field.getMoverDeltaY() < -0.22f);  // Stops at radius + configured skin.

    field.setMoverSlopeLimit(50.f);
    CHECK(field.moveCapsule(0.f, 0.5f, 0.f, 0.f, 1.5f, 0.f, 0.25f, 0.f, -1.f, 0.f));
    CHECK(field.isMoverGrounded());
}

TEST_CASE("box3d.sdf.moverGroundSnapAndLedge") {
    DistanceField3D field(9, 9, 9, 0.5f, -2.f, -2.f, -2.f, 100.f);
    for (int z = 0; z < 9; ++z) {
        for (int y = 0; y < 9; ++y) {
            for (int x = 0; x < 9; ++x) {
                const float worldX = -2.f + 0.5f * static_cast<float>(x);
                const float worldY = -2.f + 0.5f * static_cast<float>(y);
                // Ground exists only on x<=0, leaving a sharp ledge to the right.
                field.setDistance(x, y, z, worldX <= 0.f ? worldY : 100.f);
            }
        }
    }

    field.setMoverSkinWidth(0.02f);
    field.setMoverGroundSnap(0.5f);
    CHECK(std::fabs(field.getMoverGroundSnap() - 0.5f) < 1e-6f);
    CHECK_THROWS((field.setMoverGroundSnap(-0.1f), false));

    // Starts 0.25m above the configured skin and is pulled down onto walkable ground.
    CHECK(field.moveCapsule(-1.f, 0.5f, 0.f, -1.f, 1.5f, 0.f, 0.25f, 0.2f, 0.f, 0.f));
    CHECK(field.isMoverGrounded());
    CHECK(field.getMoverDeltaY() < -0.22f);
    CHECK(field.getMoverDeltaY() > -0.24f);
    CHECK(std::fabs(field.getMoverDeltaX() - 0.2f) < 1e-5f);

    // Over the empty side of the ledge, snap must not invent a ground contact.
    CHECK(!field.moveCapsule(1.f, 0.5f, 0.f, 1.f, 1.5f, 0.f, 0.25f, 0.2f, 0.f, 0.f));
    CHECK(!field.isMoverGrounded());
    CHECK(std::fabs(field.getMoverDeltaY()) < 1e-6f);
}

TEST_CASE("box3d.sdf.moverStepOffsetClimbsOnlyLowLedges") {
    // The grid must resolve the 1cm skin and 2cm no-climb tolerance.
    DistanceField3D field(201, 151, 5, 0.02f, -2.f, -1.f, -0.04f, 100.f);
    for (int z = 0; z < 5; ++z) {
        for (int y = 0; y < 151; ++y) {
            for (int x = 0; x < 201; ++x) {
                const float worldX = -2.f + 0.02f * static_cast<float>(x);
                const float worldY = -1.f + 0.02f * static_cast<float>(y);
                // Ground union a 0.4m-high semi-infinite step at x>=0.
                const float step = std::max(-worldX, worldY - 0.4f);
                field.setDistance(x, y, z, std::min(worldY, step));
            }
        }
    }

    field.setMoverSkinWidth(0.01f);
    field.setMoverGroundSnap(0.1f);
    field.setMoverStepHeight(0.5f);
    CHECK(std::fabs(field.getMoverStepHeight() - 0.5f) < 1e-6f);
    CHECK_THROWS((field.setMoverStepHeight(-0.1f), false));

    CHECK(field.moveCapsule(-1.f, 0.26f, 0.f, -1.f, 1.26f, 0.f, 0.25f, 1.75f, 0.f, 0.f));
    CHECK(field.isMoverGrounded());
    CHECK(field.getMoverDeltaX() > 1.74f);
    CHECK(field.getMoverDeltaY() > 0.39f);
    CHECK(field.getMoverDeltaY() < 0.41f);
    CHECK(!field.didMoverHitWall());

    // A step allowance lower than the obstacle must fall back to the direct wall result.
    field.setMoverStepHeight(0.3f);
    CHECK(field.moveCapsule(-1.f, 0.26f, 0.f, -1.f, 1.26f, 0.f, 0.25f, 1.75f, 0.f, 0.f));
    CHECK(field.didMoverHitWall());
    CHECK(field.getMoverDeltaX() < 1.f);
    CHECK(field.getMoverDeltaY() < 0.02f);
}
}  // namespace test_box3d_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_jolt)
#include <cmath>
#include <memory>
#include "PrivateTestScope.h"
#include "common/Snapshot.h"
#include "physics/Body3D.h"
#include "physics/Joint3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// physics_jolt.cpp
namespace test_physics_jolt {
using namespace eve::physics;
#if EVENGINE_TEST_HAS_JOLT
TEST_CASE("physics.jolt.nativeDropContactRayAndLifetime") {
    auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, -9.8f, 0, false);
    REQUIRE(created.ok());
    auto                     world = std::move(created).takeValue();
    std::unique_ptr<Body3D>  ground(world->newBody("static", 0, -0.5f, 0));
    std::unique_ptr<Shape3D> floor(ground->newBoxShape(20, 1, 20));
    std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 4, 0));
    std::unique_ptr<Shape3D> ball(body->newSphereShape(0.5f));
    bool                     contact = false;
    for (int i = 0; i < 200; ++i) {
        world->update(0.01f);
        contact = contact || world->getBeginContactCount() > 0;
    }
    CHECK(contact);
    CHECK(std::abs(body->getY() - 0.5f) < 0.06f);
    auto ray = world->rayCastOwned(0, 5, 0, 0, -2, 0, {});
    REQUIRE(ray.ok());
    CHECK(ray.value().hit);
    CHECK(ray.value().body == body->runtimeHandle());
    auto saved = ray.value();
    world->destroy();
    CHECK(!body->isValid());
    CHECK(!ball->isValid());
    CHECK(saved.hit);
}
TEST_CASE("physics.jolt.compoundShapeRemovalAndHitEvents") {
    auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, 0, 0, false);
    REQUIRE(created.ok());
    auto                     world = std::move(created).takeValue();
    std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 0, 0));
    std::unique_ptr<Shape3D> first(body->newBoxShape(2, 2, 2));
    std::unique_ptr<Shape3D> second(body->newSphereShape(2));
    CHECK(world->getShapeCount() == 2);
    second.reset();
    CHECK(world->getShapeCount() == 1);
    first->setHitEventsEnabled(true);
    CHECK(first->areHitEventsEnabled());
    first->setHitEventsEnabled(false);
    CHECK(!first->areHitEventsEnabled());
    auto ray = world->rayCastOwned(0, 3, 0, 0, -3, 0, {});
    REQUIRE(ray.ok());
    CHECK(ray.value().hit);
    body.reset();
    CHECK(!first->isValid());
    CHECK(world->getBodyCount() == 0);
}


TEST_CASE("physics.jolt.sharedSensorAndFilterContract") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  fixed(world->newBody("static", 0, 0, 0));
        std::unique_ptr<Shape3D> sensor(fixed->newBoxShape(4, 4, 4));
        sensor->setSensor(true);
        std::unique_ptr<Body3D>  moving(world->newBody("dynamic", 0, 0, 0));
        std::unique_ptr<Shape3D> ball(moving->newSphereShape(0.5f));
        ball->setMaskBits(0);
        world->update(0.01f);
        CHECK(world->getBeginTriggerCount() == 0);
        ball->setMaskBits(UINT64_MAX);
        world->update(0.01f);
        CHECK(world->getBeginTriggerCount() > 0);
        CHECK(std::abs(moving->getY()) < 0.001f);
    }
}
TEST_CASE("physics.jolt.snapshotRestoreKeepsNativeStepDriver") {
    auto hash = [](std::string_view input) -> eve::Result<eve::ContentId> {
        eve::ContentId::Bytes bytes{};
        uint64_t              value = 14695981039346656037ull;
        for (unsigned char byte : input) value = (value ^ byte) * 1099511628211ull;
        for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<uint8_t>(value >> ((i % 8) * 8));
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
    auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, -9.8f, 0, false);
    REQUIRE(created.ok());
    auto  world = std::move(created).takeValue();
    auto* body  = world->newBody("dynamic", 0, 10, 0);
    body->newSphereShape(0.5f);
    const auto id        = body->getId();
    const auto oldHandle = body->runtimeHandle();
    auto       snapshot  = world->snapshot(hash);
    REQUIRE(snapshot.ok());
    auto restored = world->restore(snapshot.value(), hash);
    REQUIRE(restored.ok());
    CHECK(world->findBody(oldHandle) == nullptr);
    auto* current = world->findBodyById(id);
    REQUIRE(current != nullptr);
    world->update(0.02f);
    CHECK(current->getY() < 10.f);
    CHECK(world->rigidBackendKind() == RigidBackend3D::Jolt);
}
TEST_CASE("physics.jolt.rotationLockSurvivesMotionTypeChange") {
    auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, 0, 0, false);
    REQUIRE(created.ok());
    auto                     world = std::move(created).takeValue();
    std::unique_ptr<Body3D>  body(world->newBody("static", 0, 0, 0));
    std::unique_ptr<Shape3D> shape(body->newBoxShape(1, 1, 1));
    body->setFixedRotation(true);
    body->setType("dynamic");
    body->applyAngularImpulse(0, 1, 0);
    CHECK(std::abs(body->getAngularVelocityY()) < 0.001f);
    body->setFixedRotation(false);
    body->applyAngularImpulse(0, 1, 0);
    CHECK(body->getAngularVelocityY() > 0.1f);
    const float oldW     = body->getRotW();
    bool        rejected = false;
    try {
        body->setRotation(1.e38f, 1.e38f, 1.e38f, 1.e38f);
    } catch (const std::exception&) {
        rejected = true;
    }
    CHECK(rejected);
    CHECK(body->getRotW() == oldW);
}

TEST_CASE("physics.jolt.meshMaterialsAndAtomicReplacement") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, -9.8f, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  ground(world->newBody("static", 0, 0, 0));
        std::vector<float>       vertices{-5, 0, -5, -5, 0, 5, 5, 0, -5, 5, 0, 5};
        std::unique_ptr<Shape3D> mesh(ground->newTriangleMeshShape(vertices, {0, 1, 2, 3, 2, 1}));
        mesh->setTriangleMeshMaterialIndices({0, 1});
        mesh->setTriangleMeshMaterial(1, .5f, 0, 0, 0, 0, 0, 42, "default", "default");
        auto ray = world->rayCastOwned(3, 3, 3, 3, -3, 3, {});
        REQUIRE(ray.ok());
        CHECK(ray.value().hit);
        CHECK(ray.value().triangleIndex == 1);
        CHECK(ray.value().materialId == 42);
        bool rejected = false;
        try {
            mesh->setTriangleMeshData(vertices, {0, 0, 0});
        } catch (const std::exception&) {
            rejected = true;
        }
        CHECK(rejected);
        CHECK(mesh->getTriangleMeshTriangleCount() == 2);
        rejected = false;
        try {
            ground->setType("dynamic");
        } catch (const std::exception&) {
            rejected = true;
        }
        CHECK(rejected);
        CHECK(ground->getType() == "static");
        std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 3, 0));
        std::unique_ptr<Shape3D> ball(body->newSphereShape(.5f));
        for (int i = 0; i < 200; ++i) world->update(.01f);
        CHECK(std::abs(body->getY() - .5f) < .08f);
    }
}
TEST_CASE("physics.jolt.rectangularHeightFieldAndMover") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  ground(world->newBody("static", 0, 0, 0));
        std::unique_ptr<Shape3D> terrain(ground->newHeightFieldShape(3, 2, 2, 3, {0, 0, 0, 0, 0, 0}, -1, 1));
        auto                     ray = world->rayCastOwned(3, 2, 2, 3, -2, 2, {});
        REQUIRE(ray.ok());
        CHECK(ray.value().hit);
        CHECK(std::abs(ray.value().y) < .001f);
        auto moved = world->moveCapsuleOwned(1, 2, 1, 1, 3, 1, .4f, 1, -5, 0, {}, {});
        REQUIRE(moved.ok());
        CHECK(moved.value().constrained);
        CHECK(moved.value().grounded);
        CHECK(moved.value().deltaY > -1.65f);
        CHECK(moved.value().deltaX > .8f);
        QueryFilter3D ignored;
        ignored.ignoredShapeId = terrain->getId();
        auto free              = world->moveCapsuleOwned(1, 2, 1, 1, 3, 1, .4f, 1, -5, 0, ignored, {});
        REQUIRE(free.ok());
        CHECK(!free.value().constrained);
        CHECK(std::abs(free.value().deltaY + 5) < .001f);
        auto invalid = world->moveCapsuleOwned(1, 2, 1, 1, 3, 1, .4f, 1, -5, 0, {}, {0, 0, 0, .5f});
        CHECK(!invalid.ok());
    }
}
TEST_CASE("physics.jolt.moverWallSlideAndInitialPenetration") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  wall(world->newBody("static", 2, 0, 0));
        std::unique_ptr<Shape3D> box(wall->newBoxShape(.2f, 20, 20));
        auto                     moved = world->moveCapsuleOwned(0, 0, 0, 0, 1, 0, .5f, 20, 0, 3, {}, {});
        REQUIRE(moved.ok());
        CHECK(moved.value().constrained);
        CHECK(moved.value().deltaX < 1.41f);
        CHECK(moved.value().deltaZ > 2.9f);
        auto overlap = world->moveCapsuleOwned(1.8f, 0, 0, 1.8f, 1, 0, .5f, 0, 0, 0, {}, {});
        REQUIRE(overlap.ok());
        CHECK(overlap.value().constrained);
        CHECK(overlap.value().deltaX < -.35f);
    }
}
TEST_CASE("physics.jolt.distanceJointAndBodyFirstLifetime") {
    auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, -9.8f, 0, false);
    REQUIRE(created.ok());
    auto                     world = std::move(created).takeValue();
    std::unique_ptr<Body3D>  a(world->newBody("static", 0, 0, 0)), b(world->newBody("dynamic", 0, -2, 0));
    std::unique_ptr<Shape3D> shape(b->newSphereShape(.25f));
    std::unique_ptr<Joint3D> joint(world->newDistanceJoint(a.get(), b.get(), 0, 0, 0, 0, -2, 0, 2));
    joint->setForceThreshold(.01f);
    bool stress = false;
    for (int i = 0; i < 100; ++i) {
        world->update(.01f);
        stress = stress || world->getJointStressCount() > 0;
    }
    CHECK(std::abs(joint->getDistanceCurrentLength() - 2) < .03f);
    CHECK(stress);
    CHECK(world->getJointCount() == 1);
    a.reset();
    CHECK(!joint->isValid());
    CHECK(world->getJointCount() == 0);
    world->update(.01f);
}
TEST_CASE("physics.jolt.prismaticMotorAndLimits") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, -9.8f, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  a(world->newBody("static", 0, 0, 0)), b(world->newBody("dynamic", 0, 0, 0));
        std::unique_ptr<Shape3D> shape(b->newBoxShape(1, 1, 1));
        std::unique_ptr<Joint3D> joint(world->newPrismaticJoint(a.get(), b.get(), 0, 0, 0, 1, 0, 0));
        joint->setPrismaticLimits(true, -1, 1);
        joint->setPrismaticMotor(true, 2, 100);
        for (int i = 0; i < 150; ++i) world->update(.01f);
        CHECK(b->getX() > .8f);
        CHECK(b->getX() < 1.04f);
        CHECK(std::abs(b->getY()) < .02f);
        bool rejected = false;
        try {
            joint->setPrismaticLimits(true, 2, -2);
        } catch (const std::exception&) {
            rejected = true;
        }
        CHECK(rejected);
        world->destroy();
        CHECK(!joint->isValid());
    }
}
TEST_CASE("physics.jolt.revoluteMotorAndLimits") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  a(world->newBody("static", 0, 0, 0)), b(world->newBody("dynamic", 0, 0, 0));
        std::unique_ptr<Shape3D> shape(b->newBoxShape(1, 2, 1));
        std::unique_ptr<Joint3D> joint(world->newRevoluteJoint(a.get(), b.get(), 0, 0, 0, 0, 0, 1));
        joint->setRevoluteLimits(true, -.5f, .5f);
        joint->setRevoluteMotor(true, 2, 100);
        for (int i = 0; i < 100; ++i) world->update(.01f);
        CHECK(joint->getRevoluteAngle() > .4f);
        CHECK(joint->getRevoluteAngle() < .55f);
    }
}
TEST_CASE("physics.jolt.remainingJointKindsAndShapeComChange") {
    for (int kind = 0; kind < 5; ++kind) {
        auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, -9.8f, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  a(world->newBody("static", 0, 0, 0)), b(world->newBody("dynamic", 0, -1, 0));
        std::unique_ptr<Shape3D> shape(b->newSphereShape(.25f));
        std::unique_ptr<Joint3D> joint;
        if (kind == 0) joint.reset(world->newWeldJoint(a.get(), b.get(), 0, 0, 0));
        if (kind == 1) joint.reset(world->newSphericalJoint(a.get(), b.get(), 0, 0, 0, 0, 1, 0));
        if (kind == 2) {
            joint.reset(world->newWheelJoint(a.get(), b.get(), 0, 0, 0, 0, 1, 0, 0, 0, 1));
            joint->setWheelSuspension(true, 5, 1);
            joint->setWheelSuspensionLimits(true, -.2f, .2f);
            joint->setWheelSpinMotor(true, 3, 100);
        }
        if (kind == 3) {
            joint.reset(world->newMotorJoint(a.get(), b.get()));
            joint->setMotorVelocityLimits(100, 100);
            joint->setMotorLinearVelocity(1, 0, 0);
        }
        if (kind == 4) joint.reset(world->newParallelJoint(a.get(), b.get(), 0, 1, 0));
        auto frames = joint->localFrames();
        REQUIRE(frames.ok());
        shape->setLocalPosition(.2f, 0, 0);
        for (int i = 0; i < 80; ++i) world->update(.01f);
        CHECK(joint->isValid());
        if (kind < 3) CHECK(b->getY() > -1.3f);
        if (kind == 2) CHECK(joint->getWheelSpinSpeed() > 2.f);
        if (kind == 3) CHECK(b->getX() > .6f);
    }
}
TEST_CASE("physics.jolt.filterJointReferenceCounting") {
    auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, 0, 0, false);
    REQUIRE(created.ok());
    auto                     world = std::move(created).takeValue();
    std::unique_ptr<Body3D>  a(world->newBody("static", 0, 0, 0)), b(world->newBody("dynamic", 0, 0, 0));
    std::unique_ptr<Shape3D> sa(a->newSphereShape(1)), sb(b->newSphereShape(1));
    std::unique_ptr<Joint3D> j1(world->newFilterJoint(a.get(), b.get())), j2(world->newFilterJoint(a.get(), b.get()));
    world->update(.01f);
    CHECK(world->getBeginContactCount() == 0);
    j1.reset();
    world->update(.01f);
    CHECK(world->getBeginContactCount() == 0);
    j2.reset();
    world->update(.01f);
    CHECK(world->getBeginContactCount() > 0);
}

TEST_CASE("physics.jolt.snapshotRestoresAllJointKindsAndTerrain") {
    auto hash = [](std::string_view input) -> eve::Result<eve::ContentId> {
        eve::ContentId::Bytes bytes{};
        uint64_t              value = 14695981039346656037ull;
        for (unsigned char byte : input) value = (value ^ byte) * 1099511628211ull;
        for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<uint8_t>(value >> ((i % 8) * 8));
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };

    auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, -9.8f, 0, false);
    REQUIRE(created.ok());
    auto  world = std::move(created).takeValue();
    auto* floor = world->newBody("static", 0, 0, 0);
    floor->newHeightFieldShape(3, 2, 10, 10, {0, 0, 0, 0, 0, 0}, -1, 1);
    auto* anchor = world->newBody("static", 0, 3, 0);
    for (int i = 0; i < 9; ++i) {
        auto* b = world->newBody("dynamic", float(i) * 2, 3, 0);
        b->newSphereShape(.2f);
        switch (i) {
            case 0: world->newDistanceJoint(anchor, b, 0, 3, 0, 0, 3, 0, 0); break;
            case 1: world->newRevoluteJoint(anchor, b, 2, 3, 0, 0, 0, 1); break;
            case 2: world->newPrismaticJoint(anchor, b, 4, 3, 0, 1, 0, 0); break;
            case 3: world->newSphericalJoint(anchor, b, 6, 3, 0, 0, 1, 0); break;
            case 4: world->newWheelJoint(anchor, b, 8, 3, 0, 0, 1, 0, 0, 0, 1); break;
            case 5: world->newWeldJoint(anchor, b, 10, 3, 0); break;
            case 6: world->newMotorJoint(anchor, b); break;
            case 7: world->newParallelJoint(anchor, b, 0, 1, 0); break;
            case 8: world->newFilterJoint(anchor, b); break;
        }
    }
    const auto oldHandle = anchor->runtimeHandle();
    auto       snapshot  = world->snapshot(hash);
    REQUIRE(snapshot.ok());
    auto restored = world->restore(snapshot.value(), hash);
    REQUIRE(restored.ok());
    CHECK(world->findBody(oldHandle) == nullptr);
    CHECK(world->getJointCount() == 9);
    for (int i = 0; i < 10; ++i) world->update(.01f);
    auto ray = world->rayCastOwned(1, 1, 1, 1, -1, 1, {});
    REQUIRE(ray.ok());
    CHECK(ray.value().hit);
}


TEST_CASE("physics.jolt.distanceSpringLimitsAndMotorShareSolver") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  a(world->newBody("static", 0, 0, 0)), b(world->newBody("dynamic", 1, 0, 0));
        std::unique_ptr<Shape3D> shape(b->newSphereShape(.25f));
        std::unique_ptr<Joint3D> joint(world->newDistanceJoint(a.get(), b.get(), 0, 0, 0, 1, 0, 0, 1));
        joint->setDistanceSpring(true, 0, 0);
        joint->setDistanceLimits(true, .5f, 2);
        joint->setDistanceMotor(true, 1, 100);
        for (int i = 0; i < 200; ++i) world->update(.01f);
        CHECK(joint->getDistanceCurrentLength() > 1.8f);
        CHECK(joint->getDistanceCurrentLength() < 2.05f);
        joint->setDistanceMotor(false, 0, 0);
        joint->setDistanceSpring(true, 4, 1);
        for (int i = 0; i < 200; ++i) world->update(.01f);
        CHECK(std::abs(joint->getDistanceCurrentLength() - 1) < .08f);
    }
}
TEST_CASE("physics.jolt.volumeQueriesAndCameraEndpoints") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  a(world->newBody("static", 10, 0, 0)), b(world->newBody("static", 14, 0, 0));
        std::unique_ptr<Shape3D> sa(a->newBoxShape(1, 1, 1)), sb(b->newSphereShape(.5f));
        CHECK(sa->testPoint(10, 0, 0));
        CHECK(!sa->testPoint(12, 0, 0));
        CHECK(world->querySphere(10, 0, 0, .25f) == 1);
        auto overlap = world->queryCapsuleOwned(10, 0, 0, 10, 1, 0, .25f, {});
        REQUIRE(overlap.ok());
        CHECK(overlap.value().bodyCount == 1);
        auto aabb = world->queryAabbBroadPhaseOwned(9, -1, -1, 15, 1, 1, {});
        REQUIRE(aabb.ok());
        CHECK(aabb.value().count == 2);
        CHECK(world->rayCastAll(8, 0, 0, 16, 0, 0, 8) >= 2);
        CHECK(world->castSphereAll(8, 0, 0, .25f, 8, 0, 0, 8) >= 2);
        CHECK(world->getShapeCastBodyId() == a->getId());
        CHECK(std::abs(world->getShapeCastFraction() - .15625f) < .01f);
        CameraSphereHit3D camera;
        CHECK(world->sphereCast(8, 0, 0, 16, 0, 0, .25f, UINT64_MAX, -1, &camera));
        CHECK(camera.bodyId == a->getId());
        CHECK(std::abs(camera.fraction - .15625f) < .01f);
        CHECK(world->sphereCast(8, 0, 0, 16, 0, 0, 0, UINT64_MAX, -1, &camera));
        CHECK(std::abs(camera.fraction - .1875f) < .01f);
        sa->setCategoryBits(uint64_t(1) << 48);
        CHECK(world->rayCastFiltered(8, 0, 0, 16, 0, 0, uint64_t(1) << 48) == a->getId());
        sa->setCategoryBits(1);
        QueryFilter3D ignored;
        ignored.ignoredBodyId = a->getId();
        auto filtered         = world->queryCapsuleOwned(10, 0, 0, 10, 1, 0, .25f, ignored);
        REQUIRE(filtered.ok());
        CHECK(filtered.value().bodyCount == 0);
    }
}
TEST_CASE("physics.jolt.localImpulsesLocksAndKinematicTarget") {
    auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, 0, 0, false);
    REQUIRE(created.ok());
    auto                     world = std::move(created).takeValue();
    std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 0, 0));
    std::unique_ptr<Shape3D> shape(body->newBoxShape(1, 1, 1));
    body->setRotation(0, 0, .70710678f, .70710678f);
    body->applyLocalLinearImpulseToCenter(1, 0, 0);
    CHECK(body->getLinearVelocityY() > .5f);
    body->setMotionLocks(true, false, false, true, true, true);
    CHECK(body->isLinearXLocked());
    CHECK(body->isAngularZLocked());
    body->applyLinearImpulse(10, 0, 0);
    CHECK(std::abs(body->getLinearVelocityX()) < .001f);
    body->setMotionLocks(true, true, true, false, false, false);
    bool rejected = false;
    try {
        body->setFixedRotation(true);
    } catch (const std::exception&) {
        rejected = true;
    }
    CHECK(rejected);
    CHECK(!body->isFixedRotation());
    body->setMotionLocks(false, false, false, false, false, false);
    body->setType("kinematic");
    body->setTargetTransform(0, 2, 0, 0, 0, 0, 1, 1);
    for (int i = 0; i < 100; ++i) world->update(.01f);
    CHECK(std::abs(body->getY() - 2) < .02f);
}
TEST_CASE("physics.jolt.moverArbitraryUpAndThinWall") {
    auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, 0, 0, false);
    REQUIRE(created.ok());
    auto                     world = std::move(created).takeValue();
    std::unique_ptr<Body3D>  floor(world->newBody("static", 0, 0, 0));
    std::unique_ptr<Shape3D> shape(floor->newBoxShape(.02f, 20, 20));
    auto                     moved = world->moveCapsuleOwned(2, 0, 0, 3, 0, 0, .5f, -50, 0, 2, {}, {1, 0, 0, .8f});
    REQUIRE(moved.ok());
    CHECK(moved.value().grounded);
    CHECK(moved.value().deltaX > -1.51f);
    CHECK(moved.value().deltaZ > 1.9f);
}


TEST_CASE("physics.jolt.motorWakesBodiesAndKeepsWorldVelocity") {
    auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, 0, 0, true);
    REQUIRE(created.ok());
    auto                     world = std::move(created).takeValue();
    std::unique_ptr<Body3D>  a(world->newBody("static", 0, 0, 0)), b(world->newBody("dynamic", 0, 0, 0));
    std::unique_ptr<Shape3D> shape(b->newSphereShape(.25f));
    std::unique_ptr<Joint3D> joint(world->newMotorJoint(a.get(), b.get()));
    b->setAwake(false);
    CHECK(!b->isAwake());
    joint->setMotorVelocityLimits(100, 100);
    joint->setMotorLinearVelocity(2, 0, 0);
    CHECK(b->isAwake());
    a->setRotation(0, 0, .70710678f, .70710678f);
    for (int i = 0; i < 50; ++i) world->update(.01f);
    CHECK(b->getX() > .8f);
    CHECK(std::abs(b->getY()) < .02f);
    CHECK(joint->getMotorLinearVelocityX() == 2);
}


TEST_CASE("physics.jolt.massDensityAndConstraintComAreAtomic") {
    auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, -9.8f, 0, false);
    REQUIRE(created.ok());
    auto                     world = std::move(created).takeValue();
    std::unique_ptr<Body3D>  a(world->newBody("static", 0, 0, 0)), b(world->newBody("dynamic", 0, -1, 0));
    std::unique_ptr<Shape3D> shape(b->newBoxShape(1, 1, 1));
    std::unique_ptr<Joint3D> joint(world->newWeldJoint(a.get(), b.get(), 0, 0, 0));
    shape->setDensity(2);
    CHECK(std::abs(b->getMass() - 2) < .001f);
    b->setMassProperties(3, .3f, 0, 0, 2, 3, 4, .1f, .2f, .1f);
    CHECK(std::abs(b->getMass() - 3) < .001f);
    CHECK(std::abs(b->getLocalCenterX() - .3f) < .001f);
    CHECK(std::abs(b->getInertiaXY() - .1f) < .001f);
    bool rejected = false;
    try {
        b->setMassProperties(5, 2, 0, 0, 1, -1, 1);
    } catch (const std::exception&) {
        rejected = true;
    }
    CHECK(rejected);
    CHECK(std::abs(b->getLocalCenterX() - .3f) < .001f);
    CHECK(std::abs(b->getMass() - 3) < .001f);
    for (int i = 0; i < 100; ++i) world->update(.01f);
    CHECK(std::abs(b->getY() + 1) < .02f);
    CHECK(std::abs(b->getX()) < .02f);
    b->resetMassProperties();
    CHECK(std::abs(b->getLocalCenterX()) < .001f);
    CHECK(std::abs(b->getMass() - 2) < .001f);
    shape->setDensity(0);
    CHECK(std::isfinite(b->getMass()));
}

TEST_CASE("physics.jolt.worldControlsAndBoundsParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 0, 0));
        std::unique_ptr<Shape3D> shape(body->newSphereShape(.5f));
        body->setBullet(true);
        world->setContinuousCollisionEnabled(false);
        CHECK(body->isBullet());
        CHECK(!world->isContinuousCollisionEnabled());
        world->setContinuousCollisionEnabled(true);
        CHECK(body->isBullet());
        world->setMaximumLinearSpeed(2);
        CHECK(world->getMaximumLinearSpeed() == 2);
        body->setLinearVelocity(20, 0, 0);
        world->update(.1f);
        CHECK(body->getLinearVelocityX() <= 2.01f);
        CHECK(world->getBoundsMinX() <= body->getX() - .49f);
        CHECK(world->getBoundsMaxX() >= body->getX() + .49f);
        CHECK(world->getProfileStepMs() >= 0);
        world->setContactRecycleDistance(.05f);
        CHECK(std::abs(world->getContactRecycleDistance() - .05f) < .00001f);
        world->setContactRecycleDistance(0);
        CHECK(world->getContactRecycleDistance() == 0);
        if (backend == RigidBackend3D::Jolt) {
            bool failed = false;
            try {
                world->setMaximumLinearSpeed(-1);
            } catch (const std::exception&) {
                failed = true;
            }
            CHECK(failed);
            CHECK(world->getMaximumLinearSpeed() == 2);
        }
    }
}
TEST_CASE("physics.jolt.closestAndExplosionParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  body(world->newBody("dynamic", 3, 0, 0));
        std::unique_ptr<Shape3D> shape(body->newSphereShape(1));
        CHECK(world->closestPoint(0, 0, 0, 5) == body->getId());
        CHECK(std::abs(world->getClosestX() - 2) < .002f);
        CHECK(std::abs(world->getClosestDistance() - 2) < .002f);
        CHECK(world->closestPoint(3, 0, 0, 0) == body->getId());
        CHECK(world->getClosestDistance() < .001f);
        shape->setExplosionScale(0);
        CHECK(world->explode(0, 0, 0, 5, 0, 1, -1) == 0);
        shape->setExplosionScale(2);
        CHECK(world->explode(0, 0, 0, 5, 0, 1, -1) == 1);
        CHECK(std::abs(body->getLinearVelocityX() - 1.5f) < .03f);
        CHECK(std::abs(body->getAngularVelocityZ()) < .001f);
        body->setLinearVelocity(0, 0, 0);
        CHECK(world->explode(0, 0, 0, 1, 2, 1, -1) == 1);
        CHECK(std::abs(body->getLinearVelocityX() - .75f) < .03f);
    }
}
TEST_CASE("physics.jolt.oneWayAndPairOverridesParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, -10, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  floor(world->newBody("static", 0, 0, 0)), body(world->newBody("dynamic", 0, -2, 0));
        std::unique_ptr<Shape3D> platform(floor->newBoxShape(10, .2f, 10)), ball(body->newSphereShape(.2f));
        platform->setOneWay(0, 1, 0, .1f, .05f, .5f);
        body->setLinearVelocity(0, 8, 0);
        for (int i = 0; i < 180; ++i) world->update(.01f);
        CHECK(body->getY() > .25f);
        CHECK(body->getY() < .35f);
        world->setBodyPairCollisionEnabled(floor.get(), body.get(), false);
        for (int i = 0; i < 60; ++i) world->update(.01f);
        CHECK(body->getY() < -.5f);
        world->setBodyPairCollisionEnabled(floor.get(), body.get(), true);
        body->setPosition(0, 2, 0);
        body->setLinearVelocity(0, 0, 0);
        for (int i = 0; i < 100; ++i) world->update(.01f);
        CHECK(body->getY() > .25f);
        world->setShapePairCollisionEnabled(platform.get(), ball.get(), false);
        for (int i = 0; i < 60; ++i) world->update(.01f);
        CHECK(body->getY() < -.5f);
    }
}
TEST_CASE("physics.jolt.concurrentSpringAndVelocityMotorParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  a(world->newBody("static", 0, 0, 0)), b(world->newBody("dynamic", 0, 0, 0));
        std::unique_ptr<Shape3D> shape(b->newBoxShape(1, 1, 1));
        std::unique_ptr<Joint3D> joint(world->newPrismaticJoint(a.get(), b.get(), 0, 0, 0, 1, 0, 0));
        joint->setPrismaticSpring(true, 0, 2, 1);
        joint->setPrismaticMotor(true, 2, 2);
        joint->setPrismaticLimits(true, -.5f, .5f);
        for (int i = 0; i < 100; ++i) world->update(.01f);
        CHECK(b->getX() > 0);
        CHECK(b->getX() < .51f);
        CHECK(std::abs(joint->getPrismaticMotorForce()) > 0);
        joint->setPrismaticMotor(false, 0, 0);
        for (int i = 0; i < 150; ++i) world->update(.01f);
        CHECK(std::abs(b->getX()) < .015f);
        joint.reset();
        joint.reset(world->newMotorJoint(a.get(), b.get()));
        joint->setMotorLinearSpring(2, 1, 100);
        joint->setMotorVelocityLimits(2, 2);
        joint->setMotorLinearVelocity(1, 0, 0);
        joint->setMotorAngularSpring(2, 1, 100);
        joint->setMotorAngularVelocity(0, 0, 1);
        for (int i = 0; i < 100; ++i) world->update(.01f);
        CHECK(std::isfinite(b->getX()));
        CHECK(std::abs(b->getX()) < .2f);
    }
}

TEST_CASE("physics.jolt.nonzeroEqualLimitsParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  a(world->newBody("static", 0, 0, 0)), b(world->newBody("dynamic", 0, 0, 0));
        std::unique_ptr<Shape3D> shape(b->newBoxShape(1, 1, 1));
        std::unique_ptr<Joint3D> joint(world->newPrismaticJoint(a.get(), b.get(), 0, 0, 0, 1, 0, 0));
        joint->setPrismaticLimits(true, .4f, .4f);
        for (int i = 0; i < 120; ++i) world->update(.01f);
        CHECK(std::abs(joint->getPrismaticTranslation() - .4f) < .015f);
        joint.reset();
        b->setPosition(0, 0, 0);
        b->setLinearVelocity(0, 0, 0);
        joint.reset(world->newRevoluteJoint(a.get(), b.get(), 0, 0, 0, 0, 0, 1));
        joint->setRevoluteLimits(true, .4f, .4f);
        for (int i = 0; i < 120; ++i) world->update(.01f);
        CHECK(std::abs(joint->getRevoluteAngle() - .4f) < .015f);
    }
}

TEST_CASE("physics.jolt.customSleepAndInactivePointParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, 0, 0, true);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 0, 0));
        std::unique_ptr<Shape3D> shape(body->newBoxShape(1, 1, 1));
        body->setSleepThreshold(.1f);
        CHECK(body->getSleepThreshold() == .1f);
        body->setLinearVelocity(.01f, 0, 0);
        for (int i = 0; i < 100; ++i) world->update(.01f);
        CHECK(!body->isAwake());
        body->setSleepEnabled(false);
        body->setAwake(true);
        body->setLinearVelocity(.01f, 0, 0);
        for (int i = 0; i < 100; ++i) world->update(.01f);
        CHECK(body->isAwake());
        body->setActive(false);
        CHECK(shape->testPoint(body->getX(), body->getY(), body->getZ()));
        ClothContact3D contact;
        const auto     status = world->pointProbeFiltered(body->getX() + .6f, 0, 0, .2f, &contact, 1, UINT64_MAX);
        CHECK(status == ClothProbeStatus::Hit);
        CHECK(contact.hit);
        CHECK(std::abs(contact.depth - .1f) < .005f);
        CHECK(contact.nx > .99f);
    }
}

TEST_CASE("physics.jolt.sensorPairExclusionParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  a(world->newBody("static", 0, 0, 0)), b(world->newBody("dynamic", 0, 0, 0));
        std::unique_ptr<Shape3D> sensor(a->newBoxShape(2, 2, 2)), shape(b->newSphereShape(.2f));
        sensor->setSensor(true);
        world->setShapePairCollisionEnabled(sensor.get(), shape.get(), false);
        world->update(.01f);
        CHECK(world->getBeginTriggerCount() == 0);
        world->setShapePairCollisionEnabled(sensor.get(), shape.get(), true);
        world->update(.01f);
        CHECK(world->getBeginTriggerCount() > 0);
        world->setBodyPairCollisionEnabled(a.get(), b.get(), false);
        world->update(.01f);
        CHECK(world->getEndTriggerCount() > 0);
    }
}

#else
TEST_CASE("physics.jolt.absentProviderRejectsExplicitSelection") {
    auto result = World3D::createWithBackend(RigidBackend3D::Jolt, 0, -9.8f, 0, true);
    REQUIRE(!result.ok());
    CHECK(result.error()->code() == eve::DiagnosticCode::Unsupported);
}
#endif
}  // namespace test_physics_jolt
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_jolt_contacts)
#include <cmath>
#include <memory>
#include "PrivateTestScope.h"
#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// physics_jolt_contacts.cpp
namespace test_physics_jolt_contacts {
#if EVENGINE_TEST_HAS_JOLT
using namespace eve::physics;

TEST_CASE("physics.jolt.solvedImpulseMomentumParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto made = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(made.ok());
        auto                     world = std::move(made).takeValue();
        std::unique_ptr<Body3D>  ground(world->newBody("static", 0, -.5f, 0));
        std::unique_ptr<Shape3D> floor(ground->newBoxShape(10, 1, 10));
        std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, .6f, 0));
        std::unique_ptr<Shape3D> sphere(body->newSphereShape(.5f));
        body->setLinearDamping(0);
        body->setAngularDamping(0);
        body->setLinearVelocity(0, -4, 0);
        sphere->setTag(72);
        for (int frame = 0; frame < 4; ++frame) {
            const float before = body->getLinearVelocityY();
            world->updateFull(.025f, 1);
            auto queried = world->queryBodyContactsOwned(body->runtimeHandle(), 4096);
            REQUIRE(queried.ok());
            float impulse = 0;
            for (const auto& point : queried.value().points) {
                CHECK(point.shapeTag == 72);
                CHECK(point.otherBodyId == ground->getId());
                CHECK(point.normalY < -.99f);
                CHECK(point.normalImpulse >= 0);
                impulse += point.totalNormalImpulse;
            }
            const float momentum = body->getMass() * (body->getLinearVelocityY() - before);
            CHECK(std::abs(impulse - momentum) < .03f);
        }
        CHECK(body->getLinearVelocityY() > -1);
    }
}

TEST_CASE("physics.jolt.solvedContactCoverageLifetimeParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto made = World3D::createWithBackend(backend, 0, -10, 0, false);
        REQUIRE(made.ok());
        auto                     world = std::move(made).takeValue();
        std::unique_ptr<Body3D>  ground(world->newBody("static", 0, -.5f, 0));
        std::unique_ptr<Shape3D> floor(ground->newBoxShape(10, 1, 10));
        std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 1, 0));
        std::unique_ptr<Shape3D> box(body->newBoxShape(1, 1, 1));
        for (int i = 0; i < 100; ++i) world->updateFull(.02f, 4);
        auto all = world->queryBodyContactsOwned(body->runtimeHandle(), 4096);
        REQUIRE(all.ok());
        REQUIRE(all.value().points.size() > 1);
        float impulse = 0;
        for (const auto& point : all.value().points) {
            impulse += point.totalNormalImpulse;
            CHECK(point.persisted);
        }
        CHECK(std::abs(impulse - body->getMass() * 10 * .02f) < .04f);
        auto limited = world->queryBodyContactsOwned(body->runtimeHandle(), 1);
        REQUIRE(limited.ok());
        CHECK(limited.value().coverage == ContactQueryCoverage3D::Truncated);
        CHECK(limited.value().points.size() == 1);
        auto opposite = world->queryBodyContactsOwned(ground->runtimeHandle(), 4096);
        REQUIRE(opposite.ok());
        CHECK(opposite.value().points.front().normalY > .99f);
        CHECK(!world->queryBodyContactsOwned(body->runtimeHandle(), 0).ok());
        body->setActive(false);
        auto inactive = world->queryBodyContactsOwned(body->runtimeHandle(), 4096);
        REQUIRE(inactive.ok());
        CHECK(inactive.value().points.empty());
        body->setActive(true);
        const auto handle  = body->runtimeHandle();
        const auto shapeId = box->getId();
        box.reset();
        auto removed = world->queryBodyContactsOwned(handle, 4096);
        REQUIRE(removed.ok());
        CHECK(removed.value().points.empty());
        world->destroy();
        CHECK(all.value().points.front().shapeId == shapeId);
        CHECK(!world->queryBodyContactsOwned(handle, 1).ok());
    }
}

TEST_CASE("physics.jolt.hitThresholdAndOptInParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt})
        for (int policy = 0; policy < 3; ++policy) {
            auto made = World3D::createWithBackend(backend, 0, 0, 0, false);
            REQUIRE(made.ok());
            auto world = std::move(made).takeValue();
            world->setHitEventThreshold(policy == 2 ? 100.f : 1.f);
            CHECK(world->getHitEventThreshold() == (policy == 2 ? 100.f : 1.f));
            std::unique_ptr<Body3D>  ground(world->newBody("static", 0, -.5f, 0));
            std::unique_ptr<Shape3D> floor(ground->newBoxShape(10, 1, 10));
            std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 1, 0));
            std::unique_ptr<Shape3D> sphere(body->newSphereShape(.5f));
            sphere->setHitEventsEnabled(policy != 0);
            sphere->setTag(73);
            body->setLinearDamping(0);
            body->setLinearVelocity(0, -5, 0);
            int hits = 0;
            for (int i = 0; i < 20; ++i) {
                world->updateFull(.01f, 2);
                hits += world->getHitCount();
                for (int j = 0; j < world->getHitCount(); ++j) {
                    CHECK(world->getHitApproachSpeed(j) > 1);
                    CHECK(world->getHitNormalImpulse(j) > 0);
                    CHECK(world->getHitShapeAId(j) < world->getHitShapeBId(j));
                    CHECK(world->getHitShapeBTag(j) == 73);
                }
            }
            CHECK((hits > 0) == (policy == 1));
            CHECK(world->getHitCount() == 0);
        }
}

TEST_CASE("physics.jolt.sensorHasNoSolvedImpulseOrHitParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto made = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(made.ok());
        auto                     world = std::move(made).takeValue();
        std::unique_ptr<Body3D>  ground(world->newBody("static", 0, 0, 0));
        std::unique_ptr<Shape3D> sensor(ground->newBoxShape(10, 1, 10));
        sensor->setSensor(true);
        std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 1, 0));
        std::unique_ptr<Shape3D> sphere(body->newSphereShape(.5f));
        sphere->setHitEventsEnabled(true);
        body->setLinearVelocity(0, -5, 0);
        for (int i = 0; i < 30; ++i) {
            world->update(.01f);
            CHECK(world->getHitCount() == 0);
            auto queried = world->queryBodyContactsOwned(body->runtimeHandle(), 4096);
            REQUIRE(queried.ok());
            CHECK(queried.value().points.empty());
        }
    }
}

TEST_CASE("physics.jolt.rollingResistanceContactOnlyParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt})
        for (int mode = 0; mode < 3; ++mode) {
            auto made = World3D::createWithBackend(backend, 0, mode == 2 ? 0.f : -10.f, 0, false);
            REQUIRE(made.ok());
            auto                     world = std::move(made).takeValue();
            std::unique_ptr<Body3D>  ground(world->newBody("static", 0, -.5f, 0));
            std::unique_ptr<Shape3D> floor(ground->newBoxShape(20, 1, 20));
            floor->setFriction(0);
            floor->setRollingResistance(mode == 0 ? 0 : .3f);
            std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, mode == 2 ? 5.f : .5f, 0));
            std::unique_ptr<Shape3D> sphere(body->newSphereShape(.5f));
            sphere->setFriction(0);
            body->setAngularDamping(0);
            body->setLinearDamping(0);
            body->setAngularVelocity(0, 10, 0);
            for (int i = 0; i < 100; ++i) world->updateFull(.01f, 2);
            if (mode == 1)
                CHECK(std::abs(body->getAngularVelocityY()) < .2f);
            else
                CHECK(std::abs(body->getAngularVelocityY() - 10) < .01f);
            CHECK_THROWS((sphere->setRollingResistance(-1), false));
            CHECK(sphere->getRollingResistance() == 0);
        }
}

TEST_CASE("physics.jolt.ccdReportsActualHitImpulse") {
    auto made = World3D::createWithBackend(RigidBackend3D::Jolt, 0, 0, 0, false);
    REQUIRE(made.ok());
    auto                     world = std::move(made).takeValue();
    std::unique_ptr<Body3D>  ground(world->newBody("static", 0, -.05f, 0));
    std::unique_ptr<Shape3D> floor(ground->newBoxShape(10, .1f, 10));
    std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 2, 0));
    std::unique_ptr<Shape3D> sphere(body->newSphereShape(.1f));
    sphere->setHitEventsEnabled(true);
    body->setBullet(true);
    body->setLinearDamping(0);
    body->setLinearVelocity(0, -100, 0);
    world->updateFull(.04f, 1);
    CHECK(body->getY() > -.1f);
    REQUIRE(world->getHitCount() == 1);
    CHECK(world->getHitApproachSpeed(0) > 90);
    CHECK(std::abs(world->getHitNormalImpulse(0) - body->getMass() * 100) < .01f);
    auto queried = world->queryBodyContactsOwned(body->runtimeHandle(), 4096);
    REQUIRE(queried.ok());
    REQUIRE(!queried.value().points.empty());
    CHECK(queried.value().points.front().normalImpulse > 0);
}

TEST_CASE("physics.jolt.meshRollingMaterialAndHitParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto made = World3D::createWithBackend(backend, 0, -10, 0, false);
        REQUIRE(made.ok());
        auto                     world = std::move(made).takeValue();
        std::unique_ptr<Body3D>  ground(world->newBody("static", 0, 0, 0));
        std::unique_ptr<Shape3D> mesh(
            ground->newTriangleMeshShape({-5, 0, -5, 5, 0, -5, 5, 0, 5, -5, 0, 5}, {0, 2, 1, 0, 3, 2}));
        mesh->setTriangleMeshMaterial(0, 0, 0, .5f, 0, 0, 0, 81, "default", "default");
        CHECK(mesh->getTriangleMeshMaterialRollingResistance(0) == .5f);
        std::unique_ptr<Body3D>  body(world->newBody("dynamic", 1, 2, 0));
        std::unique_ptr<Shape3D> sphere(body->newSphereShape(.5f));
        sphere->setHitEventsEnabled(true);
        sphere->setFriction(0);
        body->setAngularDamping(0);
        body->setAngularVelocity(0, 8, 0);
        bool hit = false;
        for (int i = 0; i < 150; ++i) {
            world->update(.01f);
            hit = hit || world->getHitCount() > 0;
        }
        CHECK(hit);
        CHECK(body->getY() > .45f);
        CHECK(std::abs(body->getAngularVelocityY()) < .2f);
    }
}

TEST_CASE("physics.jolt.disabledPairHasNoSolvedHitParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto made = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(made.ok());
        auto                     world = std::move(made).takeValue();
        std::unique_ptr<Body3D>  a(world->newBody("static", 0, 0, 0)), b(world->newBody("dynamic", 0, 1, 0));
        std::unique_ptr<Shape3D> floor(a->newBoxShape(10, .1f, 10)), ball(b->newSphereShape(.2f));
        ball->setHitEventsEnabled(true);
        world->setShapePairCollisionEnabled(floor.get(), ball.get(), false);
        b->setLinearVelocity(0, -10, 0);
        for (int i = 0; i < 20; ++i) {
            world->update(.01f);
            CHECK(world->getHitCount() == 0);
            auto contact = world->queryBodyContactsOwned(b->runtimeHandle(), 4096);
            REQUIRE(contact.ok());
            CHECK(contact.value().points.empty());
        }
        CHECK(b->getY() < 0);
    }
}
#endif
}  // namespace test_physics_jolt_contacts
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_jolt_cooking)
#include <cmath>
#include <limits>
#include <memory>
#include "PrivateTestScope.h"
#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "physics/box3d/Box3DWorldFactory.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#if EVENGINE_TEST_HAS_JOLT
#include "physics/jolt/JoltWorldFactory.h"
#endif
// physics_jolt_cooking.cpp
namespace test_physics_jolt_cooking_unit {
#if EVENGINE_TEST_HAS_JOLT

using namespace eve::physics;


TEST_CASE("physics.jolt.hullBudgetAndAtomicRebuildParity") {
    std::vector<float> points;
    for (int x : {-1, 1})
        for (int y : {-1, 1})
            for (int z : {-1, 1}) points.insert(points.end(), {float(x), float(y), float(z)});
    // Interior and repeated source points must not consume the hull's vertex budget.
    for (int i = 0; i < 32; ++i) points.insert(points.end(), {0, 0, 0});
    for (auto kind : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto made = World3D::createWithBackend(kind, 0, 0, 0, false);
        REQUIRE(made.ok());
        auto                     world = std::move(made).takeValue();
        std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 0, 0));
        std::unique_ptr<Shape3D> hull(body->newConvexHullShape(points, 8));
        CHECK(std::abs(body->getMass() - 8) < .02f);
        CHECK(hull->testPoint(.9f, .9f, .9f));
        const auto id = hull->getId();
        hull->setConvexHullVertices(points, 4);
        CHECK(hull->getId() == id);
        CHECK(hull->getConvexHullMaxVertices() == 4);
        CHECK(body->getMass() > .01f);
        CHECK(body->getMass() < 7.9f);
        const auto               mass = body->getMass();
        const std::vector<float> flat{0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0};
        CHECK_THROWS((hull->setConvexHullVertices(flat, 4), false));
        CHECK_THROWS((hull->setConvexHullVertices(points, 255), false));
        CHECK(hull->getConvexHullMaxVertices() == 4);
        CHECK(body->getMass() == mass);
        CHECK(world->getShapeCount() == 1);
        hull->setConvexHullVertices(points, 8);
        CHECK(std::abs(body->getMass() - 8) < .02f);
    }
}
TEST_CASE("physics.jolt.simplifiedHullCollisionParity") {
    std::vector<float> points;
    for (int x : {-1, 1})
        for (int y : {-1, 1})
            for (int z : {-1, 1}) points.insert(points.end(), {float(x), float(y), float(z)});
    for (auto kind : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto made = World3D::createWithBackend(kind, 0, -10, 0, false);
        REQUIRE(made.ok());
        auto                     world = std::move(made).takeValue();
        std::unique_ptr<Body3D>  ground(world->newBody("static", 0, -.5f, 0));
        std::unique_ptr<Shape3D> floor(ground->newBoxShape(20, 1, 20));
        std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 4, 0));
        std::unique_ptr<Shape3D> hull(body->newConvexHullShape(points, 4));
        bool                     contacted = false;
        for (int i = 0; i < 300; ++i) {
            world->update(.01f);
            contacted = contacted || world->getBeginContactCount() > 0;
        }
        CHECK(contacted);
        CHECK(std::isfinite(body->getY()));
        CHECK(body->getY() > -2);
        CHECK(body->getY() < 2);
        CHECK(std::abs(body->getLinearVelocityY()) < .1f);
        auto ray = world->rayCastOwned(body->getWorldCenterX(), 5, body->getWorldCenterZ(), body->getWorldCenterX(), -3,
                                       body->getWorldCenterZ(), {});
        REQUIRE(ray.ok());
        CHECK(ray.value().hit);
        CHECK(ray.value().body == body->runtimeHandle());
    }
}
#endif
}  // namespace test_physics_jolt_cooking_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_rigid_backend)
#include <memory>
#include "PrivateTestScope.h"
#include "common/Exception.h"
#include "physics/Body3D.h"
#include "physics/Joint3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "zeroerr/unittest.h"
// physics_rigid_backend.cpp
namespace test_physics_rigid_backend_unit {
using namespace eve::physics;
namespace {
struct ProbeState {
    float x         = 0.f;
    int   destroyed = 0;
    bool  alive     = true;
};
template <class T>
[[maybe_unused]] eve::Result<T> unsupported() {
    return eve::Result<T>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Unsupported,
                                                          "injected provider rejection", "physics.backend.probe"));
}
#define PROBE_UNSUPPORTED(type, name, arguments, qualifiers) \
    eve::Result<type> name arguments qualifiers override { return unsupported<type>(); }
class BodyProbe final : public IBodyBackend3D {
public:
    PROBE_UNSUPPORTED(std::unique_ptr<IShapeBackend3D>, createShape, (const ShapeGeometry3D&, float, float, float), )
    void bindOwner(Body3D&) noexcept override {}
    explicit BodyProbe(ProbeState& state) : state_(state) {}
    ~BodyProbe() override { destroy(); }
    bool isValid() const noexcept override { return state_.alive; }
    void destroy() noexcept override {
        if (state_.alive) ++state_.destroyed;
        state_.alive = false;
    }
    void              invalidate() noexcept override { state_.alive = false; }
    eve::Result<void> setPosition(float x, float, float) override {
        state_.x = x;
        return eve::Result<void>::success();
    }
    eve::Result<float> getX() const override { return eve::Result<float>::success(state_.x); }
    PROBE_UNSUPPORTED(float, getY, (), const)
    PROBE_UNSUPPORTED(float, getZ, (), const)
    PROBE_UNSUPPORTED(void, setRotation, (float, float, float, float), )
    PROBE_UNSUPPORTED(float, getRotX, (), const)
    PROBE_UNSUPPORTED(float, getRotY, (), const)
    PROBE_UNSUPPORTED(float, getRotZ, (), const)
    PROBE_UNSUPPORTED(float, getRotW, (), const)
    PROBE_UNSUPPORTED(void, setLinearVelocity, (float, float, float), )
    PROBE_UNSUPPORTED(float, getLinearVelocityX, (), const)
    PROBE_UNSUPPORTED(float, getLinearVelocityY, (), const)
    PROBE_UNSUPPORTED(float, getLinearVelocityZ, (), const)
    PROBE_UNSUPPORTED(float, getMass, (), const)
    PROBE_UNSUPPORTED(void, setAngularVelocity, (float, float, float), )
    PROBE_UNSUPPORTED(float, getAngularVelocityX, (), const)
    PROBE_UNSUPPORTED(float, getAngularVelocityY, (), const)
    PROBE_UNSUPPORTED(float, getAngularVelocityZ, (), const)
    PROBE_UNSUPPORTED(std::vector<float>, localToWorldPoint, (float, float, float), const)
    PROBE_UNSUPPORTED(PhysicsVector3D, localToWorldPointOwned, (float, float, float), const)
    PROBE_UNSUPPORTED(std::vector<float>, worldToLocalPoint, (float, float, float), const)
    PROBE_UNSUPPORTED(PhysicsVector3D, worldToLocalPointOwned, (float, float, float), const)
    PROBE_UNSUPPORTED(std::vector<float>, localToWorldVector, (float, float, float), const)
    PROBE_UNSUPPORTED(PhysicsVector3D, localToWorldVectorOwned, (float, float, float), const)
    PROBE_UNSUPPORTED(std::vector<float>, worldToLocalVector, (float, float, float), const)
    PROBE_UNSUPPORTED(std::vector<float>, getLocalPointVelocity, (float, float, float), const)
    PROBE_UNSUPPORTED(PhysicsVector3D, getLocalPointVelocityOwned, (float, float, float), const)
    PROBE_UNSUPPORTED(std::vector<float>, getWorldPointVelocity, (float, float, float), const)
    PROBE_UNSUPPORTED(void, applyForce, (float, float, float), )
    PROBE_UNSUPPORTED(void, applyForceAt, (float, float, float, float, float, float), )
    PROBE_UNSUPPORTED(void, applyTorque, (float, float, float), )
    PROBE_UNSUPPORTED(void, applyLinearImpulse, (float, float, float), )
    PROBE_UNSUPPORTED(void, applyLinearImpulseAt, (float, float, float, float, float, float), )
    PROBE_UNSUPPORTED(void, applyAngularImpulse, (float, float, float), )
    PROBE_UNSUPPORTED(void, applyLocalForce, (float, float, float, float, float, float), )
    PROBE_UNSUPPORTED(void, applyLocalForceToCenter, (float, float, float), )
    PROBE_UNSUPPORTED(void, applyLocalTorque, (float, float, float), )
    PROBE_UNSUPPORTED(void, applyLocalLinearImpulse, (float, float, float, float, float, float), )
    PROBE_UNSUPPORTED(void, applyLocalLinearImpulseToCenter, (float, float, float), )
    PROBE_UNSUPPORTED(void, applyLocalAngularImpulse, (float, float, float), )
    PROBE_UNSUPPORTED(void, setTargetTransform, (float, float, float, float, float, float, float, float), )
    PROBE_UNSUPPORTED(void, setLinearDamping, (float), )
    PROBE_UNSUPPORTED(float, getLinearDamping, (), const)
    PROBE_UNSUPPORTED(void, setAngularDamping, (float), )
    PROBE_UNSUPPORTED(float, getAngularDamping, (), const)
    PROBE_UNSUPPORTED(void, setGravityScale, (float), )
    PROBE_UNSUPPORTED(float, getGravityScale, (), const)
    PROBE_UNSUPPORTED(void, setSleepEnabled, (bool), )
    PROBE_UNSUPPORTED(bool, isSleepEnabled, (), const)
    PROBE_UNSUPPORTED(void, setSleepThreshold, (float), )
    PROBE_UNSUPPORTED(float, getSleepThreshold, (), const)
    PROBE_UNSUPPORTED(void, setMotionLocks, (bool, bool, bool, bool, bool, bool), )
    PROBE_UNSUPPORTED(bool, isLinearXLocked, (), const)
    PROBE_UNSUPPORTED(bool, isLinearYLocked, (), const)
    PROBE_UNSUPPORTED(bool, isLinearZLocked, (), const)
    PROBE_UNSUPPORTED(bool, isAngularXLocked, (), const)
    PROBE_UNSUPPORTED(bool, isAngularYLocked, (), const)
    PROBE_UNSUPPORTED(bool, isAngularZLocked, (), const)
    PROBE_UNSUPPORTED(void, setMassProperties, (float, float, float, float, float, float, float, float, float, float), )
    PROBE_UNSUPPORTED(void, resetMassProperties, (), )
    PROBE_UNSUPPORTED(float, getInertiaXX, (), const)
    PROBE_UNSUPPORTED(float, getInertiaYY, (), const)
    PROBE_UNSUPPORTED(float, getInertiaZZ, (), const)
    PROBE_UNSUPPORTED(float, getInertiaXY, (), const)
    PROBE_UNSUPPORTED(float, getInertiaXZ, (), const)
    PROBE_UNSUPPORTED(float, getInertiaYZ, (), const)
    PROBE_UNSUPPORTED(float, getLocalCenterX, (), const)
    PROBE_UNSUPPORTED(float, getLocalCenterY, (), const)
    PROBE_UNSUPPORTED(float, getLocalCenterZ, (), const)
    PROBE_UNSUPPORTED(float, getWorldCenterX, (), const)
    PROBE_UNSUPPORTED(float, getWorldCenterY, (), const)
    PROBE_UNSUPPORTED(float, getWorldCenterZ, (), const)
    PROBE_UNSUPPORTED(void, setType, (const std::string&), )
    PROBE_UNSUPPORTED(std::string, getType, (), const)
    PROBE_UNSUPPORTED(void, setFixedRotation, (bool), )
    PROBE_UNSUPPORTED(bool, isFixedRotation, (), const)
    PROBE_UNSUPPORTED(void, setActive, (bool), )
    PROBE_UNSUPPORTED(bool, isActive, (), const)
    PROBE_UNSUPPORTED(void, setBullet, (bool), )
    PROBE_UNSUPPORTED(bool, isBullet, (), const)
    PROBE_UNSUPPORTED(void, setAwake, (bool), )
    PROBE_UNSUPPORTED(bool, isAwake, (), const)
private:
    ProbeState& state_;
};
class JointProbe final : public IJointBackend3D {
public:
    void bindOwner(Joint3D&) noexcept override {}
    explicit JointProbe(ProbeState& state) : state_(state) {}
    ~JointProbe() override { destroy(); }
    bool isValid() const noexcept override { return state_.alive; }
    void destroy() noexcept override {
        if (state_.alive) ++state_.destroyed;
        state_.alive = false;
    }
    void                       invalidate() noexcept override { state_.alive = false; }
    eve::Result<JointFrames3D> localFrames() const override {
        JointFrames3D frames;
        frames.bodyA[0] = state_.x;
        return eve::Result<JointFrames3D>::success(frames);
    }
    PROBE_UNSUPPORTED(void, setCollideConnected, (bool), )
    PROBE_UNSUPPORTED(bool, getCollideConnected, (), const)
    PROBE_UNSUPPORTED(void, setForceThreshold, (float), )
    PROBE_UNSUPPORTED(float, getForceThreshold, (), const)
    PROBE_UNSUPPORTED(void, setTorqueThreshold, (float), )
    PROBE_UNSUPPORTED(float, getTorqueThreshold, (), const)
    PROBE_UNSUPPORTED(float, getConstraintForceX, (), const)
    PROBE_UNSUPPORTED(float, getConstraintForceY, (), const)
    PROBE_UNSUPPORTED(float, getConstraintForceZ, (), const)
    PROBE_UNSUPPORTED(float, getConstraintTorqueX, (), const)
    PROBE_UNSUPPORTED(float, getConstraintTorqueY, (), const)
    PROBE_UNSUPPORTED(float, getConstraintTorqueZ, (), const)
    PROBE_UNSUPPORTED(float, getLinearSeparation, (), const)
    PROBE_UNSUPPORTED(float, getAngularSeparation, (), const)
    PROBE_UNSUPPORTED(void, setDistanceLength, (float), )
    PROBE_UNSUPPORTED(float, getDistanceLength, (), const)
    PROBE_UNSUPPORTED(float, getDistanceCurrentLength, (), const)
    PROBE_UNSUPPORTED(void, setDistanceSpring, (bool, float, float), )
    PROBE_UNSUPPORTED(void, setDistanceLimits, (bool, float, float), )
    PROBE_UNSUPPORTED(void, setDistanceMotor, (bool, float, float), )
    PROBE_UNSUPPORTED(void, setRevoluteSpring, (bool, float, float, float), )
    PROBE_UNSUPPORTED(void, setRevoluteLimits, (bool, float, float), )
    PROBE_UNSUPPORTED(void, setRevoluteMotor, (bool, float, float), )
    PROBE_UNSUPPORTED(float, getRevoluteAngle, (), const)
    PROBE_UNSUPPORTED(float, getRevoluteMotorTorque, (), const)
    PROBE_UNSUPPORTED(void, setPrismaticSpring, (bool, float, float, float), )
    PROBE_UNSUPPORTED(void, setPrismaticLimits, (bool, float, float), )
    PROBE_UNSUPPORTED(void, setPrismaticMotor, (bool, float, float), )
    PROBE_UNSUPPORTED(float, getPrismaticTranslation, (), const)
    PROBE_UNSUPPORTED(float, getPrismaticSpeed, (), const)
    PROBE_UNSUPPORTED(float, getPrismaticMotorForce, (), const)
    PROBE_UNSUPPORTED(void, setSphericalConeLimit, (bool, float), )
    PROBE_UNSUPPORTED(void, setSphericalTwistLimits, (bool, float, float), )
    PROBE_UNSUPPORTED(void, setSphericalMotor, (bool, float, float, float, float), )
    PROBE_UNSUPPORTED(float, getSphericalConeAngle, (), const)
    PROBE_UNSUPPORTED(float, getSphericalTwistAngle, (), const)
    PROBE_UNSUPPORTED(void, setWheelSuspension, (bool, float, float), )
    PROBE_UNSUPPORTED(void, setWheelSuspensionLimits, (bool, float, float), )
    PROBE_UNSUPPORTED(void, setWheelSpinMotor, (bool, float, float), )
    PROBE_UNSUPPORTED(void, setWheelSteering, (bool, float, float, float, float), )
    PROBE_UNSUPPORTED(void, setWheelSteeringLimits, (bool, float, float), )
    PROBE_UNSUPPORTED(float, getWheelSpinSpeed, (), const)
    PROBE_UNSUPPORTED(float, getWheelSpinTorque, (), const)
    PROBE_UNSUPPORTED(float, getWheelSteeringAngle, (), const)
    PROBE_UNSUPPORTED(float, getWheelSteeringTorque, (), const)
    PROBE_UNSUPPORTED(void, setWeldLinearSpring, (float, float), )
    PROBE_UNSUPPORTED(void, setWeldAngularSpring, (float, float), )
    PROBE_UNSUPPORTED(float, getWeldLinearHertz, (), const)
    PROBE_UNSUPPORTED(float, getWeldLinearDampingRatio, (), const)
    PROBE_UNSUPPORTED(float, getWeldAngularHertz, (), const)
    PROBE_UNSUPPORTED(float, getWeldAngularDampingRatio, (), const)
    PROBE_UNSUPPORTED(void, setMotorLinearVelocity, (float, float, float), )
    PROBE_UNSUPPORTED(void, setMotorAngularVelocity, (float, float, float), )
    PROBE_UNSUPPORTED(void, setMotorVelocityLimits, (float, float), )
    PROBE_UNSUPPORTED(void, setMotorLinearSpring, (float, float, float), )
    PROBE_UNSUPPORTED(void, setMotorAngularSpring, (float, float, float), )
    PROBE_UNSUPPORTED(float, getMotorLinearVelocityX, (), const)
    PROBE_UNSUPPORTED(float, getMotorLinearVelocityY, (), const)
    PROBE_UNSUPPORTED(float, getMotorLinearVelocityZ, (), const)
    PROBE_UNSUPPORTED(float, getMotorAngularVelocityX, (), const)
    PROBE_UNSUPPORTED(float, getMotorAngularVelocityY, (), const)
    PROBE_UNSUPPORTED(float, getMotorAngularVelocityZ, (), const)
    PROBE_UNSUPPORTED(void, setParallelSpring, (float, float, float), )
    PROBE_UNSUPPORTED(float, getParallelHertz, (), const)
    PROBE_UNSUPPORTED(float, getParallelDampingRatio, (), const)
    PROBE_UNSUPPORTED(float, getParallelMaxTorque, (), const)
private:
    ProbeState& state_;
};
class ShapeProbe final : public IShapeBackend3D {
public:
    bool              alive = true;
    bool              isValid() const noexcept override { return alive; }
    void              destroy() noexcept override { alive = false; }
    void              invalidate() noexcept override { alive = false; }
    void              bindOwner(Shape3D&) noexcept override {}
    eve::Result<bool> isSensor() const override { return eve::Result<bool>::success(false); }
    PROBE_UNSUPPORTED(ShapePlane3D, oneWayPlane, (const ShapeGeometry3D&, PhysicsVector3D, float), const)
    PROBE_UNSUPPORTED(void, rebuild, (const ShapeGeometry3D&, ShapeBehavior3D, std::span<const SurfaceMaterial3D>), )
    PROBE_UNSUPPORTED(float, density, (), const)
    PROBE_UNSUPPORTED(void, setDensity, (float, bool), )
    PROBE_UNSUPPORTED(float, friction, (), const)
    PROBE_UNSUPPORTED(void, setFriction, (float), )
    PROBE_UNSUPPORTED(float, restitution, (), const)
    PROBE_UNSUPPORTED(void, setRestitution, (float), )
    PROBE_UNSUPPORTED(SurfaceMaterial3D, material, (), const)
    PROBE_UNSUPPORTED(void, setMaterial, (SurfaceMaterial3D), )
    PROBE_UNSUPPORTED(ShapeFilter3D, filter, (), const)
    PROBE_UNSUPPORTED(void, setFilter, (ShapeFilter3D, bool), )
    PROBE_UNSUPPORTED(void, enableHitEvents, (bool), )
    PROBE_UNSUPPORTED(void, enablePreSolveEvents, (bool), )
    PROBE_UNSUPPORTED(int, meshMaterialCount, (), const)
    PROBE_UNSUPPORTED(SurfaceMaterial3D, meshMaterial, (int), const)
    PROBE_UNSUPPORTED(void, setMeshMaterial, (SurfaceMaterial3D, int), )
    PROBE_UNSUPPORTED(bool, testPoint, (float, float, float), const)
};

[[maybe_unused]] void checkPositionContract(Body3D& body) {
    for (float x : {7.f, -3.f, 0.f}) {
        body.setPosition(x, 0.f, 0.f);
        CHECK(body.getX() == x);
    }
    body.destroy();
    CHECK(!body.isValid());
    body.destroy();
    CHECK(!body.isValid());
}
#undef PROBE_UNSUPPORTED
}  // namespace


TEST_CASE("physics.backend.explicitWorldSelectionRejectsAbsentProvider") {
    auto missing = World3D::createWithBackend(RigidBackend3D::Jolt, 0.f, -9.8f, 0.f, true);
    if (missing.ok()) {
        CHECK(missing.value()->rigidBackendKind() == RigidBackend3D::Jolt);
    } else {
        REQUIRE(missing.error() != nullptr);
        CHECK(missing.error()->code() == eve::DiagnosticCode::Unsupported);
    }
    auto available = World3D::createWithBackend(RigidBackend3D::Box3D, 0.f, -9.8f, 0.f, true);
    REQUIRE(available.ok());
    CHECK(available.value()->rigidBackendKind() == RigidBackend3D::Box3D);
    std::unique_ptr<Body3D> body(available.value()->newBody("dynamic", 0.f, 10.f, 0.f));
    available.value()->update(0.02f);
    CHECK(body->getY() < 10.f);
}


TEST_CASE("physics.backend.shapeFailureKeepsPortableState") {
    ProbeState      state;
    Body3D          body(nullptr, std::make_unique<BodyProbe>(state), 1, PhysicsBodyHandle::invalid());
    ShapeGeometry3D geometry;
    geometry.a = geometry.b = geometry.c = 1.f;
    Shape3D shape(nullptr, &body, std::make_unique<ShapeProbe>(), PhysicsShapeHandle::invalid(), geometry);
    int     rejected = 0;
    try {
        shape.setBoxSize(4.f, 4.f, 4.f);
    } catch (const eve::Exception&) {
        ++rejected;
    }
    CHECK(shape.getBoxWidth() == 2.f);
    try {
        shape.setHitEventsEnabled(true);
    } catch (const eve::Exception&) {
        ++rejected;
    }
    CHECK(!shape.areHitEventsEnabled());
    try {
        shape.setOneWay(0.f, 1.f, 0.f, 0.f, 0.f, 0.f);
    } catch (const eve::Exception&) {
        ++rejected;
    }
    CHECK(!shape.isOneWay());
    try {
        shape.setExplosionScale(2.f);
    } catch (const eve::Exception&) {
        ++rejected;
    }
    CHECK(shape.getExplosionScale() == 1.f);
    CHECK(rejected == 4);
    CHECK(shape.isValid());
}

TEST_CASE("physics.backend.detachedBodyDestructionReleasesOwner") {
    ProbeState state;
    { Body3D probe(nullptr, std::make_unique<BodyProbe>(state), 1, PhysicsBodyHandle::invalid()); }
    CHECK(state.destroyed == 1);
}
}  // namespace test_physics_rigid_backend_unit
#undef PROBE_UNSUPPORTED
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_targeting)
#include <type_traits>
#include <utility>
#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "common/Identity.h"
#include "physics/TargetingLineOfSightAdapter.h"
#include "physics/World3D.h"
#include "sensing/Targeting.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// targeting.cpp
namespace test_targeting_unit {
namespace {

using eve::LogicalId;
using eve::PersistentId;
using eve::physics::TargetingLineOfSightAdapter;
using eve::physics::World3D;
using eve::sensing::CoordinateSpace;
using eve::sensing::CountPolicy;
using eve::sensing::GridPoint;
using eve::sensing::ILineOfSightQuery;
using eve::sensing::ISensingCandidateProvider;
using eve::sensing::LineOfSightResult;
using eve::sensing::SensingCandidateProvider;
using eve::sensing::SensingWorld;
using eve::sensing::SensingWorldCandidateProvider;
using eve::sensing::SubjectRef;
using eve::sensing::TargetCandidate;
using eve::sensing::TargetDomain;
using eve::sensing::TargetingQuery;
using eve::sensing::TargetingResolver;
using eve::sensing::TargetingSpec;
using eve::sensing::TargetLocation;
using eve::sensing::WorldArea;
using eve::sensing::WorldPoint;

[[maybe_unused]] SubjectRef subject(const char* text) {
    auto id = PersistentId::parse(text);
    REQUIRE(id.has_value());
    return SubjectRef::fromPersistentId(*id);
}

[[maybe_unused]] WorldPoint world2D(float x, float y) {
    auto point = WorldPoint::world2D(x, y);
    return std::move(point).expect("test World2D point must be valid");
}

[[maybe_unused]] WorldPoint world3D(float x, float y, float z) {
    auto point = WorldPoint::world3D(x, y, z);
    return std::move(point).expect("test World3D point must be valid");
}

struct ResetCapabilities {
    ResetCapabilities() { eve::cap::detail::clearAllRaw(); }
    ~ResetCapabilities() { eve::cap::detail::clearAllRaw(); }
};

class VisibleLineOfSight final : public ILineOfSightQuery {
public:
    explicit VisibleLineOfSight(bool visible) : visible_(visible) {}

    eve::Result<LineOfSightResult> query(const TargetLocation& from, const TargetLocation& to) const override {
        if (from.index() != to.index())
            return eve::Result<LineOfSightResult>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "test LOS received mixed coordinate spaces"));
        if (from.index() == 1)
            return eve::Result<LineOfSightResult>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "test LOS does not support grid coordinates"));
        return eve::Result<LineOfSightResult>::success(LineOfSightResult{visible_, std::nullopt});
    }

private:
    bool visible_ = true;
};

}  // namespace

TEST_CASE("targeting.typesKeepIdentityAndCoordinateSpacesSeparate") {
    CHECK((!std::is_convertible_v<PersistentId, SubjectRef>));
    CHECK((!std::is_convertible_v<SubjectRef, PersistentId>));
    CHECK((!std::is_convertible_v<GridPoint, WorldPoint>));

    const auto p2     = world2D(1.f, 2.f);
    const auto p3     = world3D(1.f, 2.f, 3.f);
    auto       circle = WorldArea::circle2D(p2, 4.f);
    REQUIRE(circle.ok());
    CHECK_EQ(circle.value().space(), CoordinateSpace::World2D);

    auto wrongCircle = WorldArea::circle2D(p3, 4.f);
    CHECK(!wrongCircle.ok());
    CHECK_EQ(wrongCircle.code(), eve::StatusCode::Rejected);

    const auto grid = GridPoint::grid2D(1, 2);
    CHECK_EQ(grid.space(), CoordinateSpace::Grid2D);
    CHECK_NE(grid.space(), p2.space());

    eve::sensing::TargetSet set;
    std::move(set.setPoint(p2)).expect("target point");
    auto gridArea = eve::sensing::GridArea::box2D(GridPoint::grid2D(0, 0), GridPoint::grid2D(2, 2));
    REQUIRE(gridArea.ok());
    auto mixedArea = set.setArea(std::move(gridArea).expect("grid area"));
    CHECK(!mixedArea.ok());
    CHECK_EQ(mixedArea.code(), eve::StatusCode::Rejected);
}

TEST_CASE("targeting.providerAndResolverApplyGenericConstraints") {
    ResetCapabilities        reset;
    SensingCandidateProvider provider;
    const auto               origin   = subject("00000000-0000-7000-8000-000000000001");
    const auto               enemy    = subject("00000000-0000-7000-8000-000000000002");
    const auto               farEnemy = subject("00000000-0000-7000-8000-000000000003");
    const auto               ally     = subject("00000000-0000-7000-8000-000000000004");
    auto                     zoneId   = LogicalId::parse("arena:central");
    REQUIRE(zoneId.has_value());
    auto zone = eve::sensing::ZoneRef::fromLogicalId(*zoneId);
    REQUIRE(zone.has_value());

    REQUIRE(
        provider.upsert(TargetCandidate{enemy, world2D(3.f, 4.f), TargetDomain::Enemy, {"unit", "visible"}, {*zone}})
            .ok());
    REQUIRE(provider.upsert(TargetCandidate{farEnemy, world2D(50.f, 0.f), TargetDomain::Enemy, {"unit"}, {}}).ok());
    REQUIRE(
        provider.upsert(TargetCandidate{ally, world2D(2.f, 0.f), TargetDomain::Ally, {"unit", "visible"}, {}}).ok());
    eve::cap::provide<ISensingCandidateProvider>(&provider);

    TargetingSpec spec;
    spec.space        = CoordinateSpace::World2D;
    spec.domain       = TargetDomain::Enemy;
    spec.minCount     = 1;
    spec.maxCount     = 2;
    spec.maxRange     = 10.f;
    spec.requiredTags = {"unit"};
    spec.zone         = *zone;
    spec.worldArea    = std::move(WorldArea::circle2D(world2D(0.f, 0.f), 10.f)).expect("area");

    TargetingQuery query{origin, world2D(0.f, 0.f), spec};
    auto           resolved = TargetingResolver{}.resolve(query);
    REQUIRE(resolved.ok());
    CHECK_EQ(resolved.value().subjects().size(), 1u);
    CHECK_EQ(resolved.value().subjects()[0], enemy);
    CHECK(!resolved.value().primary().has_value());
}

TEST_CASE("targeting.missingCapabilitiesAreUnsupported") {
    ResetCapabilities reset;
    TargetingQuery    query{subject("00000000-0000-7000-8000-000000000001"), world2D(0.f, 0.f), TargetingSpec{}};

    auto noCandidates = TargetingResolver{}.resolve(query);
    CHECK(!noCandidates.ok());
    CHECK_EQ(noCandidates.code(), eve::StatusCode::Unsupported);

    TargetingQuery mixed{query.origin, GridPoint::grid2D(0, 0), query.spec};
    auto           mixedResult = TargetingResolver{}.resolve(mixed);
    CHECK(!mixedResult.ok());
    CHECK_EQ(mixedResult.code(), eve::StatusCode::Rejected);
    REQUIRE(mixedResult.error());
    CHECK_EQ(mixedResult.error()->code(), eve::DiagnosticCode::InvalidArgument);

    SensingCandidateProvider provider;
    eve::cap::provide<ISensingCandidateProvider>(&provider);
    query.spec.lineOfSight = eve::sensing::LineOfSightMode::Required;
    auto noLos             = TargetingResolver{}.resolve(query);
    CHECK(!noLos.ok());
    CHECK_EQ(noLos.code(), eve::StatusCode::Unsupported);
}

TEST_CASE("targeting.losCapabilityIsInjectedAndNeverConvertsGridCoordinates") {
    ResetCapabilities        reset;
    SensingCandidateProvider provider;
    const auto               origin     = subject("00000000-0000-7000-8000-000000000001");
    const auto               target     = subject("00000000-0000-7000-8000-000000000002");
    const auto               gridTarget = subject("00000000-0000-7000-8000-000000000003");
    REQUIRE(provider.upsert(TargetCandidate{target, world3D(1.f, 0.f, 0.f), TargetDomain::Enemy, {}, {}}).ok());
    REQUIRE(provider.upsert(TargetCandidate{gridTarget, GridPoint::grid2D(1, 0), TargetDomain::Enemy, {}, {}}).ok());
    eve::cap::provide<ISensingCandidateProvider>(&provider);

    TargetingSpec spec;
    spec.space       = CoordinateSpace::World3D;
    spec.domain      = TargetDomain::Enemy;
    spec.minCount    = 1;
    spec.maxCount    = 1;
    spec.maxRange    = 10.f;
    spec.lineOfSight = eve::sensing::LineOfSightMode::Required;
    TargetingQuery query{origin, world3D(0.f, 0.f, 0.f), spec};

    VisibleLineOfSight hidden(false);
    eve::cap::provide<ILineOfSightQuery>(&hidden);
    auto hiddenResult = TargetingResolver{}.resolve(query);
    CHECK(!hiddenResult.ok());
    CHECK_EQ(hiddenResult.code(), eve::StatusCode::Rejected);
    REQUIRE(hiddenResult.error());
    CHECK_EQ(hiddenResult.error()->code(), eve::DiagnosticCode::PreconditionViolation);

    VisibleLineOfSight visible(true);
    eve::cap::provide<ILineOfSightQuery>(&visible);
    auto visibleResult = TargetingResolver{}.resolve(query);
    REQUIRE(visibleResult.ok());
    CHECK_EQ(visibleResult.value().subjects().size(), 1u);

    TargetingSpec gridSpec;
    gridSpec.space       = CoordinateSpace::Grid2D;
    gridSpec.minCount    = 0;
    gridSpec.maxCount    = 1;
    gridSpec.lineOfSight = eve::sensing::LineOfSightMode::Required;
    TargetingQuery gridQuery{origin, GridPoint::grid2D(0, 0), gridSpec};
    auto           gridResult = TargetingResolver{}.resolve(gridQuery);
    CHECK(!gridResult.ok());
    CHECK_EQ(gridResult.code(), eve::StatusCode::Unsupported);
}


TEST_CASE("targeting.sensingWorldProviderSharesFactsAndRequiresDomainRelation") {
    ResetCapabilities reset;
    SensingWorld      world;
    const auto        origin = subject("00000000-0000-7000-8000-000000000011");
    const auto        enemy  = subject("00000000-0000-7000-8000-000000000012");
    const auto        ally   = subject("00000000-0000-7000-8000-000000000013");

    REQUIRE(world.upsert(origin.format(), 0.f, 0.f, "blue", "unit", "").ok());
    REQUIRE(world.upsert(enemy.format(), 3.f, 4.f, "red", "unit", "").ok());
    REQUIRE(world.upsert(ally.format(), 2.f, 0.f, "blue", "unit", "").ok());

    SensingWorldCandidateProvider provider(world);
    eve::cap::provide<ISensingCandidateProvider>(&provider);

    TargetingSpec needsDomain;
    needsDomain.space        = CoordinateSpace::World2D;
    needsDomain.domain       = TargetDomain::Enemy;
    needsDomain.minCount     = 1;
    needsDomain.maxCount     = 2;
    needsDomain.maxRange     = 10.f;
    needsDomain.requiredTags = {"unit"};
    TargetingQuery blocked{origin, world2D(0.f, 0.f), needsDomain};
    auto           unsupported = TargetingResolver{}.resolve(blocked);
    CHECK(!unsupported.ok());
    CHECK_EQ(unsupported.code(), eve::StatusCode::Unsupported);

    provider.setFactionRelation(
        [](std::string_view originFaction, std::string_view candidateFaction) -> eve::Result<TargetDomain> {
            if (originFaction.empty() || candidateFaction.empty())
                return eve::Result<TargetDomain>::success(TargetDomain::Neutral);
            if (originFaction == candidateFaction) return eve::Result<TargetDomain>::success(TargetDomain::Ally);
            return eve::Result<TargetDomain>::success(TargetDomain::Enemy);
        });

    auto resolved = TargetingResolver{}.resolve(blocked);
    REQUIRE(resolved.ok());
    CHECK_EQ(resolved.value().subjects().size(), 1u);
    CHECK_EQ(resolved.value().subjects()[0], enemy);

    TargetingSpec truncate = needsDomain;
    truncate.domain        = TargetDomain::Any;
    truncate.maxCount      = 1;
    truncate.countPolicy   = CountPolicy::TruncateToMax;
    TargetingQuery nearest{origin, world2D(0.f, 0.f), truncate};
    auto           truncated = TargetingResolver{}.resolve(nearest);
    REQUIRE(truncated.ok());
    CHECK_EQ(truncated.value().subjects().size(), 1u);
    CHECK_EQ(truncated.value().subjects()[0], ally);
}

TEST_CASE("targeting.worldAreaCone2DContainsFacingSector") {
    auto apex = WorldPoint::world2D(0.f, 0.f);
    REQUIRE(apex.ok());
    auto cone = WorldArea::cone2D(apex.value(), 1.f, 0.f, 0.785398163f, 10.f);
    REQUIRE(cone.ok());
    CHECK(cone.value().contains(WorldPoint::world2D(5.f, 0.f).value()));
    CHECK(!cone.value().contains(WorldPoint::world2D(0.f, 5.f).value()));
    CHECK(!cone.value().contains(WorldPoint::world2D(-5.f, 0.f).value()));

    auto bad = WorldArea::cone2D(apex.value(), 0.f, 0.f, 0.5f, 10.f);
    CHECK(!bad.ok());
}

TEST_CASE("targeting.sensingWorldProviderHonorsZoneMembership") {
    ResetCapabilities reset;
    SensingWorld      world;
    const auto        origin  = subject("00000000-0000-7000-8000-000000000021");
    const auto        inside  = subject("00000000-0000-7000-8000-000000000022");
    const auto        outside = subject("00000000-0000-7000-8000-000000000023");

    REQUIRE(world.upsert(origin.format(), 0.f, 0.f, "blue", "unit", "").ok());
    REQUIRE(world.upsert(inside.format(), 3.f, 0.f, "red", "unit", "").ok());
    REQUIRE(world.upsert(outside.format(), 4.f, 0.f, "red", "unit", "").ok());
    REQUIRE(world.setZones(inside.format(), "arena:central").ok());
    REQUIRE(world.setZones(outside.format(), "arena:edge").ok());
    CHECK(!world.setZones(outside.format(), "not-a-logical-id").ok());

    auto zoneId = LogicalId::parse("arena:central");
    REQUIRE(zoneId.has_value());
    auto zone = eve::sensing::ZoneRef::fromLogicalId(*zoneId);
    REQUIRE(zone.has_value());

    SensingWorldCandidateProvider provider(world);
    eve::cap::provide<ISensingCandidateProvider>(&provider);
    provider.setFactionRelation(
        [](std::string_view, std::string_view) { return eve::Result<TargetDomain>::success(TargetDomain::Enemy); });

    TargetingSpec spec;
    spec.space        = CoordinateSpace::World2D;
    spec.domain       = TargetDomain::Enemy;
    spec.minCount     = 1;
    spec.maxCount     = 8;
    spec.maxRange     = 10.f;
    spec.requiredTags = {"unit"};
    spec.zone         = *zone;

    TargetingQuery query{origin, world2D(0.f, 0.f), spec};
    auto           resolved = TargetingResolver{}.resolve(query);
    REQUIRE(resolved.ok());
    CHECK_EQ(resolved.value().subjects().size(), 1u);
    CHECK_EQ(resolved.value().subjects()[0], inside);
}
}  // namespace test_targeting_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_water_mesh)
#include <cmath>
#include <limits>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include "Fixtures.h"
#include "PrivateTestScope.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/WaterPlanarReflection.h"
#include "graphics/WaterReflectionMasker.h"
#include "graphics/WaterStyleConfig.h"
#include "graphics/WaterSystem.h"
#include "graphics/WaterUnderwaterEffects.h"
#include "image/ImageData.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// water_mesh.cpp
namespace test_water_mesh_unit {
// water_mesh.cpp
using namespace eve::graphics;

TEST_CASE("graphics.WaterMesh.pcgPlaneAndCircleCounts") {
    WaterMeshSettings plane;
    plane.sizeX         = 8.0F;
    plane.sizeZ         = 4.0F;
    plane.densityX      = 40.0F;
    plane.densityY      = 40.0F;
    auto planeTriangles = calculateWaterMeshTriangles(plane);
    REQUIRE(static_cast<bool>(planeTriangles));
    CHECK(planeTriangles.value() == 64);

    WaterMeshSettings circle;
    circle.type          = WaterMeshType::Circle;
    circle.sizeX         = 8.0F;
    circle.sizeZ         = 8.0F;
    circle.densityX      = 40.0F;
    circle.densityY      = 40.0F;
    auto circleTriangles = calculateWaterMeshTriangles(circle);
    REQUIRE(static_cast<bool>(circleTriangles));
    CHECK(circleTriangles.value() == 96);
}

TEST_CASE("graphics.WaterMesh.rejectsInvalidAndExcessiveSettings") {
    WaterMeshSettings settings;
    settings.sizeX = 0.0F;
    CHECK(!calculateWaterMeshTriangles(settings));
    settings.sizeX = 100000.0F;
    CHECK(!calculateWaterMeshTriangles(settings));
    settings.sizeX = 8.0F;
    settings.type  = static_cast<WaterMeshType>(9);
    CHECK(!calculateWaterMeshTriangles(settings));
}


// water_planar_reflection.cpp
using namespace eve::graphics;

TEST_CASE("graphics.WaterPlanarReflection.pcgMirrorResolutionMaskAndDistances") {
    WaterPlanarReflectionSettings settings;
    settings.textureResolution    = 600;
    settings.resolutionMultiplier = WaterReflectionResolution::Third;
    settings.clipPlaneOffset      = 0.2F;
    settings.reflectLayers        = 0xffffffffU;
    settings.shadows              = true;
    settings.enableRenderDistance = true;
    settings.customRenderDistance = 275.0F;

    WaterPlanarReflectionInput input;
    input.cameraPosition = {3.0F, 10.0F, -4.0F};
    input.cameraForward  = {0.0F, -0.6F, -0.8F};
    input.waterPlaneY    = 2.0F;
    input.renderScale    = 0.8F;

    auto result = buildWaterPlanarReflectionPlan(settings, input);
    REQUIRE(static_cast<bool>(result));
    const auto& plan = result.value();
    CHECK(plan.shouldRender);
    CHECK(plan.renderShadows);
    CHECK(plan.textureWidth == 158);  // Pcg truncates 600 * 0.8 * 0.33.
    CHECK(plan.textureHeight == 158);
    CHECK(std::abs(plan.cameraPosition.y - (-5.6F)) < 0.0001F);
    CHECK(std::abs(plan.cameraForward.y - 0.6F) < 0.0001F);
    CHECK(plan.reflectionMatrix[1][1] == -1.0F);
    CHECK(std::abs(plan.reflectionMatrix[3][1] - 4.4F) < 0.0001F);
    CHECK((plan.cullingMask & (1U << 4U)) == 0U);
    CHECK(plan.layerCullDistances[0] == 275.0F);
    CHECK(plan.layerCullDistances[31] == 275.0F);
}

TEST_CASE("graphics.WaterPlanarReflection.skipRulesPerLayerAndInvalidInput") {
    WaterPlanarReflectionSettings settings;
    settings.enableRenderDistance     = true;
    settings.enablePerLayerDistances  = true;
    settings.customRenderDistances[7] = 91.0F;
    WaterPlanarReflectionInput input;
    input.orthographic = true;

    auto result = buildWaterPlanarReflectionPlan(settings, input);
    REQUIRE(static_cast<bool>(result));
    CHECK(!result.value().shouldRender);
    CHECK(result.value().layerCullDistances[7] == 91.0F);
    CHECK(result.value().layerCullDistances[6] == 0.0F);

    settings.customRenderDistances[4] = std::numeric_limits<float>::quiet_NaN();
    auto invalid                      = buildWaterPlanarReflectionPlan(settings, input);
    CHECK(!invalid);
}

// water_reflection_masker.cpp
using namespace eve::graphics;

TEST_CASE("graphics.waterReflectionMasker.samplesPcgRoundedCoordinatesAndChannels") {
    eve::image::ImageData mask(2, 2, "RGBA8");
    mask.setPixel(0, 0, {.1f, .2f, .3f, .4f});
    mask.setPixel(1, 0, {.6f, .1f, .1f, .1f});
    mask.setPixel(0, 1, {.1f, .7f, .1f, .1f});
    mask.setPixel(1, 1, {.1f, .1f, .8f, .1f});
    WaterReflectionMasker         masker;
    WaterPlanarReflectionSettings settings;
    settings.enabled = false;
    REQUIRE(masker.configure(WaterReflectionMaskChannel::R, .5f, .7f, false).ok());
    REQUIRE(masker.setMask(mask).ok());
    auto enabled = masker.evaluate(25, 10, 0, 0, 100, 100, settings);
    REQUIRE(enabled.ok());
    CHECK_EQ(enabled.value(), WaterReflectionMaskTransition::Enabled);
    CHECK(settings.enabled);
    CHECK_EQ(masker.getSampleX(), 1);
    CHECK_EQ(masker.getSampleY(), 0);
    auto disabled = masker.evaluate(75, 75, 0, 0, 100, 100, settings);
    REQUIRE(disabled.ok());
    CHECK_EQ(disabled.value(), WaterReflectionMaskTransition::Disabled);
    CHECK(!settings.enabled);
    REQUIRE(masker.configure(WaterReflectionMaskChannel::RGBA, .75f, .85f, false).ok());
    REQUIRE(masker.evaluate(75, 75, 0, 0, 100, 100, settings).ok());
    CHECK(settings.enabled);
}

TEST_CASE("graphics.waterReflectionMasker.handlesMissingEdgesAndFailureAtomically") {
    eve::image::ImageData mask(1, 1, "RGBA8");
    mask.setPixel(0, 0, {1, 0, 0, 1});
    WaterReflectionMasker         masker;
    WaterPlanarReflectionSettings settings;
    settings.enabled = true;
    REQUIRE(masker.configure(WaterReflectionMaskChannel::R, .9f, 1.f, true).ok());
    REQUIRE(masker.setMask(mask).ok());
    auto unchanged = masker.evaluate(0, 0, 0, 0, 100, 100, settings);
    REQUIRE(unchanged.ok());
    CHECK_EQ(unchanged.value(), WaterReflectionMaskTransition::Unchanged);
    auto edge = masker.evaluate(100, 100, 0, 0, 100, 100, settings);
    REQUIRE(edge.ok());
    CHECK_EQ(edge.value(), WaterReflectionMaskTransition::Disabled);
    CHECK_EQ(masker.getSampleX(), 1);
    CHECK(!masker.evaluate(0, 0, 0, 0, -1, 1, settings).ok());
    CHECK(!settings.enabled);
    CHECK(!masker.configure(WaterReflectionMaskChannel::R, 2, 1, false).ok());
}


// water_style_config.cpp
using eve::DiagnosticCode;
using eve::graphics::WaterStyleConfig;

TEST_CASE("graphics.waterStyleConfig.roundTrip") {
    WaterStyleConfig authored;
    authored.deepColor                     = {0.01F, 0.08F, 0.18F};
    authored.shallowColor                  = {0.18F, 0.72F, 0.64F};
    authored.waveSharpness                 = 2.25F;
    authored.depthDistance                 = 7.5F;
    authored.foamStrength                  = 1.2F;
    authored.screenSpaceReflection         = true;
    authored.screenSpaceReflectionStrength = 0.7F;
    authored.refractionStrength            = 0.04F;
    authored.causticsStrength              = 0.65F;
    authored.causticsScale                 = 4.5F;

    auto json = authored.toJson();
    REQUIRE(static_cast<bool>(json));
    auto decoded = WaterStyleConfig::fromJson(json.value());
    REQUIRE(static_cast<bool>(decoded));

    CHECK(std::abs(decoded.value().deepColor.z - 0.18F) < 0.0001F);
    CHECK(std::abs(decoded.value().shallowColor.y - 0.72F) < 0.0001F);
    CHECK(std::abs(decoded.value().waveSharpness - 2.25F) < 0.0001F);
    CHECK(std::abs(decoded.value().depthDistance - 7.5F) < 0.0001F);
    CHECK(std::abs(decoded.value().foamStrength - 1.2F) < 0.0001F);
    CHECK(decoded.value().screenSpaceReflection);
    CHECK(std::abs(decoded.value().refractionStrength - 0.04F) < 0.0001F);
    CHECK(std::abs(decoded.value().causticsStrength - 0.65F) < 0.0001F);
    CHECK(std::abs(decoded.value().causticsScale - 4.5F) < 0.0001F);
}

TEST_CASE("graphics.waterStyleConfig.rejectsUnknownFields") {
    WaterStyleConfig config;
    auto             encoded = config.toValue();
    REQUIRE(static_cast<bool>(encoded));

    auto* root   = encoded.value().getIf<eve::Value::Object>();
    auto* colors = root->at("colors").getIf<eve::Value::Object>();
    colors->emplace("surprise", 1.0);
    auto decoded = WaterStyleConfig::fromValue(encoded.value());
    CHECK(!decoded);
    REQUIRE(decoded.error() != nullptr);
    CHECK(decoded.error()->code() == DiagnosticCode::InvalidArgument);
}

TEST_CASE("graphics.waterStyleConfig.rejectsFutureVersionsAndInvalidRanges") {
    WaterStyleConfig config;
    auto             encoded = config.toValue();
    REQUIRE(static_cast<bool>(encoded));

    auto* root                = encoded.value().getIf<eve::Value::Object>();
    root->at("schemaVersion") = static_cast<std::int64_t>(WaterStyleConfig::SchemaVersion + 1);
    auto future               = WaterStyleConfig::fromValue(encoded.value());
    CHECK(!future);
    REQUIRE(future.error() != nullptr);
    CHECK(future.error()->code() == DiagnosticCode::UnknownVersion);

    config.depthDistance = 0.0F;
    auto invalid         = config.validate();
    CHECK(!invalid);
}

// water_system.cpp
using namespace eve::graphics;

TEST_CASE("graphics.WaterSystem.infiniteIntervalAndSeaLevel") {
    WaterSystemState     state;
    WaterSystemSettings  settings;
    WaterSceneConditions scene;
    auto                 initialized = initializeWaterSystem(state, settings, scene, 7);
    REQUIRE(static_cast<bool>(initialized));

    auto first = advanceWaterSystem(state, settings, scene, 4.0F, -3.0F, 0.25F, 9);
    REQUIRE(static_cast<bool>(first));
    CHECK(!first.value());
    CHECK(state.positionX == 4.0F);
    CHECK(state.positionY == 25.0F);
    CHECK(state.positionZ == -3.0F);

    auto second = advanceWaterSystem(state, settings, scene, 8.0F, 2.0F, 0.3F, 9);
    REQUIRE(static_cast<bool>(second));
    CHECK(second.value());
    CHECK(state.refreshRevision == 1);

    auto level = updateWaterSeaLevel(state, 31.5F, true);
    REQUIRE(static_cast<bool>(level));
    CHECK(level.value());
    CHECK(state.positionY == 31.5F);
    CHECK(state.refreshRevision == 2);
}

TEST_CASE("graphics.WaterSystem.sceneConditionsAndAtomicFailure") {
    WaterSystemState    state;
    WaterSystemSettings settings;
    settings.autoUpdateMode        = WaterAutoUpdateMode::SceneConditions;
    settings.ignoreSceneConditions = false;
    WaterSceneConditions scene;
    auto                 initialized = initializeWaterSystem(state, settings, scene, 0);
    REQUIRE(static_cast<bool>(initialized));

    WaterSceneConditions changedScene = scene;
    changedScene.sunIntensity         = 2.0F;
    auto refresh                      = advanceWaterSystem(state, settings, changedScene, 0.0F, 0.0F, 0.0F, 8);
    REQUIRE(static_cast<bool>(refresh));
    CHECK(refresh.value());
    CHECK(state.sceneCheckFrames == 7);

    const WaterSystemState before  = state;
    auto                   invalid = advanceWaterSystem(state, settings, changedScene, 0.0F, 0.0F, -1.0F, 8);
    CHECK(!invalid);
    CHECK(state.refreshRevision == before.refreshRevision);
    CHECK(state.positionX == before.positionX);
    CHECK(state.sceneCheckFrames == before.sceneCheckFrames);
}

// water_underwater_effects.cpp
using namespace eve::graphics;

namespace {
[[maybe_unused]] UnderwaterColorGradient depthGradient() {
    UnderwaterColorGradient gradient;
    REQUIRE(static_cast<bool>(gradient.addStop(0.0F, 0.4F, 0.7F, 1.0F)));
    REQUIRE(static_cast<bool>(gradient.addStop(1.0F, 0.03F, 0.08F, 0.18F)));
    return gradient;
}
[[maybe_unused]] UnderwaterColorGradient timeGradient() {
    UnderwaterColorGradient gradient;
    REQUIRE(static_cast<bool>(gradient.addStop(0.0F, 0.1F, 0.2F, 0.4F)));
    REQUIRE(static_cast<bool>(gradient.addStop(1.0F, 0.8F, 0.5F, 0.2F)));
    return gradient;
}
[[maybe_unused]] UnderwaterScalarCurve exposureCurve() {
    UnderwaterScalarCurve curve;
    REQUIRE(static_cast<bool>(curve.addKey(0.0F, -0.5F)));
    REQUIRE(static_cast<bool>(curve.addKey(1.0F, 0.5F)));
    return curve;
}
}  // namespace


TEST_CASE("graphics.WaterUnderwater.transitionsFogCausticsAndSurfaceRestore") {
    WaterUnderwaterSettings settings;
    settings.causticTextureCount = 16;
    settings.fogColorMultiplier  = {0.1F, -1.0F, 0.0F};
    WaterUnderwaterInput input;
    input.seaLevel       = 50.0F;
    input.cameraY        = 60.0F;
    input.mainLightColor = {0.7F, 0.8F, 0.9F};
    WaterUnderwaterState  state;
    WaterUnderwaterOutput output;
    const auto            depth     = depthGradient();
    const auto            time      = timeGradient();
    const auto            exposure  = exposureCurve();
    const auto            postColor = timeGradient();

    WaterUnderwaterState  transitionState;
    WaterUnderwaterOutput transitionOutput;
    WaterUnderwaterInput  transitionInput = input;
    transitionInput.cameraY               = 50.04F;
    REQUIRE(static_cast<bool>(advanceWaterUnderwaterEffects(transitionState, transitionOutput, settings,
                                                            transitionInput, depth, time, exposure, postColor)));
    transitionInput.cameraY = 49.96F;
    REQUIRE(static_cast<bool>(advanceWaterUnderwaterEffects(transitionState, transitionOutput, settings,
                                                            transitionInput, depth, time, exposure, postColor)));
    CHECK(transitionOutput.entered);
    CHECK(std::abs(transitionOutput.transitionWeight - 0.4F) < 0.0002F);
    CHECK(std::abs(transitionOutput.transitionVignette - 0.1F) < 0.0002F);
    CHECK(std::abs(transitionOutput.transitionVignetteSmoothness - 0.44F) < 0.0002F);
    CHECK(std::abs(transitionOutput.transitionLensDistortion - 0.1008F) < 0.0002F);
    CHECK(std::abs(transitionOutput.transitionLensScale - 1.008F) < 0.0002F);
    CHECK(std::abs(transitionOutput.transitionLift.r - settings.transitionLift.r * 0.4F) < 0.0002F);
    CHECK(std::abs(transitionOutput.transitionInverseGamma.r -
                   (1.0F + (settings.transitionInverseGamma.r - 1.0F) * 0.4F)) < 0.0002F);
    CHECK(std::abs(transitionOutput.transitionGain.r - (1.0F + (settings.transitionGain.r - 1.0F) * 0.4F)) < 0.0002F);
    CHECK(std::abs(transitionOutput.transitionColorFilter.r - 0.6F) < 0.0002F);

    REQUIRE(static_cast<bool>(
        advanceWaterUnderwaterEffects(state, output, settings, input, depth, time, exposure, postColor)));
    CHECK(!output.isUnderwater);
    CHECK(output.surfaceVfx);

    input.cameraY      = 25.0F;
    input.causticTicks = 18;
    REQUIRE(static_cast<bool>(
        advanceWaterUnderwaterEffects(state, output, settings, input, depth, time, exposure, postColor)));
    CHECK(output.entered);
    CHECK(output.playSubmergeDown);
    CHECK(output.loopAudio);
    CHECK(output.particles);
    CHECK(output.horizon);
    CHECK(output.causticFrame == 2);
    CHECK(std::abs(output.depth01 - 0.25F) < 0.0001F);
    CHECK(std::abs(output.fogColor.r - 0.4075F) < 0.0001F);
    CHECK(std::abs(output.fogColor.g - 0.545F) < 0.0001F);
    CHECK(output.underwaterMaterialColor == input.mainLightColor);

    input.cameraY      = 51.0F;
    input.causticTicks = 0;
    REQUIRE(static_cast<bool>(
        advanceWaterUnderwaterEffects(state, output, settings, input, depth, time, exposure, postColor)));
    CHECK(output.exited);
    CHECK(output.playSubmergeUp);
    CHECK(!output.loopAudio);
    CHECK(output.surfaceVfx);
    CHECK(state.causticFrame == 0);
}

TEST_CASE("graphics.WaterUnderwater.hdrpOverrideAndAtomicFailure") {
    WaterUnderwaterSettings settings;
    settings.hdrp                  = true;
    settings.overrideFogColor      = true;
    settings.timeDrivenPostFx      = true;
    settings.overrideFogMultiplier = {0.5F, 0.8F, 1.0F};
    settings.overrideFogCurve      = 0.75F;
    WaterUnderwaterInput input;
    input.seaLevel     = 100.0F;
    input.cameraY      = 0.0F;
    input.timeOfDay    = 0.5F;
    input.hasMainLight = false;
    WaterUnderwaterState  state;
    WaterUnderwaterOutput output;
    const auto            depth     = depthGradient();
    const auto            time      = timeGradient();
    const auto            exposure  = exposureCurve();
    const auto            postColor = timeGradient();

    REQUIRE(static_cast<bool>(
        advanceWaterUnderwaterEffects(state, output, settings, input, depth, time, exposure, postColor)));
    CHECK(output.isUnderwater);
    CHECK(!output.horizon);
    CHECK(output.hdrpBaseHeight == 6000.0F);
    CHECK(output.hdrpMeanFreePath == 1.0F);
    CHECK(output.hdrpProbeDimmer == 0.325F);
    CHECK(output.hdrpDepthExtent == 300.0F);
    CHECK(std::abs(output.fogColor.r - 0.028125F) < 0.0001F);
    CHECK(std::abs(output.postExposure) < 0.0001F);
    CHECK(std::abs(output.postColor.r - 0.45F) < 0.0001F);
    CHECK(std::abs(output.underwaterMaterialColor.r - 207.0F / 255.0F) < 0.0001F);

    const WaterUnderwaterState  beforeState  = state;
    const WaterUnderwaterOutput beforeOutput = output;
    input.cameraY                            = std::numeric_limits<float>::quiet_NaN();
    auto invalid = advanceWaterUnderwaterEffects(state, output, settings, input, depth, time, exposure, postColor);
    CHECK(!invalid);
    CHECK(state.isUnderwater == beforeState.isUnderwater);
    CHECK(output.hdrpBaseHeight == beforeOutput.hdrpBaseHeight);
}
}  // namespace test_water_mesh_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_force_models)
#include "physics/forces/ForceModels.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>

namespace test_physics_force_models {
using namespace eve::physics;
namespace {
template <class T>
T take(eve::Result<T> result) {
    const bool ok = result.ok();
    REQUIRE(ok);
    return std::move(result).takeValue();
}
}  // namespace
TEST_CASE("physics.forces.directionAndRegionAreMassScaled") {
    DirectionalField field{{{}, 10, 2}, {0, 5, 0}};
    CHECK_EQ(take(ForceModels::directional(field, {}, 2)).y, 10);
    CHECK_EQ(take(ForceModels::directional(field, {9, 0, 0}, 2)).y, 5);
    CHECK_EQ(take(ForceModels::directional(field, {10, 0, 0}, 2)).y, 0);
    CHECK_EQ(take(ForceModels::directional(field, {0, 0, 11}, 2)).y, 0);
}
TEST_CASE("physics.forces.radialSoftCoreAndRepulsion") {
    RadialField field;
    field.strength = 10;
    auto center    = take(ForceModels::radial(field, {}, 2));
    CHECK_EQ(center.x, 0);
    auto a = take(ForceModels::radial(field, {2, 0, 0}, 2));
    CHECK_LT(a.x, 0);
    CHECK(std::abs(a.x + 20 * 2 / std::pow(4.01, 1.5)) < 1e-10);
    field.strength = -10;
    auto b         = take(ForceModels::radial(field, {0, 0, 2}, 2));
    CHECK(std::abs(a.x + b.z) < 1e-10);
    auto near = take(ForceModels::radial(field, {1e-12, 0, 0}, 1));
    CHECK(std::isfinite(near.x));
}
TEST_CASE("physics.forces.buoyancyDisplacedVolume") {
    BuoyancyModel m;
    m.displacedVolume = 0.25;
    CHECK(std::abs(take(ForceModels::buoyancy(m)).y - 2452.5) < 1e-8);
    m.displacedVolume = 0;
    CHECK_EQ(take(ForceModels::buoyancy(m)).y, 0);
}
TEST_CASE("physics.forces.dampingDissipatesWithoutOvershoot") {
    for (double dt : {0.001, 0.02, 10.0}) {
        const auto force = take(ForceModels::damping({10, 2}, {20, -10, 5}, 2, dt));
        CHECK_LT(force.x, 0);
        CHECK_GT(force.y, 0);
        CHECK(20 + force.x * dt / 2 >= 0);
        CHECK(-10 + force.y * dt / 2 <= 0);
        CHECK(5 + force.z * dt / 2 >= 0);
    }
    CHECK_EQ(take(ForceModels::damping({}, {10, 0, 0}, 1, 0.1)).x, 0);
    CHECK_EQ(take(ForceModels::damping({3, 4}, {}, 1, 0.1)).x, 0);
}
TEST_CASE("physics.forces.wingLiftDragStallAndWind") {
    WingModel  m;
    const auto level = take(ForceModels::wing(m, {20, 0, 0}));
    CHECK_EQ(level.force.y, 0);
    CHECK_LT(level.force.x, 0);
    const auto positive = take(ForceModels::wing(m, {20, -2, 0}));
    const auto negative = take(ForceModels::wing(m, {20, 2, 0}));
    CHECK_GT(positive.force.y, 0);
    CHECK_LT(negative.force.y, 0);
    CHECK(std::abs(positive.force.y + negative.force.y) < 1e-8);
    CHECK(positive.force.x * 20 - positive.force.y * 2 < 0);
    const auto fast = take(ForceModels::wing(m, {40, -4, 0}));
    CHECK(std::abs(fast.force.y / positive.force.y - 4) < 1e-10);
    const auto stall  = take(ForceModels::wing(m, {20 * std::cos(m.stallAngle), -20 * std::sin(m.stallAngle), 0}));
    const auto beyond = take(ForceModels::wing(m, {10, -17.320508, 0}));
    CHECK_LT(beyond.liftCoefficient, stall.liftCoefficient);
    CHECK_GT(beyond.dragCoefficient, stall.dragCoefficient);
    CHECK_EQ(take(ForceModels::wing(m, {})).force.x, 0);
    CHECK_EQ(take(ForceModels::wing(m, {0, 0, 100})).force.x, 0);
}
TEST_CASE("physics.forces.frictionStickSlipAndNoContact") {
    FrictionModel m{0.9, 0.7};
    CHECK_EQ(take(ForceModels::friction(m, {}, {8, 0, 0}, 10)).x, -8);
    CHECK_EQ(take(ForceModels::friction(m, {}, {10, 0, 0}, 10)).x, -7);
    const auto sliding = take(ForceModels::friction(m, {3, 4, 0}, {}, 10));
    CHECK(std::abs(std::hypot(sliding.x, sliding.y) - 7) < 1e-10);
    CHECK(sliding.x * 3 + sliding.y * 4 < 0);
    CHECK_EQ(take(ForceModels::friction(m, {3, 4, 0}, {}, 0)).x, 0);
}
TEST_CASE("physics.forces.tireCombinedSlipAndWheelTorque") {
    TireModel m;
    TireInput in;
    in.normalLoad           = 1000;
    in.longitudinalVelocity = 10;
    in.lateralVelocity      = 3;
    in.angularVelocity      = 50;
    auto out                = take(ForceModels::tire(m, in, 0.001));
    CHECK_GT(out.longitudinalForce, 0);
    CHECK_LT(out.lateralForce, 0);
    CHECK(std::hypot(out.longitudinalForce, out.lateralForce) <= 1000.000001);
    CHECK_EQ(out.reactionTorque, -out.longitudinalForce * m.radius);
    CHECK_LT(out.angularVelocity, in.angularVelocity);
    in.longitudinalVelocity = -10;
    in.lateralVelocity      = -3;
    in.angularVelocity      = -50;
    auto reverse            = take(ForceModels::tire(m, in, 0.001));
    CHECK(std::abs(reverse.longitudinalForce + out.longitudinalForce) < 1e-8);
    CHECK(std::abs(reverse.angularVelocity + out.angularVelocity) < 1e-8);
}
TEST_CASE("physics.forces.tireAirborneFreeRollingAndBrakeLock") {
    TireModel m;
    TireInput in;
    in.angularVelocity = 10;
    in.driveTorque     = 5;
    auto airborne      = take(ForceModels::tire(m, in, 0.1));
    CHECK_EQ(airborne.longitudinalForce, 0);
    CHECK_EQ(airborne.angularVelocity, 10.5);
    in.brakeTorque = 1e6;
    CHECK_EQ(take(ForceModels::tire(m, in, 0.1)).angularVelocity, 0);
    in                      = {};
    in.normalLoad           = 1000;
    in.longitudinalVelocity = 3;
    in.angularVelocity      = 10;
    auto rolling            = take(ForceModels::tire(m, in, 0.01));
    // Rolling resistance now transfers drag to the chassis in the same coupled step.
    CHECK_LT(rolling.slipRatio, 0);
    CHECK_LT(rolling.longitudinalForce, 0);
    CHECK_LT(rolling.resistanceTorque, 0);
    CHECK_LT(rolling.angularVelocity, 10);
    m.rollingResistance = 0;
    auto freeRolling    = take(ForceModels::tire(m, in, 0.01));
    CHECK_EQ(freeRolling.slipRatio, 0);
    CHECK_EQ(freeRolling.longitudinalForce, 0);
    CHECK_EQ(freeRolling.angularVelocity, 10);
}
TEST_CASE("physics.forces.invalidAndOverflowInputsReject") {
    const double nan       = std::numeric_limits<double>::quiet_NaN();
    auto         direction = ForceModels::directional({}, {}, 0);
    REQUIRE(!direction.ok());
    RadialField radial;
    radial.softening = 0;
    REQUIRE(!ForceModels::radial(radial, {}, 1).ok());
    BuoyancyModel buoyancy;
    buoyancy.displacedVolume = -1;
    REQUIRE(!ForceModels::buoyancy(buoyancy).ok());
    REQUIRE(!ForceModels::damping({}, {nan, 0, 0}, 1, 0.1).ok());
    REQUIRE(!ForceModels::damping({}, {}, 1, 0).ok());
    WingModel wing;
    wing.normal = wing.chord;
    REQUIRE(!ForceModels::wing(wing, {}).ok());
    REQUIRE(!ForceModels::friction({0.1, 0.9}, {}, {}, 1).ok());
    TireInput input;
    input.brakeTorque = -1;
    REQUIRE(!ForceModels::tire({}, input, 0.1).ok());
    input            = {};
    input.normalLoad = nan;
    REQUIRE(!ForceModels::tire({}, input, 0.1).ok());
    REQUIRE(!ForceModels::directional({{}, {1e308, 0, 0}}, {}, 1e308).ok());
}

TEST_CASE("physics.forces.constantDirectionAndRadialCaps") {
    DirectionalForceField constant{{{}, 10, 2}, {0, 12, 0}};
    CHECK_EQ(take(ForceModels::directionalForce(constant, {})).y, 12);
    CHECK_EQ(take(ForceModels::directionalForce(constant, {9, 0, 0})).y, 6);
    RadialField radial;
    radial.strength        = 1e300;
    radial.softening       = 1e-300;
    radial.maxAcceleration = 3;
    radial.maxForce        = 10;
    auto light             = take(ForceModels::radial(radial, {1e-300, 0, 0}, 2));
    CHECK(std::abs(light.x + 6) < 1e-10);
    auto heavy = take(ForceModels::radial(radial, {0, 0, 1e-300}, 100));
    CHECK(std::abs(heavy.z + 10) < 1e-10);
    radial.strength = -1e300;
    auto repelled   = take(ForceModels::radial(radial, {1e-320, 0, 0}, 2));
    CHECK(std::abs(repelled.x - 6) < 1e-10);
    radial.maxForce = 0;
    CHECK_EQ(take(ForceModels::radial(radial, {1, 0, 0}, 2)).x, 0);
    radial.maxForce = -1;
    CHECK(!ForceModels::radial(radial, {1, 0, 0}, 2).ok());
    radial.maxForce        = 1;
    radial.maxAcceleration = std::numeric_limits<double>::infinity();
    CHECK(!ForceModels::radial(radial, {1, 0, 0}, 2).ok());
}

TEST_CASE("physics.forces.boxAndRectangleRegionFade") {
    DirectionalForceField field;
    field.force              = {10, 0, 0};
    field.region.kind        = FieldRegionKind::Box;
    field.region.center      = {10, 20, 30};
    field.region.halfExtents = {2, 3, 4};
    field.region.edgeWidth   = 1;
    CHECK_EQ(take(ForceModels::directionalForce(field, {10, 20, 30})).x, 10);
    CHECK_EQ(take(ForceModels::directionalForce(field, {11.5, 20, 30})).x, 5);
    CHECK_EQ(take(ForceModels::directionalForce(field, {10, 22.5, 30})).x, 5);
    CHECK_EQ(take(ForceModels::directionalForce(field, {10, 20, 33.5})).x, 5);
    CHECK_EQ(take(ForceModels::directionalForce(field, {12, 20, 30})).x, 0);
    CHECK_EQ(take(ForceModels::directionalForce(field, {10, 20, 35})).x, 0);
    field.region.kind          = FieldRegionKind::RectangleXY;
    field.region.halfExtents.z = 0;
    CHECK_EQ(take(ForceModels::directionalForce(field, {10, 20, 3000})).x, 10);
    CHECK_EQ(take(ForceModels::directionalForce(field, {8.5, 20, 0})).x, 5);
    field.region.edgeWidth = 3;
    CHECK(!ForceModels::directionalForce(field, {}).ok());
    field.region.kind = static_cast<FieldRegionKind>(99);
    CHECK(!ForceModels::directionalForce(field, {}).ok());
    field.region.kind = FieldRegionKind::Global;
    CHECK_EQ(take(ForceModels::directionalForce(field, {1000, 1000, 1000})).x, 10);
}

TEST_CASE("physics.forces.angularDampingDissipatesRelativeRotation") {
    AngularDampingModel model{10, 2};
    for (double dt : {.001, .02, 10.0}) {
        auto scalar = ForceModels::angularDamping2D(model, -4, 3, dt);
        REQUIRE(scalar.ok());
        const double next = -4 + scalar.value() * dt / 3;
        CHECK(next >= -4);
        CHECK(next <= 0);
        ForceVector w{4, -2, 1}, inertia{1, 3, 9};
        auto        torque = ForceModels::angularDamping3D(model, w, inertia, dt);
        REQUIRE(torque.ok());
        const auto  t = torque.value();
        ForceVector n{w.x + t.x * dt / inertia.x, w.y + t.y * dt / inertia.y, w.z + t.z * dt / inertia.z};
        CHECK(n.x >= 0);
        CHECK(n.y <= 0);
        CHECK(n.z >= 0);
        CHECK(n.x * n.x + 3 * n.y * n.y + 9 * n.z * n.z <= 37);
        CHECK(t.x * w.x + t.y * w.y + t.z * w.z <= 0);
    }
    // A faster rotating medium spins the body up while reducing body-minus-medium rotation.
    auto medium = ForceModels::angularDamping2D(model, 2 - 3, 1, .1);
    REQUIRE(medium.ok());
    CHECK(medium.value() > 0);
    CHECK(!ForceModels::angularDamping2D(model, 1, 0, .1).ok());
    CHECK(!ForceModels::angularDamping3D(model, {1, 2, 3}, {1, -1, 1}, .1).ok());
    CHECK(!ForceModels::angularDamping3D(model, {1, 2, 3}, {1, 1, 1}, 0).ok());
    auto zero = ForceModels::angularDamping3D({}, {1, 2, 3}, {1, 1, 1}, 1);
    REQUIRE(zero.ok());
    CHECK_EQ(zero.value().x, 0);
    CHECK_EQ(zero.value().y, 0);
    CHECK_EQ(zero.value().z, 0);
}
}  // namespace test_physics_force_models
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_tire_stability)
#include <cmath>
#include <limits>
#include "physics/forces/ForceModels.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_physics_tire_stability {
using namespace eve::physics;

TEST_CASE("physics.forces.tireStiffLowSpeedConservesImpulseAndDissipates") {
    TireModel model;
    model.longitudinalStiffness = 1e8;
    model.corneringStiffness    = 1e8;
    model.rollingResistance     = 0;
    for (double dt : {.001, .01, .1, 1.0}) {
        TireInput input;
        input.angularVelocity         = .1;
        input.lateralVelocity         = .02;
        input.normalLoad              = 1000;
        input.longitudinalInverseMass = .01;
        input.lateralInverseMass      = .01;
        auto sampled                  = ForceModels::tire(model, input, dt);
        REQUIRE(sampled.ok());
        const auto   s  = sampled.value();
        const double vx = s.longitudinalForce * dt * .01, vy = .02 + s.lateralForce * dt * .01;
        CHECK(vx >= 0);
        CHECK(vy >= -1e-12);
        CHECK(s.angularVelocity >= 0);
        CHECK(model.radius * s.angularVelocity - vx >= -1e-12);
        CHECK(std::abs(100 * vx + model.inertia * s.angularVelocity / model.radius -
                       model.inertia * .1 / model.radius) < 1e-10);
        CHECK(100 * (vx * vx + vy * vy) + model.inertia * s.angularVelocity * s.angularVelocity <=
              100 * .02 * .02 + model.inertia * .1 * .1 + 1e-12);
        CHECK(std::hypot(s.longitudinalForce, s.lateralForce) <= model.friction * input.normalLoad + 1e-8);
    }
}
TEST_CASE("physics.forces.tireDriveAndBrakeSolveTogether") {
    TireModel model;
    model.rollingResistance = 0;
    TireInput input;
    input.normalLoad              = 1000;
    input.longitudinalInverseMass = .01;
    input.driveTorque             = 10;
    auto driven                   = ForceModels::tire(model, input, .02);
    REQUIRE(driven.ok());
    CHECK(driven.value().longitudinalForce > 0);
    CHECK(driven.value().angularVelocity > 0);
    CHECK(std::abs(driven.value().angularVelocity -
                   (input.driveTorque + driven.value().reactionTorque + driven.value().resistanceTorque) * .02 /
                       model.inertia) < 1e-10);
    input.driveTorque = -10;
    auto reverse      = ForceModels::tire(model, input, .02);
    REQUIRE(reverse.ok());
    CHECK(std::abs(reverse.value().longitudinalForce + driven.value().longitudinalForce) < 1e-10);
    CHECK(std::abs(reverse.value().angularVelocity + driven.value().angularVelocity) < 1e-10);
    input.driveTorque = 10;
    input.brakeTorque = 100;
    auto held         = ForceModels::tire(model, input, .02);
    REQUIRE(held.ok());
    CHECK_EQ(held.value().angularVelocity, 0);
    CHECK(std::abs(held.value().longitudinalForce) < 1e-8);
    CHECK(std::abs(held.value().resistanceTorque + 10) < 1e-8);
    input.driveTorque          = 0;
    input.longitudinalVelocity = 10;
    input.brakeTorque          = 1000;
    auto locked                = ForceModels::tire(model, input, .02);
    REQUIRE(locked.ok());
    CHECK_EQ(locked.value().angularVelocity, 0);
    CHECK(locked.value().longitudinalForce < 0);
    CHECK(std::abs(locked.value().resistanceTorque) <= input.brakeTorque);
    CHECK(std::abs(locked.value().reactionTorque + locked.value().resistanceTorque) < 1e-8);
    input.longitudinalInverseMass = -1;
    CHECK(!ForceModels::tire(model, input, .02).ok());
}
TEST_CASE("physics.forces.tireRefinementConvergesToSlipDecay") {
    TireModel model;
    model.rollingResistance     = 0;
    model.longitudinalStiffness = 500;
    double previousError        = 1;
    for (int count : {5, 10, 20, 40}) {
        const double dt = .05 / count;
        TireInput    input;
        input.angularVelocity         = 1;
        input.normalLoad              = 1000;
        input.longitudinalInverseMass = .1;
        for (int i = 0; i < count; ++i) {
            auto sampled = ForceModels::tire(model, input, dt);
            REQUIRE(sampled.ok());
            input.angularVelocity = sampled.value().angularVelocity;
            input.longitudinalVelocity += sampled.value().longitudinalForce * dt * input.longitudinalInverseMass;
        }
        const double rate =
            model.longitudinalStiffness / model.minimumSlipSpeed * (model.radius * model.radius / model.inertia + .1);
        const double exactSlip = .3 * std::exp(-rate * .05);
        const double error = std::abs(model.radius * input.angularVelocity - input.longitudinalVelocity - exactSlip);
        CHECK(error < previousError);
        previousError = error;
    }
}
}  // namespace test_physics_tire_stability
#endif
