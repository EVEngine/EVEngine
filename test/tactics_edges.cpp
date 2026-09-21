#include "common/ECS.h"
#include "common/Snapshot.h"
#include "tactics/Tactics.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::SimulationStep step(std::uint64_t tick) {
    return {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)};
}

/** @brief Deterministic non-cryptographic test digest, mirroring the replay tests. */
eve::SnapshotHashProvider testHash() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        std::uint64_t left  = 14695981039346656037ull;
        std::uint64_t right = 1099511628211ull;
        for (const unsigned char byte : input) {
            left  = (left ^ byte) * 1099511628211ull;
            right = (right ^ (static_cast<std::uint64_t>(byte) + 0x9e3779b97f4a7c15ull)) * 14029467366897019727ull;
        }
        eve::ContentId::Bytes bytes{};
        for (int index = 0; index < 8; ++index) {
            bytes[static_cast<std::size_t>(index)]      = static_cast<std::uint8_t>(left >> (56 - index * 8));
            bytes[static_cast<std::size_t>(index + 8)]  = static_cast<std::uint8_t>(right >> (56 - index * 8));
        }
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

}  // namespace

TEST_CASE("tactics.edgeDeclarationsAreValidatedAndDirected") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::tactics::Tactics tactics;

    auto battleResult = tactics.newBattle(subject("00000000-0000-0000-0000-000000000200"));
    REQUIRE(battleResult.ok());
    const auto battle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
    REQUIRE(tactics.addCell(battle, {1, 0, 0}).ok());
    // (2,0) exists but is two steps from (0,0): not a neighbour under Square4.
    REQUIRE(tactics.addCell(battle, {2, 0, 0}).ok());

    // Missing endpoint cells are refused distinctly from non-adjacent endpoints.
    auto missing = tactics.addEdge(battle, {0, 0, 0}, {9, 0, 0}, {});
    CHECK(!missing.ok());
    CHECK_EQ(missing.code(), eve::StatusCode::NotFound);
    auto notAdjacent = tactics.addEdge(battle, {0, 0, 0}, {2, 0, 0}, {});
    CHECK(!notAdjacent.ok());
    CHECK_EQ(notAdjacent.code(), eve::StatusCode::Rejected);
    // A negative extra cost is a validation error, not a silent clamp.
    eve::tactics::EdgeState negative;
    negative.extraCost = -1;
    auto badCost = tactics.addEdge(battle, {0, 0, 0}, {1, 0, 0}, negative);
    CHECK(!badCost.ok());
    CHECK_EQ(badCost.code(), eve::StatusCode::Rejected);

    // A one-way edge is declared in exactly one direction.
    eve::tactics::EdgeState blocked;
    blocked.passable = false;
    blocked.tags     = {"door"};
    REQUIRE(tactics.addEdge(battle, {0, 0, 0}, {1, 0, 0}, blocked).ok());
    auto duplicate = tactics.addEdge(battle, {0, 0, 0}, {1, 0, 0}, {});
    CHECK(!duplicate.ok());
    CHECK_EQ(duplicate.code(), eve::StatusCode::Conflict);

    auto forward = tactics.edge(battle, {0, 0, 0}, {1, 0, 0});
    REQUIRE(forward.ok());
    CHECK(!forward.value().passable);
    CHECK_EQ(forward.value().tags.size(), 1u);
    // The reverse direction stays undeclared, which is what makes it one-way.
    auto reverse = tactics.edge(battle, {1, 0, 0}, {0, 0, 0});
    CHECK(!reverse.ok());
    CHECK_EQ(reverse.code(), eve::StatusCode::NotFound);
    CHECK(!tactics.tryEdge(battle, {1, 0, 0}, {0, 0, 0}).has_value());
    CHECK(tactics.tryEdge(battle, {0, 0, 0}, {1, 0, 0}).has_value());
}

