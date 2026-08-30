#include "common/ECS.h"
#include "tactics/Tactics.h"
#include "tactics/TacticsPersistence.h"
#include "tactics/TacticsReplay.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>

namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::SnapshotHashProvider testHash() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        std::uint64_t left = 14695981039346656037ull;
        std::uint64_t right = 1099511628211ull;
        for (const unsigned char byte : input) {
            left = (left ^ byte) * 1099511628211ull;
            right = (right ^ (static_cast<std::uint64_t>(byte) + 0x9e3779b97f4a7c15ull)) *
                    14029467366897019727ull;
        }
        eve::ContentId::Bytes bytes{};
        for (int index = 0; index < 8; ++index) {
            bytes[static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(left >> (56 - index * 8));
            bytes[static_cast<std::size_t>(index + 8)] = static_cast<std::uint8_t>(right >> (56 - index * 8));
        }
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

eve::SimulationStep step(std::uint64_t tick) {
    return {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)};
}

}  // namespace

TEST_CASE("tactics.snapshotRoundTripAndFailureIsTransactional") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::tactics::Tactics tactics;
    const auto battleSubject = subject("00000000-0000-0000-0000-000000000080");
    const auto unitSubject = subject("00000000-0000-0000-0000-000000000082");
    auto battleResult = tactics.newBattle(battleSubject, 91);
    REQUIRE(battleResult.ok());
    const auto battleHandle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battleHandle, {0, 0, 0}).ok());
    REQUIRE(tactics.addCell(battleHandle, {1, 0, 0}).ok());
    auto sideResult = tactics.newSide(battleHandle, subject("00000000-0000-0000-0000-000000000081"));
    REQUIRE(sideResult.ok());
    const auto firstSide = std::move(sideResult).takeValue();
    auto otherSideResult = tactics.newSide(battleHandle, subject("00000000-0000-0000-0000-000000000083"));
    REQUIRE(otherSideResult.ok());
    const auto otherSide = std::move(otherSideResult).takeValue();
    const auto definition = eve::LogicalId::parse("test:scout");
    REQUIRE(definition.has_value());
    auto unitResult = tactics.newUnit(battleHandle, firstSide, unitSubject, *definition, {0, 0, 0},
                                      {1, 100, 1, 10});
    REQUIRE(unitResult.ok());
    const auto unitHandle = std::move(unitResult).takeValue();
    REQUIRE(tactics.start(battleHandle, eve::tactics::TurnPolicyKind::Initiative).ok());
    REQUIRE(tactics.advance(battleHandle, step(1)).ok());
    REQUIRE(tactics.advance(battleHandle, step(2)).ok());
    REQUIRE(tactics.advance(battleHandle, step(3)).ok());

    auto* battle = dynamic_cast<eve::tactics::Battle*>(ecs::try_get(battleHandle));
    auto* unit = dynamic_cast<eve::tactics::TacticalUnit*>(ecs::try_get(unitHandle));
    REQUIRE(battle != nullptr);
    REQUIRE(unit != nullptr);
    const auto hash = testHash();
    auto snapshot = eve::tactics::TacticsPersistence::snapshot(*battle, hash);
    REQUIRE(snapshot.ok());
    REQUIRE(tactics.moveUnit(battleHandle, unitSubject, {1, 0, 0}).ok());
    unit->membership()->side = otherSide;
    unit->identity()->definition = {};
    std::reverse(battle->turn()->sides.begin(), battle->turn()->sides.end());
    CHECK_EQ(unit->position()->cell, (eve::tactics::Cell{1, 0, 0}));

    REQUIRE(eve::tactics::TacticsPersistence::restore(*battle, snapshot.value(), hash).ok());
    CHECK_EQ(unit->position()->cell, (eve::tactics::Cell{0, 0, 0}));
    CHECK_EQ(unit->turn()->movePoints, 100);
    CHECK_EQ(unit->membership()->side.table, firstSide.table);
    CHECK_EQ(unit->membership()->side.type, firstSide.type);
    CHECK_EQ(unit->membership()->side.id, firstSide.id);
    CHECK_EQ(unit->membership()->side.generation, firstSide.generation);
    CHECK_EQ(unit->identity()->definition, *definition);
    REQUIRE_EQ(battle->turn()->sides.size(), 2u);
    CHECK_EQ(battle->turn()->sides[0].id, firstSide.id);
    CHECK_EQ(battle->turn()->sides[0].generation, firstSide.generation);
    CHECK_EQ(battle->turn()->sides[1].id, otherSide.id);
    CHECK_EQ(battle->turn()->sides[1].generation, otherSide.generation);
    auto repeated = eve::tactics::TacticsPersistence::snapshot(*battle, hash);
    REQUIRE(repeated.ok());
    CHECK_EQ(repeated.value().contentHash, snapshot.value().contentHash);

    auto unsupported = snapshot.value();
    unsupported.schemaVersion = eve::SchemaVersion(2);
    auto resealed = eve::makeSnapshotEnvelope(unsupported.type, unsupported.schema, unsupported.schemaVersion,
                                               unsupported.instanceId, unsupported.revision, unsupported.tick,
                                               unsupported.payload, hash);
    REQUIRE(resealed.ok());
    REQUIRE(tactics.moveUnit(battleHandle, unitSubject, {1, 0, 0}).ok());
    auto failed = eve::tactics::TacticsPersistence::restore(*battle, resealed.value(), hash);
    CHECK(!failed.ok());
    CHECK_EQ(unit->position()->cell, (eve::tactics::Cell{1, 0, 0}));
}

