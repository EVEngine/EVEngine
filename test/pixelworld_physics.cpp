#include "physics/Body.h"
#include "physics/World.h"
#include "pixelworld/PixelWorld.h"
#include "pixelworld_physics/PixelWorldPhysics.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>

using namespace eve::pixelworld;
using namespace eve::pixelworld_physics;

namespace {

PixelFragment detachL(PixelWorld& world) {
    world.setMaterial(0, 0, "wood");
    world.setMaterial(1, 0, "wood");
    world.setMaterial(0, 1, "wood");
    auto fragments = world.extractUnsupportedFragments({-1, -1, 2, 2}, 10)
                         .expect("detach L fragment");
    REQUIRE_EQ(fragments.size(), std::size_t(1));
    return std::move(fragments.front());
}

}  // namespace

TEST_CASE("pixelworld_physics.fragment_decomposition_and_sleep_settlement_are_transactional") {
    PixelWorld pixels(501);
    PixelFragment fragment = detachL(pixels);
    eve::physics::World physics(0.f, 100.f, true, 64.f);
    auto adapter = PixelFragmentBody::create(physics, std::move(fragment))
                       .expect("create fragment body");

    REQUIRE_EQ(adapter->collisionRects().size(), std::size_t(2));
    auto body = adapter->physicsLink().resolve(physics).expect("resolve fragment body");
    body->setPosition(20.f, 20.f);
    body->setAngle(1.57079632679f);
    pixels.setMaterial(19, 19, "stone");
    body->setAwake(false);

    auto conflict = adapter->settleIfSleeping(physics, pixels);
    CHECK(!conflict.ok());
    CHECK(body->isValid());
    CHECK(!adapter->isRasterized());

    body->setPosition(40.f, 40.f);
    body->setAwake(false);
    const auto settled = adapter->settleIfSleeping(physics, pixels).expect("settle fragment");
    CHECK_EQ(int(settled.disposition), int(FragmentSettleDisposition::Rasterized));
    CHECK_EQ(settled.cellsRasterized, std::uint32_t(3));
    CHECK_EQ(settled.quarterTurns, 1);
    CHECK(adapter->isRasterized());
    CHECK(!body->isValid());
    CHECK_EQ(pixels.getMaterial(39, 39), int(MaterialId::Wood));
    CHECK_EQ(pixels.getMaterial(40, 39), int(MaterialId::Wood));
    CHECK_EQ(pixels.getMaterial(40, 40), int(MaterialId::Wood));
}

TEST_CASE("pixelworld_physics.both_destruction_orders_and_restore_staleness_are_safe") {
    PixelWorld pixels(502);
    eve::physics::World firstPhysics(0.f, 0.f, true, 64.f);
    auto first = PixelFragmentBody::create(firstPhysics, detachL(pixels))
                     .expect("first fragment body");
    first->releasePhysics(firstPhysics).expect("body first release");
    CHECK(firstPhysics.isValid());

    pixels.clear();
    auto secondFragment = detachL(pixels);
    const auto checkpoint = pixels.saveSnapshot().expect("pixel checkpoint");
    auto second = PixelFragmentBody::create(firstPhysics, std::move(secondFragment))
                      .expect("second fragment body");
    auto secondBody = second->physicsLink().resolve(firstPhysics).expect("second body");
    secondBody->setAwake(false);
    pixels.restoreSnapshot(checkpoint).expect("restore invalidates pixel link");
    CHECK(!second->settleIfSleeping(firstPhysics, pixels).ok());
    CHECK(secondBody->isValid());

    auto thirdPhysics = std::make_unique<eve::physics::World>(0.f, 0.f, true, 64.f);
    pixels.clear();
    auto third = PixelFragmentBody::create(*thirdPhysics, detachL(pixels))
                     .expect("third fragment body");
    thirdPhysics.reset();
    eve::physics::World foreignPhysics(0.f, 0.f, true, 64.f);
    CHECK(!third->releasePhysics(foreignPhysics).ok());
}

