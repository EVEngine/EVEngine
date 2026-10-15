#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_gameplay_control_tactics)
#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "common/ECS.h"
#include "common/GameplayControl.h"
#include "common/GameplayControlJson.h"
#include "common/GameplayInstanceCatalog.h"
#include "tactics/Tactics.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// gameplay_control_tactics.cpp
namespace test_gameplay_control_tactics_unit {
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

eve::SimulationStep step(std::uint64_t tick) { return {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)}; }

struct Fixture {
    ecs::Table            world;
    ecs::ScopedTable      guard{world};
    eve::tactics::Tactics tactics;
    eve::SubjectRef       battleSubject = subject("00000000-0000-7000-8000-000000000401");
    eve::SubjectRef       sideSubject   = subject("00000000-0000-7000-8000-000000000402");
    eve::SubjectRef       unitSubject   = subject("00000000-0000-7000-8000-000000000403");
    ecs::EntityHandle     battle;

    Fixture() {
        auto battleResult = tactics.newBattle(battleSubject, 41);
        REQUIRE(battleResult.ok());
        battle = std::move(battleResult).takeValue();
        for (int x = 0; x < 3; ++x) REQUIRE(tactics.addCell(battle, {x, 0, 0}).ok());
        auto sideResult = tactics.newSide(battle, sideSubject);
        REQUIRE(sideResult.ok());
        const auto side = std::move(sideResult).takeValue();
        auto       unit = tactics.newUnit(battle, side, unitSubject, action("unit:test"), {0, 0, 0}, {1, 200, 0, 10});
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
    auto                 observation = tactics.observeGameplay(player, battleSubject);
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
    auto                 automationAvailable = tactics.availableGameplayActions(automation, battleSubject, unitSubject);
    REQUIRE(automationAvailable.ok());
    const auto automationDescriptors = std::move(automationAvailable).takeValue();
    REQUIRE_EQ(automationDescriptors.size(), descriptors.size());
    for (std::size_t i = 0; i < descriptors.size(); ++i) CHECK_EQ(automationDescriptors[i].id, descriptors[i].id);

    eve::GameplayCommand command;
    command.id               = "player-command-1";
    command.action           = action("tactics:move");
    command.subject          = unitSubject;
    command.observedTick     = before.tick;
    command.expectedRevision = before.revision;
    command.parameters =
        eve::Value(eve::Value::Object{{"layer", eve::Value(0)}, {"x", eve::Value(2)}, {"y", eve::Value(0)}});
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
    const auto           moved = std::move(afterMove).takeValue();
    eve::GameplayCommand ability;
    ability.id               = "player-command-2";
    ability.action           = action("tactics:use-ability");
    ability.subject          = unitSubject;
    ability.observedTick     = moved.tick;
    ability.expectedRevision = moved.revision;
    ability.parameters       = eve::Value(eve::Value::Object{{"action", eve::Value(std::string("unit:test"))},
                                                             {"layer", eve::Value(0)},
                                                             {"payload", eve::Value(std::string("{\"power\":2}"))},
                                                             {"targetUnit", eve::Value(unitSubject.format())},
                                                             {"x", eve::Value(2)},
                                                             {"y", eve::Value(0)}});
    auto declared            = tactics.submitGameplay(player, battleSubject, ability);
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
    const auto afterAbility  = std::move(again).takeValue();
    ability.id               = "player-command-3";
    ability.observedTick     = afterAbility.tick;
    ability.expectedRevision = afterAbility.revision;
    auto refused             = tactics.submitGameplay(player, battleSubject, ability);
    CHECK(!refused.ok());
    CHECK_EQ(refused.code(), eve::StatusCode::Rejected);
}

TEST_CASE_FIXTURE(Fixture, "gameplay.control.tacticsRejectsUnauthorizedAndStaleCommandsWithoutMutation") {
    const auto           intruder = subject("00000000-0000-7000-8000-000000000404");
    eve::GameplaySession unauthorized{"intruder", eve::GameplayAccess::PlayerEquivalent, {intruder}};
    auto                 denied = tactics.availableGameplayActions(unauthorized, battleSubject, unitSubject);
    CHECK(!denied.ok());

    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {unitSubject}};
    auto                 observation = tactics.observeGameplay(player, battleSubject);
    REQUIRE(observation.ok());
    const auto before = std::move(observation).takeValue();

    eve::GameplayCommand stale;
    stale.id               = "stale-command";
    stale.action           = action("tactics:wait");
    stale.subject          = unitSubject;
    stale.observedTick     = eve::SimulationTick(2);
    stale.expectedRevision = before.revision;
    stale.parameters       = eve::Value(eve::Value::Object{});
    auto rejected          = tactics.submitGameplay(player, battleSubject, stale);
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
        ecs::Table            world;
        ecs::ScopedTable      guard(world);
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
}  // namespace test_gameplay_control_tactics_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tactics_ability)
#include "PrivateTestScope.h"
#include <algorithm>
#include <utility>
#include <vector>
#include "common/ECS.h"
#include "common/GameplayControl.h"
#include "tactics/Tactics.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// tactics_ability.cpp
namespace test_tactics_ability_unit {
namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::SimulationStep step(std::uint64_t tick) { return {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)}; }

/** @brief One side with an acting unit and a slower ally, ready to declare abilities. */
struct Fixture {
    ecs::Table            world;
    ecs::ScopedTable      guard{world};
    eve::tactics::Tactics tactics;
    ecs::EntityHandle     battle{};
    eve::SubjectRef       battleSubject = subject("00000000-0000-0000-0000-000000000500");
    eve::SubjectRef       sideSubject   = subject("00000000-0000-0000-0000-000000000501");
    eve::SubjectRef       unitSubject   = subject("00000000-0000-0000-0000-000000000502");
    // Lower initiative, so the acting unit is always `unitSubject` under every policy
    // this file uses, and the ally can be defeated or targeted without disturbing it.
    eve::SubjectRef allySubject = subject("00000000-0000-0000-0000-000000000503");

    /** @param actionPoints per-round action points, so exhaustion is reachable. */
    void build(int actionPoints = 2) {
        auto created = tactics.newBattle(battleSubject, 17);
        REQUIRE(created.ok());
        battle = std::move(created).takeValue();
        REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
        REQUIRE(tactics.addCell(battle, {1, 0, 0}).ok());
        REQUIRE(tactics.addCell(battle, {2, 0, 0}).ok());
        auto side = tactics.newSide(battle, sideSubject);
        REQUIRE(side.ok());
        REQUIRE(tactics.newUnit(battle, side.value(), unitSubject, {}, {0, 0, 0}, {actionPoints, 300, 0, 10}).ok());
        REQUIRE(tactics.newUnit(battle, side.value(), allySubject, {}, {2, 0, 0}, {1, 300, 0, 5}).ok());
    }

    /** @brief Start and step to Acting, which is the only phase that accepts an ability. */
    void enterActing() {
        REQUIRE(tactics.start(battle, eve::tactics::kInitiativePolicyId).ok());
        REQUIRE(tactics.advance(battle, step(1)).ok());
        REQUIRE(tactics.advance(battle, step(2)).ok());
        REQUIRE(tactics.advance(battle, step(3)).ok());
        auto phase = tactics.phase(battle);
        REQUIRE(phase.ok());
        REQUIRE(phase.value() == eve::tactics::BattlePhase::Acting);
    }

    [[nodiscard]] int actionPoints() {
        auto resources = tactics.unitResources(battle, unitSubject);
        REQUIRE(resources.ok());
        return resources.value().actionPoints;
    }

    [[nodiscard]] std::vector<eve::tactics::BattleCommand> commands() {
        auto commands = tactics.commandsFrom(battle, eve::Revision(0));
        REQUIRE(commands.ok());
        return std::move(commands).takeValue();
    }
};

eve::LogicalId action(const char* text) {
    const auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

}  // namespace

TEST_CASE("tactics.abilitySpendsAnActionPointAndRecordsTheDeclaration") {
    Fixture fixture;
    fixture.build(2);
    fixture.enterActing();
    REQUIRE_EQ(fixture.actionPoints(), 2);

    // The declaration names both a target cell and a target unit, plus an opaque payload:
    // all three are tactical facts the effect owner reads back from the receipt or the
    // command log.
    auto declared = fixture.tactics.useAbility(fixture.battle, fixture.unitSubject, action("test:strike"), {1, 0, 0},
                                               fixture.unitSubject, "{\"power\":3}");
    REQUIRE(declared.ok());
    // Applied, not NoOp: an ability that silently did nothing would still cost a point.
    CHECK_EQ(declared.code(), eve::StatusCode::Applied);
    CHECK_EQ(declared.value().remainingActionPoints, 1);
    CHECK_EQ(declared.value().action.format(), std::string("test:strike"));
    CHECK_EQ(declared.value().targetUnit, fixture.unitSubject);
    CHECK_EQ(fixture.actionPoints(), 1);

    const auto commands = fixture.commands();
    REQUIRE(!commands.empty());
    const auto& last = commands.back();
    CHECK(last.kind == eve::tactics::BattleCommandKind::UseAbility);
    CHECK_EQ(last.actor, fixture.unitSubject);
    CHECK_EQ(last.action.format(), std::string("test:strike"));
    const eve::tactics::Cell expectedTarget{1, 0, 0};
    CHECK(last.cell == expectedTarget);
    CHECK_EQ(last.targetUnit, fixture.unitSubject);
    CHECK_EQ(last.payload, std::string("{\"power\":3}"));
    // A declaration is not a reaction, so it must not claim a reaction trigger.
    CHECK_EQ(last.triggerSequence, std::uint64_t{0});
    // The command is replay substrate: it has to occupy a real revision slot.
    CHECK(last.resultingRevision > last.expectedRevision);

    // The event stream is the other observable face of the same fact.
    eve::GameplaySession session{"ability-test", eve::GameplayAccess::TestDriver, {fixture.unitSubject}};
    auto                 events = fixture.tactics.gameplayEvents(session, fixture.battleSubject, 0);
    REQUIRE(events.ok());
    const auto declaredEvents = std::move(events).takeValue();
    const auto found          = std::find_if(declaredEvents.begin(), declaredEvents.end(),
                                             [](const eve::GameplayEvent& event) { return event.type == "action.declared"; });
    REQUIRE(found != declaredEvents.end());
    CHECK_EQ(found->subject, fixture.unitSubject);
    // The event names the actor and points at the command that owns the rest of the
    // declaration, so a consumer joins the two instead of the framework copying the
    // action id, the targets and the payload into a second shape.
    CHECK_EQ(found->causationCommandId, "tactics:" + std::to_string(last.sequence));
}

