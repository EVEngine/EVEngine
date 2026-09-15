#include "action/ActionBlockRuntime.h"
#include "action/ActionDamageBlock.h"
#include "action/ActionNotifyRegistry.h"
#include "action/ActionStateWindowBlock.h"
#include "combat/ActionDamageSink.h"
#include "combat/ActionWindowState.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string_view>

namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return *parsed;
}

eve::SubjectRef subject(std::string_view value) {
    auto parsed = eve::PersistentId::parse(value);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

eve::action::ActionActiveBlock active(const eve::action::ActionTimelineEvent& event) {
    return {event.trackId, event.itemId, event.type, eve::Duration::zero(),
            eve::Duration::fromSeconds(1.0).takeValue(), event.payload};
}

}  // namespace

TEST_CASE("combatActionWindows.pairHitboxInvulnerabilityAndInterruptCleanup") {
    CHECK(!eve::action::ActionStateWindowBinding::fromPayload("combat:hitbox-window", {}).ok());
    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    const eve::action::ActionExecutionId execution(501);
    ecs::EntityHandle sourceHandle{nullptr, typeid(void), 1, 3};
    ecs::EntityHandle targetHandle{nullptr, typeid(void), 2, 4};
    const auto sourceSubject = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    const auto targetSubject = subject("01020304-0506-0708-890a-0b0c0d0e0f10");
    bool resolverAlive = true;
    eve::combat::CombatActionWindowState windows(
        [&](ecs::EntityHandle handle) -> eve::Result<eve::SubjectRef> {
            if (!resolverAlive)
                return eve::Result<eve::SubjectRef>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "entity is stale", "entity"));
            if (handle.id == sourceHandle.id) return eve::Result<eve::SubjectRef>::success(sourceSubject);
            if (handle.id == targetHandle.id) return eve::Result<eve::SubjectRef>::success(targetSubject);
            return eve::Result<eve::SubjectRef>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "entity is unknown", "entity"));
        });

    eve::action::ActionTimelineEvent hitbox{eve::action::ActionTimelineEventKind::StateEnter,
                                             id("combat-track:windows"), id("combat-window:hitbox"),
                                             id("combat:hitbox-window"), eve::Duration::zero(),
                                             {{"hitbox", eve::Value("weapon.main")}}};
    eve::action::ActionTimelineEvent invulnerability{
        eve::action::ActionTimelineEventKind::StateEnter, id("combat-track:windows"),
        id("combat-window:invulnerability"), id("combat:invulnerability-window"), eve::Duration::zero(),
        {{"targetIndex", eve::Value(std::int64_t{0})}}};
    auto overlappingInvulnerability = invulnerability;
    overlappingInvulnerability.itemId = id("combat-window:invulnerability-overlap");
    eve::action::ActionNotifyContext context;
    context.executionId = execution;
    context.source = sourceHandle;
    context.targets.push_back(targetHandle);
    CHECK(!registry.value().dispatch(hitbox, context).ok());

    windows.setEnabled(true);
    eve::action::ActionBlockRuntime blockRuntime(registry.value());
    eve::action::ActionAdvance advance;
    advance.id = execution;
    advance.phase = eve::action::ActionPhase::Active;
    advance.timelineEvents = {hitbox, invulnerability, overlappingInvulnerability};
    advance.activeBlocks = {active(hitbox), active(invulnerability), active(overlappingInvulnerability)};
    REQUIRE(blockRuntime.apply(advance, context).ok());
    CHECK_EQ(windows.activeCount(), 3U);
    REQUIRE_EQ(windows.activeHitboxes(sourceSubject).size(), 1U);
    CHECK_EQ(windows.activeHitboxes(sourceSubject).front(), std::string("weapon.main"));
    CHECK(windows.isInvulnerable(targetSubject));
    auto firstInvulnerabilityExit = invulnerability;
    firstInvulnerabilityExit.kind = eve::action::ActionTimelineEventKind::StateExit;
    REQUIRE(registry.value().dispatch(firstInvulnerabilityExit, context).ok());
    CHECK_EQ(windows.activeCount(), 2U);
    CHECK(windows.isInvulnerable(targetSubject));

    eve::combat::CombatState source{sourceSubject, 80.0, 80.0, 20.0, 20.0};
    eve::combat::CombatState target{targetSubject, 100.0, 100.0, 40.0, 40.0};
    eve::combat::CombatActionDamageSink damage(
        [&](ecs::EntityHandle handle) -> eve::OptionalRef<eve::combat::CombatState> {
            if (handle.id == sourceHandle.id) return std::ref(source);
            if (handle.id == targetHandle.id) return std::ref(target);
            return std::nullopt;
        });
    damage.setWindowState(windows);
    damage.setEnabled(true);
    eve::action::ActionTimelineEvent damageEvent{
        eve::action::ActionTimelineEventKind::Notify, id("combat-track:damage"), id("combat-notify:damage"),
        id("combat:damage"), eve::Duration::zero(),
        {{"damageType", eve::Value("Damage.Physical.Slash")}, {"amount", eve::Value(25.0)}}};
    auto blocked = registry.value().dispatch(damageEvent, context);
    REQUIRE(blocked.ok());
    CHECK_EQ(blocked.status().code(), eve::StatusCode::NoOp);
    CHECK_EQ(target.health, 100.0);
    CHECK(!damage.lastOutcome().has_value());

    resolverAlive = false;
    REQUIRE(blockRuntime.interrupt(context).ok());
    CHECK_EQ(windows.activeCount(), 0U);
    CHECK(!windows.isInvulnerable(targetSubject));
    resolverAlive = true;
    REQUIRE(registry.value().dispatch(damageEvent, context).ok());
    CHECK_EQ(target.health, 75.0);

    damage.clearWindowState();
    damage.setEnabled(false);
    windows.setEnabled(false);
}