TEST_CASE("tactics.edgeBlockingAndCostApplyToReachabilityAndPath") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::tactics::Tactics tactics;

    auto battleResult = tactics.newBattle(subject("00000000-0000-0000-0000-000000000210"), 11);
    REQUIRE(battleResult.ok());
    const auto battle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
    REQUIRE(tactics.addCell(battle, {1, 0, 0}).ok());
    auto sideResult = tactics.newSide(battle, subject("00000000-0000-0000-0000-000000000211"));
    REQUIRE(sideResult.ok());
    const auto side = std::move(sideResult).takeValue();
    const auto unit = subject("00000000-0000-0000-0000-000000000212");
    // movePoints 1000 keeps the budget out of the way of the cost assertions.
    REQUIRE(tactics.newUnit(battle, side, unit, {}, {0, 0, 0}, {1, 1000, 0, 10}).ok());

    // Blocking the only outgoing direction makes the neighbour unreachable, and the
    // path query must agree with the reachability query rather than finding a route.
    eve::tactics::EdgeState blocked;
    blocked.passable = false;
    REQUIRE(tactics.addEdge(battle, {0, 0, 0}, {1, 0, 0}, blocked).ok());
    auto reachableBlocked = tactics.reachable(battle, unit, 1000);
    REQUIRE(reachableBlocked.ok());
    CHECK_EQ(reachableBlocked.value().cells().size(), 1u);
    CHECK(!reachableBlocked.value().contains({1, 0, 0}));
    auto pathBlocked = reachableBlocked.value().pathTo({1, 0, 0});
    CHECK(!pathBlocked.ok());
    CHECK_EQ(pathBlocked.code(), eve::StatusCode::NotFound);

    // Re-declaring is a Conflict, so the fixture builds a fresh battle for the cost
    // half rather than mutating a declared direction in place.
    ecs::Table       secondWorld;
    ecs::ScopedTable secondGuard(secondWorld);
    eve::tactics::Tactics other;
    auto                  otherResult = other.newBattle(subject("00000000-0000-0000-0000-000000000220"), 12);
    REQUIRE(otherResult.ok());
    const auto otherBattle = std::move(otherResult).takeValue();
    REQUIRE(other.addCell(otherBattle, {0, 0, 0}).ok());
    REQUIRE(other.addCell(otherBattle, {1, 0, 0}).ok());
    auto otherSideResult = other.newSide(otherBattle, subject("00000000-0000-0000-0000-000000000221"));
    REQUIRE(otherSideResult.ok());
    const auto otherSide = std::move(otherSideResult).takeValue();
    const auto otherUnit = subject("00000000-0000-0000-0000-000000000222");
    REQUIRE(other.newUnit(otherBattle, otherSide, otherUnit, {}, {0, 0, 0}, {1, 1000, 0, 10}).ok());

    // Asymmetric extra cost: crossing out costs more than crossing back.
    eve::tactics::EdgeState outbound;
    outbound.extraCost = 50;
    eve::tactics::EdgeState inbound;
    inbound.extraCost = 250;
    REQUIRE(other.addEdge(otherBattle, {0, 0, 0}, {1, 0, 0}, outbound).ok());
    REQUIRE(other.addEdge(otherBattle, {1, 0, 0}, {0, 0, 0}, inbound).ok());

    auto outboundReach = other.reachable(otherBattle, otherUnit, 1000);
    REQUIRE(outboundReach.ok());
    auto outboundCost = outboundReach.value().cost({1, 0, 0});
    REQUIRE(outboundCost.ok());
    // cell moveCost 100 + outbound extra 50
    CHECK_EQ(outboundCost.value(), 150);

    REQUIRE(other.start(otherBattle, eve::tactics::TurnPolicyKind::Initiative).ok());
    REQUIRE(other.advance(otherBattle, step(1)).ok());
    REQUIRE(other.advance(otherBattle, step(2)).ok());
    REQUIRE(other.advance(otherBattle, step(3)).ok());
    auto moved = other.moveUnit(otherBattle, otherUnit, {1, 0, 0});
    REQUIRE(moved.ok());
    CHECK_EQ(std::move(moved).takeValue().cost, 150);

    // From (1,0) the reverse extra cost applies instead: 100 + 250.
    auto inboundReach = other.reachable(otherBattle, otherUnit, 1000);
    REQUIRE(inboundReach.ok());
    auto inboundCost = inboundReach.value().cost({0, 0, 0});
    REQUIRE(inboundCost.ok());
    CHECK_EQ(inboundCost.value(), 350);
}

TEST_CASE("tactics.snapshotRoundTripsEdgesAtTheCurrentVersion") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::tactics::Tactics tactics;
    const auto            hash = testHash();

    const auto battleSubject = subject("00000000-0000-0000-0000-000000000230");
    auto       battleResult  = tactics.newBattle(battleSubject);
    REQUIRE(battleResult.ok());
    const auto battle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
    REQUIRE(tactics.addCell(battle, {1, 0, 0}).ok());
    eve::tactics::EdgeState blocked;
    blocked.passable = false;
    blocked.tags     = {"door", "one_way"};
    REQUIRE(tactics.addEdge(battle, {0, 0, 0}, {1, 0, 0}, blocked).ok());

    auto captured = tactics.snapshot(battle, hash);
    REQUIRE(captured.ok());
    auto envelope = std::move(captured).takeValue();
    // Adding the edge set and the stable policy id are both schema changes, so the
    // version must advance with them.
    CHECK(envelope.schemaVersion == eve::SchemaVersion(5));

    // Restoring into an identity-compatible battle must rebuild the declared edges,
    // not silently drop them.
    auto targetResult = tactics.newBattle(battleSubject);
    REQUIRE(targetResult.ok());
    const auto target = std::move(targetResult).takeValue();
    REQUIRE(tactics.addCell(target, {0, 0, 0}).ok());
    REQUIRE(tactics.addCell(target, {1, 0, 0}).ok());
    CHECK(!tactics.tryEdge(target, {0, 0, 0}, {1, 0, 0}).has_value());

    REQUIRE(tactics.restore(target, envelope, hash).ok());
    auto restored = tactics.edge(target, {0, 0, 0}, {1, 0, 0});
    REQUIRE(restored.ok());
    CHECK(!restored.value().passable);
    CHECK_EQ(restored.value().tags.size(), 2u);
    // The reverse direction is still undeclared after the round trip.
    CHECK(!tactics.tryEdge(target, {1, 0, 0}, {0, 0, 0}).has_value());
    // A re-declared direction now conflicts, which is further evidence the edge is live.
    auto duplicate = tactics.addEdge(target, {0, 0, 0}, {1, 0, 0}, {});
    CHECK(!duplicate.ok());
    CHECK_EQ(duplicate.code(), eve::StatusCode::Conflict);
}