TEST_CASE("tactics.abilityPreviewMatchesCommitWithoutMutating") {
    Fixture fixture;
    fixture.build(2);
    fixture.enterActing();

    auto preview = fixture.tactics.previewAbility(fixture.battle, fixture.unitSubject, action("test:strike"), {1, 0, 0},
                                                  fixture.unitSubject, "{\"power\":3}");
    REQUIRE(preview.ok());
    CHECK_EQ(preview.value().remainingActionPoints, 1);
    // A preview is not a commitment: no point spent, no event, no command.
    CHECK_EQ(fixture.actionPoints(), 2);
    CHECK(fixture.commands().size() == std::size_t{4});  // start + three advances

    auto declared = fixture.tactics.useAbility(fixture.battle, fixture.unitSubject, action("test:strike"), {1, 0, 0},
                                               fixture.unitSubject, "{\"power\":3}");
    REQUIRE(declared.ok());
    // The committed receipt must equal the previewed one, which is the whole point of
    // sharing a single validator.
    CHECK_EQ(declared.value().remainingActionPoints, preview.value().remainingActionPoints);
    CHECK_EQ(declared.value().targetUnit, preview.value().targetUnit);
    CHECK(declared.value().targetCell == preview.value().targetCell);
    CHECK_EQ(fixture.actionPoints(), 1);

    // A preview of something the commit would refuse must refuse it too, with the same
    // diagnostic: otherwise a UI could offer an activation the commit then rejects.
    auto refusedPreview =
        fixture.tactics.previewAbility(fixture.battle, fixture.unitSubject, action("test:strike"), {1, 0, 4});
    auto refusedCommit =
        fixture.tactics.useAbility(fixture.battle, fixture.unitSubject, action("test:strike"), {1, 0, 4});
    CHECK(!refusedPreview.ok());
    CHECK(!refusedCommit.ok());
    REQUIRE(refusedPreview.status().primaryDiagnostic() != nullptr);
    REQUIRE(refusedCommit.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(refusedPreview.status().primaryDiagnostic()->code(), refusedCommit.status().primaryDiagnostic()->code());
    CHECK_EQ(fixture.actionPoints(), 1);
}

TEST_CASE("tactics.abilityRefusesTargetsAndPayloadsItCannotCarry") {
    Fixture fixture;
    fixture.build(2);
    fixture.enterActing();

    // A subject that is not a unit of this battle is a refusal, not a dropped target.
    const auto stranger = subject("00000000-0000-0000-0000-000000000599");
    auto       foreignTarget =
        fixture.tactics.useAbility(fixture.battle, fixture.unitSubject, action("test:strike"), {1, 0, 0}, stranger);
    CHECK(!foreignTarget.ok());
    REQUIRE(foreignTarget.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(foreignTarget.status().primaryDiagnostic()->code(), eve::DiagnosticCode::NotFound);

    // A defeated unit cannot be targeted: a stale outcome must not read as "no target".
    REQUIRE(fixture.tactics.defeatUnit(fixture.battle, fixture.allySubject).ok());
    auto defeatedTarget = fixture.tactics.useAbility(fixture.battle, fixture.unitSubject, action("test:strike"),
                                                     {1, 0, 0}, fixture.allySubject);
    CHECK(!defeatedTarget.ok());
    REQUIRE(defeatedTarget.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(defeatedTarget.status().primaryDiagnostic()->code(), eve::DiagnosticCode::PreconditionViolation);

    // The payload is bounded because it is persisted: an oversized blob is refused
    // instead of silently truncating or unboundedly growing a snapshot.
    const std::string oversized(eve::tactics::kMaxAbilityPayloadBytes + 1, 'x');
    auto tooLarge = fixture.tactics.useAbility(fixture.battle, fixture.unitSubject, action("test:strike"), {1, 0, 0},
                                               {}, oversized);
    CHECK(!tooLarge.ok());
    REQUIRE(tooLarge.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(tooLarge.status().primaryDiagnostic()->code(), eve::DiagnosticCode::InvalidArgument);

    // The exact bound is accepted, and so is an ability with no target unit at all.
    const std::string atBound(eve::tactics::kMaxAbilityPayloadBytes, 'x');
    auto              atBoundResult =
        fixture.tactics.useAbility(fixture.battle, fixture.unitSubject, action("test:strike"), {1, 0, 0}, {}, atBound);
    REQUIRE(atBoundResult.ok());
    CHECK(!atBoundResult.value().targetUnit.isValid());
    CHECK_EQ(fixture.commands().back().payload, atBound);
}

TEST_CASE("tactics.abilityRefusesWhenActionPointsAreExhaustedWithoutMutating") {
    Fixture fixture;
    fixture.build(1);
    fixture.enterActing();

    REQUIRE(fixture.tactics.useAbility(fixture.battle, fixture.unitSubject, action("test:strike"), {1, 0, 0}).ok());
    CHECK_EQ(fixture.actionPoints(), 0);

    const auto commandsBefore = fixture.commands();

    auto refused = fixture.tactics.useAbility(fixture.battle, fixture.unitSubject, action("test:strike"), {1, 0, 0});
    CHECK(!refused.ok());
    REQUIRE(refused.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(refused.status().primaryDiagnostic()->code(), eve::DiagnosticCode::PreconditionViolation);

    // A refusal must cost nothing and log nothing.
    CHECK_EQ(fixture.actionPoints(), 0);
    CHECK_EQ(fixture.commands().size(), commandsBefore.size());
}

TEST_CASE("tactics.abilityRefusesWrongActorLayerAndActionShape") {
    Fixture fixture;
    fixture.build(2);
    fixture.enterActing();

    // A different subject does not own the activation even when it exists.
    auto other      = subject("00000000-0000-0000-0000-000000000503");
    auto wrongActor = fixture.tactics.useAbility(fixture.battle, other, action("test:strike"), {1, 0, 0});
    CHECK(!wrongActor.ok());
    REQUIRE(wrongActor.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(wrongActor.status().primaryDiagnostic()->code(), eve::DiagnosticCode::PreconditionViolation);

    // An invalid action id is a validation failure, not a precondition failure.
    auto emptyAction = fixture.tactics.useAbility(fixture.battle, fixture.unitSubject, eve::LogicalId{}, {1, 0, 0});
    CHECK(!emptyAction.ok());
    REQUIRE(emptyAction.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(emptyAction.status().primaryDiagnostic()->code(), eve::DiagnosticCode::InvalidArgument);

    // An off-board layer cannot be a target: the board owns cell existence.
    auto wrongLayer = fixture.tactics.useAbility(fixture.battle, fixture.unitSubject, action("test:strike"), {1, 0, 4});
    CHECK(!wrongLayer.ok());
    REQUIRE(wrongLayer.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(wrongLayer.status().primaryDiagnostic()->code(), eve::DiagnosticCode::InvalidArgument);

    // Every refusal above must leave the activation untouched and unrecorded.
    CHECK_EQ(fixture.actionPoints(), 2);
    const auto commands = fixture.commands();
    CHECK(std::none_of(commands.begin(), commands.end(), [](const eve::tactics::BattleCommand& command) {
        return command.kind == eve::tactics::BattleCommandKind::UseAbility;
    }));
}
}  // namespace test_tactics_ability_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tactics_board) ||      \
    defined(EVE_TEST_ORIGIN_tactics_edges) || defined(EVE_TEST_ORIGIN_tactics_graph) || \
    defined(EVE_TEST_ORIGIN_tactics_line_of_sight) || defined(EVE_TEST_ORIGIN_tactics_path)
#include "PrivateTestScope.h"
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <utility>
#include <vector>
#include "common/Capability.h"
#include "common/ECS.h"
#include "common/Module.h"
#include "common/Snapshot.h"
#include "hexmap/HexSphereTopology.h"
#include "sensing/LineOfSightRouter.h"
#include "sensing/Targeting.h"
#include "tactics/LineOfSight.h"
#include "tactics/Tactics.h"
#include "tactics/TacticsLineOfSightAdapter.h"
#include "tactics/TacticsPath.h"
#include "tactics/TacticsTypes.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Responsibility: tactics_board_geometry
namespace test_tactics_board_geometry {
// tactics_board.cpp
namespace {
namespace tactics_board {
namespace {

[[maybe_unused]] eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

}  // namespace

TEST_CASE("tactics.boardMaintainsAtomicOccupancyIndexes") {
    eve::tactics::BoardState board;
    REQUIRE(board.addCell({0, 0, 0}).ok());
    REQUIRE(board.addCell({1, 0, 0}).ok());
    REQUIRE(board.addCell({2, 0, 0}, eve::tactics::CellState{100, 0, false, {}}).ok());

    const auto alpha = subject("00000000-0000-0000-0000-000000000001");
    const auto beta  = subject("00000000-0000-0000-0000-000000000002");
    REQUIRE(board.place(alpha, {0, 0, 0}).ok());
    REQUIRE(board.place(beta, {1, 0, 0}).ok());

    auto occupied = board.move(alpha, {1, 0, 0});
    CHECK(!occupied.ok());
    CHECK_EQ(occupied.code(), eve::StatusCode::Conflict);
    CHECK((board.position(alpha) == eve::tactics::Cell{0, 0, 0}));
    CHECK(board.occupant({1, 0, 0}) == beta);

    auto blocked = board.move(alpha, {2, 0, 0});
    CHECK(!blocked.ok());
    CHECK_EQ(blocked.code(), eve::StatusCode::Rejected);
    CHECK((board.position(alpha) == eve::tactics::Cell{0, 0, 0}));
    REQUIRE(board.validateInvariants().ok());
}

TEST_CASE("tactics.boardEnumeratesTopologyDeterministically") {
    eve::tactics::BoardState board;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) REQUIRE(board.addCell({x, y, 0}).ok());
    }

    const auto square = board.neighbours({0, 0, 0});
    REQUIRE_EQ(square.size(), 4u);
    CHECK((square[0] == eve::tactics::Cell{1, 0, 0}));
    CHECK((square[1] == eve::tactics::Cell{0, 1, 0}));

    board.setTopology(eve::tactics::BoardTopology::HexAxial);
    const auto hex = board.neighbours({0, 0, 0});
    REQUIRE_EQ(hex.size(), 6u);
    CHECK((hex[0] == eve::tactics::Cell{1, 0, 0}));
    CHECK((hex[1] == eve::tactics::Cell{1, -1, 0}));
}
}  // namespace tactics_board
}  // namespace

// tactics_edges.cpp
namespace {
namespace tactics_edges {
namespace {

[[maybe_unused]] eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

[[maybe_unused]] eve::SimulationStep step(std::uint64_t tick) {
    return {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)};
}

/** @brief Deterministic non-cryptographic test digest, mirroring the replay tests. */
[[maybe_unused]] eve::SnapshotHashProvider testHash() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        std::uint64_t left  = 14695981039346656037ull;
        std::uint64_t right = 1099511628211ull;
        for (const unsigned char byte : input) {
            left  = (left ^ byte) * 1099511628211ull;
            right = (right ^ (static_cast<std::uint64_t>(byte) + 0x9e3779b97f4a7c15ull)) * 14029467366897019727ull;
        }
        eve::ContentId::Bytes bytes{};
        for (int index = 0; index < 8; ++index) {
            bytes[static_cast<std::size_t>(index)]     = static_cast<std::uint8_t>(left >> (56 - index * 8));
            bytes[static_cast<std::size_t>(index + 8)] = static_cast<std::uint8_t>(right >> (56 - index * 8));
        }
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

}  // namespace

TEST_CASE("tactics.edgeDeclarationsAreValidatedAndDirected") {
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
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
    auto badCost       = tactics.addEdge(battle, {0, 0, 0}, {1, 0, 0}, negative);
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
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
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
    ecs::Table            secondWorld;
    ecs::ScopedTable      secondGuard(secondWorld);
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
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
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
}  // namespace tactics_edges
}  // namespace

// tactics_graph.cpp
namespace {
namespace tactics_graph {
TEST_CASE("tactics.graphSpherePentagonsUseRealEdges") {
    auto sphere = eve::hexmap::HexSphereTopology::build(1);
    REQUIRE(sphere.ok());
    eve::tactics::BoardState board;
    board.setTopology(eve::tactics::BoardTopology::ExplicitGraph);
    for (int i = 0; i < sphere.value().cellCount(); ++i) REQUIRE(board.addCell({i, 0, 0}, {1}).ok());
    for (int i = 0; i < sphere.value().cellCount(); ++i)
        for (int d = 0; d < sphere.value().neighborCount(i); ++d)
            REQUIRE(board.addEdge({i, 0, 0}, {sphere.value().neighbor(i, d), 0, 0}).ok());
    const auto id = eve::PersistentId::parse("00000000-0000-0000-0000-000000009101");
    REQUIRE(id.has_value());
    const auto actor = eve::SubjectRef::fromPersistentId(*id);
    REQUIRE(board.place(actor, {0, 0, 0}).ok());
    auto reached = eve::tactics::PathQuery::reachable(board, actor, 1);
    REQUIRE(reached.ok());
    CHECK_EQ(board.neighbours({0, 0, 0}).size(), 5u);
    CHECK_EQ(reached.value().cells().size(), 6u);
    for (const auto& cell : reached.value().cells())
        CHECK((cell.cell.x == 0 || board.tryEdge({0, 0, 0}, cell.cell).has_value()));
    CHECK(!eve::tactics::GridLineOfSightPolicy().visible(board, {0, 0, 0}, {1, 0, 0}).ok());
    CHECK(!eve::tactics::PathQuery::cellsInRange(board, {0, 0, 0}, 0, 2, eve::tactics::CellRangeMetric::Hex).ok());
}
}  // namespace tactics_graph
}  // namespace

// tactics_line_of_sight.cpp
namespace {
namespace tactics_line_of_sight {
// tactics_line_of_sight.cpp
namespace {

using eve::tactics::BoardState;
using eve::tactics::BoardTopology;
using eve::tactics::Cell;
using eve::tactics::CellRangeMetric;
using eve::tactics::CellState;

[[maybe_unused]] bool isBlocked(const std::vector<Cell>& blockers, Cell cell) {
    return std::find(blockers.begin(), blockers.end(), cell) != blockers.end();
}

/**
 * @brief A 5x5 square board, with chosen cells created as sight blockers or left off.
 *
 * Blockers are created as **passable** cells carrying the sight-blocker tag, because that is
 * the case the tag exists for: smoke or a low wall stops sight without stopping movement.
 */
[[maybe_unused]] BoardState squareBoard(const std::vector<Cell>& blockers = {}, const std::vector<Cell>& missing = {}) {
    BoardState board;
    board.setTopology(BoardTopology::Square8);
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x) {
            const Cell cell{x, y, 0};
            if (isBlocked(missing, cell)) continue;
            const bool               blocked = isBlocked(blockers, cell);
            std::vector<std::string> tags;
            if (blocked) tags.emplace_back(eve::tactics::kSightBlockerTag);
            REQUIRE(board.addCell(cell, CellState{100, 0, true, std::move(tags)}).ok());
        }
    return board;
}

/** @brief A hex board large enough for the axis traces. */
[[maybe_unused]] BoardState hexBoard(const std::vector<Cell>& blockers = {}) {
    BoardState board;
    board.setTopology(BoardTopology::HexAxial);
    for (int q = -3; q <= 3; ++q)
        for (int r = -3; r <= 3; ++r) {
            const Cell               cell{q, r, 0};
            const bool               blocked = isBlocked(blockers, cell);
            std::vector<std::string> tags;
            if (blocked) tags.emplace_back(eve::tactics::kSightBlockerTag);
            REQUIRE(board.addCell(cell, CellState{100, 0, true, std::move(tags)}).ok());
        }
    return board;
}

}  // namespace

TEST_CASE("tactics.gridLineOfSightBlocksOnlyStrictlyBetweenTheEndpoints") {
    const auto policy = eve::tactics::gridLineOfSightPolicy();
    const Cell origin{0, 0, 0};
    const Cell far{4, 0, 0};

    CHECK(policy->visible(squareBoard(), origin, far).value());
    // A blocker on the line blocks.
    CHECK(!policy->visible(squareBoard({{2, 0, 0}}), origin, far).value());
    // Endpoints never block: a unit in smoke can still see out of its own cell, and a blocker
    // on the target cell protects the target without hiding it.
    CHECK(policy->visible(squareBoard({{0, 0, 0}, {4, 0, 0}}), origin, far).value());
    // A blocker off the line does not block.
    CHECK(policy->visible(squareBoard({{2, 1, 0}}), origin, far).value());
    // A cell the board does not contain blocks: the unused grid is not a free firing lane.
    CHECK(!policy->visible(squareBoard({}, {{2, 0, 0}}), origin, far).value());
}

TEST_CASE("tactics.gridLineOfSightIsSymmetricAcrossAWholeBoardSweep") {
    const auto policy = eve::tactics::gridLineOfSightPolicy();

    // The gap analysis flagged that the repository had no field-of-view symmetry coverage at
    // all. This sweeps every ordered pair on a board with several blockers, so a trace that
    // disagreed with its own reverse cannot hide behind one hand-picked case.
    const auto board = squareBoard({{1, 1, 0}, {2, 3, 0}, {3, 1, 0}});

    std::size_t pairs = 0;
    for (int ay = 0; ay < 5; ++ay)
        for (int ax = 0; ax < 5; ++ax)
            for (int by = 0; by < 5; ++by)
                for (int bx = 0; bx < 5; ++bx) {
                    const Cell a{ax, ay, 0};
                    const Cell b{bx, by, 0};
                    const auto forward  = policy->visible(board, a, b);
                    const auto backward = policy->visible(board, b, a);
                    REQUIRE(forward.ok());
                    REQUIRE(backward.ok());
                    CHECK_EQ(forward.value(), backward.value());
                    ++pairs;
                }
    CHECK_EQ(pairs, std::size_t{625});
}

TEST_CASE("tactics.gridLineOfSightUsesTheBoardsOwnTopology") {
    const auto policy = eve::tactics::gridLineOfSightPolicy();

    // Hex axial: a straight line along one axis is blocked by the cell in the middle.
    CHECK(policy->visible(hexBoard(), {-3, 0, 0}, {3, 0, 0}).value());
    const auto blocked = hexBoard({{0, 0, 0}});
    CHECK(!policy->visible(blocked, {-3, 0, 0}, {3, 0, 0}).value());
    // A different axial direction also blocks through the shared cell.
    CHECK(!policy->visible(blocked, {-2, 2, 0}, {2, -2, 0}).value());
    // And the hex trace is symmetric too.
    CHECK(policy->visible(blocked, {3, 0, 0}, {-3, 0, 0}).value() ==
          policy->visible(blocked, {-3, 0, 0}, {3, 0, 0}).value());

    // Refusals are structured, not silent: another layer is a rule this policy does not own,
    // and an unknown cell is missing data rather than "not visible".
    auto crossLayer = policy->visible(hexBoard(), {-3, 0, 0}, {3, 0, 1});
    CHECK(!crossLayer.ok());
    REQUIRE(crossLayer.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(crossLayer.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Unsupported);
    auto unknown = policy->visible(hexBoard(), {-3, 0, 0}, {9, 9, 0});
    CHECK(!unknown.ok());
    REQUIRE(unknown.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(unknown.status().primaryDiagnostic()->code(), eve::DiagnosticCode::NotFound);
}

TEST_CASE("tactics.gridCoverSeparatesInTheWayFromBeside") {
    const auto policy = eve::tactics::gridCoverPolicy();
    const Cell attacker{0, 2, 0};
    const Cell target{4, 2, 0};
    CHECK(policy->cover(squareBoard(), attacker, target).value() == eve::tactics::CoverLevel::None);

    // A blocker between the two is full cover: the shot is absorbed. Note the blocker is
    // passable, which is what makes sight a separate fact from movement.
    CHECK(policy->cover(squareBoard({{2, 2, 0}}), attacker, target).value() == eve::tactics::CoverLevel::Full);
    // A blocker merely adjacent to the target is half cover: harder to hit, not absorbed.
    CHECK(policy->cover(squareBoard({{4, 1, 0}}), attacker, target).value() == eve::tactics::CoverLevel::Half);
    // A blocker next to the attacker is neither: it does not protect the target.
    CHECK(policy->cover(squareBoard({{1, 2, 0}}), attacker, target).value() == eve::tactics::CoverLevel::None);
    // Cover must not depend on which way the query runs.
    CHECK(policy->cover(squareBoard({{4, 1, 0}}), target, attacker).value() == eve::tactics::CoverLevel::Half);
    CHECK_EQ(eve::tactics::coverLevelName(eve::tactics::CoverLevel::Full), std::string_view("full"));
}

TEST_CASE("tactics.visibleCellsInRangeComposesRangeWithSight") {
    const auto policy = eve::tactics::gridLineOfSightPolicy();
    const auto board  = squareBoard();
    const Cell origin{2, 2, 0};

    auto open = eve::tactics::visibleCellsInRange(board, *policy, origin, 1, 2, CellRangeMetric::Chebyshev);
    REQUIRE(open.ok());
    // With nothing blocking, the visible set is exactly the range set.
    auto range = eve::tactics::PathQuery::cellsInRange(board, origin, 1, 2, CellRangeMetric::Chebyshev);
    REQUIRE(range.ok());
    CHECK(open.value() == range.value());
    CHECK(!open.value().empty());

    // Blocking one ring cell removes everything strictly behind it from the same query.
    const auto blocked = squareBoard({{3, 2, 0}});
    auto       visible = eve::tactics::visibleCellsInRange(blocked, *policy, origin, 1, 2, CellRangeMetric::Chebyshev);
    REQUIRE(visible.ok());
    CHECK(visible.value().size() < range.value().size());
    const Cell behind{4, 2, 0};
    CHECK(std::find(visible.value().begin(), visible.value().end(), behind) == visible.value().end());

    // Invalid bounds and unknown origins are refusals, and the range rule stays their
    // authority: this query composes the two rules instead of restating either.
    CHECK(!eve::tactics::visibleCellsInRange(board, *policy, origin, 3, 1, CellRangeMetric::Chebyshev).ok());
    CHECK(!eve::tactics::visibleCellsInRange(board, *policy, {9, 9, 0}, 1, 2, CellRangeMetric::Chebyshev).ok());
}

TEST_CASE("tactics.lineOfSightPoliciesAreCapabilityProviders") {
    // The interfaces exist so a different board can answer these questions instead of the
    // tactics core growing a second geometry. This proves the capability shape works, which is
    // what an adapter (for example a hex map with its own projection) registers through.
    const auto sight = eve::tactics::gridLineOfSightPolicy();
    eve::cap::provide<eve::tactics::ILineOfSightPolicy>(sight.get());
    auto* queriedSight = eve::cap::query<eve::tactics::ILineOfSightPolicy>();
    REQUIRE(queriedSight != nullptr);
    CHECK_EQ(std::string(queriedSight->id()), std::string("grid_line_of_sight"));

    const auto cover = eve::tactics::gridCoverPolicy();
    eve::cap::provide<eve::tactics::ICoverPolicy>(cover.get());
    auto* queriedCover = eve::cap::query<eve::tactics::ICoverPolicy>();
    REQUIRE(queriedCover != nullptr);
    CHECK_EQ(std::string(queriedCover->id()), std::string("grid_cover"));
    // Revoking leaves the slot empty again, which is the "provider absent" configuration a
    // caller has to handle: an absent provider is a null query, never a silent default.
    eve::cap::revoke<eve::tactics::ILineOfSightPolicy>(sight.get());
    CHECK(eve::cap::query<eve::tactics::ILineOfSightPolicy>() == nullptr);
    eve::cap::revoke<eve::tactics::ICoverPolicy>(cover.get());

    CHECK_EQ(std::string(eve::tactics::ILineOfSightPolicy::capabilityName),
             std::string("eve.tactics.ILineOfSightPolicy"));
    CHECK_EQ(std::string(eve::tactics::ICoverPolicy::capabilityName), std::string("eve.tactics.ICoverPolicy"));
}

// tactics_line_of_sight_provider.cpp
namespace {

[[maybe_unused]] eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

[[maybe_unused]] eve::sensing::TargetLocation gridAt(std::int32_t x, std::int32_t y) {
    return eve::sensing::TargetLocation(eve::sensing::GridPoint::grid2D(x, y));
}

[[maybe_unused]] eve::sensing::TargetLocation grid3At(std::int32_t x, std::int32_t y, std::int32_t z) {
    return eve::sensing::TargetLocation(eve::sensing::GridPoint::grid3D(x, y, z));
}

[[maybe_unused]] eve::sensing::TargetLocation worldAt(float x, float y, float z) {
    auto point = eve::sensing::WorldPoint::world3D(x, y, z);
    REQUIRE(point.ok());
    return eve::sensing::TargetLocation(std::move(point).takeValue());
}

}  // namespace

TEST_CASE("tactics.gridLineOfSightIsReachableFromTheSensingCapability") {
    // Importing the module claims the grid spaces with the sensing router: the targeting pipeline
    // asks through the capability and reaches the tactics adapter.
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
    eve::tactics::Tactics tactics;

    auto router = eve::sensing::ensureLineOfSightRouter();
    REQUIRE(router.ok());
    auto* registered = eve::cap::query<eve::sensing::ILineOfSightQuery>();
    REQUIRE(registered == router.value());
    const auto spaces = router.value()->spaces();
    // Both grid spaces are claimed by tactics; the world spaces stay available for physics.
    CHECK(router.value()->hasProvider(eve::sensing::CoordinateSpace::Grid2D));
    CHECK(router.value()->hasProvider(eve::sensing::CoordinateSpace::Grid3D));
    CHECK(!router.value()->hasProvider(eve::sensing::CoordinateSpace::World3D));
    CHECK_EQ(spaces.size(), std::size_t{2});

    // Nothing is bound yet: the answer is Unsupported, not "not visible".
    auto unbound = registered->query(gridAt(0, 0), gridAt(3, 0));
    CHECK(!unbound.ok());
    REQUIRE(unbound.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(unbound.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Unsupported);
    CHECK_EQ(unbound.status().primaryDiagnostic()->message(),
             std::string("no tactics battle is bound for grid line of sight"));

    // A world-space query stays refused by the router even though the adapter is registered.
    auto crossSpace = registered->query(gridAt(0, 0), worldAt(1.f, 0.f, 0.f));
    CHECK(!crossSpace.ok());
    CHECK_EQ(crossSpace.status().primaryDiagnostic()->code(), eve::DiagnosticCode::InvalidArgument);
    (void)tactics;
}

TEST_CASE("tactics.boundBattleAnswersGridLineOfSightAndRefusesASecondBoard") {
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
    eve::tactics::Tactics tactics;

    auto first = tactics.newBattle(subject("00000000-0000-0000-0000-000000000c10"), 31);
    REQUIRE(first.ok());
    const auto firstBattle = std::move(first).takeValue();
    for (int x = 0; x < 4; ++x) REQUIRE(tactics.addCell(firstBattle, {x, 0, 0}).ok());

    auto* registered = eve::cap::query<eve::sensing::ILineOfSightQuery>();
    REQUIRE(registered != nullptr);
    REQUIRE(tactics.attachLineOfSightBoard(firstBattle).ok());

    // An open corridor is visible, and the layered variant maps z to the tactical layer.
    auto open = registered->query(gridAt(0, 0), gridAt(3, 0));
    REQUIRE(open.ok());
    CHECK(open.value().visible);
    // The blocker field is intentionally empty: the grid policy answers visibility, not identity.
    CHECK(!open.value().blocker.has_value());
    auto layered = registered->query(grid3At(0, 0, 0), grid3At(3, 0, 0));
    REQUIRE(layered.ok());
    CHECK(layered.value().visible);
    // A cell the board does not contain is missing data, not "visible".
    auto missing = registered->query(gridAt(0, 0), gridAt(9, 9));
    CHECK(!missing.ok());
    REQUIRE(missing.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(missing.status().primaryDiagnostic()->code(), eve::DiagnosticCode::NotFound);

    // One board at a time: a second, different battle is refused rather than silently replacing
    // the first, because the answer would otherwise depend on binding order.
    auto second = tactics.newBattle(subject("00000000-0000-0000-0000-000000000c11"), 32);
    REQUIRE(second.ok());
    const auto secondBattle = std::move(second).takeValue();
    REQUIRE(tactics.addCell(secondBattle, {0, 0, 0}).ok());
    auto conflict = tactics.attachLineOfSightBoard(secondBattle);
    CHECK(!conflict.ok());
    REQUIRE(conflict.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(conflict.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Conflict);
    // Re-binding the same battle is a NoOp, and a foreign detach is a NoOp too.
    auto again = tactics.attachLineOfSightBoard(firstBattle);
    REQUIRE(again.ok());
    CHECK(again.code() == eve::StatusCode::NoOp);
    auto foreignDetach = tactics.detachLineOfSightBoard(secondBattle);
    REQUIRE(foreignDetach.ok());
    CHECK(foreignDetach.code() == eve::StatusCode::NoOp);

    // Detaching returns to "unbound": Unsupported again, never a stale board answer.
    auto detached = tactics.detachLineOfSightBoard(firstBattle);
    REQUIRE(detached.ok());
    CHECK(detached.code() == eve::StatusCode::Applied);
    CHECK(!eve::tactics::tacticsLineOfSightAdapter().hasBoundBattle());
    auto afterDetach = registered->query(gridAt(0, 0), gridAt(3, 0));
    CHECK(!afterDetach.ok());
    CHECK_EQ(afterDetach.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Unsupported);
}

TEST_CASE("tactics.sightBlockerCellMakesTheGridQueryOpaque") {
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
    eve::tactics::Tactics tactics;

    auto created = tactics.newBattle(subject("00000000-0000-0000-0000-000000000c20"), 33);
    REQUIRE(created.ok());
    const auto battle = std::move(created).takeValue();
    REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
    // The blocking fact is the persisted cell tag, written through the same setup path a game uses,
    // so the adapter reads exactly what the module's own range query reads: one authority for
    // "what blocks sight" and no second schema.
    eve::tactics::CellState blocker;
    blocker.tags.emplace_back(eve::tactics::kSightBlockerTag);
    REQUIRE(tactics.addCell(battle, {1, 0, 0}, blocker).ok());
    REQUIRE(tactics.addCell(battle, {2, 0, 0}).ok());
    auto* registered = eve::cap::query<eve::sensing::ILineOfSightQuery>();
    REQUIRE(registered != nullptr);
    REQUIRE(tactics.attachLineOfSightBoard(battle).ok());

    auto blocked = registered->query(gridAt(0, 0), gridAt(2, 0));
    REQUIRE(blocked.ok());
    CHECK(!blocked.value().visible);
    // Symmetry holds because the policy computes the trace from the canonical cell order.
    auto reversed = registered->query(gridAt(2, 0), gridAt(0, 0));
    REQUIRE(reversed.ok());
    CHECK_EQ(reversed.value().visible, blocked.value().visible);
    // Endpoints never block: the blocking cell is still visible from itself.
    auto self = registered->query(gridAt(1, 0), gridAt(1, 0));
    REQUIRE(self.ok());
    CHECK(self.value().visible);
}
}  // namespace tactics_line_of_sight
}  // namespace

// tactics_path.cpp
namespace {
namespace tactics_path {
namespace {

[[maybe_unused]] eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

}  // namespace

TEST_CASE("tactics.reachabilityUsesFixedCostsAndOccupancy") {
    eve::tactics::BoardState board;
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 3; ++x) REQUIRE(board.addCell({x, y, 0}).ok());
    }
    const auto mover   = subject("00000000-0000-0000-0000-000000000031");
    const auto blocker = subject("00000000-0000-0000-0000-000000000032");
    REQUIRE(board.place(mover, {0, 0, 0}).ok());
    REQUIRE(board.place(blocker, {1, 0, 0}).ok());

    auto reachable = eve::tactics::PathQuery::reachable(board, mover, 400);
    REQUIRE(reachable.ok());
    CHECK(reachable.value().contains({0, 0, 0}));
    CHECK(!reachable.value().contains({1, 0, 0}));
    CHECK(reachable.value().contains({2, 0, 0}));
    auto cost = reachable.value().cost({2, 0, 0});
    REQUIRE(cost.ok());
    CHECK_EQ(std::move(cost).takeValue(), 400);
}

TEST_CASE("tactics.equalCostPathHasStableCoordinateTieBreak") {
    eve::tactics::BoardState board;
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) REQUIRE(board.addCell({x, y, 0}).ok());
    }
    const auto mover = subject("00000000-0000-0000-0000-000000000033");
    REQUIRE(board.place(mover, {0, 0, 0}).ok());

    auto path = eve::tactics::PathQuery::path(board, mover, {1, 1, 0}, 200);
    REQUIRE(path.ok());
    const auto cells = std::move(path).takeValue();
    REQUIRE_EQ(cells.size(), 3u);
    CHECK((cells[0] == eve::tactics::Cell{0, 0, 0}));
    CHECK((cells[1] == eve::tactics::Cell{1, 0, 0}));
    CHECK((cells[2] == eve::tactics::Cell{1, 1, 0}));
}

TEST_CASE("tactics.cellsInRangeUsesStableLogicalMetrics") {
    eve::tactics::BoardState board;
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x) REQUIRE(board.addCell({x, y, 0}).ok());

    auto manhattan =
        eve::tactics::PathQuery::cellsInRange(board, {0, 0, 0}, 1, 1, eve::tactics::CellRangeMetric::Manhattan);
    REQUIRE(manhattan.ok());
    CHECK_EQ(manhattan.value().size(), 4u);
    CHECK_EQ(manhattan.value().front(), (eve::tactics::Cell{0, -1, 0}));

    auto chebyshev =
        eve::tactics::PathQuery::cellsInRange(board, {0, 0, 0}, 2, 2, eve::tactics::CellRangeMetric::Chebyshev);
    REQUIRE(chebyshev.ok());
    CHECK_EQ(chebyshev.value().size(), 16u);

    auto invalid = eve::tactics::PathQuery::cellsInRange(board, {0, 0, 0}, 3, 2, eve::tactics::CellRangeMetric::Hex);
    CHECK(!invalid.ok());
}
}  // namespace tactics_path
}  // namespace
}  // namespace test_tactics_board_geometry
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tactics_interaction)
#include "PrivateTestScope.h"
#include <utility>
#include "common/ECS.h"
#include "tactics/Interaction.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// tactics_interaction.cpp
namespace test_tactics_interaction_unit {
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

const eve::SubjectRef kActor = subject("00000000-0000-0000-0000-000000000700");
const eve::SubjectRef kAlly  = subject("00000000-0000-0000-0000-000000000701");
const eve::SubjectRef kEnemy = subject("00000000-0000-0000-0000-000000000702");

/** @brief A context where the controlled unit is the one allowed to act. */
eve::tactics::InteractionContext actingContext() {
    eve::tactics::InteractionContext context;
    context.status         = eve::tactics::BattleStatus::Running;
    context.phase          = eve::tactics::BattlePhase::Acting;
    context.activeUnit     = kActor;
    context.controlledUnit = kActor;
    context.controlledCell = {0, 0, 0};
    context.reachableCells = {{1, 0, 0}, {0, 1, 0}};
    context.unitsByCell    = {{{0, 0, 0}, kActor}, {{2, 0, 0}, kEnemy}, {{0, 2, 0}, kAlly}};
    context.revision       = eve::Revision(7);
    return context;
}

}  // namespace

TEST_CASE("tactics.interactionSessionIsBlockedExceptOnTheActiveUnitsTurn") {
    // Not running yet.
    auto pending   = actingContext();
    pending.status = eve::tactics::BattleStatus::Setup;
    auto setup     = eve::tactics::InteractionSession::create(pending);
    CHECK(setup.state() == eve::tactics::InteractionState::Blocked);
    auto refused = setup.onCellClicked({1, 0, 0});
    CHECK(!refused.ok());
    REQUIRE(refused.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(refused.status().primaryDiagnostic()->code(), eve::DiagnosticCode::PreconditionViolation);

    // Running, but somebody else is acting: the session exists and says so instead of
    // letting a click through.
    auto otherTurn       = actingContext();
    otherTurn.activeUnit = kEnemy;
    auto waiting         = eve::tactics::InteractionSession::create(otherTurn);
    CHECK(waiting.state() == eve::tactics::InteractionState::Blocked);
    CHECK(!waiting.onCellClicked({1, 0, 0}).ok());

    // Finished battle.
    auto finished   = actingContext();
    finished.status = eve::tactics::BattleStatus::Ended;
    finished.phase  = eve::tactics::BattlePhase::BattleEnd;
    auto over       = eve::tactics::InteractionSession::create(finished);
    CHECK(over.state() == eve::tactics::InteractionState::Ended);
    CHECK(!over.onEndTurn().ok());

    // The acting unit's turn: the session accepts input.
    auto live = eve::tactics::InteractionSession::create(actingContext());
    CHECK(live.state() == eve::tactics::InteractionState::AwaitSelection);
}

TEST_CASE("tactics.interactionSessionReturnsIntentsWithoutMutatingTheBattle") {
    auto session = eve::tactics::InteractionSession::create(actingContext());

    // Clicking the controlled unit selects it and reports the revision it decided against.
    auto selected = session.onCellClicked({0, 0, 0});
    REQUIRE(selected.ok());
    CHECK(selected.value().kind == eve::tactics::InteractionIntentKind::SelectUnit);
    CHECK_EQ(selected.value().actor, kActor);
    CHECK_EQ(selected.value().expected.value(), std::uint64_t{7});
    CHECK(session.state() == eve::tactics::InteractionState::UnitSelected);

    // A second click on a reachable cell turns into a move intention.
    auto move = session.onCellClicked({1, 0, 0});
    REQUIRE(move.ok());
    CHECK(move.value().kind == eve::tactics::InteractionIntentKind::MoveTo);
    const eve::tactics::Cell destination{1, 0, 0};
    CHECK(move.value().cell == destination);
    CHECK(session.state() == eve::tactics::InteractionState::Resolving);

    // A pending intention must be committed or dropped before a new one is accepted.
    auto conflicting = session.onCellClicked({0, 1, 0});
    CHECK(!conflicting.ok());
    REQUIRE(conflicting.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(conflicting.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Conflict);
    CHECK(session.resolve().ok());
    CHECK(session.state() == eve::tactics::InteractionState::AwaitSelection);

    // A click on an unreachable, unoccupied cell is normal, not an error.
    auto nothing = session.onCellClicked({5, 5, 0});
    REQUIRE(nothing.ok());
    CHECK(nothing.value().kind == eve::tactics::InteractionIntentKind::None);
    CHECK(session.state() == eve::tactics::InteractionState::AwaitSelection);
}

TEST_CASE("tactics.interactionTargetingResolvesTheUnitUnderTheCell") {
    auto session = eve::tactics::InteractionSession::create(actingContext());
    // Arming with no legal target is allowed: a UI can show the ability and get `None` for
    // every click instead of a failure that reads like a bug.
    REQUIRE(session.armAbility(action("test:strike"), {}).ok());
    CHECK(session.state() == eve::tactics::InteractionState::Targeting);
    auto noTarget = session.onCellClicked({2, 0, 0});
    REQUIRE(noTarget.ok());
    CHECK(noTarget.value().kind == eve::tactics::InteractionIntentKind::None);

    // Re-arm with a legal target set.
    REQUIRE(session.armAbility(action("test:strike"), {{2, 0, 0}}).ok());
    auto declared = session.onCellClicked({2, 0, 0});
    REQUIRE(declared.ok());
    CHECK(declared.value().kind == eve::tactics::InteractionIntentKind::UseAbilityOn);
    CHECK_EQ(declared.value().action.format(), std::string("test:strike"));
    // The intent names the unit under the cell, so the caller does not re-derive it — and
    // getting that lookup wrong is exactly the class of bug this machine exists to prevent.
    CHECK_EQ(declared.value().targetUnit, kEnemy);

    // An empty target cell still yields the intent, with no unit attached.
    REQUIRE(session.resolve().ok());
    REQUIRE(session.armAbility(action("test:strike"), {{3, 0, 0}}).ok());
    auto emptyCell = session.onCellClicked({3, 0, 0});
    REQUIRE(emptyCell.ok());
    CHECK(emptyCell.value().kind == eve::tactics::InteractionIntentKind::UseAbilityOn);
    CHECK(!emptyCell.value().targetUnit.isValid());
}

TEST_CASE("tactics.interactionCancelAndEndTurnAreExplicit") {
    auto session = eve::tactics::InteractionSession::create(actingContext());
    // Nothing armed or selected: cancelling reports "nothing to do" rather than pretending
    // the player cancelled something.
    auto idleCancel = session.onCancel();
    REQUIRE(idleCancel.ok());
    CHECK(idleCancel.value().kind == eve::tactics::InteractionIntentKind::None);

    REQUIRE(session.armAbility(action("test:strike"), {{2, 0, 0}}).ok());
    auto cancelled = session.onCancel();
    REQUIRE(cancelled.ok());
    CHECK(cancelled.value().kind == eve::tactics::InteractionIntentKind::Cancel);
    CHECK(session.state() == eve::tactics::InteractionState::AwaitSelection);
    CHECK(!session.armedAction().isValid());

    auto ended = session.onEndTurn();
    REQUIRE(ended.ok());
    CHECK(ended.value().kind == eve::tactics::InteractionIntentKind::EndTurn);
    CHECK_EQ(ended.value().actor, kActor);
    CHECK(session.state() == eve::tactics::InteractionState::Resolving);
    // Ending the turn is not confirmable: it is already a complete instruction.
    CHECK(session.resolve().ok());
}

TEST_CASE("tactics.interactionRejectsInvalidActions") {
    auto session = eve::tactics::InteractionSession::create(actingContext());
    auto invalid = session.armAbility(eve::LogicalId{}, {{2, 0, 0}});
    CHECK(!invalid.ok());
    REQUIRE(invalid.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(invalid.status().primaryDiagnostic()->code(), eve::DiagnosticCode::InvalidArgument);

    // Confirming without a pending intent is a refusal, not a silent success.
    auto nothingPending = session.markResolving();
    CHECK(!nothingPending.ok());
    REQUIRE(nothingPending.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(nothingPending.status().primaryDiagnostic()->code(), eve::DiagnosticCode::PreconditionViolation);

    // Blocked sessions refuse everything except inspection.
    auto blockedContext  = actingContext();
    blockedContext.phase = eve::tactics::BattlePhase::RoundStart;
    auto blocked         = eve::tactics::InteractionSession::create(blockedContext);
    CHECK(!blocked.armAbility(action("test:strike"), {{2, 0, 0}}).ok());
    CHECK(!blocked.onCancel().ok());
    CHECK(!blocked.markResolving().ok());

    // The protocol spellings are stable text, so a caller may log or serialize them.
    CHECK_EQ(eve::tactics::interactionStateName(eve::tactics::InteractionState::Targeting),
             std::string_view("targeting"));
    CHECK_EQ(eve::tactics::interactionIntentKindName(eve::tactics::InteractionIntentKind::UseAbilityOn),
             std::string_view("use_ability_on"));
}
}  // namespace test_tactics_interaction_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tactics_objective)
#include "PrivateTestScope.h"
#include "common/ECS.h"
#include "tactics/Tactics.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// tactics_objective.cpp
namespace test_tactics_objective_unit {
namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::LogicalId logicalId(const char* text) {
    const auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

eve::SimulationStep step(std::uint64_t tick) { return {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)}; }

}  // namespace

TEST_CASE("tactics.eliminateObjectiveEndsBattleAfterDefeatOutcome") {
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
    eve::tactics::Tactics tactics;
    auto                  battleResult = tactics.newBattle(subject("00000000-0000-0000-0000-0000000000b0"));
    REQUIRE(battleResult.ok());
    const auto battle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
    REQUIRE(tactics.addCell(battle, {1, 0, 0}).ok());
    const auto blueSubject = subject("00000000-0000-0000-0000-0000000000b1");
    const auto redSubject  = subject("00000000-0000-0000-0000-0000000000b2");
    const auto blueUnit    = subject("00000000-0000-0000-0000-0000000000b3");
    const auto redUnit     = subject("00000000-0000-0000-0000-0000000000b4");
    auto       blueSide    = tactics.newSide(battle, blueSubject);
    auto       redSide     = tactics.newSide(battle, redSubject);
    REQUIRE(blueSide.ok());
    REQUIRE(redSide.ok());
    REQUIRE(tactics.newUnit(battle, blueSide.value(), blueUnit, {}, {0, 0, 0}, {1, 100, 0, 10}).ok());
    REQUIRE(tactics.newUnit(battle, redSide.value(), redUnit, {}, {1, 0, 0}, {1, 100, 0, 5}).ok());
    eve::tactics::ObjectiveSpec objective;
    objective.id              = logicalId("test:eliminate-red");
    objective.kind            = eve::tactics::ObjectiveKind::EliminateSide;
    objective.beneficiarySide = blueSubject;
    objective.targetSide      = redSubject;
    REQUIRE(tactics.addObjective(battle, objective).ok());
    REQUIRE(tactics.start(battle, eve::tactics::TurnPolicyKind::Initiative).ok());
    REQUIRE(tactics.defeatUnit(battle, redUnit).ok());

    auto status = tactics.status(battle);
    REQUIRE(status.ok());
    CHECK_EQ(static_cast<int>(status.value()), static_cast<int>(eve::tactics::BattleStatus::Ended));
    auto* value = dynamic_cast<eve::tactics::Battle*>(ecs::try_get(battle));
    REQUIRE(value != nullptr);
    REQUIRE_EQ(value->objectives()->values.size(), 1u);
    CHECK_EQ(static_cast<int>(value->objectives()->values.front().status),
             static_cast<int>(eve::tactics::ObjectiveStatus::Completed));
}

TEST_CASE("tactics.occupyAndSurviveObjectivesUseBoardAndRoundAuthority") {
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
    eve::tactics::Tactics tactics;
    auto                  battleResult = tactics.newBattle(subject("00000000-0000-0000-0000-0000000000c0"));
    REQUIRE(battleResult.ok());
    const auto battle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
    REQUIRE(tactics.addCell(battle, {1, 0, 0}).ok());
    const auto sideSubject = subject("00000000-0000-0000-0000-0000000000c1");
    const auto unitSubject = subject("00000000-0000-0000-0000-0000000000c2");
    auto       side        = tactics.newSide(battle, sideSubject);
    REQUIRE(side.ok());
    REQUIRE(tactics.newUnit(battle, side.value(), unitSubject, {}, {0, 0, 0}, {1, 100, 0, 10}).ok());
    eve::tactics::ObjectiveSpec occupy;
    occupy.id              = logicalId("test:occupy");
    occupy.kind            = eve::tactics::ObjectiveKind::OccupyCells;
    occupy.beneficiarySide = sideSubject;
    occupy.requiredCells   = {{1, 0, 0}};
    occupy.endsBattle      = false;
    REQUIRE(tactics.addObjective(battle, occupy).ok());
    eve::tactics::ObjectiveSpec survive;
    survive.id              = logicalId("test:survive");
    survive.kind            = eve::tactics::ObjectiveKind::SurviveRounds;
    survive.beneficiarySide = sideSubject;
    survive.requiredRound   = 2;
    REQUIRE(tactics.addObjective(battle, survive).ok());
    REQUIRE(tactics.start(battle, eve::tactics::TurnPolicyKind::Initiative).ok());
    REQUIRE(tactics.advance(battle, step(1)).ok());
    REQUIRE(tactics.advance(battle, step(2)).ok());
    REQUIRE(tactics.advance(battle, step(3)).ok());
    REQUIRE(tactics.moveUnit(battle, unitSubject, {1, 0, 0}).ok());
    auto* value = dynamic_cast<eve::tactics::Battle*>(ecs::try_get(battle));
    REQUIRE(value != nullptr);
    CHECK_EQ(static_cast<int>(value->objectives()->values[0].status),
             static_cast<int>(eve::tactics::ObjectiveStatus::Completed));
    REQUIRE(tactics.waitUnit(battle, unitSubject).ok());
    REQUIRE(tactics.advance(battle, step(4)).ok());
    REQUIRE(tactics.advance(battle, step(5)).ok());
    CHECK_EQ(static_cast<int>(value->objectives()->values[1].status),
             static_cast<int>(eve::tactics::ObjectiveStatus::Completed));
    CHECK_EQ(static_cast<int>(value->turn()->status), static_cast<int>(eve::tactics::BattleStatus::Ended));
}
}  // namespace test_tactics_objective_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tactics_presentation_script)
#include "PrivateTestScope.h"
#include <algorithm>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <utility>
#include <vector>
#include "common/Capability.h"
#include "common/ECS.h"
#include "common/Module.h"
#include "tactics/LineOfSight.h"
#include "tactics/Presentation.h"
#include "tactics/Tactics.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// tactics_presentation_script.cpp
namespace test_tactics_presentation_script_unit {
namespace {

[[maybe_unused]] eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

[[maybe_unused]] eve::SimulationStep step(std::uint64_t tick) {
    return {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)};
}

/** @brief One side with two units on a straight corridor, ready to produce events. */
struct Fixture {
    ecs::Table            world;
    ecs::ScopedTable      guard{world};
    eve::tactics::Tactics tactics;
    ecs::EntityHandle     battle{};
    eve::SubjectRef       battleSubject = subject("00000000-0000-0000-0000-000000000a00");
    eve::SubjectRef       actor         = subject("00000000-0000-0000-0000-000000000a01");
    eve::SubjectRef       ally          = subject("00000000-0000-0000-0000-000000000a02");

    void build() {
        auto created = tactics.newBattle(battleSubject, 19);
        REQUIRE(created.ok());
        battle = std::move(created).takeValue();
        for (int x = 0; x < 4; ++x) REQUIRE(tactics.addCell(battle, {x, 0, 0}).ok());
        auto side = tactics.newSide(battle, subject("00000000-0000-0000-0000-000000000a03"));
        REQUIRE(side.ok());
        REQUIRE(tactics.newUnit(battle, side.value(), actor, {}, {0, 0, 0}, {2, 300, 0, 10}).ok());
        REQUIRE(tactics.newUnit(battle, side.value(), ally, {}, {3, 0, 0}, {1, 300, 0, 5}).ok());
        REQUIRE(tactics.start(battle, eve::tactics::kInitiativePolicyId).ok());
        REQUIRE(tactics.advance(battle, step(1)).ok());
        REQUIRE(tactics.advance(battle, step(2)).ok());
        REQUIRE(tactics.advance(battle, step(3)).ok());
    }
};

/** @brief A sight policy that blocks everything, to prove the registered one is used. */
class BlindPolicy final : public eve::tactics::ILineOfSightPolicy {
public:
    [[nodiscard]] std::string_view  id() const noexcept override { return "test-blind"; }
    [[nodiscard]] eve::Result<bool> visible(const eve::tactics::BoardState&, eve::tactics::Cell,
                                            eve::tactics::Cell) const override {
        return eve::Result<bool>::success(false);
    }
};

}  // namespace

TEST_CASE("tactics.presentationIntentsFollowTheEventLogAndCarryCommandGeometry") {
    Fixture fixture;
    fixture.build();

    // Turn boundaries are already in the log from `build`: the pending turn selects the actor.
    auto opening = fixture.tactics.presentationIntents(fixture.battle, 0, 5);
    REQUIRE(opening.ok());
    REQUIRE(!opening.value().empty());
    const auto selected = std::find_if(opening.value().begin(), opening.value().end(),
                                       [](const eve::tactics::PresentationCommand& command) {
                                           return command.intent.state == eve::tactics::UnitVisualState::Selected;
                                       });
    REQUIRE(selected != opening.value().end());
    CHECK_EQ(selected->intent.subject, fixture.actor);
    // Selection is cleared by the next turn, not by a timer, and returns to the unit's resting
    // state: these are the two facts a view-object API cannot state.
    CHECK(selected->revert.trigger == eve::tactics::PresentationRevertTrigger::OnNextTurn);
    CHECK(selected->revert.restingState == eve::tactics::UnitVisualState::Idle);

    // A move produces a Moving intent whose destination comes from the accepted command, which
    // the event points at. The event itself carries no geometry.
    const std::uint64_t beforeMove = selected->intent.sequence;
    REQUIRE(fixture.tactics.moveUnit(fixture.battle, fixture.actor, {1, 0, 0}).ok());
    auto moved = fixture.tactics.presentationIntents(fixture.battle, beforeMove, 5);
    REQUIRE(moved.ok());
    REQUIRE_EQ(moved.value().size(), std::size_t{1});
    CHECK(moved.value()[0].intent.state == eve::tactics::UnitVisualState::Moving);
    const eve::tactics::Cell destination{1, 0, 0};
    CHECK(moved.value()[0].intent.to == destination);
    CHECK_EQ(moved.value()[0].intent.expiresAtTick.value(), std::uint64_t{3 + 5});

    // An ability declaration names its target through the command as well.
    REQUIRE(fixture.tactics
                .useAbility(fixture.battle, fixture.actor, *eve::LogicalId::parse("test:strike"), {3, 0, 0},
                            fixture.ally, "{\"power\":1}")
                .ok());
    auto declared = fixture.tactics.presentationIntents(fixture.battle, moved.value()[0].intent.sequence, 5);
    REQUIRE(declared.ok());
    REQUIRE_EQ(declared.value().size(), std::size_t{1});
    CHECK(declared.value()[0].intent.state == eve::tactics::UnitVisualState::Attacking);
    CHECK_EQ(declared.value()[0].intent.other, fixture.ally);

    // Finishing the activation makes "finished" the resting state, so a later transient intent
    // for that unit reverts to Finished rather than to Idle.
    REQUIRE(fixture.tactics.endTurn(fixture.battle, fixture.actor).ok());
    auto completed = fixture.tactics.presentationIntents(fixture.battle, declared.value()[0].intent.sequence, 5);
    REQUIRE(completed.ok());
    REQUIRE_EQ(completed.value().size(), std::size_t{1});
    CHECK(completed.value()[0].intent.state == eve::tactics::UnitVisualState::Finished);
    CHECK(completed.value()[0].revert.trigger == eve::tactics::PresentationRevertTrigger::OnRoundStart);

    // A defeat is durable: no trigger, no deadline, and asking to revert it is refused.
    REQUIRE(fixture.tactics.defeatUnit(fixture.battle, fixture.ally).ok());
    auto defeated = fixture.tactics.presentationIntents(fixture.battle, completed.value()[0].intent.sequence, 5);
    REQUIRE(defeated.ok());
    REQUIRE_EQ(defeated.value().size(), std::size_t{1});
    CHECK(defeated.value()[0].intent.state == eve::tactics::UnitVisualState::Destroyed);
    CHECK(!defeated.value()[0].revert.isRevertable());
    CHECK_EQ(defeated.value()[0].intent.expiresAtTick.value(), std::uint64_t{0});
}

TEST_CASE("tactics.sightAwareRangeUsesTheRegisteredPolicyAndReportsWhichOne") {
    Fixture fixture;
    fixture.build();

    // Default: the built-in grid rule, reported by id so a caller never has to assume it.
    CHECK_EQ(fixture.tactics.lineOfSightAlgorithm(), std::string_view("grid_line_of_sight"));
    auto open =
        fixture.tactics.visibleCellsInRange(fixture.battle, {0, 0, 0}, 1, 3, eve::tactics::CellRangeMetric::Chebyshev);
    REQUIRE(open.ok());
    CHECK(!open.value().empty());

    // Registered: a project board replaces the rule, and the reported id changes with it.
    BlindPolicy blind;
    eve::cap::provide<eve::tactics::ILineOfSightPolicy>(&blind);
    CHECK_EQ(fixture.tactics.lineOfSightAlgorithm(), std::string_view("test-blind"));
    auto blinded =
        fixture.tactics.visibleCellsInRange(fixture.battle, {0, 0, 0}, 1, 3, eve::tactics::CellRangeMetric::Chebyshev);
    REQUIRE(blinded.ok());
    CHECK(blinded.value().empty());

    // Revoking returns to the built-in rule instead of leaving the facade pointing at a dead
    // provider.
    eve::cap::revoke<eve::tactics::ILineOfSightPolicy>(&blind);
    auto again =
        fixture.tactics.visibleCellsInRange(fixture.battle, {0, 0, 0}, 1, 3, eve::tactics::CellRangeMetric::Chebyshev);
    REQUIRE(again.ok());
    CHECK(again.value() == open.value());
}
}  // namespace test_tactics_presentation_script_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tactics_presentation)
#include "PrivateTestScope.h"
#include <utility>
#include "common/ECS.h"
#include "tactics/Presentation.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// tactics_presentation.cpp
namespace test_tactics_presentation_unit {
namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

const eve::SubjectRef kActor = subject("00000000-0000-0000-0000-000000000800");
const eve::SubjectRef kEnemy = subject("00000000-0000-0000-0000-000000000801");

eve::tactics::BattleEvent event(std::uint64_t sequence, const char* type, eve::SubjectRef subject, std::uint64_t tick) {
    eve::tactics::BattleEvent value;
    value.sequence = sequence;
    value.type     = type;
    value.subject  = subject;
    value.tick     = eve::SimulationTick(tick);
    value.from     = eve::tactics::BattlePhase::Acting;
    value.to       = eve::tactics::BattlePhase::Acting;
    return value;
}

eve::tactics::PresentationFrame frame() {
    eve::tactics::PresentationFrame value;
    value.revision       = eve::Revision(9);
    value.transientTicks = 5;
    value.restingState   = {{kActor.format(), eve::tactics::UnitVisualState::Friendly},
                            {kEnemy.format(), eve::tactics::UnitVisualState::Idle}};
    return value;
}

}  // namespace

TEST_CASE("tactics.presentationProjectsTurnAndRoundBoundaries") {
    eve::tactics::PresentationProjector projector;
    const auto                          anchor = frame();

    auto pending = projector.project(event(3, "turn.pending", kActor, 10), nullptr, anchor);
    REQUIRE(pending.ok());
    REQUIRE_EQ(pending.value().size(), std::size_t{1});
    CHECK(pending.value()[0].intent.state == eve::tactics::UnitVisualState::Selected);
    CHECK_EQ(pending.value()[0].intent.subject, kActor);
    // Selection is transient and must say what clears it: the next turn, not a guess.
    CHECK(pending.value()[0].revert.isRevertable());
    CHECK(pending.value()[0].revert.trigger == eve::tactics::PresentationRevertTrigger::OnNextTurn);
    CHECK_EQ(pending.value()[0].revert.restingState, eve::tactics::UnitVisualState::Friendly);

    auto completed = projector.project(event(4, "turn.completed", kActor, 11), nullptr, anchor);
    REQUIRE(completed.ok());
    REQUIRE_EQ(completed.value().size(), std::size_t{1});
    CHECK(completed.value()[0].intent.state == eve::tactics::UnitVisualState::Finished);
    // "Finished" lasts exactly one round: the boundary that clears it is stated, which is the
    // fact a view-object API cannot express.
    CHECK(completed.value()[0].revert.trigger == eve::tactics::PresentationRevertTrigger::OnRoundStart);

    // The round boundary itself emits nothing new; it is the trigger consumers watch.
    auto roundPending = projector.project(event(5, "round.pending", {}, 12), nullptr, anchor);
    REQUIRE(roundPending.ok());
    CHECK(roundPending.value().empty());
}

TEST_CASE("tactics.presentationTakesGeometryFromTheCommandNotTheEvent") {
    eve::tactics::PresentationProjector projector;
    const auto                          anchor = frame();

    eve::tactics::BattleCommand move;
    move.kind  = eve::tactics::BattleCommandKind::Move;
    move.actor = kActor;
    move.cell  = {1, 0, 0};

    auto moved = projector.project(event(6, "unit.moved", kActor, 20), &move, anchor);
    REQUIRE(moved.ok());
    REQUIRE_EQ(moved.value().size(), std::size_t{1});
    CHECK(moved.value()[0].intent.state == eve::tactics::UnitVisualState::Moving);
    const eve::tactics::Cell destination{1, 0, 0};
    CHECK(moved.value()[0].intent.to == destination);
    CHECK_EQ(moved.value()[0].intent.path.size(), std::size_t{1});
    // Movement is a short animation, so it expires instead of lingering.
    CHECK(moved.value()[0].revert.trigger == eve::tactics::PresentationRevertTrigger::OnExpiry);
    CHECK_EQ(moved.value()[0].intent.expiresAtTick.value(), std::uint64_t{25});

    eve::tactics::BattleCommand ability;
    ability.kind       = eve::tactics::BattleCommandKind::UseAbility;
    ability.actor      = kActor;
    ability.cell       = {2, 0, 0};
    ability.targetUnit = kEnemy;
    ability.payload    = "{\"power\":4}";

    auto declared = projector.project(event(7, "action.declared", kActor, 30), &ability, anchor);
    REQUIRE(declared.ok());
    REQUIRE_EQ(declared.value().size(), std::size_t{1});
    CHECK(declared.value()[0].intent.state == eve::tactics::UnitVisualState::Attacking);
    CHECK_EQ(declared.value()[0].intent.other, kEnemy);
    const eve::tactics::Cell targetCell{2, 0, 0};
    CHECK(declared.value()[0].intent.to == targetCell);

    // Without the command the intent is still emitted, just without geometry: guessing the
    // destination from the event would be inventing a second authority for it.
    auto geometryless = projector.project(event(8, "unit.moved", kActor, 31), nullptr, anchor);
    REQUIRE(geometryless.ok());
    REQUIRE_EQ(geometryless.value().size(), std::size_t{1});
    CHECK(geometryless.value()[0].intent.path.empty());
}

TEST_CASE("tactics.presentationRevertContractIsExplicitAndRefusesTheImpossible") {
    eve::tactics::PresentationProjector projector;
    const auto                          anchor = frame();

    auto defeated = projector.project(event(9, "unit.defeated", kEnemy, 40), nullptr, anchor);
    REQUIRE(defeated.ok());
    REQUIRE_EQ(defeated.value().size(), std::size_t{1});
    const auto& command = defeated.value()[0];
    CHECK(command.intent.state == eve::tactics::UnitVisualState::Destroyed);
    // A defeat is durable: no trigger, and no deadline pretending there is one.
    CHECK(!command.revert.isRevertable());
    CHECK(command.revert.trigger == eve::tactics::PresentationRevertTrigger::Never);
    CHECK_EQ(command.intent.expiresAtTick.value(), std::uint64_t{0});

    // Asking to revert it is a refusal, not a silent no-op that would leave the view lying.
    auto impossible = projector.revert(command.intent, command.revert);
    CHECK(!impossible.ok());
    REQUIRE(impossible.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(impossible.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Unsupported);

    // A transient intent does revert, back to the subject's resting state.
    auto selected = projector.project(event(10, "turn.pending", kActor, 41), nullptr, anchor);
    REQUIRE(selected.ok());
    REQUIRE_EQ(selected.value().size(), std::size_t{1});
    auto inverse = projector.revert(selected.value()[0].intent, selected.value()[0].revert);
    REQUIRE(inverse.ok());
    CHECK(inverse.value().state == eve::tactics::UnitVisualState::Friendly);
    CHECK_EQ(inverse.value().subject, kActor);
    CHECK_EQ(inverse.value().sequence, std::uint64_t{10});
    // The undo is itself transient, or it would become the next stale state.
    CHECK_EQ(inverse.value().expiresAtTick.value(), selected.value()[0].intent.expiresAtTick.value());
}

TEST_CASE("tactics.presentationIgnoresEventsWithNoVisualMeaning") {
    eve::tactics::PresentationProjector projector;
    const auto                          anchor = frame();
    // A random draw or an objective evaluation changes nothing a player sees; an empty
    // projection is the correct answer, not an error.
    for (const char* type : {"random.rolled", "objective.completed", "battle.started"}) {
        auto projected = projector.project(event(11, type, kActor, 50), nullptr, anchor);
        REQUIRE(projected.ok());
        CHECK(projected.value().empty());
    }
    // Stable protocol spellings, so a consumer may switch on them.
    CHECK_EQ(eve::tactics::unitVisualStateName(eve::tactics::UnitVisualState::Destroyed),
             std::string_view("destroyed"));
    CHECK_EQ(eve::tactics::presentationRevertTriggerName(eve::tactics::PresentationRevertTrigger::OnRoundStart),
             std::string_view("on_round_start"));
}
}  // namespace test_tactics_presentation_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tactics_reaction)
#include "PrivateTestScope.h"
#include "common/ECS.h"
#include "tactics/Tactics.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// tactics_reaction.cpp
namespace test_tactics_reaction_unit {
namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

}  // namespace

TEST_CASE("tactics.reactionConsumesResourceAndPreventsSameTriggerCycle") {
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
    eve::tactics::Tactics tactics;

    auto battleResult = tactics.newBattle(subject("00000000-0000-0000-0000-000000000060"));
    REQUIRE(battleResult.ok());
    const auto battle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
    REQUIRE(tactics.addCell(battle, {1, 0, 0}).ok());
    auto sideResult = tactics.newSide(battle, subject("00000000-0000-0000-0000-000000000061"));
    REQUIRE(sideResult.ok());
    const auto side     = std::move(sideResult).takeValue();
    const auto actor    = subject("00000000-0000-0000-0000-000000000062");
    const auto defender = subject("00000000-0000-0000-0000-000000000063");
    REQUIRE(tactics.newUnit(battle, side, actor, {}, {0, 0, 0}, {1, 0, 0, 20}).ok());
    REQUIRE(tactics.newUnit(battle, side, defender, {}, {1, 0, 0}, {1, 0, 1, 10}).ok());
    REQUIRE(tactics.start(battle, eve::tactics::TurnPolicyKind::Initiative).ok());
    REQUIRE(tactics.advance(battle, {eve::SimulationTick(1), eve::Duration::zero()}).ok());
    REQUIRE(tactics.advance(battle, {eve::SimulationTick(2), eve::Duration::zero()}).ok());
    REQUIRE(tactics.advance(battle, {eve::SimulationTick(3), eve::Duration::zero()}).ok());

    const auto reaction = *eve::LogicalId::parse("tactics:counter");
    auto       opened   = tactics.openReaction(battle, 4, {{defender, reaction, 5, 10}});
    REQUIRE(opened.ok());
    CHECK_EQ(std::move(opened).takeValue(), 1u);
    auto accepted = tactics.acceptReaction(battle, defender, reaction);
    REQUIRE(accepted.ok());
    CHECK_EQ(std::move(accepted).takeValue().remainingReactionPoints, 0);
    auto* battleValue = dynamic_cast<eve::tactics::Battle*>(ecs::try_get(battle));
    REQUIRE(battleValue != nullptr);
    const auto& acceptedEvent = battleValue->events()->values.back();
    CHECK(acceptedEvent.causationCommand != 0);
    CHECK(acceptedEvent.correlationCommand != 0);
    CHECK(acceptedEvent.correlationCommand != acceptedEvent.causationCommand);

    REQUIRE(tactics.openReaction(battle, 4, {{defender, reaction, 5, 10}}).ok());
    auto duplicate = tactics.acceptReaction(battle, defender, reaction);
    CHECK(!duplicate.ok());
    CHECK_EQ(duplicate.code(), eve::StatusCode::Conflict);
    REQUIRE(tactics.declineReaction(battle).ok());
}

TEST_CASE("tactics.reactionDepthIsBounded") {
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
    eve::tactics::Tactics tactics;
    auto                  battleResult = tactics.newBattle(subject("00000000-0000-0000-0000-000000000070"));
    REQUIRE(battleResult.ok());
    const auto battle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
    auto sideResult = tactics.newSide(battle, subject("00000000-0000-0000-0000-000000000071"));
    REQUIRE(sideResult.ok());
    const auto side  = std::move(sideResult).takeValue();
    const auto actor = subject("00000000-0000-0000-0000-000000000072");
    REQUIRE(tactics.newUnit(battle, side, actor, {}, {0, 0, 0}, {1, 0, 8, 1}).ok());
    REQUIRE(tactics.start(battle, eve::tactics::TurnPolicyKind::Initiative).ok());
    REQUIRE(tactics.advance(battle, {eve::SimulationTick(1), eve::Duration::zero()}).ok());
    REQUIRE(tactics.advance(battle, {eve::SimulationTick(2), eve::Duration::zero()}).ok());
    REQUIRE(tactics.advance(battle, {eve::SimulationTick(3), eve::Duration::zero()}).ok());
    const auto reaction    = *eve::LogicalId::parse("tactics:counter");
    auto*      battleValue = dynamic_cast<eve::tactics::Battle*>(ecs::try_get(battle));
    REQUIRE(battleValue != nullptr);
    for (std::uint64_t depth = 1; depth <= 8; ++depth) {
        const auto trigger = battleValue->events()->nextSequence - 1;
        REQUIRE(tactics.openReaction(battle, trigger, {{actor, reaction, 0, 1}}).ok());
    }
    const auto overflowTrigger = battleValue->events()->nextSequence - 1;
    auto       overflow        = tactics.openReaction(battle, overflowTrigger, {{actor, reaction, 0, 1}});
    CHECK(!overflow.ok());
    CHECK_EQ(overflow.code(), eve::StatusCode::Rejected);
}
}  // namespace test_tactics_reaction_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tactics_migration) || \
    defined(EVE_TEST_ORIGIN_tactics_replay) || defined(EVE_TEST_ORIGIN_tactics_snapshot_script)
#include "PrivateTestScope.h"
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include "common/Capability.h"
#include "common/ECS.h"
#include "common/Snapshot.h"
#include "common/SnapshotHash.h"
#include "tactics/Tactics.h"
#include "tactics/TacticsPersistence.h"
#include "tactics/TacticsReplay.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Responsibility: tactics_state_formats
namespace test_tactics_state_formats {
// tactics_migration.cpp
namespace {
namespace tactics_migration {
namespace {

[[maybe_unused]] eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

[[maybe_unused]] eve::SimulationStep step(std::uint64_t tick) {
    return {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)};
}

[[maybe_unused]] eve::LogicalId action(const char* text) {
    const auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

[[maybe_unused]] eve::SnapshotHashProvider testHash() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        std::uint64_t left  = 14695981039346656037ull;
        std::uint64_t right = 1099511628211ull;
        for (const unsigned char byte : input) {
            left  = (left ^ byte) * 1099511628211ull;
            right = (right ^ (static_cast<std::uint64_t>(byte) + 0x9e3779b97f4a7c15ull)) * 14029467366897019727ull;
        }
        eve::ContentId::Bytes bytes{};
        for (int index = 0; index < 8; ++index) {
            bytes[static_cast<std::size_t>(index)]     = static_cast<std::uint8_t>(left >> (56 - index * 8));
            bytes[static_cast<std::size_t>(index + 8)] = static_cast<std::uint8_t>(right >> (56 - index * 8));
        }
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

/**
 * @brief Rewrite a current payload into a legacy *version* shape.
 *
 * Every field group introduced after `target` is removed, so the result is exactly what
 * that version would have written:
 *  - v1 predates the board edge set;
 *  - v1-v2 predate the stable policy id (they stored the numeric enum `1`, the frozen
 *    mapping to `initiative`);
 *  - v1-v4 predate the persisted scheduling charge and the explicit round schedule;
 *  - v1-v5 predate the ability target unit and its opaque payload on every command record.
 *
 * Deliberately defensive instead of using REQUIRE: it returns a value, and a
 * failing lookup should degrade to "unchanged payload" so the test fails on the
 * restore assertion rather than on an unrelated precondition.
 */
[[maybe_unused]] eve::Value legacyPayload(const eve::Value& current, eve::SchemaVersion target) {
    auto* root = current.getIf<eve::Value::Object>();
    if (root == nullptr) return current;
    eve::Value::Object payload = *root;

    if (target < eve::SchemaVersion(6)) {
        auto commandsIt = payload.find("commands");
        if (commandsIt != payload.end()) {
            if (auto* commands = commandsIt->second.getIf<eve::Value::Object>(); commands != nullptr) {
                auto valuesIt = commands->find("values");
                if (valuesIt != commands->end()) {
                    if (auto* records = valuesIt->second.getIf<eve::Value::Array>(); records != nullptr) {
                        for (auto& record : *records) {
                            if (auto* fields = record.getIf<eve::Value::Object>(); fields != nullptr) {
                                fields->erase("targetUnit");
                                fields->erase("payload");
                            }
                        }
                    }
                }
            }
        }
    }

    if (target == eve::SchemaVersion(1)) {
        auto boardIt = payload.find("board");
        if (boardIt != payload.end()) {
            if (auto* board = boardIt->second.getIf<eve::Value::Object>(); board != nullptr) board->erase("edges");
        }
    }
    if (target < eve::SchemaVersion(3)) {
        payload["policy"] = eve::Value(1);
        auto commandsIt   = payload.find("commands");
        if (commandsIt != payload.end()) {
            if (auto* commands = commandsIt->second.getIf<eve::Value::Object>(); commands != nullptr) {
                auto valuesIt = commands->find("values");
                if (valuesIt != commands->end()) {
                    if (auto* records = valuesIt->second.getIf<eve::Value::Array>(); records != nullptr) {
                        for (auto& record : *records) {
                            if (auto* fields = record.getIf<eve::Value::Object>(); fields != nullptr)
                                (*fields)["policy"] = eve::Value(1);
                        }
                    }
                }
            }
        }
    }
    if (target < eve::SchemaVersion(5)) {
        payload.erase("schedule");
        auto unitsIt = payload.find("units");
        if (unitsIt != payload.end()) {
            if (auto* records = unitsIt->second.getIf<eve::Value::Array>(); records != nullptr) {
                for (auto& record : *records) {
                    if (auto* fields = record.getIf<eve::Value::Object>(); fields != nullptr) fields->erase("charge");
                }
            }
        }
    }
    return eve::Value(std::move(payload));
}

/** @brief One battle with a single side and two units, ready to be snapshotted. */
struct Fixture {
    ecs::Table            world;
    ecs::ScopedTable      guard{world};
    eve::tactics::Tactics tactics;
    ecs::EntityHandle     battle{};
    // Canonical subject order puts `slow` first, so an initiative schedule (which
    // picks the higher initiative) is distinguishable from a side/tie schedule.
    eve::SubjectRef slow = subject("00000000-0000-0000-0000-000000000401");
    eve::SubjectRef fast = subject("00000000-0000-0000-0000-000000000402");

    void build() {
        auto created = tactics.newBattle(subject("00000000-0000-0000-0000-000000000400"), 31);
        REQUIRE(created.ok());
        battle = std::move(created).takeValue();
        REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
        REQUIRE(tactics.addCell(battle, {1, 0, 0}).ok());
        auto side = tactics.newSide(battle, subject("00000000-0000-0000-0000-000000000403"));
        REQUIRE(side.ok());
        // Same side, so the side index cannot break the tie; initiative must.
        REQUIRE(tactics.newUnit(battle, side.value(), slow, {}, {0, 0, 0}, {1, 0, 0, 10}).ok());
        REQUIRE(tactics.newUnit(battle, side.value(), fast, {}, {1, 0, 0}, {1, 0, 0, 20}).ok());
    }

    /** @brief Advance to Acting and return whoever the schedule selected. */
    eve::SubjectRef firstActor() {
        REQUIRE(tactics.advance(battle, step(1)).ok());
        REQUIRE(tactics.advance(battle, step(2)).ok());
        REQUIRE(tactics.advance(battle, step(3)).ok());
        auto active = tactics.activeUnit(battle);
        REQUIRE(active.ok());
        return std::move(active).takeValue();
    }

    [[nodiscard]] int chargeOf(eve::SubjectRef unit) {
        auto resources = tactics.unitResources(battle, unit);
        REQUIRE(resources.ok());
        return resources.value().charge;
    }
};

}  // namespace

TEST_CASE("tactics.versionOneSnapshotMigratesEdgesAbsentAndPolicyId") {
    auto    hash = testHash();
    Fixture source;
    source.build();
    REQUIRE(source.tactics.start(source.battle, eve::tactics::kInitiativePolicyId).ok());

    auto captured = source.tactics.snapshot(source.battle, hash);
    REQUIRE(captured.ok());
    auto current = std::move(captured).takeValue();
    CHECK(current.schemaVersion == eve::SchemaVersion(6));

    // Version 1 predates board edges, so the legacy payload drops that field.
    auto legacy = eve::makeSnapshotEnvelope(current.type, current.schema, eve::SchemaVersion(1), current.instanceId,
                                            current.revision, current.tick,
                                            legacyPayload(current.payload, eve::SchemaVersion(1)), hash);
    REQUIRE(legacy.ok());

    Fixture target;
    target.build();
    REQUIRE(target.tactics.restore(target.battle, legacy.value(), hash).ok());

    // Assert the migrated id directly, then confirm it is the one actually used.
    auto migrated = target.tactics.policyId(target.battle);
    REQUIRE(migrated.ok());
    CHECK_EQ(migrated.value(), std::string(eve::tactics::kInitiativePolicyId));

    // The numeric 1 must have migrated to `initiative`. With one side the
    // side-alternating policy would tie and fall back to canonical order, so
    // selecting the *fast* unit is only possible if the id migrated correctly.
    CHECK_EQ(target.firstActor(), target.fast);
}

TEST_CASE("tactics.versionTwoSnapshotKeepsEdgesAndMigratesPolicyId") {
    auto    hash = testHash();
    Fixture source;
    source.build();
    REQUIRE(source.tactics.start(source.battle, eve::tactics::kInitiativePolicyId).ok());

    auto captured = source.tactics.snapshot(source.battle, hash);
    REQUIRE(captured.ok());
    auto current = std::move(captured).takeValue();

    // Version 2 already carried edges, so only the policy representation differs.
    auto legacy = eve::makeSnapshotEnvelope(current.type, current.schema, eve::SchemaVersion(2), current.instanceId,
                                            current.revision, current.tick,
                                            legacyPayload(current.payload, eve::SchemaVersion(2)), hash);
    REQUIRE(legacy.ok());

    Fixture target;
    target.build();
    REQUIRE(target.tactics.restore(target.battle, legacy.value(), hash).ok());
    auto migrated = target.tactics.policyId(target.battle);
    REQUIRE(migrated.ok());
    CHECK_EQ(migrated.value(), std::string(eve::tactics::kInitiativePolicyId));
    CHECK_EQ(target.firstActor(), target.fast);
}

TEST_CASE("tactics.futureSnapshotVersionIsRejectedWithoutMutatingTarget") {
    auto    hash = testHash();
    Fixture source;
    source.build();
    auto captured = source.tactics.snapshot(source.battle, hash);
    REQUIRE(captured.ok());
    auto current = std::move(captured).takeValue();

    auto future = eve::makeSnapshotEnvelope(current.type, current.schema, eve::SchemaVersion(99), current.instanceId,
                                            current.revision, current.tick, current.payload, hash);
    REQUIRE(future.ok());

    Fixture target;
    target.build();
    auto restored = target.tactics.restore(target.battle, future.value(), hash);
    CHECK(!restored.ok());
    // A refused version must leave the target untouched, not half-restored.
    auto status = target.tactics.status(target.battle);
    REQUIRE(status.ok());
    CHECK(status.value() == eve::tactics::BattleStatus::Setup);
}

/**
 * @brief The version-3 shape is still readable once version 5 exists.
 *
 * Versions 4 and 5 only *added* fields (a command kind, then charge and the round
 * schedule), so a version-3 payload written by that version must keep restoring. This is
 * the "does not regress" half of the gate: widening a range must not invalidate the
 * older versions.
 */
TEST_CASE("tactics.versionThreeSnapshotStillRestoresCommandlessBattles") {
    auto    hash = testHash();
    Fixture source;
    source.build();
    REQUIRE(source.tactics.start(source.battle, eve::tactics::kInitiativePolicyId).ok());
    REQUIRE(source.tactics.advance(source.battle, step(1)).ok());

    auto captured = source.tactics.snapshot(source.battle, hash);
    REQUIRE(captured.ok());
    auto current = std::move(captured).takeValue();

    auto relabelled = eve::makeSnapshotEnvelope(current.type, current.schema, eve::SchemaVersion(3), current.instanceId,
                                                current.revision, current.tick,
                                                legacyPayload(current.payload, eve::SchemaVersion(3)), hash);
    REQUIRE(relabelled.ok());

    Fixture target;
    target.build();
    REQUIRE(target.tactics.restore(target.battle, relabelled.value(), hash).ok());
    auto migrated = target.tactics.policyId(target.battle);
    REQUIRE(migrated.ok());
    CHECK_EQ(migrated.value(), std::string(eve::tactics::kInitiativePolicyId));
}

/**
 * @brief A version-3 payload may not smuggle in the version-4 command kind.
 *
 * The payload bytes are exactly what version 4 writes; only the declared version
 * differs, so the ability command is the single fact under test. Without the
 * version gate the restore would succeed and a version-3 reader would later see a
 * command kind it cannot interpret.
 */
TEST_CASE("tactics.versionThreeSnapshotRefusesTheAbilityCommandKind") {
    auto    hash = testHash();
    Fixture source;
    source.build();
    REQUIRE(source.tactics.start(source.battle, eve::tactics::kInitiativePolicyId).ok());
    const auto actor = source.firstActor();
    REQUIRE(source.tactics.useAbility(source.battle, actor, action("test:strike"), {0, 0, 0}).ok());

    auto captured = source.tactics.snapshot(source.battle, hash);
    REQUIRE(captured.ok());
    auto current = std::move(captured).takeValue();

    auto relabelled = eve::makeSnapshotEnvelope(current.type, current.schema, eve::SchemaVersion(3), current.instanceId,
                                                current.revision, current.tick, current.payload, hash);
    REQUIRE(relabelled.ok());

    Fixture target;
    target.build();
    auto restored = target.tactics.restore(target.battle, relabelled.value(), hash);
    CHECK(!restored.ok());
    REQUIRE(restored.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(restored.status().primaryDiagnostic()->code(), eve::DiagnosticCode::ParseError);
    // The refusal must not leave a half-restored battle behind.
    auto status = target.tactics.status(target.battle);
    REQUIRE(status.ok());
    CHECK(status.value() == eve::tactics::BattleStatus::Setup);
    auto commands = target.tactics.commandsFrom(target.battle, eve::Revision(0));
    REQUIRE(commands.ok());
    CHECK(commands.value().empty());
}

/** @brief The version-4 round trip keeps both the cost and the declaration. */
TEST_CASE("tactics.abilityDeclarationSurvivesSnapshotRoundTrip") {
    auto    hash = testHash();
    Fixture source;
    source.build();
    REQUIRE(source.tactics.start(source.battle, eve::tactics::kInitiativePolicyId).ok());
    const auto actor = source.firstActor();
    REQUIRE(source.tactics.useAbility(source.battle, actor, action("test:strike"), {0, 0, 0}).ok());

    auto captured = source.tactics.snapshot(source.battle, hash);
    REQUIRE(captured.ok());
    auto current = std::move(captured).takeValue();
    CHECK(current.schemaVersion == eve::SchemaVersion(6));

    Fixture target;
    target.build();
    REQUIRE(target.tactics.restore(target.battle, current, hash).ok());

    // Both faces of the fact are restored: the spent point and the log entry, since
    // the log is what replay re-applies.
    auto resources = target.tactics.unitResources(target.battle, actor);
    REQUIRE(resources.ok());
    CHECK_EQ(resources.value().actionPoints, 0);

    auto commands = target.tactics.commandsFrom(target.battle, eve::Revision(0));
    REQUIRE(commands.ok());
    const auto values = std::move(commands).takeValue();
    REQUIRE(!values.empty());
    CHECK(values.back().kind == eve::tactics::BattleCommandKind::UseAbility);
    CHECK_EQ(values.back().action.format(), std::string("test:strike"));
    const eve::tactics::Cell declaredTarget{0, 0, 0};
    CHECK(values.back().cell == declaredTarget);
}

/** @brief Scheduling charge survives a round trip, so a saved round resumes where it was. */
TEST_CASE("tactics.chargeSurvivesSnapshotRoundTrip") {
    auto    hash = testHash();
    Fixture source;
    source.build();
    REQUIRE(source.tactics.start(source.battle, eve::tactics::kChargeTimeBattlePolicyId).ok());

    // Four quiet rounds leave the fast unit at 80 and the slow one at 40: a state that
    // decides who activates next, and which the round machine spent charge to reach.
    for (std::uint64_t tick = 1; tick <= 5; ++tick) REQUIRE(source.tactics.advance(source.battle, step(tick)).ok());
    REQUIRE_EQ(source.chargeOf(source.fast), 80);
    REQUIRE_EQ(source.chargeOf(source.slow), 40);

    auto captured = source.tactics.snapshot(source.battle, hash);
    REQUIRE(captured.ok());
    auto current = std::move(captured).takeValue();

    Fixture target;
    target.build();
    REQUIRE(target.tactics.restore(target.battle, current, hash).ok());
    CHECK_EQ(target.chargeOf(target.fast), 80);
    CHECK_EQ(target.chargeOf(target.slow), 40);

    // Continuing the restored battle must select the same unit the source would have:
    // the fifth gain takes the fast unit to exactly the threshold and leaves the slow
    // one short, so only the persisted charge can explain the choice.
    auto advanced = target.tactics.advance(target.battle, step(6));
    REQUIRE(advanced.ok());
    CHECK(advanced.value() == eve::tactics::BattlePhase::TurnStart);
    auto active = target.tactics.activeUnit(target.battle);
    REQUIRE(active.ok());
    CHECK_EQ(active.value(), target.fast);
    CHECK_EQ(target.chargeOf(target.fast), 0);
    CHECK_EQ(target.chargeOf(target.slow), 50);
}

/**
 * @brief A version-4 payload has no charge and no explicit schedule, and is migrated.
 *
 * Version 4 could not express charge-time scheduling, so the documented default is
 * "charge is zero, the roster is the round's queue". Both halves are asserted here:
 * the payload is accepted, and the missing state really is absent afterwards.
 */
TEST_CASE("tactics.versionFourSnapshotMigratesAbsentChargeAndSchedule") {
    auto    hash = testHash();
    Fixture source;
    source.build();
    REQUIRE(source.tactics.start(source.battle, eve::tactics::kChargeTimeBattlePolicyId).ok());
    for (std::uint64_t tick = 1; tick <= 5; ++tick) REQUIRE(source.tactics.advance(source.battle, step(tick)).ok());
    REQUIRE_EQ(source.chargeOf(source.fast), 80);

    auto captured = source.tactics.snapshot(source.battle, hash);
    REQUIRE(captured.ok());
    auto current = std::move(captured).takeValue();

    auto legacy = eve::makeSnapshotEnvelope(current.type, current.schema, eve::SchemaVersion(4), current.instanceId,
                                            current.revision, current.tick,
                                            legacyPayload(current.payload, eve::SchemaVersion(4)), hash);
    REQUIRE(legacy.ok());

    Fixture target;
    target.build();
    REQUIRE(target.tactics.restore(target.battle, legacy.value(), hash).ok());
    // The policy id is a v3 field, so it survives: the battle is still charge-scheduled,
    // it simply restarts its accumulation from the documented default.
    auto policy = target.tactics.policyId(target.battle);
    REQUIRE(policy.ok());
    CHECK_EQ(policy.value(), std::string(eve::tactics::kChargeTimeBattlePolicyId));
    CHECK_EQ(target.chargeOf(target.fast), 0);
    CHECK_EQ(target.chargeOf(target.slow), 0);
    // With charge reset, the fifth gain cannot reach the threshold, so the migrated
    // battle reports a quiet round instead of activating a unit on invented charge.
    auto advanced = target.tactics.advance(target.battle, step(6));
    REQUIRE(advanced.ok());
    CHECK(advanced.code() == eve::StatusCode::NoOp);
    CHECK(advanced.value() == eve::tactics::BattlePhase::RoundStart);
}

/** @brief A version-4 payload may not carry the version-5 fields. */
TEST_CASE("tactics.versionFourSnapshotRefusesVersionFiveFields") {
    auto    hash = testHash();
    Fixture source;
    source.build();
    REQUIRE(source.tactics.start(source.battle, eve::tactics::kChargeTimeBattlePolicyId).ok());
    for (std::uint64_t tick = 1; tick <= 5; ++tick) REQUIRE(source.tactics.advance(source.battle, step(tick)).ok());
    auto captured = source.tactics.snapshot(source.battle, hash);
    REQUIRE(captured.ok());
    auto current = std::move(captured).takeValue();

    // Keep the v5 payload bytes and only declare version 4: the unit charge and the
    // round schedule are then the single facts under test.
    auto mislabelled =
        eve::makeSnapshotEnvelope(current.type, current.schema, eve::SchemaVersion(4), current.instanceId,
                                  current.revision, current.tick, current.payload, hash);
    REQUIRE(mislabelled.ok());

    Fixture target;
    target.build();
    auto restored = target.tactics.restore(target.battle, mislabelled.value(), hash);
    CHECK(!restored.ok());
    REQUIRE(restored.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(restored.status().primaryDiagnostic()->code(), eve::DiagnosticCode::ParseError);
    auto status = target.tactics.status(target.battle);
    REQUIRE(status.ok());
    CHECK(status.value() == eve::tactics::BattleStatus::Setup);
}

/** @brief The version-5 round trip keeps the targeted unit and the opaque payload. */
TEST_CASE("tactics.abilityTargetAndPayloadSurviveSnapshotRoundTrip") {
    auto    hash = testHash();
    Fixture source;
    source.build();
    REQUIRE(source.tactics.start(source.battle, eve::tactics::kInitiativePolicyId).ok());
    const auto actor = source.firstActor();
    REQUIRE(
        source.tactics.useAbility(source.battle, actor, action("test:strike"), {1, 0, 0}, source.slow, "{\"power\":7}")
            .ok());

    auto captured = source.tactics.snapshot(source.battle, hash);
    REQUIRE(captured.ok());
    auto current = std::move(captured).takeValue();

    Fixture target;
    target.build();
    REQUIRE(target.tactics.restore(target.battle, current, hash).ok());

    auto commands = target.tactics.commandsFrom(target.battle, eve::Revision(0));
    REQUIRE(commands.ok());
    const auto values = std::move(commands).takeValue();
    REQUIRE(!values.empty());
    CHECK(values.back().kind == eve::tactics::BattleCommandKind::UseAbility);
    // The payload is caller-owned effect data: tactics must return it byte-identically,
    // because a replay hands it back to the effect owner.
    CHECK_EQ(values.back().payload, std::string("{\"power\":7}"));
    CHECK_EQ(values.back().targetUnit, target.slow);
    const eve::tactics::Cell declaredTarget{1, 0, 0};
    CHECK(values.back().cell == declaredTarget);
}

/**
 * @brief A version-5 payload has no ability target or payload, and is migrated.
 *
 * Version 5 could not express either, so the documented default is "no target unit, empty
 * payload". Both halves are asserted: the payload restores, and the fields really are
 * empty afterwards instead of being invented.
 */
TEST_CASE("tactics.versionFiveSnapshotMigratesAbsentAbilityTargetAndPayload") {
    auto    hash = testHash();
    Fixture source;
    source.build();
    REQUIRE(source.tactics.start(source.battle, eve::tactics::kInitiativePolicyId).ok());
    const auto actor = source.firstActor();
    REQUIRE(
        source.tactics.useAbility(source.battle, actor, action("test:strike"), {1, 0, 0}, source.slow, "{\"power\":7}")
            .ok());

    auto captured = source.tactics.snapshot(source.battle, hash);
    REQUIRE(captured.ok());
    auto current = std::move(captured).takeValue();

    auto legacy = eve::makeSnapshotEnvelope(current.type, current.schema, eve::SchemaVersion(5), current.instanceId,
                                            current.revision, current.tick,
                                            legacyPayload(current.payload, eve::SchemaVersion(5)), hash);
    REQUIRE(legacy.ok());

    Fixture target;
    target.build();
    REQUIRE(target.tactics.restore(target.battle, legacy.value(), hash).ok());
    auto commands = target.tactics.commandsFrom(target.battle, eve::Revision(0));
    REQUIRE(commands.ok());
    const auto values = std::move(commands).takeValue();
    REQUIRE(!values.empty());
    // The declaration itself survives: only the fields version 5 could not express are
    // migrated to their documented defaults.
    CHECK(values.back().kind == eve::tactics::BattleCommandKind::UseAbility);
    CHECK_EQ(values.back().action.format(), std::string("test:strike"));
    CHECK(!values.back().targetUnit.isValid());
    CHECK(values.back().payload.empty());
}

/** @brief A version-5 payload may not carry the version-6 command fields. */
TEST_CASE("tactics.versionFiveSnapshotRefusesVersionSixFields") {
    auto    hash = testHash();
    Fixture source;
    source.build();
    REQUIRE(source.tactics.start(source.battle, eve::tactics::kInitiativePolicyId).ok());
    const auto actor = source.firstActor();
    REQUIRE(
        source.tactics.useAbility(source.battle, actor, action("test:strike"), {1, 0, 0}, source.slow, "{\"power\":7}")
            .ok());
    auto captured = source.tactics.snapshot(source.battle, hash);
    REQUIRE(captured.ok());
    auto current = std::move(captured).takeValue();

    // Keep the v6 payload bytes and only declare version 5: the two extra command fields
    // are then the single facts under test.
    auto mislabelled =
        eve::makeSnapshotEnvelope(current.type, current.schema, eve::SchemaVersion(5), current.instanceId,
                                  current.revision, current.tick, current.payload, hash);
    REQUIRE(mislabelled.ok());

    Fixture target;
    target.build();
    auto restored = target.tactics.restore(target.battle, mislabelled.value(), hash);
    CHECK(!restored.ok());
    REQUIRE(restored.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(restored.status().primaryDiagnostic()->code(), eve::DiagnosticCode::ParseError);
    auto status = target.tactics.status(target.battle);
    REQUIRE(status.ok());
    CHECK(status.value() == eve::tactics::BattleStatus::Setup);
}
}  // namespace tactics_migration
}  // namespace

// tactics_replay.cpp
namespace {
namespace tactics_replay {
namespace {

[[maybe_unused]] eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

[[maybe_unused]] eve::SnapshotHashProvider testHash() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        std::uint64_t left  = 14695981039346656037ull;
        std::uint64_t right = 1099511628211ull;
        for (const unsigned char byte : input) {
            left  = (left ^ byte) * 1099511628211ull;
            right = (right ^ (static_cast<std::uint64_t>(byte) + 0x9e3779b97f4a7c15ull)) * 14029467366897019727ull;
        }
        eve::ContentId::Bytes bytes{};
        for (int index = 0; index < 8; ++index) {
            bytes[static_cast<std::size_t>(index)]     = static_cast<std::uint8_t>(left >> (56 - index * 8));
            bytes[static_cast<std::size_t>(index + 8)] = static_cast<std::uint8_t>(right >> (56 - index * 8));
        }
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

[[maybe_unused]] eve::SimulationStep step(std::uint64_t tick) {
    return {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)};
}

}  // namespace

TEST_CASE("tactics.snapshotRoundTripAndFailureIsTransactional") {
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
    eve::tactics::Tactics tactics;
    const auto            battleSubject = subject("00000000-0000-0000-0000-000000000080");
    const auto            unitSubject   = subject("00000000-0000-0000-0000-000000000082");
    auto                  battleResult  = tactics.newBattle(battleSubject, 91);
    REQUIRE(battleResult.ok());
    const auto battleHandle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battleHandle, {0, 0, 0}).ok());
    REQUIRE(tactics.addCell(battleHandle, {1, 0, 0}).ok());
    auto sideResult = tactics.newSide(battleHandle, subject("00000000-0000-0000-0000-000000000081"));
    REQUIRE(sideResult.ok());
    const auto firstSide       = std::move(sideResult).takeValue();
    auto       otherSideResult = tactics.newSide(battleHandle, subject("00000000-0000-0000-0000-000000000083"));
    REQUIRE(otherSideResult.ok());
    const auto otherSide  = std::move(otherSideResult).takeValue();
    const auto definition = eve::LogicalId::parse("test:scout");
    REQUIRE(definition.has_value());
    auto unitResult = tactics.newUnit(battleHandle, firstSide, unitSubject, *definition, {0, 0, 0}, {1, 100, 1, 10});
    REQUIRE(unitResult.ok());
    const auto unitHandle = std::move(unitResult).takeValue();
    REQUIRE(tactics.start(battleHandle, eve::tactics::TurnPolicyKind::Initiative).ok());
    REQUIRE(tactics.advance(battleHandle, step(1)).ok());
    REQUIRE(tactics.advance(battleHandle, step(2)).ok());
    REQUIRE(tactics.advance(battleHandle, step(3)).ok());

    auto* battle = dynamic_cast<eve::tactics::Battle*>(ecs::try_get(battleHandle));
    auto* unit   = dynamic_cast<eve::tactics::TacticalUnit*>(ecs::try_get(unitHandle));
    REQUIRE(battle != nullptr);
    REQUIRE(unit != nullptr);
    const auto hash     = testHash();
    auto       snapshot = eve::tactics::TacticsPersistence::snapshot(*battle, hash);
    REQUIRE(snapshot.ok());
    REQUIRE(tactics.moveUnit(battleHandle, unitSubject, {1, 0, 0}).ok());
    unit->membership()->side     = otherSide;
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

    auto unsupported          = snapshot.value();
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
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
    eve::tactics::Tactics tactics;
    const auto            battleSubject = subject("00000000-0000-0000-0000-000000000090");
    const auto            unitSubject   = subject("00000000-0000-0000-0000-000000000092");
    auto                  battleResult  = tactics.newBattle(battleSubject, 12);
    REQUIRE(battleResult.ok());
    const auto battleHandle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battleHandle, {0, 0, 0}).ok());
    auto sideResult = tactics.newSide(battleHandle, subject("00000000-0000-0000-0000-000000000091"));
    REQUIRE(sideResult.ok());
    auto unitResult =
        tactics.newUnit(battleHandle, std::move(sideResult).takeValue(), unitSubject, {}, {0, 0, 0}, {1, 0, 2, 10});
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
    const auto hash     = testHash();
    auto       snapshot = eve::tactics::TacticsPersistence::snapshot(*battle, hash);
    REQUIRE(snapshot.ok());
    const auto expectedNextSequence = battle->events()->nextSequence;
    REQUIRE(tactics.declineReaction(battleHandle).ok());
    REQUIRE(eve::tactics::TacticsPersistence::restore(*battle, snapshot.value(), hash).ok());
    CHECK_EQ(battle->events()->nextSequence, expectedNextSequence);
    CHECK_EQ(battle->reactions()->stack.size(), 1u);
    CHECK_EQ(static_cast<int>(battle->turn()->phase), static_cast<int>(eve::tactics::BattlePhase::Reaction));
    auto accepted = tactics.acceptReaction(battleHandle, unitSubject, *action);
    REQUIRE(accepted.ok());
    CHECK_EQ(std::move(accepted).takeValue().remainingReactionPoints, 1);
    CHECK_EQ(battle->events()->nextSequence, expectedNextSequence + 1);
}

TEST_CASE("tactics.commandReplayProducesByteIdenticalFinalSnapshot") {
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
    eve::tactics::Tactics tactics;
    const auto            battleSubject = subject("00000000-0000-0000-0000-0000000000a0");
    const auto            unitSubject   = subject("00000000-0000-0000-0000-0000000000a2");
    auto                  battleResult  = tactics.newBattle(battleSubject, 33);
    REQUIRE(battleResult.ok());
    const auto battleHandle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battleHandle, {0, 0, 0}).ok());
    REQUIRE(tactics.addCell(battleHandle, {1, 0, 0}).ok());
    auto sideResult = tactics.newSide(battleHandle, subject("00000000-0000-0000-0000-0000000000a1"));
    REQUIRE(sideResult.ok());
    auto unitResult =
        tactics.newUnit(battleHandle, std::move(sideResult).takeValue(), unitSubject, {}, {0, 0, 0}, {1, 100, 1, 10});
    REQUIRE(unitResult.ok());
    std::move(unitResult).takeValue();
    REQUIRE(tactics.start(battleHandle, eve::tactics::TurnPolicyKind::Initiative).ok());
    REQUIRE(tactics.advance(battleHandle, step(1)).ok());
    REQUIRE(tactics.advance(battleHandle, step(2)).ok());
    REQUIRE(tactics.advance(battleHandle, step(3)).ok());
    auto* battle = dynamic_cast<eve::tactics::Battle*>(ecs::try_get(battleHandle));
    REQUIRE(battle != nullptr);
    const auto hash             = testHash();
    const auto baselineRevision = battle->turn()->revision;
    auto       baseline         = eve::tactics::TacticsPersistence::snapshot(*battle, hash);
    REQUIRE(baseline.ok());

    REQUIRE(tactics.faceUnit(battleHandle, unitSubject, 2).ok());
    const auto randomStream = eve::LogicalId::parse("combat:hit");
    REQUIRE(randomStream.has_value());
    auto firstRoll  = tactics.roll(battleHandle, *randomStream);
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

/**
 * @brief An ability declaration replays from the command log, including its targets.
 *
 * The declaration is not just "spend a point": the targeted unit and the opaque payload
 * are facts a replay has to reproduce, or the effect owner would receive a different
 * declaration after a restore than it did live.
 */
TEST_CASE("tactics.abilityDeclarationReplaysWithTargetAndPayload") {
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
    eve::tactics::Tactics tactics;
    const auto            battleSubject = subject("00000000-0000-0000-0000-0000000000b0");
    const auto            unitSubject   = subject("00000000-0000-0000-0000-0000000000b2");
    const auto            allySubject   = subject("00000000-0000-0000-0000-0000000000b3");
    auto                  battleResult  = tactics.newBattle(battleSubject, 37);
    REQUIRE(battleResult.ok());
    const auto battleHandle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battleHandle, {0, 0, 0}).ok());
    REQUIRE(tactics.addCell(battleHandle, {1, 0, 0}).ok());
    REQUIRE(tactics.addCell(battleHandle, {2, 0, 0}).ok());
    auto sideResult = tactics.newSide(battleHandle, subject("00000000-0000-0000-0000-0000000000b1"));
    REQUIRE(sideResult.ok());
    const auto side = std::move(sideResult).takeValue();
    REQUIRE(tactics.newUnit(battleHandle, side, unitSubject, {}, {0, 0, 0}, {2, 100, 1, 10}).ok());
    REQUIRE(tactics.newUnit(battleHandle, side, allySubject, {}, {2, 0, 0}, {1, 100, 1, 5}).ok());
    REQUIRE(tactics.start(battleHandle, eve::tactics::TurnPolicyKind::Initiative).ok());
    REQUIRE(tactics.advance(battleHandle, step(1)).ok());
    REQUIRE(tactics.advance(battleHandle, step(2)).ok());
    REQUIRE(tactics.advance(battleHandle, step(3)).ok());
    auto* battle = dynamic_cast<eve::tactics::Battle*>(ecs::try_get(battleHandle));
    REQUIRE(battle != nullptr);
    const auto hash             = testHash();
    const auto baselineRevision = battle->turn()->revision;
    auto       baseline         = eve::tactics::TacticsPersistence::snapshot(*battle, hash);
    REQUIRE(baseline.ok());

    const auto strike = eve::LogicalId::parse("test:strike");
    REQUIRE(strike.has_value());
    REQUIRE(tactics.useAbility(battleHandle, unitSubject, *strike, {2, 0, 0}, allySubject, "{\"power\":5}").ok());
    const auto commands = eve::tactics::BattleReplay::commandsFrom(*battle, baselineRevision);
    REQUIRE_EQ(commands.size(), 1u);
    auto direct = eve::tactics::TacticsPersistence::snapshot(*battle, hash);
    REQUIRE(direct.ok());

    REQUIRE(eve::tactics::TacticsPersistence::restore(*battle, baseline.value(), hash).ok());
    REQUIRE(eve::tactics::BattleReplay::replay(*battle, commands).ok());
    auto replayed = eve::tactics::TacticsPersistence::snapshot(*battle, hash);
    REQUIRE(replayed.ok());
    CHECK_EQ(replayed.value().contentHash, direct.value().contentHash);
    CHECK_EQ(replayed.value().payload, direct.value().payload);
}
}  // namespace tactics_replay
}  // namespace

