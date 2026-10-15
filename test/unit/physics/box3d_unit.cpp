#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "physics/Body3D.h"
#include "physics/DistanceField3D.h"
#include "physics/Joint3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/TerrainLoadGravity.h"
#include "physics/World3D.h"
#include "physics/box3d/Box3DBody.h"

#include <box3d/box3d.h>

#include <cmath>
#include <limits>
#include <memory>
#include <vector>

using namespace eve::physics;


TEST_CASE("box3d.sdf.interpolationNormalAndSphere") {
    DistanceField3D field(5, 5, 5, 1.f, -2.f, -2.f, -2.f, 100.f);
    // Exact SDF for the plane y=0. Trilinear interpolation reproduces it exactly.
    for (int z = 0; z < 5; ++z)
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 5; ++x)
                field.setDistance(x, y, z, static_cast<float>(y - 2));

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
            for (int x = 0; x < 5; ++x)
                field.setDistance(x, y, z, static_cast<float>(y - 2));

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
    invalid[4] = std::numeric_limits<float>::quiet_NaN();
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
    invalidRegion[2] = std::numeric_limits<float>::quiet_NaN();
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
            for (int x = 0; x < 5; ++x)
                field.setDistance(x, y, z, static_cast<float>(y - 2));

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
            for (int x = 0; x < 7; ++x)
                field.setDistance(x, y, z, static_cast<float>(y - 3));

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
            for (int x = 0; x < 7; ++x)
                field.setDistance(x, y, z, static_cast<float>(x - 3));

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
            for (int x = 0; x < 7; ++x)
                field.setDistance(x, y, z, static_cast<float>(x - 3));

    CHECK(field.moveCapsule(0.1f, -1.f, 0.f, 0.1f, 1.f, 0.f, 0.25f, 0.f, 0.f, 0.f));
    CHECK(field.getMoverDeltaX() > 0.15f);
    CHECK(field.getMoverDeltaX() < 0.152f);
    CHECK(field.getMoverNormalX() > 0.999f);
}

TEST_CASE("box3d.sdf.moverStopsAtTwoPlaneCorner") {
    DistanceField3D field(9, 7, 9, 0.5f, -2.f, -1.5f, -2.f, 100.f);
    for (int z = 0; z < 9; ++z) {
        for (int y = 0; y < 7; ++y) {
            for (int x = 0; x < 9; ++x) {
                const float worldX = -2.f + 0.5f * static_cast<float>(x);
                const float worldZ = -2.f + 0.5f * static_cast<float>(z);
                // Union of x<=0 and z<=0.5 half spaces.
                field.setDistance(x, y, z, std::min(worldX, worldZ - 0.5f));
            }
        }
    }

    CHECK(field.moveCapsule(1.5f, -0.5f, 2.f, 1.5f, 0.5f, 2.f, 0.25f, -2.f, 0.f,
                            -3.f));
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
            for (int x = 0; x < 7; ++x)
                field.setDistance(x, y, z, static_cast<float>(y - 3));

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
    DistanceField3D field(41, 31, 5, 0.1f, -2.f, -1.f, -0.2f, 100.f);
    for (int z = 0; z < 5; ++z) {
        for (int y = 0; y < 31; ++y) {
            for (int x = 0; x < 41; ++x) {
                const float worldX = -2.f + 0.1f * static_cast<float>(x);
                const float worldY = -1.f + 0.1f * static_cast<float>(y);
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

    CHECK(field.moveCapsule(-1.f, 0.26f, 0.f, -1.f, 1.26f, 0.f, 0.25f, 1.75f, 0.f,
                            0.f));
    CHECK(field.isMoverGrounded());
    CHECK(field.getMoverDeltaX() > 1.74f);
    CHECK(field.getMoverDeltaY() > 0.39f);
    CHECK(field.getMoverDeltaY() < 0.41f);
    CHECK(!field.didMoverHitWall());

    // A step allowance lower than the obstacle must fall back to the direct wall result.
    field.setMoverStepHeight(0.3f);
    CHECK(field.moveCapsule(-1.f, 0.26f, 0.f, -1.f, 1.26f, 0.f, 0.25f, 1.75f, 0.f,
                            0.f));
    CHECK(field.didMoverHitWall());
    CHECK(field.getMoverDeltaX() < 1.f);
    CHECK(field.getMoverDeltaY() < 0.02f);
}
