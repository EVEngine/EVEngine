#include "common/ECS.h"
#include "tactics/Tactics.h"
#include "tactics/TurnPolicy.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <memory>

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
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::tactics::Tactics tactics;

    auto battleResult = tactics.newBattle(subject("00000000-0000-0000-0000-000000000300"));
    REQUIRE(battleResult.ok());
    const auto battle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
    auto sideResult = tactics.newSide(battle, subject("00000000-0000-0000-0000-000000000301"));
    REQUIRE(sideResult.ok());
    const auto side = std::move(sideResult).takeValue();
    REQUIRE(tactics.newUnit(battle, side, subject("00000000-0000-0000-0000-000000000302"), {}, {0, 0, 0}, {1, 0, 0, 10}).ok());

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
    ecs::Table       world;
    ecs::ScopedTable guard(world);
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