// tactics_snapshot_script.cpp
namespace {
namespace tactics_snapshot_script {
namespace {

[[maybe_unused]] eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

[[maybe_unused]] eve::SimulationStep step(std::uint64_t tick) {
    return {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)};
}

/** @brief A hasher that counts calls and delegates, to prove the registration is used. */
class CountingHasher final : public eve::ISnapshotContentHasher {
public:
    [[nodiscard]] std::string_view       id() const noexcept override { return "test-counting-noncrypto"; }
    [[nodiscard]] eve::ContentDigestKind digestKind() const noexcept override {
        return eve::ContentDigestKind::NonCryptographic;
    }
    [[nodiscard]] eve::Result<eve::ContentId> hash(std::string_view canonicalInput) const override {
        ++calls;
        return eve::builtinSnapshotContentHasher()->hash(canonicalInput);
    }

    mutable int calls = 0;
};

struct Fixture {
    ecs::Table            world;
    ecs::ScopedTable      guard{world};
    eve::tactics::Tactics tactics;
    ecs::EntityHandle     battle{};
    eve::SubjectRef       unitSubject = subject("00000000-0000-0000-0000-000000000900");

    void build() {
        auto created = tactics.newBattle(subject("00000000-0000-0000-0000-000000000901"), 11);
        REQUIRE(created.ok());
        battle = std::move(created).takeValue();
        REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
        REQUIRE(tactics.addCell(battle, {1, 0, 0}).ok());
        auto side = tactics.newSide(battle, subject("00000000-0000-0000-0000-000000000902"));
        REQUIRE(side.ok());
        REQUIRE(tactics.newUnit(battle, side.value(), unitSubject, {}, {0, 0, 0}, {2, 300, 0, 10}).ok());
        REQUIRE(tactics.start(battle, eve::tactics::kInitiativePolicyId).ok());
        REQUIRE(tactics.advance(battle, step(1)).ok());
        REQUIRE(tactics.advance(battle, step(2)).ok());
        REQUIRE(tactics.advance(battle, step(3)).ok());
    }
};

}  // namespace

TEST_CASE("tactics.builtinSnapshotHasherSaysWhatItIsAndRegistersOnceByIdempotence") {
    const auto builtin = eve::builtinSnapshotContentHasher();
    REQUIRE(builtin != nullptr);
    // The name states the guarantee, and the kind reports it: a caller never has to guess
    // whether a weak digest was used for an integrity check or claimed as security.
    CHECK_EQ(std::string(builtin->id()), std::string("fnv1a64x2-noncrypto"));
    CHECK(builtin->digestKind() == eve::ContentDigestKind::NonCryptographic);
    CHECK_EQ(eve::contentDigestKindName(builtin->digestKind()), std::string_view("non_cryptographic"));
    // It is deterministic: the same canonical input always produces the same id.
    CHECK(eve::builtinSnapshotContentHasher()->hash("canonical").value() ==
          eve::builtinSnapshotContentHasher()->hash("canonical").value());
    CHECK(eve::builtinSnapshotContentHasher()->hash("canonical").value() !=
          eve::builtinSnapshotContentHasher()->hash("canonical2").value());

    // Host boot registers it; a second call must not replace anything and says so with NoOp.
    auto first = eve::registerBuiltinSnapshotHasher();
    REQUIRE(first.ok());
    CHECK(first.code() == eve::StatusCode::Applied);
    auto second = eve::registerBuiltinSnapshotHasher();
    REQUIRE(second.ok());
    CHECK(second.code() == eve::StatusCode::NoOp);
    CHECK(eve::registeredSnapshotContentHasher() == builtin.get());
    CHECK_EQ(eve::activeSnapshotHashAlgorithm(), std::string_view("fnv1a64x2-noncrypto"));
}

TEST_CASE("tactics.snapshotBridgeUsesTheRegisteredHasherAndFallsBackExplicitly") {
    Fixture fixture;
    fixture.build();

    // Provider absent: the engine's built-in hasher is the documented default, and the caller
    // can see that from the reported algorithm id.
    CHECK(eve::registeredSnapshotContentHasher() == nullptr);
    CHECK_EQ(fixture.tactics.snapshotHashAlgorithm(), std::string_view("fnv1a64x2-noncrypto"));
    auto builtinSnapshot = fixture.tactics.snapshotJson(fixture.battle);
    REQUIRE(builtinSnapshot.ok());

    // Provider present: the registered hasher is the one that answers, and the algorithm id
    // changes with it. This is the "present" half of the optional-provider contract.
    CountingHasher counting;
    eve::cap::provide<eve::ISnapshotContentHasher>(&counting);
    CHECK_EQ(fixture.tactics.snapshotHashAlgorithm(), std::string_view("test-counting-noncrypto"));
    const int callsBefore        = counting.calls;
    auto      registeredSnapshot = fixture.tactics.snapshotJson(fixture.battle);
    REQUIRE(registeredSnapshot.ok());
    CHECK(counting.calls > callsBefore);
    // The digests agree because the counting hasher delegates; the point is which one ran.
    CHECK_EQ(registeredSnapshot.value(), builtinSnapshot.value());
    // A snapshot sealed by one hasher still verifies while that hasher is active.
    CHECK(fixture.tactics.restoreJson(fixture.battle, registeredSnapshot.value()).ok());

    // Provider absent again: revoking returns to the documented default rather than leaving the
    // bridge with a dangling provider.
    eve::cap::revoke<eve::ISnapshotContentHasher>(&counting);
    CHECK(eve::registeredSnapshotContentHasher() == nullptr);
    CHECK_EQ(fixture.tactics.snapshotHashAlgorithm(), std::string_view("fnv1a64x2-noncrypto"));
    CHECK(fixture.tactics.restoreJson(fixture.battle, builtinSnapshot.value()).ok());
}

TEST_CASE("tactics.snapshotJsonRoundTripRefusesTamperingAndForeignDocuments") {
    Fixture fixture;
    fixture.build();
    auto sealed = fixture.tactics.snapshotJson(fixture.battle);
    REQUIRE(sealed.ok());
    const std::string document = std::move(sealed).takeValue();

    // Play forward, then restore: the battle really does go back.
    REQUIRE(fixture.tactics.moveUnit(fixture.battle, fixture.unitSubject, {1, 0, 0}).ok());
    auto moved = fixture.tactics.unitResources(fixture.battle, fixture.unitSubject);
    REQUIRE(moved.ok());
    const int spent = moved.value().movePoints;
    REQUIRE(fixture.tactics.restoreJson(fixture.battle, document).ok());
    auto restored = fixture.tactics.unitResources(fixture.battle, fixture.unitSubject);
    REQUIRE(restored.ok());
    CHECK(restored.value().movePoints > spent);

    // A tampered document is refused by the digest, and the battle keeps its current state.
    std::string tampered = document;
    const auto  position = tampered.find("\"moveCost\":100");
    REQUIRE(position != std::string::npos);
    tampered.replace(position, 15, "\"moveCost\":900");
    auto refused = fixture.tactics.restoreJson(fixture.battle, tampered);
    CHECK(!refused.ok());
    REQUIRE(refused.status().primaryDiagnostic() != nullptr);
    CHECK(refused.status().primaryDiagnostic()->code() == eve::DiagnosticCode::HashMismatch);
    auto afterRefusal = fixture.tactics.unitResources(fixture.battle, fixture.unitSubject);
    REQUIRE(afterRefusal.ok());
    CHECK_EQ(afterRefusal.value().movePoints, restored.value().movePoints);

    // Text that is not an envelope at all is a parse failure, not a silent success.
    auto malformed = fixture.tactics.restoreJson(fixture.battle, "{\"not\":\"a snapshot\"}");
    CHECK(!malformed.ok());
    REQUIRE(malformed.status().primaryDiagnostic() != nullptr);
    CHECK(malformed.status().primaryDiagnostic()->code() == eve::DiagnosticCode::ParseError);
}

TEST_CASE("tactics.commandLogJsonReplaysThroughTheSameCodecAsRestore") {
    Fixture fixture;
    fixture.build();
    // The snapshot's own revision is the anchor a replay log has to start from; taking it from
    // the envelope keeps the two in step without a second accessor that could drift.
    auto envelope = fixture.tactics.snapshot(fixture.battle, eve::snapshotContentHashProvider());
    REQUIRE(envelope.ok());
    const eve::Revision baselineRevision = envelope.value().revision;
    auto                baseline         = fixture.tactics.snapshotJson(fixture.battle);
    REQUIRE(baseline.ok());
    const std::string document = std::move(baseline).takeValue();

    // A log with nothing to replay is valid, empty, and replayed as a no-op.
    auto emptyLog = fixture.tactics.commandLogJson(fixture.battle, baselineRevision);
    REQUIRE(emptyLog.ok());
    CHECK(fixture.tactics.replayJson(fixture.battle, emptyLog.value()).ok());

    // Play, capture the log, then come back and replay it.
    REQUIRE(fixture.tactics.moveUnit(fixture.battle, fixture.unitSubject, {1, 0, 0}).ok());
    REQUIRE(fixture.tactics.endTurn(fixture.battle, fixture.unitSubject).ok());
    auto log = fixture.tactics.commandLogJson(fixture.battle, baselineRevision);
    REQUIRE(log.ok());
    const std::string logText = std::move(log).takeValue();
    auto              direct  = fixture.tactics.snapshotJson(fixture.battle);
    REQUIRE(direct.ok());

    REQUIRE(fixture.tactics.restoreJson(fixture.battle, document).ok());
    auto replay = fixture.tactics.replayJson(fixture.battle, logText);
    REQUIRE(replay.ok());
    auto replayed = fixture.tactics.snapshotJson(fixture.battle);
    REQUIRE(replayed.ok());
    // Byte-identical after a restore + replay, which is what makes the log a replay substrate
    // rather than a debug view.
    CHECK_EQ(replayed.value(), direct.value());

    // A log captured against a different point in the battle is refused instead of being
    // applied onto state its commands were never accepted against.
    REQUIRE(fixture.tactics.advance(fixture.battle, step(9)).ok());
    auto staleLog = fixture.tactics.replayJson(fixture.battle, logText);
    CHECK(!staleLog.ok());
    // Garbage is a parse failure, not an empty replay.
    CHECK(!fixture.tactics.replayJson(fixture.battle, "[]").ok());
    CHECK(!fixture.tactics.replayJson(fixture.battle, "not json").ok());
}
}  // namespace tactics_snapshot_script
}  // namespace
}  // namespace test_tactics_state_formats
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tactics_charge_time) || \
    defined(EVE_TEST_ORIGIN_tactics_turn) || defined(EVE_TEST_ORIGIN_tactics_turn_policy)
