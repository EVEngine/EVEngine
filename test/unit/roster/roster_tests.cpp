#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "PrivateTestScope.h"
#include "roster/Roster.h"

namespace test_roster {

TEST_CASE("roster.postAssignReleaseEmitsVacancy") {
    eve::roster::Roster roster;
    REQUIRE(roster.registerPost("gate", 2, "sentry").ok());
    REQUIRE(roster.assignPost("gate", "guard:a", "").ok());
    REQUIRE(roster.assignPost("gate", "guard:b", "heavy").ok());
    CHECK_EQ(roster.postOccupancy("gate").value(), 2);
    CHECK(!roster.assignPost("gate", "guard:c", "").ok());
    REQUIRE(roster.releasePost("gate", "guard:a", 10).ok());
    REQUIRE_EQ(roster.vacancyCount(), 1);
    auto vacancy = roster.vacancyAt(0);
    REQUIRE(vacancy.ok());
    CHECK_EQ(vacancy.value().kind, "post");
    CHECK_EQ(vacancy.value().targetId, "gate");
    CHECK_EQ(vacancy.value().role, "sentry");
    CHECK_EQ(vacancy.value().lostSubject, "guard:a");
}

TEST_CASE("roster.squadIntentAndMemberLoss") {
    eve::roster::Roster roster;
    REQUIRE(roster.createSquad("patrol-north", 3, "idle").ok());
    REQUIRE(roster.setIntent("patrol-north", "patrol").ok());
    CHECK_EQ(roster.intent("patrol-north").value(), "patrol");
    REQUIRE(roster.addMember("patrol-north", "unit:2", "lead").ok());
    REQUIRE(roster.addMember("patrol-north", "unit:1", "support").ok());
    CHECK_EQ(roster.memberCount("patrol-north").value(), 2);
    CHECK_EQ(roster.memberAt("patrol-north", 0).value(), "unit:1");
    REQUIRE(roster.removeMember("patrol-north", "unit:2", 4).ok());
    REQUIRE_EQ(roster.vacancyCount(), 1);
    CHECK_EQ(roster.vacancyAt(0).value().kind, "squad");
    CHECK_EQ(roster.vacancyAt(0).value().lostSubject, "unit:2");
}

TEST_CASE("roster.reinforcePolicyDelayFallbackAndBudget") {
    eve::roster::Roster roster;
    REQUIRE(roster.registerPost("tower", 1, "sentry").ok());
    REQUIRE(roster.assignPost("tower", "guard:old", "sentry").ok());
    REQUIRE(roster.setPolicy("tower-refill", "unit.sentry", /*delay*/ 2, /*budget*/ 2, "tower", "", "sentry").ok());
    REQUIRE(roster.setFallback("tower-refill", "unit.sentry", "unit.militia").ok());

    REQUIRE(roster.releaseSubject("guard:old", 10).ok());
    REQUIRE_EQ(roster.vacancyCount(), 1);
    REQUIRE(roster.step(11).ok());
    CHECK_EQ(roster.spawnRequestCount(), 0);
    REQUIRE_EQ(roster.vacancyCount(), 1);

    auto stepped = roster.step(12);
    REQUIRE(stepped.ok());
    CHECK_EQ(stepped.value(), 1);
    CHECK_EQ(roster.spawnRequestCount(), 1);
    auto request = roster.spawnRequestAt(0);
    REQUIRE(request.ok());
    CHECK_EQ(request.value().preferredDef, "unit.sentry");
    CHECK_EQ(request.value().postId, "tower");

    REQUIRE(roster.failSpawnRequest(request.value().requestId, 13).ok());
    CHECK_EQ(roster.spawnRequestCount(), 1);
    CHECK_EQ(roster.spawnRequestAt(0).value().preferredDef, "unit.militia");
    REQUIRE(roster.ackSpawnRequest(roster.spawnRequestAt(0).value().requestId).ok());
    CHECK_EQ(roster.spawnRequestCount(), 0);
    REQUIRE(roster.assignPost("tower", "guard:new", "sentry").ok());
    CHECK_EQ(roster.postOccupancy("tower").value(), 1);
}

TEST_CASE("roster.releaseSubjectClearsPostAndSquad") {
    eve::roster::Roster roster;
    REQUIRE(roster.registerPost("wall", 1, "guard").ok());
    REQUIRE(roster.createSquad("alpha", 2, "patrol").ok());
    REQUIRE(roster.assignPost("wall", "npc:1", "").ok());
    REQUIRE(roster.addMember("alpha", "npc:1", "scout").ok());
    auto released = roster.releaseSubject("npc:1", 7);
    REQUIRE(released.ok());
    CHECK_EQ(released.value(), 2);
    CHECK_EQ(roster.postOccupancy("wall").value(), 0);
    CHECK_EQ(roster.memberCount("alpha").value(), 0);
    CHECK_EQ(roster.vacancyCount(), 2);
}

}  // namespace test_roster
