#include "combat/CombatMotionWarp.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("combatMotionWarp.pullsTowardStandoffAndRejectsOverBudget") {
    eve::combat::CombatWarpRequest request;
    request.attacker        = {0.0, 0.0, 0.0};
    request.target          = {4.0, 0.0, 0.0};
    request.attackerFacing  = {0.0, 0.0, 1.0};
    request.desiredDistance = 1.4;
    request.maxTranslation  = 0.12;
    request.remainingBudget = 0.6;
    auto pulled             = eve::combat::CombatMotionWarp::solve(request);
    REQUIRE(pulled.ok());
    CHECK(pulled.value().translation.x > 0.0);
    CHECK_EQ(pulled.value().translation.x, 0.12);
    CHECK_EQ(pulled.value().translation.z, 0.0);
    CHECK(pulled.value().facing.x > 0.9);

    request.attacker        = {2.6, 0.0, 0.0};
    request.desiredDistance = 1.4;
    auto already            = eve::combat::CombatMotionWarp::solve(request);
    REQUIRE(already.ok());
    CHECK_EQ(already.value().translation.x, 0.0);
    CHECK_EQ(already.status().code(), eve::StatusCode::NoOp);

    request.attacker        = {0.0, 0.0, 0.0};
    request.remainingBudget = 0.05;
    auto over               = eve::combat::CombatMotionWarp::solve(request);
    CHECK(!over.ok());
    CHECK_EQ(over.status().code(), eve::StatusCode::Conflict);

    request.remainingBudget = 0.6;
    request.attacker        = {0.0, 0.0, 0.0};
    request.target          = {0.5, 0.0, 0.0};
    request.desiredDistance = 1.4;
    auto pushBack           = eve::combat::CombatMotionWarp::solve(request);
    REQUIRE(pushBack.ok());
    CHECK(pushBack.value().translation.x < 0.0);
}

TEST_CASE("combatMotionWarp.rejectsNonFiniteInput") {
    eve::combat::CombatWarpRequest request;
    request.attacker = {0.0 / 0.0, 0.0, 0.0};
    request.target   = {1.0, 0.0, 0.0};
    CHECK(!eve::combat::CombatMotionWarp::solve(request).ok());
}