#include "PrivateTestScope.h"
#include <algorithm>
#include <memory>
#include <utility>
#include <vector>
#include "common/ECS.h"
#include "tactics/Tactics.h"
#include "tactics/TurnPolicy.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Responsibility: tactics_turn_rules
namespace test_tactics_turn_rules {
// tactics_charge_time.cpp
namespace {
namespace tactics_charge_time {
namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::SimulationStep step(std::uint64_t tick) { return {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)}; }

/**
 * @brief Two units on one side, so ordering can only come from the policy.
 *
 * A single side removes the side index as an explanation for the order, and
 * differing initiatives make "who gains charge faster" the only variable.
 */
struct Fixture {
    ecs::Table            world;
    ecs::ScopedTable      guard{world};
    eve::tactics::Tactics tactics;
    ecs::EntityHandle     battle{};
    eve::SubjectRef       sideSubject = subject("00000000-0000-0000-0000-000000000600");
    eve::SubjectRef       fast        = subject("00000000-0000-0000-0000-000000000601");
    eve::SubjectRef       slow        = subject("00000000-0000-0000-0000-000000000602");

    void build(int fastInitiative = 20, int slowInitiative = 10) {
        auto created = tactics.newBattle(subject("00000000-0000-0000-0000-000000000603"), 23);
        REQUIRE(created.ok());
        battle = std::move(created).takeValue();
        REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
        REQUIRE(tactics.addCell(battle, {1, 0, 0}).ok());
        auto side = tactics.newSide(battle, sideSubject);
        REQUIRE(side.ok());
        REQUIRE(tactics.newUnit(battle, side.value(), fast, {}, {0, 0, 0}, {1, 100, 0, fastInitiative}).ok());
        REQUIRE(tactics.newUnit(battle, side.value(), slow, {}, {1, 0, 0}, {1, 100, 0, slowInitiative}).ok());
    }

