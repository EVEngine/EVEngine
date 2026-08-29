#include "action/AbilityController.h"

#include "common/Diagnostic.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {

eve::LogicalId logicalId(const char* ns, const char* name) {
    const auto result = eve::LogicalId::fromParts(ns, name);
    REQUIRE(result.has_value());
    return *result;
}

struct Fixture {
    Fixture() : abilities(actions), controller(abilities) {
        definition.id        = logicalId("ability", "strike");
        definition.action.id = logicalId("action", "strike");
        REQUIRE(abilities.registerDefinition(definition).ok());
        auto created = abilities.grant("fighter", definition.id);
        REQUIRE(created.ok());
        grant = created.value();
    }

    eve::action::AbilityIntent intent() const {
        eve::action::AbilityIntent value;
        value.grantId         = grant;
        value.request.actionId = definition.action.id;
        return value;
    }

    eve::action::ActionRuntime            actions;
    eve::action::AbilityRuntime           abilities;
    eve::action::AbilityControllerRuntime controller;
    eve::action::AbilityDefinition        definition;
    eve::action::AbilityGrantId           grant;
};

class AiIntentSource final : public eve::action::IAbilityIntentSource {
public:
    [[nodiscard]] eve::Result<std::optional<eve::action::AbilityIntent>> nextIntent(
        eve::SimulationTick tick) override {
        observedTick = tick;
        if (reject)
            return eve::Result<std::optional<eve::action::AbilityIntent>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation, "AI has no valid plan"));
        auto result = pending;
        pending.reset();
        return eve::Result<std::optional<eve::action::AbilityIntent>>::success(std::move(result));
    }

    bool                                              reject = false;
    eve::SimulationTick                               observedTick;
    std::optional<eve::action::AbilityIntent>         pending;
};

}  // namespace

TEST_CASE("abilityController.playerQueueRoutesFifoIntentWithInjectedTick") {
    Fixture                               fixture;
    eve::action::PlayerAbilityIntentQueue player;
    REQUIRE(player.enqueue(fixture.intent()).ok());
    REQUIRE_EQ(player.pendingCount(), 1u);

    auto processed = fixture.controller.processNext(player, eve::SimulationTick(17));
    REQUIRE(processed.ok());
    REQUIRE(processed.value().has_value());
    CHECK_EQ(player.pendingCount(), 0u);
    const auto* execution = fixture.actions.find(processed.value()->executionId);
    REQUIRE(execution != nullptr);
    CHECK(execution->request().requestedTick == eve::SimulationTick(17));
}

TEST_CASE("abilityController.aiAndPlayerUseTheSameActivationPath") {
    Fixture        fixture;
    AiIntentSource ai;
    ai.pending = fixture.intent();

    auto processed = fixture.controller.processNext(ai, eve::SimulationTick(23));
    REQUIRE(processed.ok());
    REQUIRE(processed.value().has_value());
    CHECK(ai.observedTick == eve::SimulationTick(23));
    CHECK_EQ(fixture.abilities.activeActivations().size(), 1u);
}

TEST_CASE("abilityController.sourceFailureDoesNotMutateAbilityRuntime") {
    Fixture        fixture;
    AiIntentSource ai;
    ai.pending = fixture.intent();
    ai.reject  = true;

    auto processed = fixture.controller.processNext(ai, eve::SimulationTick(3));
    CHECK(!processed.ok());
    CHECK(fixture.abilities.activeActivations().empty());
    CHECK(ai.pending.has_value());
}

TEST_CASE("abilityController.emptySourceIsAnExplicitNoOp") {
    Fixture                               fixture;
    eve::action::PlayerAbilityIntentQueue player;
    auto processed = fixture.controller.processNext(player, eve::SimulationTick(5));
    REQUIRE(processed.ok());
    CHECK(!processed.value().has_value());
    CHECK(fixture.abilities.activeActivations().empty());
}

TEST_CASE("abilityController.rejectedIntentIsConsumedExactlyOnce") {
    Fixture                               fixture;
    eve::action::PlayerAbilityIntentQueue player;
    auto invalid              = fixture.intent();
    invalid.request.actionId = logicalId("action", "other");
    REQUIRE(player.enqueue(std::move(invalid)).ok());

    auto processed = fixture.controller.processNext(player, eve::SimulationTick(9));
    CHECK(!processed.ok());
    CHECK_EQ(player.pendingCount(), 0u);
    CHECK(fixture.abilities.activeActivations().empty());
}
