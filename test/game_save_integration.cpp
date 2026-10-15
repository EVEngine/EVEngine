#include "zeroerr/unittest.h"

#include "common/ECS.h"
#include "common/GameSaveSession.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "rpg/GameState.h"
#include "rpg/Quest.h"
#include "rpg/RPGActor.h"
#include "rpg/RPGGameSaveParticipant.h"
#include "rpg/RPGSaveSession.h"
#include "rpg/Tracker.h"
#include "tactics/Tactics.h"
#include "tactics/TacticsGameSaveParticipant.h"

#include <cstdint>
#include <string_view>
#include <utility>

namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::SnapshotHashProvider testHash() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        eve::ContentId::Bytes bytes{};
        std::uint64_t         hash = 14695981039346656037ull;
        for (const unsigned char byte : input) {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        for (std::size_t index = 0; index < bytes.size(); ++index)
            bytes[index] = static_cast<std::uint8_t>(hash >> ((index % 8) * 8));
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

}  // namespace

TEST_CASE("gameSave.rpgAndTacticsRestoreTogether") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rpg::QuestRegistry::clear();
    eve::rpg::GameState state;
    state.setVariable("gold", 25.0);
    eve::rpg::Tracker            tracker;
    auto*                        actor = eve::rpg::RPGActor::createActor();
    eve::inventory::Bag          bag(2);
    eve::inventory::EquipmentSet equipment;
    eve::rpg::RPGSaveSession     rpgSession;
    rpgSession.bind(state, tracker, *actor, bag, equipment);
    REQUIRE(rpgSession.setContentVersion("game-content-v1").ok());

    eve::tactics::Tactics tactics;
    auto                  battleCreated = tactics.newBattle(subject("00000000-0000-0000-0000-000000000080"), 91);
    REQUIRE(battleCreated.ok());
    const auto battleHandle = std::move(battleCreated).takeValue();
    REQUIRE(tactics.addCell(battleHandle, {0, 0, 0}).ok());
    REQUIRE(tactics.addCell(battleHandle, {1, 0, 0}).ok());
    auto sideCreated = tactics.newSide(battleHandle, subject("00000000-0000-0000-0000-000000000081"));
    REQUIRE(sideCreated.ok());
    const auto side      = std::move(sideCreated).takeValue();
    auto       otherSide = tactics.newSide(battleHandle, subject("00000000-0000-0000-0000-000000000083"));
    REQUIRE(otherSide.ok());
    const auto definition = eve::LogicalId::parse("test:scout");
    REQUIRE(definition.has_value());
    const auto unitSubject = subject("00000000-0000-0000-0000-000000000082");
    auto       unitCreated = tactics.newUnit(battleHandle, side, unitSubject, *definition, {0, 0, 0}, {1, 100, 1, 10});
    REQUIRE(unitCreated.ok());
    const auto unitHandle = std::move(unitCreated).takeValue();
    REQUIRE(tactics.start(battleHandle, eve::tactics::TurnPolicyKind::Initiative).ok());
    for (std::uint64_t tick = 1; tick <= 3; ++tick)
        REQUIRE(tactics.advance(battleHandle, {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)}).ok());
    auto* battle = dynamic_cast<eve::tactics::Battle*>(ecs::try_get(battleHandle));
    auto* unit   = dynamic_cast<eve::tactics::TacticalUnit*>(ecs::try_get(unitHandle));
    REQUIRE(battle != nullptr);
    REQUIRE(unit != nullptr);

    eve::rpg::RPGGameSaveParticipant         rpgParticipant(rpgSession);
    eve::tactics::TacticsGameSaveParticipant tacticsParticipant(*battle, testHash());
    eve::GameSaveSession                     gameSave("game-content-v1");
    REQUIRE(gameSave.add(rpgParticipant).ok());
    REQUIRE(gameSave.add(tacticsParticipant).ok());
    auto saved = gameSave.snapshotBinary(testHash());
    REQUIRE(saved.ok());

    state.setVariable("gold", 99.0);
    REQUIRE(tactics.moveUnit(battleHandle, unitSubject, {1, 0, 0}).ok());
    CHECK_EQ(unit->position()->cell, (eve::tactics::Cell{1, 0, 0}));
    REQUIRE(gameSave.restoreBinary(saved.value(), testHash()).ok());
    CHECK_EQ(state.getVariable("gold"), 25.0);
    CHECK_EQ(unit->position()->cell, (eve::tactics::Cell{0, 0, 0}));
    actor->release();
}