    /** @brief Advance until the machine is in Acting, closing each activation as it ends. */
    struct Log {
        std::vector<eve::SubjectRef> actors;
        std::size_t                  quietRounds = 0;
    };

    Log run(int advances) {
        Log log;
        for (int i = 1; i <= advances; ++i) {
            auto phase = tactics.phase(battle);
            REQUIRE(phase.ok());
            if (phase.value() == eve::tactics::BattlePhase::Acting) {
                auto active = tactics.activeUnit(battle);
                REQUIRE(active.ok());
                log.actors.push_back(active.value());
                REQUIRE(tactics.endTurn(battle, active.value()).ok());
            }
            auto advanced = tactics.advance(battle, step(static_cast<std::uint64_t>(i)));
            REQUIRE(advanced.ok());
            // A round in which nobody reached the threshold is reported as the phase
            // it started in with NoOp, which is how a caller tells it from an activation.
            if (advanced.code() == eve::StatusCode::NoOp) ++log.quietRounds;
        }
        return log;
    }

    [[nodiscard]] int chargeOf(eve::SubjectRef unit) {
        auto resources = tactics.unitResources(battle, unit);
        REQUIRE(resources.ok());
        return resources.value().charge;
    }
};

}  // namespace

TEST_CASE("tactics.chargeTimePolicyLetsTheFasterUnitActMoreOften") {
    Fixture fixture;
    fixture.build(20, 10);
    REQUIRE(fixture.tactics.start(fixture.battle, eve::tactics::kChargeTimeBattlePolicyId).ok());

    // Hand-computed schedule at threshold 100: the 20-initiative unit reaches 100 in
    // round 5 and again in round 10, where the 10-initiative unit also arrives, so the
    // slow unit activates exactly once for every two activations of the fast one.
    const auto log = fixture.run(18);
    REQUIRE_EQ(log.actors.size(), std::size_t{3});
    CHECK_EQ(log.actors[0], fixture.fast);
    CHECK_EQ(log.actors[1], fixture.fast);
    CHECK_EQ(log.actors[2], fixture.slow);
    // Four quiet rounds before each of the two threshold crossings.
    CHECK_EQ(log.quietRounds, std::size_t{8});
    // Activation spends exactly the threshold, so the fast unit is back at zero after
    // its first activation and both are spent after the round-10 double activation.
    CHECK_EQ(fixture.chargeOf(fixture.fast), 0);
    CHECK_EQ(fixture.chargeOf(fixture.slow), 0);
}

TEST_CASE("tactics.chargeTimePolicyConsumesOnlyTheActivationCost") {
    Fixture fixture;
    fixture.build(20, 10);
    REQUIRE(fixture.tactics.start(fixture.battle, eve::tactics::kChargeTimeBattlePolicyId).ok());

    // Drive to just before the first activation: four quiet rounds leave the charges at
    // 80/40, which is the state the consumption rule has to act on.
    const auto log = fixture.run(5);
    CHECK(log.actors.empty());
    CHECK_EQ(log.quietRounds, std::size_t{4});
    CHECK_EQ(fixture.chargeOf(fixture.fast), 80);
    CHECK_EQ(fixture.chargeOf(fixture.slow), 40);

    // The fifth round-start gains once more: 100 for the fast unit, which is ready, and
    // 50 for the slow one, which is not. Only the ready unit pays.
    auto advanced = fixture.tactics.advance(fixture.battle, step(6));
    REQUIRE(advanced.ok());
    CHECK(advanced.value() == eve::tactics::BattlePhase::TurnStart);
    CHECK_EQ(fixture.chargeOf(fixture.fast), 0);
    CHECK_EQ(fixture.chargeOf(fixture.slow), 50);
}

TEST_CASE("tactics.chargeTimePolicyRefusesABattleThatCannotAccumulate") {
    Fixture fixture;
    fixture.build(0, 0);

    // A threshold no unit can ever reach is a refusal, not an endless sequence of
    // empty rounds: the battle must stay startable and untouched.
    auto refused = fixture.tactics.start(fixture.battle, eve::tactics::kChargeTimeBattlePolicyId);
    CHECK(!refused.ok());
    REQUIRE(refused.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(refused.status().primaryDiagnostic()->code(), eve::DiagnosticCode::PreconditionViolation);
    auto status = fixture.tactics.status(fixture.battle);
    REQUIRE(status.ok());
    CHECK(status.value() == eve::tactics::BattleStatus::Setup);

    // The same data schedules fine under a policy that does not use charge, which is
    // what makes this a policy requirement rather than a battle invariant.
    CHECK(fixture.tactics.start(fixture.battle, eve::tactics::kInitiativePolicyId).ok());
}

TEST_CASE("tactics.orderingPoliciesKeepOneActivationPerUnitPerRound") {
    Fixture fixture;
    fixture.build(20, 10);
    REQUIRE(fixture.tactics.start(fixture.battle, eve::tactics::kInitiativePolicyId).ok());

    // The default charge model must leave scheduling exactly as it was: both units act
    // once per round in initiative order, with no quiet round in between.
    const auto log = fixture.run(9);
    REQUIRE_EQ(log.actors.size(), std::size_t{2});
    CHECK_EQ(log.actors[0], fixture.fast);
    CHECK_EQ(log.actors[1], fixture.slow);
    CHECK_EQ(log.quietRounds, std::size_t{0});
    CHECK_EQ(fixture.chargeOf(fixture.fast), 0);
    CHECK_EQ(fixture.chargeOf(fixture.slow), 0);
}

TEST_CASE("tactics.customChargeModelIsHonouredByTheRoundMachine") {
    // A project policy declares its own charge rule without touching the round machine.
    // Ordering by initiative keeps the *order* familiar while the threshold, not the
    // built-in constant, decides *when* an activation happens.
    class LowerThresholdPolicy final : public eve::tactics::ITurnPolicy {
    public:
        [[nodiscard]] std::string_view id() const noexcept override { return "test:lower_threshold"; }

        [[nodiscard]] eve::tactics::TurnOrder order(const eve::tactics::Battle&, const eve::tactics::UnitOrder& left,
                                                    const eve::tactics::UnitOrder& right) const override {
            if (left.initiative > right.initiative) return eve::tactics::TurnOrder::LeftFirst;
            if (right.initiative > left.initiative) return eve::tactics::TurnOrder::RightFirst;
            return eve::tactics::TurnOrder::Equivalent;
        }

        [[nodiscard]] eve::tactics::ChargeModel chargeModel(const eve::tactics::Battle&) const override {
            return eve::tactics::ChargeModel{1, 40, 40};
        }
    };

    Fixture fixture;
    fixture.build(20, 10);
    auto registered = eve::tactics::TurnPolicyRegistry::builtins().add(std::make_shared<const LowerThresholdPolicy>());
    REQUIRE(registered.ok());
    REQUIRE(fixture.tactics.start(fixture.battle, "test:lower_threshold").ok());

    // Hand-computed at threshold 40: quiet, fast, quiet, then both. The built-in
    // threshold of 100 produces a single activation over the same twelve advances, so
    // this can only pass if the model the policy declares is the one being applied.
    const auto log = fixture.run(12);
    REQUIRE_EQ(log.actors.size(), std::size_t{3});
    CHECK_EQ(log.actors[0], fixture.fast);
    CHECK_EQ(log.actors[1], fixture.fast);
    CHECK_EQ(log.actors[2], fixture.slow);
    CHECK_EQ(log.quietRounds, std::size_t{2});
}

TEST_CASE("tactics.chargeRuleWithoutProgressIsRefused") {
    // Declares a threshold but no gain, so no unit could ever become ready. The refusal
    // happens at start, before any mutation, instead of as an endless quiet round.
    class NoGainPolicy final : public eve::tactics::ITurnPolicy {
    public:
        [[nodiscard]] std::string_view id() const noexcept override { return "test:no_gain"; }

        [[nodiscard]] eve::tactics::TurnOrder order(const eve::tactics::Battle&, const eve::tactics::UnitOrder&,
                                                    const eve::tactics::UnitOrder&) const override {
            return eve::tactics::TurnOrder::Equivalent;
        }

        [[nodiscard]] eve::tactics::ChargeModel chargeModel(const eve::tactics::Battle&) const override {
            return eve::tactics::ChargeModel{0, 30, 30};
        }
    };

    Fixture fixture;
    fixture.build(20, 10);
    auto registered = eve::tactics::TurnPolicyRegistry::builtins().add(std::make_shared<const NoGainPolicy>());
    REQUIRE(registered.ok());

    auto refused = fixture.tactics.start(fixture.battle, "test:no_gain");
    CHECK(!refused.ok());
    REQUIRE(refused.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(refused.status().primaryDiagnostic()->code(), eve::DiagnosticCode::PreconditionViolation);
    auto status = fixture.tactics.status(fixture.battle);
    REQUIRE(status.ok());
    CHECK(status.value() == eve::tactics::BattleStatus::Setup);
}
}  // namespace tactics_charge_time
}  // namespace

// tactics_turn.cpp
namespace {
namespace tactics_turn {
namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::SimulationStep step(std::uint64_t tick) { return {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)}; }

}  // namespace

TEST_CASE("tactics.facadeSetupIsAtomicAndHandleOwned") {
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
    eve::tactics::Tactics tactics;

    const auto battleSubject = subject("00000000-0000-0000-0000-000000000010");
    const auto sideSubject   = subject("00000000-0000-0000-0000-000000000011");
    const auto unitSubject   = subject("00000000-0000-0000-0000-000000000012");
    const auto otherSubject  = subject("00000000-0000-0000-0000-000000000013");

    auto battleResult = tactics.newBattle(battleSubject, 42);
    REQUIRE(battleResult.ok());
    const auto battle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
    auto sideResult = tactics.newSide(battle, sideSubject);
    REQUIRE(sideResult.ok());
    const auto side = std::move(sideResult).takeValue();

    auto unit = tactics.newUnit(battle, side, unitSubject, {}, {0, 0, 0});
    REQUIRE(unit.ok());
    std::move(unit).takeValue();
    auto conflict = tactics.newUnit(battle, side, otherSubject, {}, {0, 0, 0});
    CHECK(!conflict.ok());
    CHECK_EQ(conflict.code(), eve::StatusCode::Conflict);
    CHECK_EQ(tactics.unitCount(), 1u);
}

TEST_CASE("tactics.initiativePolicyAdvancesOneObservablePhase") {
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
    eve::tactics::Tactics tactics;

    auto battleResult = tactics.newBattle(subject("00000000-0000-0000-0000-000000000020"), 7);
    REQUIRE(battleResult.ok());
    const auto battleHandle = std::move(battleResult).takeValue();
    for (int x = 0; x < 3; ++x) REQUIRE(tactics.addCell(battleHandle, {x, 0, 0}).ok());

    auto sideResult = tactics.newSide(battleHandle, subject("00000000-0000-0000-0000-000000000021"));
    REQUIRE(sideResult.ok());
    const auto sideHandle  = std::move(sideResult).takeValue();
    const auto slowSubject = subject("00000000-0000-0000-0000-000000000022");
    const auto fastSubject = subject("00000000-0000-0000-0000-000000000023");
    auto       slowResult  = tactics.newUnit(battleHandle, sideHandle, slowSubject, {}, {0, 0, 0}, {1, 100, 0, 10});
    auto       fastResult  = tactics.newUnit(battleHandle, sideHandle, fastSubject, {}, {1, 0, 0}, {1, 100, 0, 20});
    REQUIRE(slowResult.ok());
    REQUIRE(fastResult.ok());
    const auto slowHandle = std::move(slowResult).takeValue();
    const auto fastHandle = std::move(fastResult).takeValue();
    CHECK(ecs::try_get(slowHandle) != nullptr);
    CHECK(ecs::try_get(fastHandle) != nullptr);

    REQUIRE(tactics.start(battleHandle, eve::tactics::TurnPolicyKind::Initiative).ok());
    auto round = tactics.advance(battleHandle, step(1));
    REQUIRE(round.ok());
    CHECK_EQ(static_cast<int>(std::move(round).takeValue()), static_cast<int>(eve::tactics::BattlePhase::RoundStart));
    auto turn = tactics.advance(battleHandle, step(2));
    REQUIRE(turn.ok());
    CHECK_EQ(static_cast<int>(std::move(turn).takeValue()), static_cast<int>(eve::tactics::BattlePhase::TurnStart));
    auto acting = tactics.advance(battleHandle, step(3));
    REQUIRE(acting.ok());
    CHECK_EQ(static_cast<int>(std::move(acting).takeValue()), static_cast<int>(eve::tactics::BattlePhase::Acting));
    auto active = tactics.activeUnit(battleHandle);
    REQUIRE(active.ok());
    CHECK_EQ(std::move(active).takeValue(), fastSubject);

    auto moved = tactics.moveUnit(battleHandle, fastSubject, {2, 0, 0});
    REQUIRE(moved.ok());
    const auto receipt = std::move(moved).takeValue();
    CHECK_EQ(receipt.cost, 100);
    CHECK_EQ(receipt.remainingMovePoints, 0);
    REQUIRE_EQ(receipt.path.size(), 2u);
    auto moveAgain = tactics.moveUnit(battleHandle, fastSubject, {1, 0, 0});
    CHECK(!moveAgain.ok());

    REQUIRE(tactics.endTurn(battleHandle, fastSubject).ok());
    auto nextTurn = tactics.advance(battleHandle, step(4));
    REQUIRE(nextTurn.ok());
    CHECK_EQ(static_cast<int>(std::move(nextTurn).takeValue()), static_cast<int>(eve::tactics::BattlePhase::TurnStart));
    REQUIRE(tactics.advance(battleHandle, step(5)).ok());
    auto nextActive = tactics.activeUnit(battleHandle);
    REQUIRE(nextActive.ok());
    CHECK_EQ(std::move(nextActive).takeValue(), slowSubject);

    auto* battle = dynamic_cast<eve::tactics::Battle*>(ecs::try_get(battleHandle));
    REQUIRE(battle != nullptr);
    const auto beforeFacing = battle->turn()->revision;
    REQUIRE(tactics.faceUnit(battleHandle, slowSubject, 3).ok());
    CHECK(battle->turn()->revision > beforeFacing);
    auto noOpFacing = tactics.faceUnit(battleHandle, slowSubject, 3);
    REQUIRE(noOpFacing.ok());
    CHECK_EQ(noOpFacing.code(), eve::StatusCode::NoOp);
    REQUIRE(tactics.waitUnit(battleHandle, slowSubject).ok());
    CHECK_EQ(static_cast<int>(battle->turn()->phase), static_cast<int>(eve::tactics::BattlePhase::TurnEnd));

    auto repeatedTick = tactics.advance(battleHandle, step(5));
    CHECK(!repeatedTick.ok());
    CHECK_EQ(repeatedTick.code(), eve::StatusCode::Rejected);
}
}  // namespace tactics_turn
}  // namespace

