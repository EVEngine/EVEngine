#include "common/ECS.h"
#include "tactics/Tactics.h"
#include "tactics/TurnPolicy.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <memory>
#include <utility>
#include <vector>

namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::SimulationStep step(std::uint64_t tick) {
    return {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)};
}

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

        [[nodiscard]] eve::tactics::TurnOrder order(const eve::tactics::Battle&,
                                                    const eve::tactics::UnitOrder& left,
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
    auto registered =
        eve::tactics::TurnPolicyRegistry::builtins().add(std::make_shared<const LowerThresholdPolicy>());
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