TEST_CASE("pixelworld_physics.terrain_cache_rebuilds_dirty_chunks_and_rolls_back_budget_failure") {
    PixelWorld pixels(503);
    for (int x = 0; x < 70; ++x) pixels.setMaterial(x, 20, "stone");
    pixels.setMaterial(5, 5, "water");
    eve::physics::World physics(0.f, 100.f, true, 64.f);
    PixelTerrainCollisionCache cache;

    const auto initial = cache.sync(physics, pixels).expect("initial terrain collision sync");
    CHECK_EQ(initial.chunksRebuilt, std::uint32_t(2));
    CHECK_EQ(initial.fixturesCreated, std::uint32_t(4));
    CHECK_EQ(cache.bodyCount(), std::size_t(2));
    CHECK(physics.rayCast(10.f, 10.f, 10.f, 30.f) >= 0);
    CHECK(std::abs(physics.getRayHitY() - 20.f) < 0.01f);
    const auto represented = cache.sourceRevision();

    pixels.setMaterial(2, 19, "stone");
    const auto incremental = cache.sync(physics, pixels).expect("dirty chunk collision sync");
    CHECK_EQ(incremental.chunksRebuilt, std::uint32_t(2));
    CHECK_EQ(incremental.bodiesRemoved, std::uint32_t(2));
    CHECK(cache.sourceRevision() > represented);

    const auto beforeFailureRevision = cache.sourceRevision();
    const auto beforeFailureBodies = cache.bodyCount();
    for (int x = 0; x < 16; x += 2) pixels.setMaterial(x, 4, "stone");
    auto rejected = cache.sync(physics, pixels, 1);
    CHECK(!rejected.ok());
    CHECK_EQ(cache.sourceRevision(), beforeFailureRevision);
    CHECK_EQ(cache.bodyCount(), beforeFailureBodies);

    cache.clearPhysics(physics).expect("clear terrain collision cache");
    CHECK_EQ(cache.bodyCount(), std::size_t(0));
    CHECK_EQ(cache.sourceRevision(), std::uint64_t(0));
}

TEST_CASE("pixelworld_physics.terrain_contours_are_simplified_and_suppress_chunk_seams") {
    PixelWorld pixels(504);
    for (int y = 10; y < 12; ++y)
        for (int x = 10; x < 12; ++x) pixels.setMaterial(x, y, "stone");
    auto simple = extractTerrainContours(pixels, 0, 0).expect("simple terrain contour");
    REQUIRE_EQ(simple.size(), std::size_t(1));
    CHECK(simple.front().loop);
    CHECK_EQ(simple.front().vertices.size(), std::size_t(8));

    pixels.setMaterial(63, 20, "stone");
    pixels.setMaterial(64, 20, "stone");
    const auto left = extractTerrainContours(pixels, 0, 0).expect("left seam contour");
    for (const auto& contour : left) {
        const std::size_t count = contour.vertices.size() / 2;
        for (std::size_t index = 0; index + 1 < count; ++index) {
            const float x1 = contour.vertices[index * 2];
            const float y1 = contour.vertices[index * 2 + 1];
            const float x2 = contour.vertices[(index + 1) * 2];
            const float y2 = contour.vertices[(index + 1) * 2 + 1];
            CHECK(!(x1 == 64.f && x2 == 64.f && y1 >= 20.f && y2 >= 20.f));
        }
    }
    CHECK(!extractTerrainContours(pixels, 0, 0, 3).ok());
}

TEST_CASE("pixelworld_physics.character_probe_and_sweep_query_authoritative_material") {
    PixelWorld pixels(505);
    pixels.setMaterial(10, 10, "stone");
    pixels.setMaterial(8, 10, "water");

    const auto probe = probeTerrainCircle(pixels, 9.75f, 10.5f, 0.5f)
                           .expect("character terrain probe");
    REQUIRE(probe.hit);
    CHECK_EQ(int(probe.material), int(MaterialId::Stone));
    CHECK_EQ(probe.cellX, 10);
    CHECK(std::abs(probe.depth - 0.25f) < 0.0001f);
    CHECK_EQ(probe.normalX, -1.f);
    CHECK_EQ(probe.normalY, 0.f);

    const auto sweep = sweepTerrainCircle(pixels, 5.5f, 10.5f, 15.5f, 10.5f, 0.25f)
                           .expect("character terrain sweep");
    REQUIRE(sweep.hit);
    CHECK_EQ(int(sweep.material), int(MaterialId::Stone));
    CHECK_EQ(sweep.cellX, 10);
    CHECK(std::abs(sweep.fraction - 0.425f) < 0.0001f);
    CHECK_EQ(sweep.normalX, -1.f);

    const auto corner = sweepTerrainCircle(pixels, 8.f, 8.f, 10.f, 10.f, 0.5f)
                            .expect("rounded corner sweep");
    REQUIRE(corner.hit);
    CHECK(corner.normalX < -0.7f);
    CHECK(corner.normalY < -0.7f);
    const auto cornerMiss = sweepTerrainCircle(pixels, 8.f, 8.f, 9.6f, 9.6f, 0.5f)
                                .expect("rounded corner miss");
    CHECK(!cornerMiss.hit);

    const auto miss = sweepTerrainCircle(pixels, 1.f, 1.f, 2.f, 1.f, 0.25f)
                          .expect("empty terrain sweep");
    CHECK(!miss.hit);
    CHECK_EQ(miss.fraction, 1.f);
    CHECK(!sweepTerrainCircle(pixels, 0.f, 0.f, 100.f, 100.f, 1.f, 4).ok());
}
