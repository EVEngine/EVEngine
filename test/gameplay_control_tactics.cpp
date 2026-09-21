#include "common/Capability.h"
#include "common/ECS.h"
#include "common/GameplayControl.h"
#include "common/GameplayControlJson.h"
#include "common/GameplayInstanceCatalog.h"
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
    // move / face / wait / end-turn / use-ability: the controller face exposes the
    // generic action protocol too, not just movement.
    REQUIRE_EQ(descriptors.size(), std::size_t{5});
    CHECK_EQ(descriptors.front().id.format(), std::string("tactics:move"));
    CHECK_EQ(descriptors.back().id.format(), std::string("tactics:use-ability"));
    // An ability declaration needs the action id, both targets and the opaque payload.
    const auto* abilitySchema = descriptors.back().parameterSchema.getIf<eve::Value::Object>();
    REQUIRE(abilitySchema != nullptr);
    CHECK(abilitySchema->contains("action"));
    CHECK(abilitySchema->contains("payload"));
    CHECK(abilitySchema->contains("targetUnit"));

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

    // The controller face can declare an ability declaration too, carrying both targets
    // and the caller-owned payload through the same validator the script face uses.
    auto afterMove = tactics.observeGameplay(player, battleSubject);
    REQUIRE(afterMove.ok());
    const auto moved = std::move(afterMove).takeValue();
    eve::GameplayCommand ability;
    ability.id = "player-command-2";
    ability.action = action("tactics:use-ability");
    ability.subject = unitSubject;
    ability.observedTick = moved.tick;
    ability.expectedRevision = moved.revision;
    ability.parameters = eve::Value(eve::Value::Object{
        {"action", eve::Value(std::string("unit:test"))},
        {"layer", eve::Value(0)},
        {"payload", eve::Value(std::string("{\"power\":2}"))},
        {"targetUnit", eve::Value(unitSubject.format())},
        {"x", eve::Value(2)},
        {"y", eve::Value(0)}});
    auto declared = tactics.submitGameplay(player, battleSubject, ability);
    REQUIRE(declared.ok());
    const auto abilityReceipt = std::move(declared).takeValue();
    CHECK_EQ(abilityReceipt.commandId, ability.id);
    const auto* details = abilityReceipt.details.getIf<eve::Value::Object>();
    REQUIRE(details != nullptr);
    // One action point was spent, and the declaration names the ability and the target.
    CHECK_EQ(details->at("action").asString(), std::string("unit:test"));
    CHECK_EQ(details->at("target").asString(), unitSubject.format());
    CHECK_EQ(details->at("remainingActionPoints").asInt(), std::int64_t{0});

    // The same command is rejected once the point is gone: the face reports the refusal
    // instead of silently accepting a declaration that could not be paid for.
    auto again = tactics.observeGameplay(player, battleSubject);
    REQUIRE(again.ok());
    const auto afterAbility = std::move(again).takeValue();
    ability.id = "player-command-3";
    ability.observedTick = afterAbility.tick;
    ability.expectedRevision = afterAbility.revision;
    auto refused = tactics.submitGameplay(player, battleSubject, ability);
    CHECK(!refused.ok());
    CHECK_EQ(refused.code(), eve::StatusCode::Rejected);
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

TEST_CASE("gameplay.control.tacticsEnumeratesItsBattleInstances") {
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
    eve::tactics::Tactics tactics;

    const auto first  = subject("00000000-0000-7000-8000-0000000004a1");
    const auto second = subject("00000000-0000-7000-8000-0000000004a2");
    const auto one    = tactics.newBattle(first, 41);
    const auto two    = tactics.newBattle(second, 42);
    REQUIRE(one.ok());
    REQUIRE(two.ok());

    // 实例身份就是 battle 的 subject，与 observeGameplay 解析用的键一致。
    const auto instances = tactics.gameplayInstances();
    REQUIRE_EQ(instances.size(), std::size_t{2});
    CHECK_EQ(instances[0].format(), first.format());
    CHECK_EQ(instances[1].format(), second.format());

    eve::IGameplayInstanceCatalog* catalog = nullptr;
    eve::cap::forEach<eve::IGameplayInstanceCatalog>([&](auto* candidate) {
        if (candidate != nullptr && candidate->gameplayDomain() == "tactics") catalog = candidate;
    });
    REQUIRE(catalog != nullptr);

    auto scoped = eve::executeGameplayControlJson(
        R"({"schemaId":"evengine.gameplay-control-request","schemaVersion":1,"op":"instances","domain":"tactics"})");
    REQUIRE(scoped.ok());
    CHECK(scoped.value().find(first.format()) != std::string::npos);
    CHECK(scoped.value().find(second.format()) != std::string::npos);

    // 目录在 provider 生命周期内注册与注销。
    CHECK_EQ(eve::cap::listenerCount<eve::IGameplayInstanceCatalog>(),
             eve::cap::listenerCount<eve::IGameplayControlProvider>());
}
