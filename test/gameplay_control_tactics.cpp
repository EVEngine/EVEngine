#include "common/Capability.h"
#include "common/ECS.h"
#include "common/GameplayControl.h"
#include "tactics/Tactics.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::LogicalId action(const char* text) {
    const auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

eve::SimulationStep step(std::uint64_t tick) {
    return {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)};
}

struct Fixture {
    ecs::Table       world;
    ecs::ScopedTable guard{world};
    eve::tactics::Tactics tactics;
    eve::SubjectRef battleSubject = subject("00000000-0000-7000-8000-000000000401");
    eve::SubjectRef sideSubject   = subject("00000000-0000-7000-8000-000000000402");
    eve::SubjectRef unitSubject   = subject("00000000-0000-7000-8000-000000000403");
    ecs::EntityHandle battle;

    Fixture() {
        auto battleResult = tactics.newBattle(battleSubject, 41);
        REQUIRE(battleResult.ok());
        battle = std::move(battleResult).takeValue();
        for (int x = 0; x < 3; ++x) REQUIRE(tactics.addCell(battle, {x, 0, 0}).ok());
        auto sideResult = tactics.newSide(battle, sideSubject);
        REQUIRE(sideResult.ok());
        const auto side = std::move(sideResult).takeValue();
        auto unit = tactics.newUnit(battle, side, unitSubject, action("unit:test"), {0, 0, 0}, {1, 200, 0, 10});
        REQUIRE(unit.ok());
        std::move(unit).takeValue();
        REQUIRE(tactics.start(battle, eve::tactics::TurnPolicyKind::Initiative).ok());
        REQUIRE(tactics.advance(battle, step(1)).ok());
        REQUIRE(tactics.advance(battle, step(2)).ok());
        REQUIRE(tactics.advance(battle, step(3)).ok());
    }
};

}  // namespace

TEST_CASE_FIXTURE(Fixture, "gameplay.control.tacticsPublishesDiscoverablePlayerEquivalentPath") {
    bool found = false;
    eve::cap::forEach<eve::IGameplayControlProvider>([&](eve::IGameplayControlProvider* provider) {
        if (provider == &tactics && provider->gameplayDomain() == "tactics") found = true;
    });
    CHECK(found);

    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {unitSubject}};
    auto observation = tactics.observeGameplay(player, battleSubject);
    REQUIRE(observation.ok());
    const auto before = std::move(observation).takeValue();
    CHECK_EQ(before.tick.value(), std::uint64_t{3});

    auto available = tactics.availableGameplayActions(player, battleSubject, unitSubject);
    REQUIRE(available.ok());
    const auto descriptors = std::move(available).takeValue();
    REQUIRE_EQ(descriptors.size(), std::size_t{4});
    CHECK_EQ(descriptors.front().id.format(), std::string("tactics:move"));

    eve::GameplaySession automation{"automation", eve::GameplayAccess::TestDriver, {unitSubject}};
    auto automationAvailable =
        tactics.availableGameplayActions(automation, battleSubject, unitSubject);
    REQUIRE(automationAvailable.ok());
    const auto automationDescriptors = std::move(automationAvailable).takeValue();
    REQUIRE_EQ(automationDescriptors.size(), descriptors.size());
    for (std::size_t i = 0; i < descriptors.size(); ++i)
        CHECK_EQ(automationDescriptors[i].id, descriptors[i].id);

    eve::GameplayCommand command;
    command.id = "player-command-1";
    command.action = action("tactics:move");
    command.subject = unitSubject;
    command.observedTick = before.tick;
    command.expectedRevision = before.revision;
    command.parameters = eve::Value(eve::Value::Object{{"layer", eve::Value(0)},
                                                        {"x", eve::Value(2)},
                                                        {"y", eve::Value(0)}});
    auto submitted = tactics.submitGameplay(player, battleSubject, command);
    REQUIRE(submitted.ok());
    const auto receipt = std::move(submitted).takeValue();
    CHECK_EQ(receipt.commandId, command.id);
    CHECK(!receipt.executionId.empty());
    CHECK(receipt.resultingRevision > before.revision);

    auto events = tactics.gameplayEvents(player, battleSubject, 0);
    REQUIRE(events.ok());
    CHECK(!std::move(events).takeValue().empty());
}

TEST_CASE_FIXTURE(Fixture, "gameplay.control.tacticsRejectsUnauthorizedAndStaleCommandsWithoutMutation") {
    const auto intruder = subject("00000000-0000-7000-8000-000000000404");
    eve::GameplaySession unauthorized{"intruder", eve::GameplayAccess::PlayerEquivalent, {intruder}};
    auto denied = tactics.availableGameplayActions(unauthorized, battleSubject, unitSubject);
    CHECK(!denied.ok());

    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {unitSubject}};
    auto observation = tactics.observeGameplay(player, battleSubject);
    REQUIRE(observation.ok());
    const auto before = std::move(observation).takeValue();

    eve::GameplayCommand stale;
    stale.id = "stale-command";
    stale.action = action("tactics:wait");
    stale.subject = unitSubject;
    stale.observedTick = eve::SimulationTick(2);
    stale.expectedRevision = before.revision;
    stale.parameters = eve::Value(eve::Value::Object{});
    auto rejected = tactics.submitGameplay(player, battleSubject, stale);
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.code(), eve::StatusCode::Conflict);

    auto afterResult = tactics.observeGameplay(player, battleSubject);
    REQUIRE(afterResult.ok());
    const auto after = std::move(afterResult).takeValue();
    CHECK_EQ(after.revision, before.revision);
    CHECK_EQ(after.tick, before.tick);
}

TEST_CASE("gameplay.control.providerLifecycleMakesMissingConfigurationObservable") {
    const auto before = eve::cap::listenerCount<eve::IGameplayControlProvider>();
    {
        ecs::Table       world;
        ecs::ScopedTable guard(world);
        eve::tactics::Tactics tactics;
        CHECK_EQ(eve::cap::listenerCount<eve::IGameplayControlProvider>(), before + 1);
    }
    CHECK_EQ(eve::cap::listenerCount<eve::IGameplayControlProvider>(), before);
}
