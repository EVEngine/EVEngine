#include <algorithm>
#include <memory>
#include <utility>
#include <vector>
#include "common/ECS.h"
#include "tactics/Tactics.h"
#include "tactics/TurnPolicy.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
