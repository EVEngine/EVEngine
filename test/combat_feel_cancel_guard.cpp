#include "action/ActionNotifyRegistry.h"
#include "action/input/ActionCancelWindowState.h"
#include "action/input/ActionInputBuffer.h"
#include "combat/ComboGraph.h"
#include "combat/GuardWindowState.h"
#include "combat/HitFeel.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return *parsed;
}

eve::SubjectRef subject(const char* value) {
    auto parsed = eve::PersistentId::parse(value);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

}  // namespace

TEST_CASE("combatFeelCancelGuardAndComboGraphCompose") {
    const auto player = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    const auto enemy = subject("01020304-0506-0708-890a-0b0c0d0e0f10");
    ecs::EntityHandle playerHandle{nullptr, typeid(void), 1, 1};

    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    eve::action::input::ActionCancelWindowState cancels([&](ecs::EntityHandle) {
        return eve::Result<eve::SubjectRef>::success(player);
    });
    cancels.setEnabled(true);
    eve::combat::GuardWindowState guards([&](ecs::EntityHandle) {
        return eve::Result<eve::SubjectRef>::success(player);
    });
    guards.setEnabled(true);

    eve::action::ActionNotifyContext context;
    context.executionId = eve::action::ActionExecutionId(7);
    context.source = playerHandle;
    eve::action::ActionTimelineEvent cancelEnter{eve::action::ActionTimelineEventKind::StateEnter,
                                                 id("track:input"), id("item:cancel"), id("input:cancel-window"),
                                                 eve::Duration::zero(),
                                                 {{"allows", eve::Value("dodge;heavy-attack")},
                                                  {"priority", eve::Value(std::int64_t{10})}}};
    REQUIRE(registry.value().dispatch(cancelEnter, context).ok());
    auto cancelMatch = cancels.match(player, "dodge");
    REQUIRE(cancelMatch.ok());
    CHECK_EQ(cancelMatch.value().priority, 10);

    eve::action::input::ActionInputBuffer buffer(4, 8);
    REQUIRE(buffer.push({player, "dodge", eve::SimulationTick(1), 5}).ok());
    auto consumed = buffer.consume(player, {"dodge"});
    REQUIRE(consumed.ok());
    CHECK_EQ(consumed.value().input, std::string("dodge"));

    eve::action::ActionTimelineEvent guardEnter{eve::action::ActionTimelineEventKind::StateEnter,
                                                id("track:guard"), id("item:parry"), id("combat:guard-window"),
                                                eve::Duration::zero(), {{"mode", eve::Value("parry")}}};
    REQUIRE(registry.value().dispatch(guardEnter, context).ok());
    eve::combat::DamageRequest request;
    request.source = enemy;
    request.target = player;
    request.damageType = "Damage.Physical.Slash";
    request.healthDamage = 20.0;
    request.poiseDamage = 10.0;
    auto guarded = guards.mitigate(player, request);
    REQUIRE(guarded.ok());
    CHECK(guarded.value().result == eve::combat::GuardResult::Parried);
    CHECK_EQ(request.healthDamage, 0.0);

    eve::combat::CombatState victim{player, 100.0, 100.0, 40.0, 40.0};
    eve::combat::DamageRuntime damage;
    request.healthDamage = 12.0;
    request.poiseDamage = 20.0;
    auto outcome = damage.apply(victim, request);
    REQUIRE(outcome.ok());
    eve::combat::HitFeelRuntime feel;
    REQUIRE(feel.applyFromOutcome(outcome.value()).ok());
    CHECK(feel.isFrozen(player) || feel.isStunned(player) || feel.activeCount() >= 1u);

    auto light = eve::LogicalId::fromParts("combat-ability", "light-attack");
    auto heavy = eve::LogicalId::fromParts("combat-ability", "heavy-attack");
    REQUIRE(light.has_value());
    REQUIRE(heavy.has_value());
    eve::combat::ComboGraph graph;
    REQUIRE(graph.addEdge({*light, *heavy, "heavy-attack", 1, true, false}).ok());
    auto matched = graph.match(*light, "heavy-attack", true, false);
    REQUIRE(matched.ok());
    CHECK_EQ(matched.value().to.format(), heavy->format());
    CHECK(!graph.match(*light, "heavy-attack", false, false).ok());

    cancels.setEnabled(false);
    guards.setEnabled(false);
}