// tactics_turn_policy.cpp
namespace {
namespace tactics_turn_policy {
namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::SimulationStep step(std::uint64_t tick) { return {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)}; }

/**
 * @brief A policy no built-in provides: slowest unit acts first.
 *
 * Its only purpose is to be observably *wrong* for the built-in expectation, so a
 * battle started with it proves the registry is actually consulted rather than the
 * ordering being hard-coded.
 */
class SlowestFirstPolicy final : public eve::tactics::ITurnPolicy {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "test:slowest_first"; }

    [[nodiscard]] eve::tactics::TurnOrder order(const eve::tactics::Battle&, const eve::tactics::UnitOrder& left,
                                                const eve::tactics::UnitOrder& right) const override {
        if (left.initiative < right.initiative) return eve::tactics::TurnOrder::LeftFirst;
        if (right.initiative < left.initiative) return eve::tactics::TurnOrder::RightFirst;
        return eve::tactics::TurnOrder::Equivalent;
    }
};

}  // namespace

TEST_CASE("tactics.turnPolicyRegistryRejectsDuplicatesAndUnknownIds") {
    auto& registry = eve::tactics::TurnPolicyRegistry::builtins();

    // Both built-ins ship registered, and the id list is the discovery surface.
    CHECK(registry.contains(eve::tactics::kSideAlternatingPolicyId));
    CHECK(registry.contains(eve::tactics::kInitiativePolicyId));
    const auto ids = registry.ids();
    CHECK(ids.size() >= 2u);
    // ids() is documented as lexical order, so the list is directly comparable.
    CHECK(std::is_sorted(ids.begin(), ids.end()));

    // Re-registering a built-in id is a Conflict, not a silent replacement: a
    // duplicate id would otherwise silently change scheduling for saved battles.
    auto duplicate = registry.add(std::make_shared<const SlowestFirstPolicy>());
    CHECK(duplicate.ok());
    auto reRegister = registry.add(std::make_shared<const SlowestFirstPolicy>());
    CHECK(!reRegister.ok());
    CHECK_EQ(reRegister.code(), eve::StatusCode::Conflict);
}

