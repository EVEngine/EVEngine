#include "action/ActionDamageBlock.h"
#include "action/ActionNotifyRegistry.h"
#include "combat/ActionDamageSink.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <functional>
#include <string_view>

namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

eve::SubjectRef subject(std::string_view value) {
    auto parsed = eve::PersistentId::parse(value);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

}  // namespace

TEST_CASE("combatActionDamage.validatesRoutesAndAtomicallyMutatesResolvedState") {
    eve::Value::Object payload{{"damageType", "Damage.Physical.Slash"},
                               {"amount", 18.0},
                               {"poiseAmount", 7.0},
                               {"targetIndex", 0},
                               {"knockback", eve::Value::Array{1.0, 0.5, -2.0}}};
    auto binding = eve::action::ActionDamageBinding::fromPayload(payload);
    REQUIRE(binding.ok());
    CHECK_EQ(binding.value().amount, 18.0);
    CHECK_EQ(binding.value().poiseAmount, 7.0);
    CHECK_EQ(binding.value().knockback.z, -2.0);

    auto invalid = payload;
    invalid["amount"] = -1.0;
    auto rejected = eve::action::ActionDamageBinding::fromPayload(invalid);
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.status().diagnostics().front().path(), "amount");

    ecs::EntityHandle sourceHandle{nullptr, typeid(void), 1, 1};
    ecs::EntityHandle targetHandle{nullptr, typeid(void), 2, 1};
    eve::combat::CombatState source{subject("11121314-1516-1718-991a-1b1c1d1e1f20"), 80.0, 80.0, 20.0, 20.0};
    eve::combat::CombatState target{subject("01020304-0506-0708-890a-0b0c0d0e0f10"), 100.0, 100.0, 40.0, 40.0};
    eve::combat::CombatActionDamageSink sink([&](ecs::EntityHandle handle) -> eve::OptionalRef<eve::combat::CombatState> {
        if (handle.id == sourceHandle.id) return std::ref(source);
        if (handle.id == targetHandle.id) return std::ref(target);
        return std::nullopt;
    });

    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    eve::action::ActionTimelineEvent event{eve::action::ActionTimelineEventKind::Notify,
                                            id("combat-track:damage"), id("combat-notify:damage"),
                                            id("combat:damage"), eve::Duration::zero(), payload};
    eve::action::ActionNotifyContext context;
    context.executionId = eve::action::ActionExecutionId{404};
    context.source = sourceHandle;
    context.targets.push_back(targetHandle);

    auto missing = registry.value().dispatch(event, context);
    CHECK(!missing.ok());
    CHECK_EQ(missing.status().code(), eve::StatusCode::NotFound);
    CHECK_EQ(target.health, 100.0);

    sink.setEnabled(true);
    REQUIRE(sink.enabled());
    REQUIRE(registry.value().dispatch(event, context).ok());
    CHECK_EQ(target.health, 82.0);
    CHECK_EQ(target.poise, 33.0);
    auto outcome = sink.lastOutcome();
    REQUIRE(outcome.has_value());
    CHECK_EQ(outcome->source, source.subject);
    CHECK_EQ(outcome->target, target.subject);
    CHECK_EQ(outcome->knockback.z, -2.0);

    sink.setEnabled(false);
    CHECK(!sink.enabled());
}
