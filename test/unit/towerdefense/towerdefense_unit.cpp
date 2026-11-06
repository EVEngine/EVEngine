#include "map/Pathfinder.h"
#include "sensing/Sensing.h"
#include "towerdefense/WaveDirector.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <memory>
#include <string>

using namespace eve::towerdefense;
using namespace eve::sensing;
using namespace eve::map;

TEST_CASE("towerdefense.waveDirector.spawnsDeterministicSchedule") {
    WaveDirector director;
    auto         loaded = director.loadScheduleJson(R"([
      {"id":"w1","prepSeconds":0.5,"rewardGold":10,
       "entries":[{"archetypeId":"creep.basic","count":2,"intervalSeconds":0.25,"delaySeconds":0.0}]},
      {"id":"w2","prepSeconds":0.25,"rewardGold":20,
       "entries":[{"archetypeId":"creep.tank","count":1,"intervalSeconds":0.0,"delaySeconds":0.0}]}
    ])");
    REQUIRE(loaded.ok());
    CHECK_EQ(loaded.value(), 2);
    REQUIRE(director.start().ok());
    CHECK_EQ(director.phaseName(), std::string("preparing"));

    int           basicSpawns    = 0;
    int           tankSpawns     = 0;
    bool          sawWaveStarted = false;
    bool          sawWaveReward  = false;
    bool          scheduleDone   = false;
    std::uint64_t tick           = 1;
    for (int i = 0; i < 16 && !scheduleDone; ++i, ++tick) {
        auto step = director.advance(tick, 0.25);
        REQUIRE(step.ok());
        for (const auto& spawn : step.value().spawns) {
            if (spawn.archetypeId == "creep.basic") ++basicSpawns;
            if (spawn.archetypeId == "creep.tank") ++tankSpawns;
        }
        for (const auto& event : step.value().events) {
            if (event.kind == "waveStarted") sawWaveStarted = true;
            if (event.kind == "waveCompleted" && event.waveId == "w1") {
                sawWaveReward = true;
                CHECK_EQ(event.rewardGold, 10);
            }
            if (event.kind == "scheduleCompleted") scheduleDone = true;
        }
    }
    CHECK(sawWaveStarted);
    CHECK(sawWaveReward);
    CHECK_EQ(basicSpawns, 2);
    CHECK_EQ(tankSpawns, 1);
    CHECK(scheduleDone);
    CHECK_EQ(director.phaseName(), std::string("completed"));
    // Terminal phases stay idempotent: empty ok result so game loops can keep polling.
    auto afterDone = director.advance(tick, 0.1);
    REQUIRE(afterDone.ok());
    CHECK(afterDone.value().spawns.empty());
    CHECK(afterDone.value().events.empty());
    CHECK_EQ(director.phaseName(), std::string("completed"));
}

TEST_CASE("towerdefense.defenseBase.leakClampsLives") {
    DefenseBase base(3);
    CHECK_EQ(base.lives(), 3);
    auto remaining = base.leak(2);
    REQUIRE(remaining.ok());
    CHECK_EQ(remaining.value(), 1);
    CHECK_EQ(base.leaks(), 2);
    REQUIRE(base.leak(5).ok());
    CHECK(base.isDefeated());
    CHECK_EQ(base.lives(), 0);
    CHECK(!base.leak(0).ok());
}

TEST_CASE("map.path.keepsPathOpenRejectsSealingCorridor") {
    Pathfinder finder(5, 3);
    auto       open = finder.keepsPathOpen(0, 1, 4, 1, 2, 0);
    REQUIRE(open.ok());
    CHECK(open.value());

    finder.setBlocked(2, 0, true);
    finder.setBlocked(2, 2, true);
    auto sealed = finder.keepsPathOpen(0, 1, 4, 1, 2, 1);
    REQUIRE(sealed.ok());
    CHECK(!sealed.value());

    auto footprint = finder.keepsPathOpenFootprint(0, 1, 4, 1, 2, 1, 1, 1);
    REQUIRE(footprint.ok());
    CHECK(!footprint.value());
}

TEST_CASE("sensing.query.sortPoliciesFirstLastStrongest") {
    SensingWorld world;
    REQUIRE(world.upsert("a", 1.f, 0.f, "enemy", "creep", "").ok());
    REQUIRE(world.upsert("b", 3.f, 0.f, "enemy", "creep", "").ok());
    REQUIRE(world.upsert("c", 5.f, 0.f, "enemy", "creep", "").ok());
    REQUIRE(world.setMetrics("a", 0.2f, 10.f).ok());
    REQUIRE(world.setMetrics("b", 0.8f, 40.f).ok());
    REQUIRE(world.setMetrics("c", 0.5f, 20.f).ok());

    auto first = world.circleSorted(0.f, 0.f, 10.f, "creep", "", "", "", "", 8, "first");
    REQUIRE(first.ok());
    CHECK_EQ(world.resultAt(0)->get().id, std::string("b"));

    auto last = world.circleSorted(0.f, 0.f, 10.f, "creep", "", "", "", "", 8, "last");
    REQUIRE(last.ok());
    CHECK_EQ(world.resultAt(0)->get().id, std::string("a"));

    auto strongest = world.circleSorted(0.f, 0.f, 10.f, "creep", "", "", "", "", 8, "strongest");
    REQUIRE(strongest.ok());
    CHECK_EQ(world.resultAt(0)->get().id, std::string("b"));

    auto weakest = world.circleSorted(0.f, 0.f, 10.f, "creep", "", "", "", "", 8, "weakest");
    REQUIRE(weakest.ok());
    CHECK_EQ(world.resultAt(0)->get().id, std::string("a"));
}