TEST_CASE("tactics.unknownTurnPolicyIsRefusedAndLeavesBattleInSetup") {
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
    eve::tactics::Tactics tactics;

    auto battleResult = tactics.newBattle(subject("00000000-0000-0000-0000-000000000300"));
    REQUIRE(battleResult.ok());
    const auto battle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
    auto sideResult = tactics.newSide(battle, subject("00000000-0000-0000-0000-000000000301"));
    REQUIRE(sideResult.ok());
    const auto side = std::move(sideResult).takeValue();
    REQUIRE(tactics.newUnit(battle, side, subject("00000000-0000-0000-0000-000000000302"), {}, {0, 0, 0}, {1, 0, 0, 10})
                .ok());

    auto refused = tactics.start(battle, "does_not_exist");
    CHECK(!refused.ok());
    CHECK_EQ(refused.code(), eve::StatusCode::NotFound);

    // The refusal must not be half-applied: an unregistered id has to leave the
    // battle startable once the caller supplies a real one.
    auto status = tactics.status(battle);
    REQUIRE(status.ok());
    CHECK(status.value() == eve::tactics::BattleStatus::Setup);
    CHECK(tactics.start(battle, eve::tactics::kInitiativePolicyId).ok());
}

TEST_CASE("tactics.registeredCustomPolicyChangesTheActivationOrder") {
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
    eve::tactics::Tactics tactics;

    auto battleResult = tactics.newBattle(subject("00000000-0000-0000-0000-000000000310"), 21);
    REQUIRE(battleResult.ok());
    const auto battle = std::move(battleResult).takeValue();
    for (int x = 0; x < 3; ++x) REQUIRE(tactics.addCell(battle, {x, 0, 0}).ok());
    auto sideResult = tactics.newSide(battle, subject("00000000-0000-0000-0000-000000000311"));
    REQUIRE(sideResult.ok());
    const auto side = std::move(sideResult).takeValue();
    const auto slow = subject("00000000-0000-0000-0000-000000000312");
    const auto fast = subject("00000000-0000-0000-0000-000000000313");
    REQUIRE(tactics.newUnit(battle, side, slow, {}, {0, 0, 0}, {1, 0, 0, 10}).ok());
    REQUIRE(tactics.newUnit(battle, side, fast, {}, {1, 0, 0}, {1, 0, 0, 20}).ok());

    // Registering the custom policy is the only difference from the built-in case.
    // Per-case process isolation keeps this registration local to this test.
    auto registered = eve::tactics::TurnPolicyRegistry::builtins().add(std::make_shared<const SlowestFirstPolicy>());
    REQUIRE(registered.ok());

    REQUIRE(tactics.start(battle, "test:slowest_first").ok());
    REQUIRE(tactics.advance(battle, step(1)).ok());
    REQUIRE(tactics.advance(battle, step(2)).ok());
    REQUIRE(tactics.advance(battle, step(3)).ok());

    auto active = tactics.activeUnit(battle);
    REQUIRE(active.ok());
    // Under the built-in initiative policy `fast` would act first; the registered
    // policy must invert that, which is only possible if the id was resolved.
    CHECK_EQ(active.value(), slow);
}
}  // namespace tactics_turn_policy
}  // namespace
}  // namespace test_tactics_turn_rules
#endif