TEST_CASE("tactics.snapshotPreservesReactionStackAndEventSequence") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::tactics::Tactics tactics;
    const auto battleSubject = subject("00000000-0000-0000-0000-000000000090");
    const auto unitSubject = subject("00000000-0000-0000-0000-000000000092");
    auto battleResult = tactics.newBattle(battleSubject, 12);
    REQUIRE(battleResult.ok());
    const auto battleHandle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battleHandle, {0, 0, 0}).ok());
    auto sideResult = tactics.newSide(battleHandle, subject("00000000-0000-0000-0000-000000000091"));
    REQUIRE(sideResult.ok());
    auto unitResult = tactics.newUnit(battleHandle, std::move(sideResult).takeValue(), unitSubject, {}, {0, 0, 0},
                                      {1, 0, 2, 10});
    REQUIRE(unitResult.ok());
    std::move(unitResult).takeValue();
    REQUIRE(tactics.start(battleHandle, eve::tactics::TurnPolicyKind::Initiative).ok());
    REQUIRE(tactics.advance(battleHandle, step(1)).ok());
    REQUIRE(tactics.advance(battleHandle, step(2)).ok());
    REQUIRE(tactics.advance(battleHandle, step(3)).ok());
    const auto action = eve::LogicalId::parse("test:counter");
    REQUIRE(action.has_value());
    auto opened = tactics.openReaction(battleHandle, 3, {{unitSubject, *action, 4, 10}});
    REQUIRE(opened.ok());

    auto* battle = dynamic_cast<eve::tactics::Battle*>(ecs::try_get(battleHandle));
    REQUIRE(battle != nullptr);
    const auto hash = testHash();
    auto snapshot = eve::tactics::TacticsPersistence::snapshot(*battle, hash);
    REQUIRE(snapshot.ok());
    const auto expectedNextSequence = battle->events()->nextSequence;
    REQUIRE(tactics.declineReaction(battleHandle).ok());
    REQUIRE(eve::tactics::TacticsPersistence::restore(*battle, snapshot.value(), hash).ok());
    CHECK_EQ(battle->events()->nextSequence, expectedNextSequence);
    CHECK_EQ(battle->reactions()->stack.size(), 1u);
    CHECK_EQ(static_cast<int>(battle->turn()->phase),
             static_cast<int>(eve::tactics::BattlePhase::Reaction));
    auto accepted = tactics.acceptReaction(battleHandle, unitSubject, *action);
    REQUIRE(accepted.ok());
    CHECK_EQ(std::move(accepted).takeValue().remainingReactionPoints, 1);
    CHECK_EQ(battle->events()->nextSequence, expectedNextSequence + 1);
}

TEST_CASE("tactics.commandReplayProducesByteIdenticalFinalSnapshot") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::tactics::Tactics tactics;
    const auto battleSubject = subject("00000000-0000-0000-0000-0000000000a0");
    const auto unitSubject = subject("00000000-0000-0000-0000-0000000000a2");
    auto battleResult = tactics.newBattle(battleSubject, 33);
    REQUIRE(battleResult.ok());
    const auto battleHandle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battleHandle, {0, 0, 0}).ok());
    REQUIRE(tactics.addCell(battleHandle, {1, 0, 0}).ok());
    auto sideResult = tactics.newSide(battleHandle, subject("00000000-0000-0000-0000-0000000000a1"));
    REQUIRE(sideResult.ok());
    auto unitResult = tactics.newUnit(battleHandle, std::move(sideResult).takeValue(), unitSubject, {}, {0, 0, 0},
                                      {1, 100, 1, 10});
    REQUIRE(unitResult.ok());
    std::move(unitResult).takeValue();
    REQUIRE(tactics.start(battleHandle, eve::tactics::TurnPolicyKind::Initiative).ok());
    REQUIRE(tactics.advance(battleHandle, step(1)).ok());
    REQUIRE(tactics.advance(battleHandle, step(2)).ok());
    REQUIRE(tactics.advance(battleHandle, step(3)).ok());
    auto* battle = dynamic_cast<eve::tactics::Battle*>(ecs::try_get(battleHandle));
    REQUIRE(battle != nullptr);
    const auto hash = testHash();
    const auto baselineRevision = battle->turn()->revision;
    auto baseline = eve::tactics::TacticsPersistence::snapshot(*battle, hash);
    REQUIRE(baseline.ok());

    REQUIRE(tactics.faceUnit(battleHandle, unitSubject, 2).ok());
    const auto randomStream = eve::LogicalId::parse("combat:hit");
    REQUIRE(randomStream.has_value());
    auto firstRoll = tactics.roll(battleHandle, *randomStream);
    auto secondRoll = tactics.roll(battleHandle, *randomStream);
    REQUIRE(firstRoll.ok());
    REQUIRE(secondRoll.ok());
    CHECK_NE(firstRoll.value(), secondRoll.value());
    REQUIRE(tactics.moveUnit(battleHandle, unitSubject, {1, 0, 0}).ok());
    REQUIRE(tactics.waitUnit(battleHandle, unitSubject).ok());
    const auto commands = eve::tactics::BattleReplay::commandsFrom(*battle, baselineRevision);
    REQUIRE_EQ(commands.size(), 5u);
    auto direct = eve::tactics::TacticsPersistence::snapshot(*battle, hash);
    REQUIRE(direct.ok());

    REQUIRE(eve::tactics::TacticsPersistence::restore(*battle, baseline.value(), hash).ok());
    REQUIRE(eve::tactics::BattleReplay::replay(*battle, commands).ok());
    auto replayed = eve::tactics::TacticsPersistence::snapshot(*battle, hash);
    REQUIRE(replayed.ok());
    CHECK_EQ(replayed.value().contentHash, direct.value().contentHash);
    CHECK_EQ(replayed.value().payload, direct.value().payload);
}
