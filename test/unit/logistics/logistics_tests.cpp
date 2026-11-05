#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "PrivateTestScope.h"
#include "logistics/Logistics.h"

#include <cmath>

namespace test_logistics {

TEST_CASE("logistics.configureAndPlaceBelt") {
    eve::logistics::Logistics world;
    auto                      cfg = world.configure(16, 8, 1, 16.0f, 0.0f, 0.0f);
    REQUIRE(cfg.ok());
    REQUIRE(world.placeBelt(0, 0, 0, 0).ok());
    CHECK_EQ(world.cellKind(0, 0, 0), static_cast<int>(eve::logistics::CellKind::Belt));
    CHECK_EQ(world.cellDir(0, 0, 0), 0);
    CHECK(!world.placeBelt(-1, 0, 0, 0).ok());
}

TEST_CASE("logistics.beltMovesItemAndJamsOnFullCell") {
    eve::logistics::Logistics world;
    REQUIRE(world.configure(8, 4, 1, 16.0f, 0.0f, 0.0f).ok());
    world.setBeltSpeed(1.0f);
    for (int x = 0; x < 4; ++x) REQUIRE(world.placeBelt(x, 1, 0, 0).ok());
    REQUIRE(world.spawnItem(0, 1, 7, 0).ok());
    REQUIRE(world.spawnItem(1, 1, 8, 0).ok());

    auto step = world.advance(0.5f);
    REQUIRE(step.ok());
    auto a = world.itemAt(0);
    auto b = world.itemAt(1);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(a.value().progress >= 0.0f);
    CHECK_EQ(world.itemCount(), 2);
}

TEST_CASE("logistics.sourceAssemblerSinkCraftsOnce") {
    eve::logistics::Logistics world;
    REQUIRE(world.configure(20, 6, 1, 16.0f, 0.0f, 0.0f).ok());
    world.setBeltSpeed(8.0f);
    REQUIRE(world.registerRecipe("ingot", /*ore*/ 1, 1, /*ingot*/ 2, 1, 0.25f).ok());

    // Source at (0,2) feeds belt row y=2 heading east into machine at (5,2).
    REQUIRE(world.placeSource(0, 2, 1, 4.0f, 0).ok());
    for (int x = 1; x <= 4; ++x) REQUIRE(world.placeBelt(x, 2, 0, 0).ok());
    auto machine = world.placeMachine(5, 2, 2, 2, "ingot", 0);
    REQUIRE(machine.ok());
    for (int x = 7; x <= 10; ++x) REQUIRE(world.placeBelt(x, 2, 0, 0).ok());
    REQUIRE(world.placeSink(11, 2, 0).ok());

    int crafts = 0;
    int sunk   = 0;
    for (int i = 0; i < 240; ++i) {
        auto report = world.advance(1.0f / 30.0f);
        REQUIRE(report.ok());
        crafts = world.craftsTotal();
        sunk   = world.sunkTotal();
        if (crafts > 0 && sunk > 0) break;
    }
    CHECK(crafts >= 1);
    CHECK(sunk >= 1);
    CHECK(world.machineOwnerId(0).find("machine:") == 0);
}

TEST_CASE("logistics.packItemSimFloatsLayout") {
    eve::logistics::Logistics world;
    REQUIRE(world.configure(4, 2, 1, 10.0f, 1.0f, 2.0f).ok());
    REQUIRE(world.placeBelt(1, 0, 0, 0).ok());
    REQUIRE(world.spawnItem(1, 0, 3, 0).ok());
    auto packed = world.packItemSimFloats();
    REQUIRE(packed.ok());
    REQUIRE_EQ(packed.value().size(), 6u);
    CHECK_EQ(packed.value()[0], 1.0f);
    CHECK_EQ(packed.value()[1], 0.0f);
    CHECK_EQ(packed.value()[4], 3.0f);
    auto worldFloats = world.packItemWorldFloats();
    REQUIRE(worldFloats.ok());
    REQUIRE_EQ(worldFloats.value().size(), 3u);
    // origin(1) + (cellX 1 + 0.5) * cellSize(10) = 16
    CHECK(std::fabs(worldFloats.value()[0] - 16.0f) < 0.01f);
}

}  // namespace test_logistics
