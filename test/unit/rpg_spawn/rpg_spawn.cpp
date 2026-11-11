#include <array>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include "RPGSpawnFixture.h"
#include "SpawnFactoryContract.h"
#include "common/Capability.h"
#include "common/ConditionKeys.h"
#include "common/StateAccess.h"
#include "definitions/Definitions.h"
#include "rpg/ActorInitialization.h"
#include "rpg/AttributeSystem.h"
#include "rpg/EffectDefinitionCodec.h"
#include "rpg/EncounterCatalogue.h"
#include "rpg/ImplementationRequirement.h"
#include "rpg/RPGActor.h"
#include "rpg/SimulationEvents.h"
#include "rpg/Skill.h"
#include "rpg/SkillConditionBindings.h"
#include "rpg/SkillContent.h"
#include "rpg/SkillDefinitionRuntime.h"
#include "rpg/SkillSystem.h"
#include "rpg/StatusSystem.h"
#include "rpg/VitalsSystem.h"
#include "rpg/spawn/RPGSpawnArchive.h"
#include "rpg/spawn/RPGSpawnCapture.h"
#include "rpg/spawn/RPGSpawnFactory.h"
#include "rpg/spawn/RPGSpawnJointRestore.h"
#include "rpg/spawn/RPGSpawnRestoreState.h"
#include "rpg/spawn/RPGSpawnSession.h"
#include "rpg/spawn/RPGUnitCatalogue.h"
#include "spawn/SpawnRestoreState.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace test_rpg_spawn {
TEST_CASE("RPG spawn factory shares atomic creation and replay contract") {
    Fixture fixture;
    spawn_factory_contract::creationAndReplay(fixture);
    auto actor = fixture.factory->inspect(fixture.request.units[0].subject);
    REQUIRE(actor.ok());
    CHECK_EQ(actor.value().hp, 20.0);
    CHECK_EQ(actor.value().attack, 7.0);
    CHECK_EQ(actor.value().position.x, 1.0);
    auto view = ecs::View<eve::rpg::RPGActor>(*fixture.table);
    for (auto it = view.begin(); it != view.end(); ++it) CHECK(it.entity()->knowsSkill("spawn.basic"));
}
TEST_CASE("RPG spawn removal contract destroys units only when explicitly requested") {
    Fixture fixture;
    spawn_factory_contract::removal(fixture, eve::spawn::DetachPolicy::RemoveUnits);
}
TEST_CASE("RPG spawn keep contract preserves domain-owned actors") {
    Fixture fixture;
    spawn_factory_contract::removal(fixture, eve::spawn::DetachPolicy::KeepUnits);
}
TEST_CASE("RPG spawn preparation rejects unknown definitions before publication") {
    Fixture fixture;
    fixture.request.units[1].definition = eve::DefinitionRef::parse("rpg.unit:missing").expect("missing reference");
    REQUIRE(!fixture.factory->prepare(fixture.request, fixture.positions).ok());
    CHECK_EQ(fixture.count(), std::size_t(0));
}
TEST_CASE("RPG spawn initialization failure rolls back the previously created prefix") {
    Fixture                   fixture;
    eve::rpg::SkillDefinition second;
    second.id = "spawn.second";
    eve::rpg::SkillRegistry::registerSkill(second);
    auto source = Fixture::asset();
    source.set("id", eve::Value("second"));
    source.set("skill", eve::Value("spawn.second"));
    REQUIRE(fixture.units->publish(source).ok());
    fixture.request.units[1].definition = eve::DefinitionRef::parse("rpg.unit:second").expect("second reference");
    const auto queries                  = eve::cap::listenerCount<eve::IStateQuery>();
    const auto mutations                = eve::cap::listenerCount<eve::IStateMutation>();
    auto token = fixture.factory->prepare(fixture.request, fixture.positions).expect("prepared before skill removal");
    REQUIRE_EQ(eve::cap::listenerCount<eve::IStateQuery>(), queries);
    REQUIRE_EQ(eve::cap::listenerCount<eve::IStateMutation>(), mutations);
    REQUIRE(eve::rpg::SkillRegistry::remove("spawn.second"));
    REQUIRE(!std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(token)->commit().ok());
    CHECK_EQ(fixture.count(), std::size_t(0));
    CHECK(!fixture.factory->inspect(fixture.request.units[0].subject).ok());
    REQUIRE_EQ(eve::cap::listenerCount<eve::IStateQuery>(), queries);
    REQUIRE_EQ(eve::cap::listenerCount<eve::IStateMutation>(), mutations);
    eve::rpg::SkillRegistry::registerSkill(second);
    REQUIRE(std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(token)->commit().expect("same token retry") ==
            eve::spawn::CommitOutcome::Committed);
    REQUIRE_EQ(fixture.count(), 2u);
}
TEST_CASE("RPG spawn pending transactions cannot outlive their factory owner") {
    Fixture fixture;
    auto    token = fixture.factory->prepare(fixture.request, fixture.positions).expect("prepared transaction");
    fixture.providers.reset();
    fixture.factory.reset();
    REQUIRE(!std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(token)->commit().ok());
    CHECK_EQ(fixture.count(), std::size_t(0));
}
TEST_CASE("RPG spawn pending transactions reject a destroyed domain world") {
    Fixture fixture;
    auto    token = fixture.factory->prepare(fixture.request, fixture.positions).expect("prepared transaction");
    fixture.table.reset();
    REQUIRE(!std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(token)->commit().ok());
    CHECK(!fixture.factory->inspect(fixture.request.units[0].subject).ok());
}
TEST_CASE("RPG spawn identity collision rolls back the entire created prefix and can retry") {
    Fixture             fixture;
    eve::rpg::RPGActor* existing = nullptr;
    {
        ecs::ScopedTable scope(*fixture.table);
        existing = eve::rpg::RPGActor::createActor();
        eve::rpg::bindActorIdentity(*existing, fixture.request.units[1].subject)
            .expect("other participant registration");
        existing->setBaseAttribute("hp", 50);
        existing->setCurrent("hp", 41);
    }
    auto  prepared = fixture.factory->prepare(fixture.request, fixture.positions).expect("prepare real batch");
    auto& token    = std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(prepared);
    auto  rejected = token->commit();
    REQUIRE(!rejected.ok());
    REQUIRE_EQ(existing->getCurrent("hp"), 41.0);
    REQUIRE(eve::rpg::actorSubject(*existing) == std::optional<eve::SubjectRef>{fixture.request.units[1].subject});
    REQUIRE(!fixture.factory->inspect(fixture.request.units[0].subject).ok());
    std::size_t count = 0;
    {
        auto view = ecs::View<eve::rpg::RPGActor>(*fixture.table);
        for (auto it = view.begin(); it != view.end(); ++it) ++count;
    }
    REQUIRE_EQ(count, std::size_t(1));
    existing->release();
    REQUIRE(token->commit().ok());
    REQUIRE(fixture.factory->inspect(fixture.request.units[0].subject).ok());
    REQUIRE(fixture.factory->inspect(fixture.request.units[1].subject).ok());
}
TEST_CASE("RPG spawn lifecycle preserves death facts after actor destruction") {
    Fixture fixture;
    fixture.commit();
    eve::rpg::RPGActor* killed = nullptr;
    {
        auto view = ecs::View<eve::rpg::RPGActor>(*fixture.table);
        killed    = view.begin().entity();
    }
    eve::rpg::VitalsSystem::takeDamage(killed, "hp", 30);
    std::vector<eve::rpg::VitalsEvent> events;
    eve::rpg::VitalsSystem::pollEvents(events);
    ecs::DestroyEntity(killed);
    auto lifecycle = fixture.factory->collectLifecycle(events);
    REQUIRE(lifecycle.ok());
    REQUIRE(lifecycle.value().size() == 1);
    CHECK(lifecycle.value()[0].reason == eve::spawn::RemovalReason::Death);
    CHECK(fixture.factory->collectLifecycle(events).expect("duplicate domain batch").empty());
}
TEST_CASE("RPG spawn death handoff retains original world identity after Table retirement") {
    Fixture fixture;
    fixture.commit();
    eve::rpg::detail::SimulationEvents     events;
    eve::rpg::detail::SimulationEventScope scope(events);
    auto*                                  actor = [&] {
        auto view = ecs::View<eve::rpg::RPGActor>(*fixture.table);
        return view.begin().entity();
    }();
    REQUIRE_EQ(eve::rpg::VitalsSystem::takeDamage(actor, "hp", 30), 20.0);
    fixture.table.reset();
    REQUIRE(events.vitals.back().actor.presence() == eve::rpg::ActorEventPresence::Retired);
    auto facts = fixture.factory->collectLifecycle(events.vitals).expect("death after world retirement");
    REQUIRE_EQ(facts.size(), std::size_t(2));
    std::size_t deaths = 0, unloaded = 0;
    for (const auto& fact : facts) {
        deaths += fact.reason == eve::spawn::RemovalReason::Death;
        unloaded += fact.reason == eve::spawn::RemovalReason::Unloaded;
    }
    CHECK_EQ(deaths, std::size_t(1));
    CHECK_EQ(unloaded, std::size_t(1));
    CHECK(fixture.factory->collectLifecycle(events.vitals).expect("duplicate expired-world batch").empty());

    Fixture target, foreign;
    target.commit();
    foreign.commit();
    events.vitals.clear();
    auto* foreignActor = [&] {
        auto view = ecs::View<eve::rpg::RPGActor>(*foreign.table);
        return view.begin().entity();
    }();
    REQUIRE_EQ(eve::rpg::VitalsSystem::takeDamage(foreignActor, "hp", 30), 20.0);
    foreign.table.reset();
    target.table.reset();
    auto unrelated = target.factory->collectLifecycle(events.vitals).expect("ignore foreign expired-world deaths");
    REQUIRE_EQ(unrelated.size(), std::size_t(2));
    for (const auto& fact : unrelated) CHECK(fact.reason == eve::spawn::RemovalReason::Unloaded);
}
TEST_CASE("RPG spawn damage resolves real owned actors and rejects invalid stale and deferred actions") {
    Fixture fixture;
    fixture.commit();
    eve::rpg::detail::SimulationEvents     events;
    eve::rpg::detail::SimulationEventScope eventScope(events);
    const auto                             subject = fixture.request.units[0].subject;
    REQUIRE(!fixture.factory->applyDamage(eve::SubjectRef::nil(), 1).ok());
    REQUIRE(!fixture.factory->applyDamage(subject, -1).ok());
    REQUIRE(!fixture.factory->applyDamage(subject, std::numeric_limits<double>::infinity()).ok());
    REQUIRE(!fixture.factory->applyDamage(subject, std::numeric_limits<double>::quiet_NaN()).ok());
    REQUIRE(events.vitals.empty());
    {
        auto view = ecs::View<eve::rpg::RPGActor>(*fixture.table);
        REQUIRE(!fixture.factory->applyDamage(subject, 1).ok());
    }
    REQUIRE_EQ(fixture.factory->applyDamage(subject, 0).expect("zero damage"), 0.0);
    REQUIRE(events.vitals.empty());
    const std::string operation  = "damage.reentry.hp";
    auto&             operations = eve::rpg::AttributeSystem::customOps();
    std::size_t       callbacks  = 0;
    operations.registerOperation(operation, [&](double base, double) {
        ++callbacks;
        REQUIRE(!fixture.factory->applyDamage(subject, 1).ok());
        REQUIRE(!fixture.factory->inspect(subject).ok());
        return base;
    });
    eve::attributes::AttributeModifier modifier;
    modifier.attribute = "hp";
    modifier.operation = eve::attributes::AttributeOperation::Custom;
    modifier.policyId  = operation;
    const auto handle  = [&] {
        auto view = ecs::View<eve::rpg::RPGActor, eve::rpg_spawn::SpawnedActorState>(*fixture.table);
        for (auto it = view.begin(); it != view.end(); ++it) {
            if (eve::rpg::actorSubject(*it.entity()) == std::optional<eve::SubjectRef>{subject})
                return ecs::handle_of(it.entity());
        }
        return ecs::EntityHandle{};
    }();
    auto* actor = dynamic_cast<eve::rpg::RPGActor*>(ecs::try_get(handle));
    REQUIRE(actor != nullptr);
    auto modifierId = eve::rpg::AttributeSystem::addModifier(actor, modifier).expect("actual HP calculator");
    REQUIRE_EQ(fixture.factory->applyDamage(subject, 5).expect("canonical actual damage"), 5.0);
    REQUIRE(callbacks > 0);
    eve::rpg::AttributeSystem::removeModifier(actor, modifierId).expect("remove calculator");
    operations.unregisterOperation(operation);
    REQUIRE_EQ(fixture.factory->inspect(subject).expect("actual HP").hp, 15.0);
    REQUIRE_EQ(fixture.factory->applyDamage(subject, 100).expect("canonical lethal damage"), 15.0);
    REQUIRE_EQ(fixture.factory->applyDamage(subject, 100).expect("already dead"), 0.0);
    REQUIRE_EQ(events.vitals.size(), std::size_t(3));
    REQUIRE(events.vitals.back().actor.capturedSubject() == std::optional<eve::SubjectRef>{subject});
    auto lifecycle = fixture.factory->collectLifecycle(events.vitals).expect("actual death handoff");
    REQUIRE_EQ(lifecycle.size(), std::size_t(1));
    REQUIRE(lifecycle[0].subject == subject);
    REQUIRE(lifecycle[0].reason == eve::spawn::RemovalReason::Death);
    REQUIRE(!fixture.factory->applyDamage(subject, 1).ok());
    fixture.sourceWorld.reset();
    REQUIRE(!fixture.factory->applyDamage(fixture.request.units[1].subject, 1).ok());
    REQUIRE_EQ(fixture.factory->inspect(fixture.request.units[1].subject).expect("retained actor HP").hp, 20.0);
}
TEST_CASE("RPG spawn destruction without a death fact stays an administrative removal") {
    Fixture fixture;
    fixture.commit();
    {
        auto view = ecs::View<eve::rpg::RPGActor>(*fixture.table);
        ecs::DestroyEntity(view.begin().entity());
    }
    auto lifecycle = fixture.factory->collectLifecycle({});
    REQUIRE(lifecycle.ok());
    REQUIRE(lifecycle.value().size() == 1);
    CHECK(lifecycle.value()[0].reason == eve::spawn::RemovalReason::Administrative);
}
TEST_CASE("RPG spawn prepared recipes stay pinned across content replacement") {
    Fixture    fixture;
    const auto original = fixture.units->resolve(fixture.request.units[0].definition).expect("original recipe");
    auto       prepared = fixture.factory->prepare(fixture.request, fixture.positions).expect("pinned recipe");
    auto       source   = Fixture::asset();
    source.set("attack", eve::Value(99));
    REQUIRE(fixture.units->publish(source).ok());
    REQUIRE(std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(prepared)->commit().ok());
    CHECK_EQ(fixture.factory->inspect(fixture.request.units[0].subject).expect("old recipe actor").attack, 7.0);
    CHECK(fixture.factory->inspect(fixture.request.units[0].subject)
              .expect("pinned definition generation")
              .definition.generation == original.definition.generation);
}
TEST_CASE("RPG unit codec rejects invalid replacements without losing the published definition") {
    Fixture fixture;
    auto    source = Fixture::asset();
    source.set("max_hp", eve::Value(0));
    REQUIRE(!fixture.units->publish(source).ok());
    CHECK(fixture.units->validate(fixture.request.units[0].definition).ok());
    source = Fixture::asset();
    source.set("typo", eve::Value(1));
    CHECK(!fixture.units->publish(source).ok());
    source = Fixture::asset();
    source.set("version", eve::Value(2));
    CHECK(!fixture.units->publish(source).ok());
}
}  // namespace test_rpg_spawn

namespace test_rpg_spawn {
TEST_CASE("RPG spawn receipts survive definition removal without recreating actors") {
    Fixture fixture;
    fixture.commit();
    auto erased = fixture.registry->remove("rpg.unit", "wolf");
    REQUIRE(erased.ok());
    auto replay = fixture.factory->prepare(fixture.request, fixture.positions);
    REQUIRE(replay.ok());
    REQUIRE(std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(replay.value())->commit().ok());
    CHECK_EQ(fixture.count(), std::size_t(2));
}
TEST_CASE("RPG spawn late operations cannot resurrect earlier published units") {
    Fixture fixture;
    fixture.commit();
    const auto original = fixture.request;
    fixture.request.operation =
        original.lifetime.operationIdentity(eve::spawn::OperationOrdinal(2)).expect("second operation");
    fixture.request.ordinal = eve::spawn::OperationOrdinal(2);
    for (auto& unit : fixture.request.units)
        unit.subject = eve::SubjectRef::fromPersistentId(unit.subject.persistentId().child("second"));
    fixture.commit();
    REQUIRE(fixture.count() == 4);
    auto old = fixture.factory->prepare(original, fixture.positions);
    REQUIRE(!old.ok());
    CHECK(old.status().primaryDiagnostic()->code() == eve::DiagnosticCode::StaleHandle);
}
TEST_CASE("RPG spawn factory release leaves domain-owned actors alive") {
    Fixture fixture;
    fixture.commit();
    fixture.providers.reset();
    fixture.factory.reset();
    REQUIRE(fixture.count() == 2);
}
TEST_CASE("RPG spawn cleanup safely handles a destroyed domain world") {
    Fixture fixture;
    fixture.commit();
    fixture.table.reset();
    eve::spawn::RemovalRequest removal;
    removal.operation = fixture.request.operation.child("remove");
    removal.source    = fixture.request.source;
    removal.policy    = eve::spawn::DetachPolicy::RemoveUnits;
    for (const auto& unit : fixture.request.units) removal.subjects.push_back(unit.subject);
    auto removed = fixture.factory->remove(removal);
    REQUIRE(removed.ok());
    CHECK_EQ(removed.value().completed, std::size_t(2));
}
}  // namespace test_rpg_spawn

namespace test_rpg_spawn {
TEST_CASE("RPG spawn prepared tokens obey the shared source revocation contract") {
    Fixture fixture;
    spawn_factory_contract::sourceRevocation(fixture);
}
TEST_CASE("RPG spawn source destruction invalidates pending creation without destroying the domain") {
    Fixture fixture;
    auto    token = fixture.factory->prepare(fixture.request, fixture.positions).expect("pending token");
    fixture.sourceWorld.reset();
    REQUIRE(!fixture.request.lifetime.isCurrent());
    REQUIRE(!std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(token)->commit().ok());
    CHECK_EQ(fixture.count(), std::size_t(0));
}
TEST_CASE("RPG spawn real actors compose with death cooldown and core budget accounting") {
    Fixture    fixture;
    const auto source  = fixture.request.source;
    auto       created = fixture.sourceWorld->advance({eve::SimulationTick(1), eve::Duration(0)});
    REQUIRE(created.ok());
    REQUIRE(created.value().units == 2);
    CHECK_EQ(fixture.count(), std::size_t(2));
    eve::rpg::RPGActor* killed = nullptr;
    {
        auto view = ecs::View<eve::rpg::RPGActor>(*fixture.table);
        killed    = view.begin().entity();
    }
    eve::rpg::VitalsSystem::takeDamage(killed, "hp", 20);
    std::vector<eve::rpg::VitalsEvent> events;
    eve::rpg::VitalsSystem::pollEvents(events);
    ecs::DestroyEntity(killed);
    auto lifecycle = fixture.factory->collectLifecycle(events).expect("real death facts");
    REQUIRE(lifecycle.size() == 1);
    auto death = fixture.sourceWorld->advance({eve::SimulationTick(2), eve::Duration(0)}, lifecycle);
    REQUIRE(death.ok());
    CHECK_EQ(death.value().units, std::uint32_t(0));
    CHECK_EQ(fixture.sourceWorld->inspect(source).expect("source after death").alive, std::uint32_t(1));
    auto replacement = fixture.sourceWorld->advance({eve::SimulationTick(3), eve::Duration(1000000000)});
    REQUIRE(replacement.ok());
    CHECK_EQ(replacement.value().units, std::uint32_t(1));
    CHECK_EQ(fixture.count(), std::size_t(2));
    CHECK_EQ(fixture.sourceWorld->inspect(source).expect("replacement source").total, std::uint64_t(3));
}
}  // namespace test_rpg_spawn

namespace test_rpg_spawn {
TEST_CASE("RPG spawn source-first destruction preserves actors with an expired origin") {
    Fixture fixture;
    fixture.commit();
    fixture.sourceWorld.reset();
    auto actor = fixture.factory->inspect(fixture.request.units[0].subject);
    REQUIRE(actor.ok());
    CHECK(!actor.value().origin.has_value());
    CHECK_EQ(fixture.count(), std::size_t(2));
    CHECK(fixture.factory->collectLifecycle({}).expect("release expired origins").empty());
}
TEST_CASE("RPG spawn commit during a domain View is rejected without structural changes") {
    Fixture fixture;
    auto    token = fixture.factory->prepare(fixture.request, fixture.positions).expect("prepared actors");
    {
        auto view = ecs::View<eve::rpg::RPGActor>(*fixture.table);
        REQUIRE(!std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(token)->commit().ok());
    }
    REQUIRE(fixture.count() == 0);
    REQUIRE(std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(token)->commit().ok());
    CHECK_EQ(fixture.count(), std::size_t(2));
}
}  // namespace test_rpg_spawn

namespace test_rpg_spawn {
TEST_CASE("RPG spawn actors retain immutable recipe source after preparation is released") {
    Fixture fixture;
    auto    prepared = fixture.factory->prepare(fixture.request, fixture.positions).expect("prepared revision");
    REQUIRE(std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(prepared)->commit().ok());
    prepared     = eve::spawn::PlacementBlocked{};
    auto changed = Fixture::asset();
    changed.set("attack", eve::Value(99));
    REQUIRE(fixture.units->publish(changed).ok());
    std::shared_ptr<const eve::rpg_spawn::UnitRecipe> pinned;
    auto                                              view = ecs::View<eve::rpg::RPGActor>(*fixture.table);
    for (auto it = view.begin(); it != view.end(); ++it) {
        ecs::ComponentRef<eve::rpg_spawn::SpawnedActorState> state(it.entity());
        REQUIRE(state->pinnedRecipe != nullptr);
        if (pinned) CHECK(pinned == state->pinnedRecipe);
        pinned      = state->pinnedRecipe;
        auto source = eve::Value::fromJson(pinned->canonicalJson).expect("retained source");
        CHECK_EQ(source.find("attack")->asInt(), std::int64_t(7));
    }
}
}  // namespace test_rpg_spawn

namespace test_rpg_spawn {
TEST_CASE("RPG spawn rejects an operation identity reused with another ordinal") {
    Fixture fixture;
    fixture.commit();
    fixture.request.ordinal = eve::spawn::OperationOrdinal(2);
    auto rejected           = fixture.factory->prepare(fixture.request, fixture.positions);
    REQUIRE(!rejected.ok());
    CHECK_EQ(fixture.count(), std::size_t(2));
    auto next = fixture.request.lifetime.operationIdentity(fixture.request.ordinal);
    REQUIRE(next.ok());
    CHECK(next.value() != fixture.request.operation);
}
TEST_CASE("RPG spawn source lease identities revoke without extending source lifetime") {
    Fixture fixture;
    auto    lease = fixture.request.lifetime;
    CHECK(!lease.operationIdentity(eve::spawn::OperationOrdinal(0)).ok());
    CHECK_EQ(lease.operationIdentity(fixture.request.ordinal).expect("same operation"), fixture.request.operation);
    fixture.sourceWorld.reset();
    REQUIRE(!lease.isCurrent());
    REQUIRE(!lease.operationIdentity(fixture.request.ordinal).ok());
    REQUIRE(!lease.unitIdentity(fixture.request.ordinal, 0).ok());
    CHECK_EQ(fixture.count(), std::size_t(0));
}
}  // namespace test_rpg_spawn

namespace test_rpg_spawn {
TEST_CASE("RPG archived unit preparation survives registry replacement and destruction without publication") {
    eve::definitions::Definition archive;
    {
        Fixture fixture;
        archive      = fixture.registry->resolve("rpg.unit", "wolf").expect("original unit source").get();
        auto changed = Fixture::asset();
        changed.set("attack", 99);
        fixture.units->publish(changed).expect("replace live unit");
    }
    const auto queries   = eve::cap::listenerCount<eve::IStateQuery>();
    const auto mutations = eve::cap::listenerCount<eve::IStateMutation>();
    auto       recipe    = eve::rpg_spawn::RPGUnitCatalogue::prepareCaptured(archive).expect("admit original unit");
    REQUIRE_EQ(eve::cap::listenerCount<eve::IStateQuery>(), queries);
    REQUIRE_EQ(eve::cap::listenerCount<eve::IStateMutation>(), mutations);
    REQUIRE(recipe.definition.generation == archive.generation);
    const auto captured = recipe.canonicalJson;
    archive.json        = "{}";
    REQUIRE(recipe.canonicalJson == captured);
    ecs::Table       world;
    ecs::ScopedTable scope(world);
    auto             handle = eve::rpg::createInitializedActor(*recipe.member).expect("canonical original actor");
    auto*            actor  = dynamic_cast<eve::rpg::RPGActor*>(ecs::try_get(handle));
    REQUIRE(actor != nullptr);
    REQUIRE_EQ(actor->getBaseAttribute("attack"), 7.0);
    ecs::DestroyEntity(actor);
}
TEST_CASE("RPG archived unit preparation rejects inconsistent envelope metadata and never uses latest content") {
    Fixture    fixture;
    const auto valid = fixture.registry->resolve("rpg.unit", "wolf").expect("original record").get();
    auto       bad   = valid;
    bad.type         = "unit";
    REQUIRE(!eve::rpg_spawn::RPGUnitCatalogue::prepareCaptured(bad).ok());
    bad    = valid;
    bad.id = "different";
    REQUIRE(!eve::rpg_spawn::RPGUnitCatalogue::prepareCaptured(bad).ok());
    bad         = valid;
    bad.version = eve::SchemaVersion(2);
    REQUIRE(!eve::rpg_spawn::RPGUnitCatalogue::prepareCaptured(bad).ok());
    bad            = valid;
    bad.generation = eve::Generation{};
    REQUIRE(!eve::rpg_spawn::RPGUnitCatalogue::prepareCaptured(bad).ok());
    bad        = valid;
    auto value = eve::Value::fromJson(valid.json).expect("source JSON");
    value.set("skill", "missing.archived.skill");
    bad.json = value.toJson().expect("missing skill source");
    REQUIRE(!eve::rpg_spawn::RPGUnitCatalogue::prepareCaptured(bad).ok());
    bad   = valid;
    value = eve::Value::fromJson(valid.json).expect("source JSON");
    value.set("unexpected", true);
    bad.json = value.toJson().expect("unknown field source");
    REQUIRE(!eve::rpg_spawn::RPGUnitCatalogue::prepareCaptured(bad).ok());
    REQUIRE_EQ(fixture.count(), 0u);
    REQUIRE(fixture.units->resolve(fixture.request.units[0].definition).ok());
}
}  // namespace test_rpg_spawn

namespace test_rpg_spawn {
TEST_CASE("RPG pure member checkpoint preparation owns state without entities or global capability publication") {
    std::optional<eve::rpg::PreparedMemberActor> candidate;
    std::string                                  expected;
    {
        Fixture          fixture;
        ecs::ScopedTable scope(*fixture.table);
        auto             recipe = fixture.units->resolve(fixture.request.units[0].definition).expect("source recipe");
        auto             handle = eve::rpg::createInitializedActor(*recipe.member).expect("source actor");
        auto*            actor  = dynamic_cast<eve::rpg::RPGActor*>(ecs::try_get(handle));
        REQUIRE(actor != nullptr);
        actor->setCurrent("hp", 9);
        actor->setSkillCooldown("spawn.basic", 2.75f);
        actor->restoreProgression(4, 25, 120).expect("source progression");
        expected = actor->checkpointJson().expect("source checkpoint");
        ecs::DestroyEntity(actor);
        const auto queries   = eve::cap::listenerCount<eve::IStateQuery>();
        const auto mutations = eve::cap::listenerCount<eve::IStateMutation>();
        candidate.emplace(eve::rpg::PreparedMemberActor::prepare(*recipe.member).expect("pure member preparation"));
        auto input = expected;
        candidate->restoreCheckpoint(input).expect("pure checkpoint admission");
        input = "{}";
        REQUIRE_EQ(fixture.count(), 0u);
        REQUIRE_EQ(eve::cap::listenerCount<eve::IStateQuery>(), queries);
        REQUIRE_EQ(eve::cap::listenerCount<eve::IStateMutation>(), mutations);
    }
    ecs::Table       world;
    ecs::ScopedTable scope(world);
    auto             attempt       = candidate->clone().expect("independent restored attempt");
    auto             attemptHandle = attempt.create().expect("create independent attempt");
    auto*            attemptActor  = dynamic_cast<eve::rpg::RPGActor*>(ecs::try_get(attemptHandle));
    REQUIRE(attemptActor != nullptr);
    REQUIRE(attemptActor->checkpointJson().expect("independent restored facts") == expected);
    ecs::DestroyEntity(attemptActor);
    auto  handle = candidate->create().expect("create after original world and recipe destruction");
    auto* actor  = dynamic_cast<eve::rpg::RPGActor*>(ecs::try_get(handle));
    REQUIRE(actor != nullptr);
    REQUIRE(actor->checkpointJson().expect("restored facts") == expected);
    REQUIRE(!candidate->create().ok());
    REQUIRE(!candidate->clone().ok());
    REQUIRE(!candidate->restoreCheckpoint(expected).ok());
    ecs::DestroyEntity(actor);
}
TEST_CASE("RPG pure member checkpoint failure preserves pinned defaults and creation rejects Views or missing skills") {
    Fixture          fixture;
    ecs::ScopedTable scope(*fixture.table);
    auto             recipe    = fixture.units->resolve(fixture.request.units[0].definition).expect("source recipe");
    auto             candidate = eve::rpg::PreparedMemberActor::prepare(*recipe.member).expect("prepare defaults");
    REQUIRE(!candidate.restoreCheckpoint("{}").ok());
    auto latest = Fixture::asset();
    latest.set("attack", 99);
    fixture.units->publish(latest).expect("replace live recipe");
    {
        auto view = ecs::View<eve::rpg::RPGActor>(*fixture.table);
        REQUIRE(!candidate.create().ok());
    }
    REQUIRE_EQ(fixture.count(), 0u);
    eve::rpg::SkillRegistry::remove("spawn.basic");
    REQUIRE(!candidate.create().ok());
    REQUIRE_EQ(fixture.count(), 0u);
    eve::rpg::SkillDefinition restoredSkill;
    restoredSkill.id = "spawn.basic";
    eve::rpg::SkillRegistry::registerSkill(restoredSkill);
    auto  handle = candidate.create().expect("retry same candidate");
    auto* actor  = dynamic_cast<eve::rpg::RPGActor*>(ecs::try_get(handle));
    REQUIRE(actor != nullptr);
    REQUIRE_EQ(actor->getBaseAttribute("attack"), 7.0);
    REQUIRE_EQ(actor->getCurrent("hp"), 20.0);
    REQUIRE_EQ(actor->getSkillCooldown("spawn.basic"), 0.f);
    ecs::DestroyEntity(actor);
}
TEST_CASE("RPG archived skill admission preserves exact facts after registry replacement and destruction") {
    eve::definitions::Definition archived;
    {
        eve::definitions::DefinitionRegistry registry;
        auto                                 handle = registry.insert("rpg.skill", "archived.fire", 1,
                                                                      R"({"id":"archived.fire","cooldown":8,"castTime":1.25,"targetType":"single",
                "costs":[{"attribute":"mana","amount":20}],"grantedEffects":["archived.burn"],
                "tags":["fire"],"extra":{"projectile":"original"},
                "castCondition":{"kind":"all","children":[]}})");
        REQUIRE(handle.ok());
        archived = registry.resolveHandle(handle.value()).expect("original skill").get();
        registry.replace("rpg.skill", "archived.fire", 1, R"({"cooldown":99})").expect("new revision");
    }
    const auto queries   = eve::cap::listenerCount<eve::IStateQuery>();
    const auto mutations = eve::cap::listenerCount<eve::IStateMutation>();
    const auto skills    = eve::rpg::SkillRegistry::count();
    auto       prepared  = eve::rpg::SkillDefinitionRuntime::prepareCaptured(archived);
    REQUIRE(prepared.ok());
    archived.json     = "null";
    const auto& skill = prepared.value();
    CHECK_EQ(skill.id, std::string("archived.fire"));
    CHECK_EQ(skill.cooldown, 8.f);
    CHECK_EQ(skill.castTime, 1.25f);
    CHECK_EQ(static_cast<int>(skill.castCondition.kind()), static_cast<int>(eve::condition::ConditionKind::All));
    CHECK_EQ(skill.targetType, std::string("single"));
    REQUIRE(skill.cost.has_value());
    REQUIRE_EQ(skill.cost->items().size(), std::size_t(1));
    CHECK_EQ(skill.cost->items()[0].amount.value(), std::int64_t(20));
    CHECK_EQ(skill.grantedEffects.at(0), std::string("archived.burn"));
    CHECK(skill.hasTag("fire"));
    CHECK_EQ(skill.extra.at("projectile"), std::string("original"));
    CHECK_EQ(eve::rpg::SkillRegistry::count(), skills);
    CHECK_EQ(eve::cap::listenerCount<eve::IStateQuery>(), queries);
    CHECK_EQ(eve::cap::listenerCount<eve::IStateMutation>(), mutations);
}
TEST_CASE("RPG archived and live skill admission share identity version and field validation") {
    eve::definitions::DefinitionRegistry registry;
    auto handle = registry.insert("rpg.skill", "archive.valid", 1, R"({"cooldown":4,"futureField":true})")
                      .expect("valid defaulted skill");
    auto original = registry.resolveHandle(handle).expect("source").get();
    auto valid    = eve::rpg::SkillDefinitionRuntime::prepareCaptured(original);
    REQUIRE(valid.ok());
    CHECK_EQ(valid.value().id, std::string("archive.valid"));
    CHECK_EQ(valid.value().targetType, std::string("self"));
    auto rejected = [&](eve::definitions::Definition record) {
        CHECK(!eve::rpg::SkillDefinitionRuntime::prepareCaptured(record).ok());
    };
    auto record = original;
    record.type = "rpg.unit";
    rejected(record);
    record         = original;
    record.version = eve::SchemaVersion(2);
    rejected(record);
    record            = original;
    record.generation = eve::Generation{};
    rejected(record);
    record    = original;
    record.id = "invalid/key";
    rejected(record);
    for (const auto* json : {R"({"id":"other"})", R"({"id":4})", R"({"cooldown":-1})", R"({"castTime":"1"})",
                             R"({"costs":[{"attribute":"mana","amount":1.5}]})", R"({"grantedEffects":[4]})",
                             R"({"extra":{"bad":4}})", "null", "{"}) {
        record      = original;
        record.json = json;
        rejected(record);
    }
    auto id        = *eve::PersistentId::parse("22222222-2222-7222-8222-222222222222");
    auto reference = eve::DefinitionRef::parse("rpg.skill:archive.valid").expect("skill reference");
    auto runtime   = eve::rpg::SkillDefinitionRuntime::create(registry, reference, id).expect("live consumer");
    REQUIRE_EQ(runtime.definition().expect("live typed skill").cooldown, 4.f);
    runtime.state().cooldownRemaining = 3.f;
    auto next = registry.replace("rpg.skill", "archive.valid", 1, R"({"cooldown":2})").expect("valid reload");
    REQUIRE(runtime.reload(eve::definition::ReloadPolicy::RebuildInstance).ok());
    CHECK_EQ(runtime.state().cooldownRemaining, 2.f);
    CHECK_EQ(runtime.definition().expect("reloaded skill").cooldown, 2.f);
    registry.replace("rpg.skill", "archive.valid", 2, R"({"cooldown":99})").expect("unsupported version");
    REQUIRE(!runtime.reload(eve::definition::ReloadPolicy::RebuildInstance).ok());
    CHECK_EQ(runtime.definitionHandle().generation.value(), next.generation.value());
    CHECK_EQ(runtime.state().cooldownRemaining, 2.f);
    CHECK(!eve::rpg::SkillDefinitionRuntime::create(registry, reference, id).ok());
    CHECK(eve::rpg::SkillDefinitionRuntime::prepareCaptured(original).ok());
}
TEST_CASE("RPG spawn domain freeze includes kept actors and outlives every live content owner") {
    using eve::rpg_spawn::RPGSpawnCapture;
    std::optional<RPGSpawnCapture>           frozen;
    auto                                     epoch = *eve::PersistentId::parse("22222222-2222-7222-8222-222222222222");
    std::map<eve::PersistentId, std::string> checkpoints;
    {
        Fixture fixture;
        fixture.commit();
        {
            auto view = ecs::View<eve::rpg::RPGActor>(*fixture.table);
            for (auto it = view.begin(); it != view.end(); ++it) {
                it.entity()->setCurrent("hp", 5);
                it.entity()->setSkillCooldown("spawn.basic", 1.25f);
                checkpoints.emplace(eve::rpg::actorSubject(*it.entity()).value().persistentId(),
                                    it.entity()->checkpointJson().expect("original facts"));
            }
        }
        auto owned = RPGSpawnCapture::capture(*fixture.table, epoch).expect("capture current origins");
        REQUIRE_EQ(owned.actors().size(), std::size_t(2));
        for (const auto& actor : owned.actors()) CHECK(actor.origin == fixture.request.source);
        spawn_factory_contract::removal(fixture, eve::spawn::DetachPolicy::KeepUnits);
        REQUIRE(!fixture.factory->inspect(fixture.request.units[0].subject).ok());
        const auto queries   = eve::cap::listenerCount<eve::IStateQuery>();
        const auto mutations = eve::cap::listenerCount<eve::IStateMutation>();
        frozen.emplace(RPGSpawnCapture::capture(*fixture.table, epoch).expect("freeze retained domain actors"));
        CHECK_EQ(fixture.count(), std::size_t(2));
        CHECK_EQ(eve::cap::listenerCount<eve::IStateQuery>(), queries);
        CHECK_EQ(eve::cap::listenerCount<eve::IStateMutation>(), mutations);
        auto replacement = Fixture::asset();
        replacement.set("attack", eve::Value(99));
        fixture.units->publish(replacement).expect("replace unit after freeze");
        eve::rpg::SkillDefinition replacementSkill;
        replacementSkill.id       = "spawn.basic";
        replacementSkill.cooldown = 99;
        eve::rpg::SkillRegistry::registerSkill(replacementSkill);
    }
    REQUIRE(frozen.has_value());
    CHECK(frozen->saveEpoch() == epoch);
    REQUIRE_EQ(frozen->actors().size(), std::size_t(2));
    CHECK(frozen->actors()[0].subject.persistentId() < frozen->actors()[1].subject.persistentId());
    for (const auto& actor : frozen->actors()) {
        CHECK(!actor.origin.has_value());
        CHECK_EQ(actor.checkpointJson, checkpoints.at(actor.subject.persistentId()));
        REQUIRE(actor.recipe != nullptr);
        REQUIRE(actor.recipe->member != nullptr);
        CHECK_EQ(actor.recipe->member->attack, 7.0);
        CHECK_EQ(actor.position.y, actor.position.x == 1 ? 2.0 : 5.0);
    }
    REQUIRE_EQ(frozen->skills().size(), std::size_t(1));
    auto skill = eve::rpg::SkillDefinitionRuntime::prepareCaptured(frozen->skills()[0]).expect("detached skill record");
    CHECK_EQ(skill.cooldown, 0.f);
    auto moved = std::move(*frozen);
    CHECK(frozen->saveEpoch().isNil());
    CHECK(frozen->actors().empty());
    CHECK(frozen->skills().empty());
    CHECK_EQ(moved.actors().size(), std::size_t(2));
}
TEST_CASE("RPG spawn domain freeze rejects unsafe or corrupt state without changing the live world") {
    Fixture fixture;
    fixture.commit();
    using eve::rpg_spawn::RPGSpawnCapture;
    const auto epoch = fixture.request.operation;
    REQUIRE(!RPGSpawnCapture::capture(*fixture.table, {}).ok());
    {
        auto view = ecs::View<eve::rpg::RPGActor>(*fixture.table);
        CHECK(!RPGSpawnCapture::capture(*fixture.table, epoch).ok());
        view.begin().entity()->skills()->casting.active = true;
    }
    CHECK(!RPGSpawnCapture::capture(*fixture.table, epoch).ok());
    {
        auto view                                       = ecs::View<eve::rpg::RPGActor>(*fixture.table);
        view.begin().entity()->skills()->casting.active = false;
        auto  it                                        = view.begin();
        auto* first                                     = it.entity();
        ++it;
        auto* second = it.entity();
        // Deliberately corrupt raw ECS storage; the registration API rejects this duplicate.
        auto* buffer             = first->getComponentManager().getComponentBuffer<eve::rpg::ActorIdentity>();
        *buffer->ptr(second->id) = *buffer->ptr(first->id);
    }
    CHECK(!RPGSpawnCapture::capture(*fixture.table, epoch).ok());
    CHECK_EQ(fixture.count(), std::size_t(2));
}
TEST_CASE("RPG spawn domain freeze rejects divergent skill projections and recovers after canonical repair") {
    Fixture fixture;
    fixture.commit();
    using eve::rpg_spawn::RPGSpawnCapture;
    auto original = eve::rpg::SkillRegistry::captureDefinition("spawn.basic").expect("canonical live skill");
    eve::rpg::SkillRegistry::definitionRegistry()
        .replace("rpg.skill", "spawn.basic", 1, R"({"cooldown":99})")
        .expect("independent canonical mutation");
    CHECK(!eve::rpg::SkillRegistry::captureDefinition("spawn.basic").ok());
    CHECK(!RPGSpawnCapture::capture(*fixture.table, fixture.request.operation).ok());
    CHECK_EQ(fixture.count(), std::size_t(2));
    auto actual = eve::rpg::SkillDefinitionRuntime::prepareCaptured(original).expect("original archived content");
    eve::rpg::SkillRegistry::registerSkill(actual);
    CHECK(RPGSpawnCapture::capture(*fixture.table, fixture.request.operation).ok());
    REQUIRE(eve::rpg::SkillRegistry::remove("spawn.basic"));
    CHECK(!RPGSpawnCapture::capture(*fixture.table, fixture.request.operation).ok());
    CHECK_EQ(fixture.count(), std::size_t(2));
    eve::rpg::SkillRegistry::registerSkill(actual);
}
TEST_CASE("RPG spawn domain freeze retains dead actors without origins and excludes unrelated actors") {
    Fixture fixture;
    fixture.commit();
    {
        ecs::ScopedTable current(*fixture.table);
        const auto       unrelated = ecs::handle_of(eve::rpg::RPGActor::createActor());
        REQUIRE(ecs::try_get(unrelated) != nullptr);
    }
    ecs::EntityHandle killed;
    {
        auto view = ecs::View<eve::rpg::RPGActor>(*fixture.table);
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto* buffer = it.entity()->getComponentManager().getComponentBuffer<eve::rpg_spawn::SpawnedActorState>();
            if (buffer && buffer->has(it.entity()->id)) {
                killed = ecs::handle_of(it.entity());
                break;
            }
        }
    }
    auto* actor = dynamic_cast<eve::rpg::RPGActor*>(ecs::try_get(killed));
    REQUIRE(actor != nullptr);
    eve::rpg::VitalsSystem::takeDamage(actor, "hp", 30);
    std::vector<eve::rpg::VitalsEvent> events;
    eve::rpg::VitalsSystem::pollEvents(events);
    auto lifecycle = fixture.factory->collectLifecycle(events).expect("handoff without deleting corpse");
    REQUIRE_EQ(lifecycle.size(), std::size_t(1));
    auto frozen = eve::rpg_spawn::RPGSpawnCapture::capture(*fixture.table, fixture.request.operation)
                      .expect("include retained corpse");
    CHECK_EQ(fixture.count(), std::size_t(3));
    REQUIRE_EQ(frozen.actors().size(), std::size_t(2));
    std::size_t cleared = 0;
    for (const auto& entry : frozen.actors()) {
        if (entry.subject != lifecycle[0].subject) continue;
        CHECK(!entry.origin.has_value());
        auto checkpoint = eve::Value::fromJson(entry.checkpointJson).expect("dead actor checkpoint");
        REQUIRE(checkpoint.find("vitals") != nullptr);
        CHECK_EQ(checkpoint.find("vitals")->find("hp")->asDouble(), 0.0);
        ++cleared;
    }
    CHECK_EQ(cleared, std::size_t(1));
}

namespace {
eve::Result<eve::ContentId> archiveTestHash(std::string_view text) {
    std::uint64_t state = 14695981039346656037ULL;
    for (const unsigned char c : text) state = (state ^ c) * 1099511628211ULL;
    eve::ContentId::Bytes bytes{};
    for (auto& byte : bytes) {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        byte = static_cast<std::uint8_t>(state * 2685821657736338717ULL >> 32);
    }
    return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
}
}  // namespace
TEST_CASE("RPG spawn archive round trips detached actors and distinct pinned revisions without live registries") {
    std::optional<eve::rpg_spawn::RPGSpawnCapture> frozen;
    auto epoch = *eve::PersistentId::parse("22222222-2222-7222-8222-222222222222");
    {
        Fixture fixture;
        fixture.commit();
        auto replacement = Fixture::asset();
        replacement.set("attack", eve::Value(99));
        fixture.units->publish(replacement).expect("new unit revision");
        fixture.request.ordinal = eve::spawn::OperationOrdinal(2);
        fixture.request.operation =
            fixture.request.lifetime.operationIdentity(fixture.request.ordinal).expect("operation two");
        for (std::size_t i = 0; i < fixture.request.units.size(); ++i)
            fixture.request.units[i].subject =
                fixture.request.lifetime.unitIdentity(fixture.request.ordinal, static_cast<std::uint32_t>(i))
                    .expect("new member identity");
        fixture.commit();
        REQUIRE_EQ(fixture.count(), std::size_t(4));
        frozen.emplace(eve::rpg_spawn::RPGSpawnCapture::capture(*fixture.table, epoch).expect("freeze two revisions"));
    }
    eve::rpg::SkillRegistry::clear();
    auto saved =
        frozen->seal(eve::Revision(7), eve::SimulationTick(9), archiveTestHash).expect("seal after owners die");
    auto again =
        frozen->seal(eve::Revision(7), eve::SimulationTick(9), archiveTestHash).expect("repeat deterministic seal");
    REQUIRE_EQ(eve::serializeSnapshotEnvelope(saved).expect("first bytes"),
               eve::serializeSnapshotEnvelope(again).expect("same bytes"));
    auto text     = eve::serializeSnapshotEnvelope(saved).expect("durable bytes");
    auto envelope = eve::parseSnapshotEnvelope(text, archiveTestHash).expect("common envelope read");
    auto parsed = eve::rpg_spawn::RPGSpawnArchive::parse(envelope, epoch, archiveTestHash).expect("structural import");
    REQUIRE_EQ(parsed.actors().size(), std::size_t(4));
    REQUIRE_EQ(parsed.units().size(), std::size_t(2));
    REQUIRE_EQ(parsed.skills().size(), std::size_t(1));
    auto admitted = eve::rpg_spawn::RPGSpawnRestoreState::prepare(parsed).expect("admit both pinned generations");
    CHECK_EQ(admitted.actorCount(), std::size_t(4));
    CHECK_EQ(admitted.unitRevisionCount(), std::size_t(2));
    CHECK(parsed.saveEpoch() == epoch);
    CHECK(parsed.units()[0].generation != parsed.units()[1].generation);
    for (const auto& actor : parsed.actors()) {
        REQUIRE(actor.origin.has_value());
        CHECK_EQ(actor.origin->slot, frozen->actors()[&actor - parsed.actors().data()].origin->slot.index());
        static_assert(!std::is_convertible_v<eve::rpg_spawn::ArchivedSpawnOrigin, eve::spawn::SpawnerHandle>);
        CHECK_EQ(actor.checkpointJson, frozen->actors()[&actor - parsed.actors().data()].checkpointJson);
        CHECK_EQ(parsed.units()[actor.unitRevision].json,
                 frozen->actors()[&actor - parsed.actors().data()].recipe->canonicalJson);
    }
    envelope.payload = eve::Value{};
    CHECK_EQ(parsed.actors().size(), std::size_t(4));
    CHECK_EQ(eve::rpg::SkillRegistry::count(), 0);
    auto moved = std::move(parsed);
    CHECK(parsed.saveEpoch().isNil());
    CHECK(parsed.actors().empty());
    CHECK(parsed.units().empty());
    CHECK_EQ(moved.actors().size(), std::size_t(4));
}
TEST_CASE("RPG spawn archive rejects damaged epochs versions fields references and noncanonical integers") {
    Fixture fixture;
    fixture.commit();
    auto epoch  = fixture.request.operation;
    auto frozen = eve::rpg_spawn::RPGSpawnCapture::capture(*fixture.table, epoch).expect("capture");
    auto valid  = frozen.seal(eve::Revision(1), eve::SimulationTick(2), archiveTestHash).expect("sealed");
    REQUIRE(!eve::rpg_spawn::RPGSpawnArchive::parse(valid, epoch.child("other"), archiveTestHash).ok());
    auto damaged = valid;
    damaged.payload.set("unknown", true);
    REQUIRE(!eve::rpg_spawn::RPGSpawnArchive::parse(damaged, epoch, archiveTestHash).ok());
    auto reject = [&](eve::SnapshotEnvelope value) {
        auto resealed = eve::makeSnapshotEnvelope(value.type, value.schema, value.schemaVersion, value.instanceId,
                                                  value.revision, value.tick, std::move(value.payload), archiveTestHash)
                            .expect("test reseal");
        REQUIRE(!eve::rpg_spawn::RPGSpawnArchive::parse(resealed, epoch, archiveTestHash).ok());
    };
    reject(damaged);
    damaged               = valid;
    damaged.schemaVersion = eve::SchemaVersion(5);
    reject(damaged);
    damaged = valid;
    damaged.payload.find("units")->at(0).set("generation", "00");
    reject(damaged);
    damaged = valid;
    damaged.payload.find("units")->at(0).set("version", 2);
    reject(damaged);
    damaged = valid;
    damaged.payload.find("units")->at(0).set("json", "{");
    reject(damaged);
    damaged = valid;
    damaged.payload.find("actors")->at(0).set("unit_revision", "99");
    reject(damaged);
    damaged = valid;
    damaged.payload.find("actors")->at(0).find("origin")->set("incarnation", "1");
    reject(damaged);
    damaged = valid;
    damaged.payload.find("actors")->at(0).find("origin")->set("epoch", "0");
    reject(damaged);
    damaged = valid;
    damaged.payload.find("actors")->at(0).set("position", eve::Value::array({"bad", 2}));
    reject(damaged);
    damaged = valid;
    damaged.payload.find("actors")->at(0).set("checkpoint", R"({"schema":"eve.rpg.actor-checkpoint","version":99})");
    reject(damaged);
    damaged = valid;
    damaged.payload.find("actors")->at(1).set("subject",
                                              damaged.payload.find("actors")->at(0).find("subject")->asString());
    reject(damaged);
    damaged     = valid;
    auto& units = *damaged.payload.find("units")->getIf<eve::Value::Array>();
    units.push_back(units[0]);
    reject(damaged);
    damaged      = valid;
    auto& skills = *damaged.payload.find("skills")->getIf<eve::Value::Array>();
    skills.push_back(skills[0]);
    reject(damaged);
    REQUIRE_EQ(fixture.count(), std::size_t(2));
    REQUIRE(eve::rpg_spawn::RPGSpawnArchive::parse(valid, epoch, archiveTestHash).ok());
}
TEST_CASE("RPG spawn archive hash failures reentry and caller mutation preserve retry and frozen bytes") {
    Fixture fixture;
    fixture.commit();
    auto epoch  = fixture.request.operation;
    auto frozen = eve::rpg_spawn::RPGSpawnCapture::capture(*fixture.table, epoch).expect("capture");
    REQUIRE(!frozen.seal(eve::Revision(0), eve::SimulationTick(1), archiveTestHash).ok());
    REQUIRE(!frozen.seal(eve::Revision(1), eve::SimulationTick(1), {}).ok());
    bool rejected = false;
    auto bad =
        frozen.seal(eve::Revision(1), eve::SimulationTick(1), [&](std::string_view) -> eve::Result<eve::ContentId> {
            rejected = !frozen.seal(eve::Revision(1), eve::SimulationTick(1), archiveTestHash).ok();
            throw std::runtime_error("injected digest failure");
        });
    REQUIRE(!bad.ok());
    REQUIRE(rejected);
    CHECK(bad.status().primaryDiagnostic()->code() == eve::DiagnosticCode::CallbackFailure);
    auto valid  = frozen.seal(eve::Revision(1), eve::SimulationTick(1), archiveTestHash).expect("retry after callback");
    auto parsed = eve::rpg_spawn::RPGSpawnArchive::parse(valid, epoch, [&](std::string_view bytes) {
                      valid.payload.find("actors")->getIf<eve::Value::Array>()->clear();
                      return archiveTestHash(bytes);
                  }).expect("input copied before callback");
    REQUIRE_EQ(parsed.actors().size(), std::size_t(2));
    CHECK(valid.payload.find("actors")->getIf<eve::Value::Array>()->empty());
    CHECK_EQ(fixture.count(), std::size_t(2));
}

TEST_CASE("RPG archive domain admission uses captured skills after all live content is replaced and removed") {
    std::optional<eve::rpg_spawn::RPGSpawnRestoreState> admitted;
    auto epoch = *eve::PersistentId::parse("22222222-2222-7222-8222-222222222222");
    {
        Fixture fixture;
        fixture.commit();
        auto frozen      = eve::rpg_spawn::RPGSpawnCapture::capture(*fixture.table, epoch).expect("freeze");
        auto saved       = frozen.seal(eve::Revision(1), eve::SimulationTick(1), archiveTestHash).expect("archive");
        auto replacement = Fixture::asset();
        replacement.set("attack", 99);
        fixture.units->publish(replacement).expect("new live recipe");
        eve::rpg::SkillRegistry::clear();
        auto       archive = eve::rpg_spawn::RPGSpawnArchive::parse(saved, epoch, archiveTestHash).expect("structure");
        const auto queries = eve::cap::listenerCount<eve::IStateQuery>();
        const auto mutations = eve::cap::listenerCount<eve::IStateMutation>();
        admitted.emplace(eve::rpg_spawn::RPGSpawnRestoreState::prepare(archive).expect("canonical domain admission"));
        CHECK_EQ(fixture.count(), std::size_t(2));
        CHECK_EQ(eve::rpg::SkillRegistry::count(), 0);
        CHECK_EQ(eve::cap::listenerCount<eve::IStateQuery>(), queries);
        CHECK_EQ(eve::cap::listenerCount<eve::IStateMutation>(), mutations);
    }
    REQUIRE(admitted.has_value());
    REQUIRE_EQ(admitted->actorCount(), std::size_t(2));
    REQUIRE_EQ(admitted->unitRevisionCount(), std::size_t(1));
    CHECK(admitted->saveEpoch() == epoch);
    auto moved = std::move(*admitted);
    CHECK_EQ(admitted->actorCount(), std::size_t(0));
    CHECK(admitted->saveEpoch().isNil());
    CHECK_EQ(moved.actorCount(), std::size_t(2));
}
TEST_CASE(
    "RPG archive domain admission rejects semantic corruption and live-only skill references without publication") {
    Fixture fixture;
    fixture.commit();
    auto       epoch     = fixture.request.operation;
    auto       frozen    = eve::rpg_spawn::RPGSpawnCapture::capture(*fixture.table, epoch).expect("freeze");
    auto       saved     = frozen.seal(eve::Revision(1), eve::SimulationTick(1), archiveTestHash).expect("archive");
    const auto queries   = eve::cap::listenerCount<eve::IStateQuery>();
    const auto mutations = eve::cap::listenerCount<eve::IStateMutation>();
    auto       reject    = [&](eve::Value payload) {
        auto envelope = eve::makeSnapshotEnvelope(saved.type, saved.schema, saved.schemaVersion, saved.instanceId,
                                                           saved.revision, saved.tick, std::move(payload), archiveTestHash)
                            .expect("semantic-corruption fixture");
        auto archive =
            eve::rpg_spawn::RPGSpawnArchive::parse(envelope, epoch, archiveTestHash).expect("valid structure only");
        REQUIRE(!eve::rpg_spawn::RPGSpawnRestoreState::prepare(archive).ok());
        CHECK_EQ(fixture.count(), std::size_t(2));
        CHECK_EQ(eve::cap::listenerCount<eve::IStateQuery>(), queries);
        CHECK_EQ(eve::cap::listenerCount<eve::IStateMutation>(), mutations);
    };
    for (bool unknown : {false, true}) {
        auto payload = saved.payload;
        auto unit    = eve::Value::fromJson(payload.find("units")->at(0).find("json")->asString()).expect("unit");
        if (unknown)
            unit.set("unknown", true);
        else
            unit.set("max_hp", -1);
        payload.find("units")->at(0).set("json", unit.toJson().expect("unit bytes"));
        reject(std::move(payload));
    }
    auto missing = saved.payload;
    missing.find("skills")->getIf<eve::Value::Array>()->clear();
    reject(std::move(missing));
    for (int kind = 0; kind < 5; ++kind) {
        auto payload = saved.payload;
        auto actor = eve::Value::fromJson(payload.find("actors")->at(1).find("checkpoint")->asString()).expect("actor");
        if (kind == 0) actor.find("vitals")->set("hp", 999);
        if (kind == 1) actor.find("progression")->set("level", -1);
        if (kind == 2) actor.find("class")->set("id", "foreign");
        if (kind == 3) actor.find("cooldowns")->at(0).set("remainingSeconds", -1);
        if (kind == 4) {
            eve::rpg::SkillDefinition liveOnly;
            liveOnly.id = "live.only";
            eve::rpg::SkillRegistry::registerSkill(liveOnly);
            actor.find("learnedSkills")->getIf<eve::Value::Array>()->at(0) = eve::Value("live.only");
            actor.find("cooldowns")->at(0).set("id", "live.only");
        }
        payload.find("actors")->at(1).set("checkpoint", actor.toJson().expect("actor bytes"));
        reject(std::move(payload));
    }
    auto archive = eve::rpg_spawn::RPGSpawnArchive::parse(saved, epoch, archiveTestHash).expect("unchanged source");
    REQUIRE(eve::rpg_spawn::RPGSpawnRestoreState::prepare(archive).ok());
}
TEST_CASE("RPG archived actor candidates retain exact skills through clone and reject live revision substitution") {
    Fixture fixture;
    fixture.commit();
    auto       source           = eve::rpg::SkillRegistry::captureDefinition("spawn.basic").expect("original source");
    const auto registrySnapshot = eve::rpg::SkillRegistry::definitionRegistry().snapshotJson();
    auto       skill            = eve::rpg::SkillDefinitionRuntime::prepareCaptured(source).expect("original skill");
    auto       content          = std::make_shared<const eve::rpg::SkillContent>(
        eve::rpg::SkillContent::prepare(std::span<const eve::definitions::Definition>(&source, 1))
            .expect("skill archive"));
    auto        member = fixture.units->resolve(fixture.request.units[0].definition).expect("original member");
    std::string checkpoint;
    {
        auto  view  = ecs::View<eve::rpg::RPGActor>(*fixture.table);
        auto* actor = view.begin().entity();
        actor->setCurrent("hp", 5);
        actor->setSkillCooldown("spawn.basic", 1.25f);
        checkpoint = actor->checkpointJson().expect("checkpoint");
    }
    eve::rpg::SkillRegistry::clear();
    auto candidate =
        eve::rpg::PreparedMemberActor::prepareCaptured(*member.member, content).expect("no live skill lookup");
    candidate.restoreCheckpoint(checkpoint).expect("canonical checkpoint with archive references");
    content.reset();
    auto             clone = candidate.clone().expect("independent actor attempt");
    ecs::Table       target;
    ecs::ScopedTable current(target);
    auto             count = [&] {
        std::size_t n    = 0;
        auto        view = ecs::View<eve::rpg::RPGActor>(target);
        for (auto it = view.begin(); it != view.end(); ++it) ++n;
        return n;
    };
    REQUIRE(!candidate.create().ok());
    CHECK_EQ(count(), std::size_t(0));
    auto replacement     = skill;
    replacement.cooldown = 99;
    eve::rpg::SkillRegistry::registerSkill(replacement);
    REQUIRE(!clone.create().ok());
    CHECK_EQ(count(), std::size_t(0));
    eve::rpg::SkillRegistry::registerSkill(skill);
    REQUIRE(!candidate.create().ok());  // Same bytes in another generation do not satisfy the exact archive.
    eve::rpg::SkillRegistry::definitionRegistry()
        .restoreJson(registrySnapshot)
        .expect("host admits exact record generation");
    auto first  = clone.create().expect("retry cloned candidate");
    auto second = candidate.create().expect("retry original candidate");
    REQUIRE_EQ(count(), std::size_t(2));
    for (auto handle : {first, second}) {
        auto* actor = dynamic_cast<eve::rpg::RPGActor*>(ecs::try_get(handle));
        REQUIRE(actor != nullptr);
        CHECK_EQ(actor->checkpointJson().expect("materialized facts"), checkpoint);
        ecs::DestroyEntity(actor);
    }
    CHECK_EQ(count(), std::size_t(0));
}

TEST_CASE("RPG archived skill admission preserves canonical unknown field policy during projection validation") {
    Fixture fixture;
    auto    source  = eve::rpg::SkillRegistry::captureDefinition("spawn.basic").expect("source");
    auto    payload = eve::Value::fromJson(source.json).expect("skill fields");
    payload.set("futureField", true);
    eve::rpg::SkillRegistry::definitionRegistry()
        .replace("rpg.skill", "spawn.basic", 1, payload.toJson().expect("extended record"))
        .expect("canonical extension");
    auto captured = eve::rpg::SkillRegistry::captureDefinition("spawn.basic").expect("known fields match gameplay");
    auto skills   = std::make_shared<const eve::rpg::SkillContent>(
        eve::rpg::SkillContent::prepare(std::span<const eve::definitions::Definition>(&captured, 1))
            .expect("admitted skill content"));
    auto member    = fixture.units->resolve(fixture.request.units[0].definition).expect("member");
    auto candidate = eve::rpg::PreparedMemberActor::prepareCaptured(*member.member, skills).expect("candidate");
    ecs::ScopedTable current(*fixture.table);
    auto             handle = candidate.create().expect("same exact source despite ignored extension");
    REQUIRE(ecs::try_get(handle) != nullptr);
    CHECK_EQ(skills->source("spawn.basic").expect("exact bytes").get().json, captured.json);
    ecs::DestroyEntity(ecs::try_get(handle));
    CHECK_EQ(fixture.count(), std::size_t(0));
}

TEST_CASE("RPG effect archive freezes direct skill dependencies and admits them after every runtime owner disappears") {
    std::optional<eve::rpg_spawn::RPGSpawnCapture> frozen;
    auto epoch = *eve::PersistentId::parse("22222222-2222-7222-8222-222222222222");
    {
        Fixture fixture;
        fixture.commit();
        eve::rpg::SkillDefinition skill;
        skill.id             = "spawn.basic";
        skill.grantedEffects = {"archive.poison", "archive.poison"};
        eve::rpg::SkillRegistry::registerSkill(skill);
        eve::rpg::EffectDefinition effect;
        effect.id             = "archive.poison";
        effect.durationPolicy = "duration";
        effect.duration       = 3;
        effect.period         = 0.5f;
        effect.modifiers.push_back({"attack", "add", -2, 7});
        effect.tags = {"poison"};
        eve::rpg::EffectRegistry::registerEffect(effect);
        frozen.emplace(
            eve::rpg_spawn::RPGSpawnCapture::capture(*fixture.table, epoch).expect("capture actual effect content"));
        REQUIRE_EQ(frozen->effects().size(), std::size_t(1));
        effect.modifiers[0].value = -99;
        eve::rpg::EffectRegistry::registerEffect(effect);
    }
    eve::rpg::SkillRegistry::clear();
    eve::rpg::EffectRegistry::clear();
    auto saved =
        frozen->seal(eve::Revision(1), eve::SimulationTick(3), archiveTestHash).expect("seal detached effect facts");
    REQUIRE_EQ(saved.schemaVersion.value(), 4u);
    auto bytes    = eve::serializeSnapshotEnvelope(saved).expect("archive bytes");
    auto envelope = eve::parseSnapshotEnvelope(bytes, archiveTestHash).expect("envelope roundtrip");
    auto archive  = eve::rpg_spawn::RPGSpawnArchive::parse(envelope, epoch, archiveTestHash).expect("effect archive");
    REQUIRE_EQ(archive.effects().size(), std::size_t(1));
    CHECK_EQ(archive.effects()[0].modifiers[0].value, -2.0);
    CHECK_EQ(archive.effects()[0].period, 0.5f);
    CHECK(archive.effects()[0].hasTag("poison"));
    auto state = eve::rpg_spawn::RPGSpawnRestoreState::prepare(archive).expect("pure direct effect closure");
    REQUIRE_EQ(state.actorCount(), std::size_t(2));
    CHECK_EQ(eve::rpg::SkillRegistry::count(), 0);
    CHECK_EQ(eve::rpg::EffectRegistry::count(), 0);
    auto moved = std::move(archive);
    CHECK(archive.effects().empty());
    CHECK_EQ(moved.effects().size(), std::size_t(1));
}
TEST_CASE("RPG effect archive rejects missing dependencies duplicates and invalid fields without live fallback") {
    Fixture fixture;
    fixture.commit();
    eve::rpg::SkillDefinition skill;
    skill.id             = "spawn.basic";
    skill.grantedEffects = {"archive.required"};
    eve::rpg::SkillRegistry::registerSkill(skill);
    eve::rpg::EffectRegistry::remove("archive.required");
    REQUIRE(!eve::rpg_spawn::RPGSpawnCapture::capture(*fixture.table, fixture.request.operation).ok());
    eve::rpg::EffectDefinition effect;
    effect.id = "archive.required";
    eve::rpg::EffectRegistry::registerEffect(effect);
    auto frozen = eve::rpg_spawn::RPGSpawnCapture::capture(*fixture.table, fixture.request.operation)
                      .expect("capture after content repair");
    auto saved  = frozen.seal(eve::Revision(1), eve::SimulationTick(1), archiveTestHash).expect("effect source");
    auto reseal = [&](eve::Value payload) {
        return eve::makeSnapshotEnvelope(saved.type, saved.schema, saved.schemaVersion, saved.instanceId,
                                         saved.revision, saved.tick, std::move(payload), archiveTestHash)
            .expect("fixture envelope");
    };
    auto missing = saved.payload;
    missing.find("effects")->getIf<eve::Value::Array>()->clear();
    auto archive = eve::rpg_spawn::RPGSpawnArchive::parse(reseal(missing), saved.instanceId, archiveTestHash)
                       .expect("valid structural absence");
    REQUIRE(!eve::rpg_spawn::RPGSpawnRestoreState::prepare(archive).ok());
    REQUIRE(eve::rpg::EffectRegistry::find(effect.id) != nullptr);
    auto  duplicate = saved.payload;
    auto& effects   = *duplicate.find("effects")->getIf<eve::Value::Array>();
    effects.push_back(effects[0]);
    REQUIRE(!eve::rpg_spawn::RPGSpawnArchive::parse(reseal(duplicate), saved.instanceId, archiveTestHash).ok());
    auto invalid = saved.payload;
    invalid.find("effects")->at(0).set("duration", -1);
    REQUIRE(!eve::rpg_spawn::RPGSpawnArchive::parse(reseal(invalid), saved.instanceId, archiveTestHash).ok());
    CHECK_EQ(fixture.count(), std::size_t(2));
    eve::rpg::EffectRegistry::remove(effect.id);
}
TEST_CASE("RPG archive v1 migration supplies no invented effects and keeps missing-reference failures explicit") {
    Fixture fixture;
    fixture.commit();
    auto frozen =
        eve::rpg_spawn::RPGSpawnCapture::capture(*fixture.table, fixture.request.operation).expect("simple source");
    auto saved     = frozen.seal(eve::Revision(1), eve::SimulationTick(1), archiveTestHash).expect("v4");
    auto v2Payload = saved.payload;
    v2Payload.getIf<eve::Value::Object>()->erase("implementations");
    v2Payload.getIf<eve::Value::Object>()->erase("future_units");
    auto v2 = eve::makeSnapshotEnvelope(saved.type, saved.schema, eve::SchemaVersion(2), saved.instanceId,
                                        saved.revision, saved.tick, v2Payload, archiveTestHash)
                  .expect("explicit v2 source");
    auto migratedV2 = eve::rpg_spawn::RPGSpawnArchive::parse(v2, saved.instanceId, archiveTestHash).expect("v2 read");
    REQUIRE(migratedV2.implementations().empty());
    REQUIRE(eve::rpg_spawn::RPGSpawnRestoreState::prepare(migratedV2).ok());
    auto legacy = saved.payload;
    legacy.getIf<eve::Value::Object>()->erase("effects");
    legacy.getIf<eve::Value::Object>()->erase("implementations");
    legacy.getIf<eve::Value::Object>()->erase("future_units");
    auto old = eve::makeSnapshotEnvelope(saved.type, saved.schema, eve::SchemaVersion(1), saved.instanceId,
                                         saved.revision, saved.tick, legacy, archiveTestHash)
                   .expect("explicit v1 source");
    auto archive = eve::rpg_spawn::RPGSpawnArchive::parse(old, saved.instanceId, archiveTestHash).expect("v1 read");
    REQUIRE(archive.effects().empty());
    REQUIRE(eve::rpg_spawn::RPGSpawnRestoreState::prepare(archive).ok());
    auto body = eve::Value::fromJson(legacy.find("skills")->at(0).find("json")->asString()).expect("skill fields");
    body.set("grantedEffects", eve::Value::array({"legacy.missing.effect"}));
    legacy.find("skills")->at(0).set("json", body.toJson().expect("old reference"));
    auto absent = eve::makeSnapshotEnvelope(saved.type, saved.schema, eve::SchemaVersion(1), saved.instanceId,
                                            saved.revision, saved.tick, legacy, archiveTestHash)
                      .expect("v1 with unarchived effect");
    auto incomplete =
        eve::rpg_spawn::RPGSpawnArchive::parse(absent, saved.instanceId, archiveTestHash).expect("v1 structure");
    REQUIRE(!eve::rpg_spawn::RPGSpawnRestoreState::prepare(incomplete).ok());
}
TEST_CASE("RPG archived actor creation binds exact effect facts through clone and retry") {
    Fixture                   fixture;
    eve::rpg::SkillDefinition skill;
    skill.id             = "spawn.basic";
    skill.grantedEffects = {"archive.bound"};
    eve::rpg::SkillRegistry::registerSkill(skill);
    eve::rpg::EffectDefinition effect;
    effect.id             = "archive.bound";
    effect.durationPolicy = "duration";
    effect.duration       = 2;
    effect.modifiers.push_back({"attack", "add", -0.0, 0});
    eve::rpg::EffectRegistry::registerEffect(effect);
    auto source  = eve::rpg::SkillRegistry::captureDefinition(skill.id).expect("skill record");
    auto content = std::make_shared<const eve::rpg::SkillContent>(
        eve::rpg::SkillContent::prepareWithEffects(std::span<const eve::definitions::Definition>(&source, 1),
                                                   std::span<const eve::rpg::EffectDefinition>(&effect, 1))
            .expect("effect closure"));
    auto member = fixture.units->resolve(fixture.request.units[0].definition).expect("member");
    auto candidate =
        eve::rpg::PreparedMemberActor::prepareCaptured(*member.member, content).expect("archive candidate");
    auto clone = candidate.clone().expect("same immutable effect content");
    content.reset();
    ecs::ScopedTable current(*fixture.table);
    auto             incompleteContent = std::make_shared<const eve::rpg::SkillContent>(
        eve::rpg::SkillContent::prepare(std::span<const eve::definitions::Definition>(&source, 1))
            .expect("skill facts only"));
    auto incomplete =
        eve::rpg::PreparedMemberActor::prepareCaptured(*member.member, incompleteContent).expect("CPU candidate");
    REQUIRE(!incomplete.create().ok());  // A live effect cannot fill missing archived effect facts.
    CHECK_EQ(fixture.count(), std::size_t(0));

    eve::rpg::EffectRegistry::remove(effect.id);
    REQUIRE(!candidate.create().ok());
    CHECK_EQ(fixture.count(), std::size_t(0));
    auto changed               = effect;
    changed.modifiers[0].value = 99;
    eve::rpg::EffectRegistry::registerEffect(changed);
    REQUIRE(!clone.create().ok());
    CHECK_EQ(fixture.count(), std::size_t(0));
    auto positiveZero               = effect;
    positiveZero.modifiers[0].value = 0.0;
    eve::rpg::EffectRegistry::registerEffect(positiveZero);
    REQUIRE(!clone.create().ok());  // Canonical bytes preserve the sign of zero in source fields.
    CHECK_EQ(fixture.count(), std::size_t(0));
    eve::rpg::EffectRegistry::registerEffect(effect);
    auto first  = candidate.create().expect("retry exact effects");
    auto second = clone.create().expect("retry clone exact effects");
    REQUIRE_EQ(fixture.count(), std::size_t(2));
    for (auto handle : {first, second}) ecs::DestroyEntity(ecs::try_get(handle));
    eve::rpg::EffectRegistry::remove(effect.id);
    CHECK_EQ(fixture.count(), std::size_t(0));
}
TEST_CASE("RPG archived content retains distinct external implementation uses without binding") {
    Fixture                   fixture;
    eve::rpg::SkillDefinition skill;
    skill.id            = "spawn.basic";
    skill.castCondition = eve::condition::Condition::all(
        {eve::condition::Condition::policyCall("archive.custom", eve::Value::object({{"mode", "first"}})),
         eve::condition::Condition::policyCall("archive.custom", eve::Value::object({{"mode", "second"}}))});
    skill.grantedEffects = {"archive.requirements"};
    eve::rpg::SkillRegistry::registerSkill(skill);
    auto source = eve::rpg::SkillRegistry::captureDefinition(skill.id).expect("actual skill source");
    eve::rpg::EffectDefinition effect;
    effect.id          = "archive.requirements";
    effect.stackPolicy = "archive.custom";
    effect.modifiers.push_back({"attack", "archive.custom", 2.0, 3});
    effect.modifiers.push_back({"attack", "add", 1.0, 0});
    auto content = eve::rpg::SkillContent::prepareWithEffects(std::span<const eve::definitions::Definition>(&source, 1),
                                                              std::span<const eve::rpg::EffectDefinition>(&effect, 1))
                       .expect("unbound immutable content");
    eve::rpg::SkillRegistry::clear();
    auto requirements = content.implementationRequirements();
    REQUIRE_EQ(requirements.size(), std::size_t(3));
    REQUIRE(requirements[0].kind == eve::rpg::ImplementationKind::ConditionPolicy);
    REQUIRE_EQ(requirements[0].uses.size(), std::size_t(2));
    REQUIRE_EQ(requirements[0].uses[0].path, "castCondition.children[0]");
    REQUIRE_EQ(requirements[0].uses[1].path, "castCondition.children[1]");
    REQUIRE_EQ(requirements[0].uses[1].arguments.find("mode")->asString(), "second");
    REQUIRE(!requirements[0].uses[0].declaration.has_value());
    REQUIRE(requirements[1].kind == eve::rpg::ImplementationKind::EffectStackPolicy);
    REQUIRE(requirements[2].kind == eve::rpg::ImplementationKind::AttributeOperation);
    REQUIRE_EQ(requirements[2].uses.size(), std::size_t(1));
    REQUIRE_EQ(requirements[2].uses[0].path, "modifiers[0].op");
    auto moved = std::move(content);
    REQUIRE(content.implementationRequirements().empty());
    REQUIRE_EQ(moved.implementationRequirements().size(), std::size_t(3));
}
TEST_CASE("RPG archive freezes actual implementation revisions and actor creation rejects changed code bindings") {
    Fixture fixture;
    fixture.commit();
    eve::rpg::SkillDefinition skill;
    skill.id             = "spawn.basic";
    skill.grantedEffects = {"identity.effect"};
    eve::rpg::SkillRegistry::registerSkill(skill);
    eve::rpg::EffectDefinition effect;
    effect.id          = "identity.effect";
    effect.stackPolicy = "identity.same-name";
    effect.modifiers.push_back({"attack", "identity.same-name", 2, 0});
    eve::rpg::EffectRegistry::registerEffect(effect);
    auto& operations = eve::rpg::AttributeSystem::customOps();
    using Status     = eve::rpg::StatusSystem;
    int  calls       = 0;
    auto operation   = [&](double a, double b) {
        ++calls;
        return a + b;
    };
    auto stack = [&](eve::rpg::RPGActor*, eve::rpg::StatusInstance&, const eve::rpg::EffectDefinition&,
                     const std::string&) {
        ++calls;
        return -1;
    };
    REQUIRE(!eve::rpg_spawn::RPGSpawnCapture::capture(*fixture.table, fixture.request.operation).ok());
    operations.registerOperation("identity.same-name", operation);
    Status::registerStackPolicy("identity.same-name", stack);
    REQUIRE(!eve::rpg_spawn::RPGSpawnCapture::capture(*fixture.table, fixture.request.operation).ok());
    REQUIRE(operations.registerVersionedOperation("identity.same-name", "operation:v1", operation).ok());
    REQUIRE(Status::registerVersionedStackPolicy("identity.same-name", "stack:v1", stack).ok());
    auto frozen =
        eve::rpg_spawn::RPGSpawnCapture::capture(*fixture.table, fixture.request.operation).expect("versioned freeze");
    REQUIRE_EQ(frozen.implementations().size(), std::size_t(2));
    REQUIRE_EQ(calls, 0);
    REQUIRE(operations.registerVersionedOperation("identity.same-name", "operation:v2", operation).ok());
    auto saved   = frozen.seal(eve::Revision(1), eve::SimulationTick(1), archiveTestHash).expect("frozen v4");
    auto archive = eve::rpg_spawn::RPGSpawnArchive::parse(saved, saved.instanceId, archiveTestHash).expect("archive");
    REQUIRE_EQ(archive.implementations()[1].revision, "operation:v1");
    auto state = eve::rpg_spawn::RPGSpawnRestoreState::prepare(archive).expect("pure admission ignores live mismatch");
    REQUIRE_EQ(state.actorCount(), std::size_t(2));
    REQUIRE(!state.validateImplementationBindings().ok());
    auto content = std::make_shared<const eve::rpg::SkillContent>(
        eve::rpg::SkillContent::prepareWithEffects(archive.skills(), archive.effects(), archive.implementations())
            .expect("saved content"));
    auto recipe    = eve::rpg_spawn::RPGUnitCatalogue::prepareCaptured(archive.units()[0], *content).expect("recipe");
    auto candidate = eve::rpg::PreparedMemberActor::prepareCaptured(*recipe.member, content).expect("candidate");
    candidate.restoreCheckpoint(archive.actors()[0].checkpointJson).expect("actor checkpoint");
    ecs::ScopedTable current(*fixture.table);
    REQUIRE(!candidate.create().ok());
    REQUIRE_EQ(fixture.count(), std::size_t(2));
    REQUIRE(operations.registerVersionedOperation("identity.same-name", "operation:v1", operation).ok());
    Status::unregisterStackPolicy("identity.same-name");
    REQUIRE(!candidate.create().ok());
    Status::registerStackPolicy("identity.same-name", stack);
    REQUIRE(!candidate.create().ok());
    REQUIRE(Status::registerVersionedStackPolicy("identity.same-name", "stack:v1", stack).ok());
    REQUIRE(state.validateImplementationBindings().ok());
    auto created = candidate.create().expect("same candidate retries with exact implementations");
    REQUIRE_EQ(fixture.count(), std::size_t(3));
    REQUIRE_EQ(calls, 0);
    ecs::DestroyEntity(ecs::try_get(created));
    operations.unregisterOperation("identity.same-name");
    Status::unregisterStackPolicy("identity.same-name");
    eve::rpg::SkillRegistry::clear();
    eve::rpg::EffectRegistry::clear();
    auto independent =
        eve::rpg_spawn::RPGSpawnRestoreState::prepare(archive).expect("no live bindings during admission");
    REQUIRE_EQ(independent.actorCount(), std::size_t(2));
    auto moved = std::move(archive);
    REQUIRE(archive.implementations().empty());
    REQUIRE_EQ(moved.implementations().size(), std::size_t(2));
}
TEST_CASE("RPG archive rejects invalid implementation identities and legacy custom closure cannot be invented") {
    Fixture fixture;
    fixture.commit();
    eve::rpg::SkillDefinition skill;
    skill.id             = "spawn.basic";
    skill.grantedEffects = {"identity.validation.effect"};
    eve::rpg::SkillRegistry::registerSkill(skill);
    eve::rpg::EffectDefinition effect;
    effect.id = "identity.validation.effect";
    effect.modifiers.push_back({"attack", "identity.validation.op", 1, 0});
    eve::rpg::EffectRegistry::registerEffect(effect);
    auto& operations = eve::rpg::AttributeSystem::customOps();
    REQUIRE(
        operations
            .registerVersionedOperation("identity.validation.op", "source:v1", [](double a, double b) { return a + b; })
            .ok());
    auto frozen = eve::rpg_spawn::RPGSpawnCapture::capture(*fixture.table, fixture.request.operation).expect("frozen");
    auto saved  = frozen.seal(eve::Revision(1), eve::SimulationTick(1), archiveTestHash).expect("v4");
    auto reseal = [&](eve::Value body, unsigned version = 4) {
        return eve::makeSnapshotEnvelope(saved.type, saved.schema, eve::SchemaVersion(version), saved.instanceId,
                                         saved.revision, saved.tick, std::move(body), archiveTestHash)
            .expect("test reseal");
    };
    for (const auto& field : {"kind", "name", "revision"}) {
        auto invalid = saved.payload;
        invalid.find("implementations")->at(0).set(field, "");
        REQUIRE(!eve::rpg_spawn::RPGSpawnArchive::parse(reseal(invalid), saved.instanceId, archiveTestHash).ok());
    }
    auto  duplicate  = saved.payload;
    auto& identities = *duplicate.find("implementations")->getIf<eve::Value::Array>();
    auto  first      = identities[0];
    identities.push_back(first);
    REQUIRE(!eve::rpg_spawn::RPGSpawnArchive::parse(reseal(duplicate), saved.instanceId, archiveTestHash).ok());
    auto absent = saved.payload;
    absent.set("implementations", eve::Value::array({}));
    auto parsed = eve::rpg_spawn::RPGSpawnArchive::parse(reseal(absent), saved.instanceId, archiveTestHash)
                      .expect("structure only");
    REQUIRE(!eve::rpg_spawn::RPGSpawnRestoreState::prepare(parsed).ok());
    auto unrelated = saved.payload;
    unrelated.find("implementations")->at(0).set("name", "unreferenced.op");
    auto foreign = eve::rpg_spawn::RPGSpawnArchive::parse(reseal(unrelated), saved.instanceId, archiveTestHash)
                       .expect("structural identity");
    REQUIRE(!eve::rpg_spawn::RPGSpawnRestoreState::prepare(foreign).ok());
    auto legacy = saved.payload;
    legacy.getIf<eve::Value::Object>()->erase("implementations");
    legacy.getIf<eve::Value::Object>()->erase("future_units");
    auto old = eve::rpg_spawn::RPGSpawnArchive::parse(reseal(legacy, 2), saved.instanceId, archiveTestHash)
                   .expect("v2 migration");
    REQUIRE(old.implementations().empty());
    REQUIRE(!eve::rpg_spawn::RPGSpawnRestoreState::prepare(old).ok());
    REQUIRE(operations.has("identity.validation.op"));
    operations.unregisterOperation("identity.validation.op");
    eve::rpg::EffectRegistry::remove(effect.id);
}
TEST_CASE("RPG capture rejects condition policies without actual call-site implementation identity") {
    Fixture fixture;
    fixture.commit();
    eve::rpg::SkillDefinition skill;
    skill.id            = "spawn.basic";
    skill.castCondition = eve::condition::Condition::policyCall("identity.unbound.policy");
    eve::rpg::SkillRegistry::registerSkill(skill);
    auto rejected = eve::rpg_spawn::RPGSpawnCapture::capture(*fixture.table, fixture.request.operation);
    REQUIRE(!rejected.ok());
    REQUIRE_EQ(fixture.count(), std::size_t(2));
    skill.castCondition = eve::condition::Condition::all({});
    eve::rpg::SkillRegistry::registerSkill(skill);
    auto retried =
        eve::rpg_spawn::RPGSpawnCapture::capture(*fixture.table, fixture.request.operation).expect("retry freeze");
    REQUIRE_EQ(retried.actors().size(), std::size_t(2));
    REQUIRE(retried.implementations().empty());
}
TEST_CASE(
    "RPG condition archive uses actual actor bindings and restored candidates retain exact executable ownership") {
    Fixture fixture;
    fixture.commit();
    eve::condition::ScriptConditionDeclaration declaration{
        "archive.actor.policy", {"state.zone"}, eve::condition::DeterminismLevel::TickDeterministic};
    eve::rpg::SkillDefinition skill;
    skill.id            = "spawn.basic";
    skill.castCondition = eve::condition::Condition::all(
        {eve::condition::Condition::policyCall("archive.actor.policy", eve::Value::object({{"scope", "cast"}}),
                                               declaration),
         eve::condition::authorityAllow("cast"), eve::condition::authorityAllow("inspect")});
    eve::rpg::SkillRegistry::registerSkill(skill);
    unsigned                                    calls = 0;
    std::vector<eve::rpg::SkillConditionPolicy> policies{
        {"archive.actor.policy", "source:policy:v1", declaration,
         [&](const eve::rpg::RPGActor*, const eve::Value& args) {
             ++calls;
             REQUIRE_EQ(args.find("scope")->asString(), "cast");
             return std::optional<eve::condition::ConditionResult>(eve::condition::ConditionResult::success());
         }},
        {"authority.allow",
         "source:authority:v1",
         {},
         eve::rpg::SkillConditionPolicy::Authority([](const eve::rpg::RPGActor*, std::string_view scope) {
             return std::optional<bool>(scope == "cast" || scope == "inspect");
         })}};
    auto correct     = eve::rpg::SkillConditionBindings::prepare(policies).expect("actual policy owner");
    auto setBindings = [&](std::shared_ptr<const eve::rpg::SkillConditionBindings> bindings) {
        auto view = ecs::View<eve::rpg::RPGActor>(*fixture.table);
        for (auto it = view.begin(); it != view.end(); ++it) it.entity()->skills()->conditionBindings = bindings;
    };
    setBindings(correct);
    auto frozen = eve::rpg_spawn::RPGSpawnCapture::capture(*fixture.table, fixture.request.operation)
                      .expect("actual condition capture");
    REQUIRE_EQ(calls, 0u);
    REQUIRE_EQ(frozen.implementations().size(), std::size_t(2));
    REQUIRE(frozen.implementations()[0].kind == eve::rpg::ImplementationKind::ConditionPolicy);
    auto saved =
        frozen.seal(eve::Revision(1), eve::SimulationTick(1), archiveTestHash).expect("condition identity in v3");
    auto archive =
        eve::rpg_spawn::RPGSpawnArchive::parse(saved, saved.instanceId, archiveTestHash).expect("independent archive");
    auto noOwner = eve::rpg_spawn::RPGSpawnRestoreState::prepare(archive).expect("pure admission without code");
    REQUIRE(!noOwner.validateImplementationBindings().ok());
    policies[0].revision = "source:policy:v2";
    auto changedCode     = eve::rpg::SkillConditionBindings::prepare(policies).expect("different code");
    auto mismatched      = eve::rpg_spawn::RPGSpawnRestoreState::prepare(archive, changedCode)
                          .expect("candidate before binding validation");
    REQUIRE(!mismatched.validateImplementationBindings().ok());
    policies[0].revision                 = "source:policy:v1";
    policies[0].declaration->determinism = eve::condition::DeterminismLevel::ExplicitlyNondeterministic;
    auto changedContract = eve::rpg::SkillConditionBindings::prepare(policies).expect("same revision, wrong contract");
    setBindings(changedContract);
    {
        auto view = ecs::View<eve::rpg::RPGActor>(*fixture.table);
        for (auto it = view.begin(); it != view.end(); ++it)
            REQUIRE(!eve::rpg::SkillSystem::canCast(it.entity(), skill.id));
    }
    REQUIRE(!eve::rpg_spawn::RPGSpawnCapture::capture(*fixture.table, fixture.request.operation).ok());
    auto badContract = eve::rpg_spawn::RPGSpawnRestoreState::prepare(archive, changedContract).expect("pure facts");
    REQUIRE(!badContract.validateImplementationBindings().ok());
    {
        auto admitted = eve::rpg_spawn::RPGSpawnRestoreState::prepare(archive, correct).expect("matching owner");
        REQUIRE(admitted.validateImplementationBindings().ok());
    }
    auto content = std::make_shared<const eve::rpg::SkillContent>(
        eve::rpg::SkillContent::prepareWithEffects(archive.skills(), archive.effects(), archive.implementations())
            .expect("content with saved policy identity"));
    REQUIRE_EQ(content->implementationRequirements()[1].uses.size(), std::size_t(2));
    REQUIRE_EQ(content->implementationRequirements()[1].uses[0].arguments.find("scope")->asString(), "cast");
    REQUIRE_EQ(content->implementationRequirements()[1].uses[1].arguments.find("scope")->asString(), "inspect");
    auto recipe = eve::rpg_spawn::RPGUnitCatalogue::prepareCaptured(archive.units()[0], *content).expect("recipe");
    auto candidate =
        eve::rpg::PreparedMemberActor::prepareCaptured(*recipe.member, content, correct).expect("bound candidate");
    candidate.restoreCheckpoint(archive.actors()[0].checkpointJson)
        .expect("retains actual owner through canonical restore");
    auto clone = candidate.clone().expect("independent state, shared code owner");
    std::weak_ptr<const eve::rpg::SkillConditionBindings> observed = correct;
    setBindings({});
    correct.reset();
    policies.clear();
    REQUIRE(!observed.expired());
    REQUIRE_EQ(calls, 0u);
    ecs::Table       restored;
    ecs::ScopedTable current(restored);
    auto             first  = candidate.create().expect("materialize exact code");
    auto             second = clone.create().expect("clone materializes exact code");
    auto*            actor  = dynamic_cast<eve::rpg::RPGActor*>(ecs::try_get(first));
    REQUIRE(actor != nullptr);
    REQUIRE(actor->skills()->conditionBindings == observed.lock());
    REQUIRE(eve::rpg::SkillSystem::canCast(actor, skill.id));
    REQUIRE_EQ(calls, 1u);
    ecs::DestroyEntity(ecs::try_get(first));
    ecs::DestroyEntity(ecs::try_get(second));
    REQUIRE(observed.expired());
    REQUIRE_EQ(archive.implementations()[0].revision, "source:policy:v1");
}
TEST_CASE("RPG capture checks every actor policy owner and refuses conflicting revisions for one saved name") {
    Fixture fixture;
    fixture.commit();
    eve::rpg::SkillDefinition skill;
    skill.id            = "spawn.basic";
    skill.castCondition = eve::condition::Condition::policyCall("archive.actor.conflict");
    eve::rpg::SkillRegistry::registerSkill(skill);
    std::vector<eve::rpg::SkillConditionPolicy> policies{
        {"archive.actor.conflict", "code:v1", {}, [](const eve::rpg::RPGActor*, const eve::Value&) {
             return std::optional<eve::condition::ConditionResult>(eve::condition::ConditionResult::success());
         }}};
    auto first                            = eve::rpg::SkillConditionBindings::prepare(policies).expect("first owner");
    policies[0].revision                  = "code:v2";
    auto                           second = eve::rpg::SkillConditionBindings::prepare(policies).expect("second owner");
    std::vector<ecs::EntityHandle> actors;
    {
        auto view = ecs::View<eve::rpg::RPGActor>(*fixture.table);
        for (auto it = view.begin(); it != view.end(); ++it) actors.push_back(ecs::handle_of(it.entity()));
    }
    ecs::ScopedTable current(*fixture.table);
    auto*            a = dynamic_cast<eve::rpg::RPGActor*>(ecs::try_get(actors[0]));
    auto*            b = dynamic_cast<eve::rpg::RPGActor*>(ecs::try_get(actors[1]));
    REQUIRE((a != nullptr && b != nullptr));
    a->skills()->conditionBindings = first;
    REQUIRE(!eve::rpg_spawn::RPGSpawnCapture::capture(*fixture.table, fixture.request.operation).ok());
    b->skills()->conditionBindings = second;
    REQUIRE(!eve::rpg_spawn::RPGSpawnCapture::capture(*fixture.table, fixture.request.operation).ok());
    b->skills()->conditionBindings = first;
    auto frozen                    = eve::rpg_spawn::RPGSpawnCapture::capture(*fixture.table, fixture.request.operation)
                      .expect("one consistent executable identity");
    REQUIRE_EQ(frozen.implementations().size(), std::size_t(1));
    REQUIRE_EQ(frozen.implementations()[0].revision, "code:v1");
    REQUIRE_EQ(fixture.count(), std::size_t(2));
}
TEST_CASE("RPG factory installs the actual condition owner before publication and rejects absent required policies") {
    unsigned                                    calls = 0;
    std::set<int>                               observedActors;
    std::vector<eve::rpg::SkillConditionPolicy> policies{
        {"factory.actual.policy", "factory:policy:v1", {}, [&](const eve::rpg::RPGActor* actor, const eve::Value&) {
             REQUIRE(actor != nullptr);
             observedActors.insert(static_cast<int>(actor->id));
             ++calls;
             return std::optional<eve::condition::ConditionResult>(eve::condition::ConditionResult::success());
         }}};
    auto bindings = eve::rpg::SkillConditionBindings::prepare(policies).expect("actual factory provider");
    eve::rpg::SkillDefinition skill;
    skill.id            = "spawn.basic";
    skill.castCondition = eve::condition::Condition::policyCall("factory.actual.policy");
    {
        Fixture fixture(bindings);
        eve::rpg::SkillRegistry::registerSkill(skill);
        fixture.commit();
        REQUIRE_EQ(calls, 0u);
        {
            auto view = ecs::View<eve::rpg::RPGActor>(*fixture.table);
            for (auto it = view.begin(); it != view.end(); ++it) {
                REQUIRE(it.entity()->skills()->conditionBindings == bindings);
                REQUIRE(eve::rpg::SkillSystem::canCast(it.entity(), skill.id));
            }
        }
        REQUIRE_EQ(calls, 2u);
        REQUIRE_EQ(observedActors.size(), std::size_t(2));
        auto captured = eve::rpg_spawn::RPGSpawnCapture::capture(*fixture.table, fixture.request.operation)
                            .expect("factory-created actor bindings");
        REQUIRE_EQ(captured.implementations().size(), std::size_t(1));
        REQUIRE_EQ(captured.implementations()[0].revision, "factory:policy:v1");
        REQUIRE_EQ(calls, 2u);
    }
    for (bool explicitEmpty : {false, true}) {
        auto    supplied = explicitEmpty ? eve::rpg::SkillConditionBindings::prepare({}).expect("empty owner")
                                         : std::shared_ptr<const eve::rpg::SkillConditionBindings>{};
        Fixture fixture(std::move(supplied));
        eve::rpg::SkillRegistry::registerSkill(skill);
        auto prepared = fixture.factory->prepare(fixture.request, fixture.positions);
        REQUIRE(!prepared.ok());
        REQUIRE_EQ(fixture.count(), std::size_t(0));
        REQUIRE_EQ(calls, 2u);
    }
    {
        Fixture fixture;
        auto outcome = fixture.factory->prepare(fixture.request, fixture.positions).expect("prepare before hot reload");
        auto& token  = std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(outcome);
        eve::rpg::SkillRegistry::registerSkill(skill);
        REQUIRE(!token->commit().ok());
        REQUIRE_EQ(fixture.count(), std::size_t(0));
        auto restoredSkill          = skill;
        restoredSkill.castCondition = eve::condition::Condition::all({});
        eve::rpg::SkillRegistry::registerSkill(restoredSkill);
        REQUIRE(token->commit().ok());
        REQUIRE_EQ(fixture.count(), std::size_t(2));
        REQUIRE_EQ(calls, 2u);
    }
}
namespace {
struct JointInputs {
    eve::SnapshotEnvelope                        core;
    eve::SnapshotEnvelope                        domain;
    eve::PersistentId                            epoch;
    std::vector<eve::spawn::SavedLifecycleInput> pending;
};
JointInputs jointInputs(unsigned mode = 0) {
    Fixture fixture;
    fixture.sourceWorld->advance({eve::SimulationTick(1), eve::Duration(0)}).expect("real core/domain spawn");
    REQUIRE_EQ(fixture.count(), std::size_t(2));
    std::vector<eve::spawn::LifecycleInput> pending;
    if (mode) {
        ecs::ScopedTable    current(*fixture.table);
        eve::rpg::RPGActor* actor = nullptr;
        {
            auto view = ecs::View<eve::rpg::RPGActor>(*fixture.table);
            actor     = view.begin().entity();
        }
        eve::rpg::VitalsSystem::takeDamage(actor, "hp", 100);
        std::vector<eve::rpg::VitalsEvent> events;
        eve::rpg::VitalsSystem::pollEvents(events);
        pending = fixture.factory->collectLifecycle(events).expect("retained transition");
        REQUIRE_EQ(pending.size(), std::size_t(1));
        if (mode == 2) ecs::DestroyEntity(actor);
        if (mode == 3) {
            fixture.sourceWorld->advance({eve::SimulationTick(2), eve::Duration(0)}, pending).expect("consume death");
            pending.clear();
        }
    }
    auto       epoch      = fixture.request.operation.child("joint-save");
    const auto tick       = eve::SimulationTick(mode == 3 ? 2 : 1);
    auto       frozenCore = fixture.sourceWorld->capture(epoch, eve::Revision(7)).expect("core freeze");
    auto       core       = frozenCore.seal(archiveTestHash).expect("core seal");
    auto       domain     = fixture.factory->capture(epoch, frozenCore.unitReferences())
                      .expect("domain freeze")
                      .seal(eve::Revision(7), tick, archiveTestHash)
                      .expect("domain seal");
    JointInputs result{std::move(core), std::move(domain), epoch, {}};
    for (const auto& input : pending) result.pending.push_back(eve::spawn::savedLifecycleInput(input));
    return result;
}
eve::SnapshotEnvelope resealJoint(eve::SnapshotEnvelope envelope) {
    return eve::makeSnapshotEnvelope(envelope.type, envelope.schema, envelope.schemaVersion, envelope.instanceId,
                                     envelope.revision, envelope.tick, std::move(envelope.payload), archiveTestHash)
        .expect("joint test reseal");
}
}  // namespace
TEST_CASE("RPG joint restore admits same-boundary real participants without live registries or source authority") {
    auto input = jointInputs();
    eve::rpg::SkillRegistry::clear();
    eve::rpg::EffectRegistry::clear();
    const auto queries   = eve::cap::listenerCount<eve::IStateQuery>();
    const auto mutations = eve::cap::listenerCount<eve::IStateMutation>();
    auto joined = eve::rpg_spawn::RPGSpawnJointRestore::prepare(input.core, input.domain, input.epoch, archiveTestHash)
                      .expect("joined admission");
    REQUIRE_EQ(joined.core().membership().size(), std::size_t(2));
    REQUIRE_EQ(joined.domain().actorCount(), std::size_t(2));
    REQUIRE(joined.core().revision() == joined.domain().revision());
    REQUIRE(joined.core().tick() == joined.domain().tick());
    REQUIRE(joined.pendingLifecycle().empty());
    REQUIRE_EQ(eve::rpg::SkillRegistry::count(), 0);
    REQUIRE_EQ(eve::cap::listenerCount<eve::IStateQuery>(), queries);
    REQUIRE_EQ(eve::cap::listenerCount<eve::IStateMutation>(), mutations);
    static_assert(!std::is_convertible_v<eve::spawn::SavedSpawnerKey, eve::spawn::SpawnerHandle>);
    auto moved = std::move(joined);
    REQUIRE(joined.saveEpoch().isNil());
    REQUIRE(joined.pendingLifecycle().empty());
    REQUIRE(moved.saveEpoch() == input.epoch);
}
TEST_CASE("RPG joint restore rejects independently valid but mismatched participant boundaries and origins") {
    auto input  = jointInputs();
    auto reject = [&](eve::SnapshotEnvelope core, eve::SnapshotEnvelope domain) {
        REQUIRE(!eve::rpg_spawn::RPGSpawnJointRestore::prepare(core, domain, input.epoch, archiveTestHash).ok());
    };
    auto domain     = input.domain;
    domain.revision = eve::Revision(8);
    reject(input.core, resealJoint(domain));
    domain      = input.domain;
    domain.tick = eve::SimulationTick(2);
    reject(input.core, resealJoint(domain));
    domain = input.domain;
    domain.payload.find("actors")->at(0).find("origin")->set("generation", "2");
    reject(input.core, resealJoint(domain));
    domain       = input.domain;
    auto& actors = *domain.payload.find("actors")->getIf<eve::Value::Array>();
    actors.erase(actors.begin());
    reject(input.core, resealJoint(domain));
    domain     = input.domain;
    auto extra = domain.payload.find("actors")->at(0);
    extra.set("subject", input.epoch.child("foreign-actor").format());
    domain.payload.find("actors")->getIf<eve::Value::Array>()->push_back(extra);
    reject(input.core, resealJoint(domain));
    domain = input.domain;
    auto state =
        eve::Value::fromJson(domain.payload.find("actors")->at(0).find("checkpoint")->asString()).expect("actor JSON");
    state.find("vitals")->set("hp", 0);
    domain.payload.find("actors")->at(0).set("checkpoint", state.toJson().expect("dead source-owned actor"));
    reject(input.core, resealJoint(domain));
    domain             = input.domain;
    auto otherRevision = domain.payload.find("units")->at(0);
    otherRevision.set("generation", "2");
    domain.payload.find("units")->getIf<eve::Value::Array>()->push_back(otherRevision);
    REQUIRE(eve::rpg_spawn::RPGSpawnJointRestore::prepare(input.core, resealJoint(domain), input.epoch, archiveTestHash)
                .ok());
    domain.payload.set("future_units", eve::Value::array({}));
    reject(input.core, resealJoint(domain));
}
TEST_CASE("RPG joint restore explains retained deaths missing actors and detached corpses without inventing events") {
    for (unsigned mode : {1u, 2u, 3u}) {
        auto input  = jointInputs(mode);
        auto joined = eve::rpg_spawn::RPGSpawnJointRestore::prepare(input.core, input.domain, input.epoch,
                                                                    archiveTestHash, input.pending)
                          .expect("retained or consumed death");
        REQUIRE_EQ(joined.domain().actorCount(), std::size_t(mode == 2 ? 1 : 2));
        REQUIRE_EQ(joined.core().trackedCount(), std::size_t(mode == 3 ? 1 : 2));
        REQUIRE_EQ(joined.pendingLifecycle().size(), std::size_t(mode == 3 ? 0 : 1));
        if (mode == 3) continue;
        REQUIRE(!eve::rpg_spawn::RPGSpawnJointRestore::prepare(input.core, input.domain, input.epoch, archiveTestHash)
                     .ok());
        auto invalid        = input.pending;
        invalid[0].sequence = 0;
        REQUIRE(!eve::rpg_spawn::RPGSpawnJointRestore::prepare(input.core, input.domain, input.epoch, archiveTestHash,
                                                               invalid)
                     .ok());
        invalid = input.pending;
        ++invalid[0].source.generation;
        REQUIRE(!eve::rpg_spawn::RPGSpawnJointRestore::prepare(input.core, input.domain, input.epoch, archiveTestHash,
                                                               invalid)
                     .ok());
        invalid           = input.pending;
        invalid[0].reason = static_cast<eve::spawn::RemovalReason>(999);
        REQUIRE(!eve::rpg_spawn::RPGSpawnJointRestore::prepare(input.core, input.domain, input.epoch, archiveTestHash,
                                                               invalid)
                     .ok());
        invalid = input.pending;
        invalid.push_back(invalid[0]);
        ++invalid[1].sequence;
        REQUIRE(!eve::rpg_spawn::RPGSpawnJointRestore::prepare(input.core, input.domain, input.epoch, archiveTestHash,
                                                               invalid)
                     .ok());
        auto consumed = input.core;
        consumed.payload.set("lifecycle_sequence", "1");
        REQUIRE(!eve::rpg_spawn::RPGSpawnJointRestore::prepare(resealJoint(consumed), input.domain, input.epoch,
                                                               archiveTestHash, input.pending)
                     .ok());
    }
}
TEST_CASE("RPG joint admission copies both participants and pending facts before the first external digest call") {
    auto input   = jointInputs(1);
    bool changed = false;
    auto hash    = [&](std::string_view text) {
        if (!changed) {
            changed = true;
            input.core.payload.set("lifecycle_sequence", "999");
            input.domain.payload.set("unknown", true);
            input.pending[0].sequence = 0;
        }
        return archiveTestHash(text);
    };
    auto joined =
        eve::rpg_spawn::RPGSpawnJointRestore::prepare(input.core, input.domain, input.epoch, hash, input.pending)
            .expect("frozen call inputs");
    REQUIRE(changed);
    REQUIRE_EQ(joined.pendingLifecycle()[0].sequence, std::uint64_t(1));
    REQUIRE_EQ(joined.core().acceptedLifecycleSequence(), std::uint64_t(0));
    REQUIRE_EQ(joined.domain().actorCount(), std::size_t(2));
}
TEST_CASE("RPG archive binds future creation to the save-time revision while preserving older live actors") {
    eve::SnapshotEnvelope core, domain;
    eve::PersistentId     epoch;
    {
        Fixture fixture;
        fixture.sourceWorld->advance({eve::SimulationTick(1), eve::Duration(0)}).expect("old actors");
        auto source = Fixture::asset();
        source.set("attack", 99);
        fixture.units->publish(source).expect("new future recipe");
        epoch           = fixture.request.operation.child("future-binding");
        auto frozenCore = fixture.sourceWorld->capture(epoch, eve::Revision(1)).expect("core freeze");
        REQUIRE_EQ(frozenCore.unitReferences().size(), std::size_t(1));
        auto frozen = fixture.factory->capture(epoch, frozenCore.unitReferences()).expect("both generations frozen");
        REQUIRE_EQ(frozen.futureUnits().size(), std::size_t(1));
        REQUIRE(frozen.futureUnits()[0].generation != frozen.actors()[0].recipe->definition.generation);
        source.set("attack", 123);
        fixture.units->publish(source).expect("later source replacement");
        eve::rpg::SkillRegistry::clear();
        core   = frozenCore.seal(archiveTestHash).expect("core envelope");
        domain = frozen.seal(eve::Revision(1), eve::SimulationTick(1), archiveTestHash).expect("v4");
    }
    auto archive = eve::rpg_spawn::RPGSpawnArchive::parse(domain, epoch, archiveTestHash).expect("v4 structure");
    REQUIRE_EQ(archive.units().size(), std::size_t(2));
    REQUIRE_EQ(archive.futureUnitRevisions().size(), std::size_t(1));
    auto joined = eve::rpg_spawn::RPGSpawnJointRestore::prepare(core, domain, epoch, archiveTestHash)
                      .expect("explicit future mapping removes ambiguity");
    auto recipe = joined.domain()
                      .futureUnit(eve::DefinitionRef::parse("rpg.unit:wolf").expect("reference"))
                      .expect("original future recipe");
    REQUIRE_EQ(recipe->definition.generation.value(), std::uint64_t(2));
    REQUIRE_EQ(recipe->member->attack, 99.0);
    REQUIRE_EQ(joined.domain().actorCount(), std::size_t(2));
    REQUIRE_EQ(joined.domain().futureUnitCount(), std::size_t(1));
    auto reference = eve::DefinitionRef::parse("rpg.unit:wolf").expect("future reference");
    auto candidate = joined.domain().prepareFutureUnit(reference).expect("prepare without live skill registry");
    REQUIRE_EQ(candidate.currentVital("hp").expect("archived future defaults"), 20.0);
    REQUIRE(!candidate.create().ok());
    REQUIRE_EQ(candidate.currentVital("hp").expect("failed creation retains candidate"), 20.0);
    auto retry = joined.domain().prepareFutureUnit(reference).expect("independent future candidate");
    REQUIRE_EQ(retry.currentVital("hp").expect("unchanged future defaults"), 20.0);
    REQUIRE(!joined.domain()
                 .prepareFutureUnit(eve::DefinitionRef::parse("rpg.unit:missing").expect("missing reference"))
                 .ok());
    auto old = eve::Value::fromJson(archive.actors()[0].checkpointJson).expect("original actor facts");
    auto attributes =
        eve::attributes::AttributeSet::prepareState(*old.find("attributes")).expect("original canonical attributes");
    REQUIRE_EQ(attributes.view().getBase("attack"), 7.0);
}
TEST_CASE("RPG future content capture includes never-spawned skills and effects for an empty world") {
    unsigned                                    calls = 0;
    std::vector<eve::rpg::SkillConditionPolicy> policies{
        {"future.only.policy", "future:policy:v1", {}, [&](const eve::rpg::RPGActor*, const eve::Value&) {
             ++calls;
             return std::optional<eve::condition::ConditionResult>(eve::condition::ConditionResult::success());
         }}};
    auto    conditions = eve::rpg::SkillConditionBindings::prepare(policies).expect("actual future factory owner");
    Fixture fixture(conditions);
    eve::rpg::EffectDefinition effect;
    effect.id             = "future.only.effect";
    effect.durationPolicy = "duration";
    effect.duration       = 2;
    eve::rpg::EffectRegistry::registerEffect(effect);
    eve::rpg::SkillDefinition skill;
    skill.id             = "future.only.skill";
    skill.grantedEffects = {effect.id};
    skill.castCondition  = eve::condition::Condition::policyCall("future.only.policy");
    eve::rpg::SkillRegistry::registerSkill(skill);
    auto source = Fixture::asset();
    source.set("skill", skill.id);
    fixture.units->publish(source).expect("future-only member");
    auto epoch      = fixture.request.operation.child("empty-save");
    auto frozenCore = fixture.sourceWorld->capture(epoch, eve::Revision(1)).expect("empty core");
    auto frozen     = fixture.factory->capture(epoch, frozenCore.unitReferences()).expect("future closure");
    REQUIRE(frozen.actors().empty());
    REQUIRE_EQ(frozen.skills().size(), std::size_t(1));
    REQUIRE_EQ(frozen.skills()[0].id, skill.id);
    REQUIRE_EQ(frozen.effects().size(), std::size_t(1));
    eve::rpg::SkillRegistry::clear();
    eve::rpg::EffectRegistry::clear();
    auto core   = frozenCore.seal(archiveTestHash).expect("core");
    auto domain = frozen.seal(eve::Revision(1), eve::SimulationTick(0), archiveTestHash).expect("domain");
    auto joined = eve::rpg_spawn::RPGSpawnJointRestore::prepare(core, domain, epoch, archiveTestHash, {}, conditions)
                      .expect("empty world with complete future data");
    REQUIRE_EQ(joined.core().trackedCount(), std::size_t(0));
    REQUIRE_EQ(joined.domain().actorCount(), std::size_t(0));
    REQUIRE_EQ(joined.domain().futureUnitCount(), std::size_t(1));
    REQUIRE(joined.domain().validateImplementationBindings().ok());
    REQUIRE_EQ(calls, 0u);
}
TEST_CASE("RPG archive v4 rejects malformed future bindings and legacy worlds never infer future generations") {
    auto input  = jointInputs();
    auto reject = [&](eve::Value payload) {
        auto envelope    = input.domain;
        envelope.payload = std::move(payload);
        REQUIRE(!eve::rpg_spawn::RPGSpawnArchive::parse(resealJoint(envelope), input.epoch, archiveTestHash).ok());
    };
    auto invalid = input.domain.payload;
    invalid.set("future_units", eve::Value::array({"999"}));
    reject(invalid);
    invalid.set("future_units", eve::Value::array({"00"}));
    reject(invalid);
    invalid.set("future_units", eve::Value::array({0}));
    reject(invalid);
    invalid.set("future_units", eve::Value::array({"0", "0"}));
    reject(invalid);
    auto old          = input.domain;
    old.schemaVersion = eve::SchemaVersion(3);
    old.payload.getIf<eve::Value::Object>()->erase("future_units");
    old          = resealJoint(old);
    auto archive = eve::rpg_spawn::RPGSpawnArchive::parse(old, input.epoch, archiveTestHash).expect("v3 migration");
    auto facts   = eve::rpg_spawn::RPGSpawnRestoreState::prepare(archive).expect("legacy actor-only admission");
    REQUIRE_EQ(facts.actorCount(), std::size_t(2));
    REQUIRE_EQ(facts.futureUnitCount(), std::size_t(0));
    REQUIRE(!eve::rpg_spawn::RPGSpawnJointRestore::prepare(input.core, old, input.epoch, archiveTestHash).ok());
}
}  // namespace test_rpg_spawn

namespace test_rpg_spawn {
TEST_CASE(
    "Restored RPG factory creates exact future units and retries content rejection without live catalogue fallback") {
    Fixture fixture;
    auto    unit = Fixture::asset();
    unit.set("attack", 99);
    fixture.units->publish(unit).expect("save-time future unit");
    auto epoch    = fixture.request.operation.child("restored-factory");
    auto core     = fixture.sourceWorld->capture(epoch, eve::Revision(1)).expect("core boundary");
    auto frozen   = fixture.factory->capture(epoch, core.unitReferences()).expect("future content");
    auto envelope = frozen.seal(eve::Revision(1), eve::SimulationTick(0), archiveTestHash).expect("domain archive");
    auto archive  = eve::rpg_spawn::RPGSpawnArchive::parse(envelope, epoch, archiveTestHash).expect("parse");
    auto admitted = std::make_shared<const eve::rpg_spawn::RPGSpawnRestoreState>(
        eve::rpg_spawn::RPGSpawnRestoreState::prepare(archive).expect("detached content"));
    auto table   = std::make_shared<ecs::Table>();
    auto factory = eve::rpg_spawn::RPGSpawnFactory::prepareRestored(table, admitted).expect("archive-backed factory");
    admitted.reset();
    unit.set("attack", 123);
    fixture.units->publish(unit).expect("unrelated newer live unit");
    auto       token            = factory->prepare(fixture.request, fixture.positions).expect("archived future recipe");
    const auto registrySnapshot = eve::rpg::SkillRegistry::definitionRegistry().snapshotJson();
    const auto originalSkill    = *eve::rpg::SkillRegistry::find("spawn.basic");
    eve::rpg::SkillDefinition changed;
    changed.id       = "spawn.basic";
    changed.cooldown = 99;
    eve::rpg::SkillRegistry::registerSkill(changed);
    auto& prepared = std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(token);
    REQUIRE(!prepared->commit().ok());
    REQUIRE(!factory->inspect(fixture.request.units[0].subject).ok());
    REQUIRE(!factory->capture(epoch.child("bad-resave"), core.unitReferences()).ok());
    {
        auto view = ecs::View<eve::rpg::RPGActor>(*table);
        REQUIRE(view.begin() == view.end());
    }
    eve::rpg::SkillRegistry::registerSkill(originalSkill);
    eve::rpg::SkillRegistry::definitionRegistry()
        .restoreJson(registrySnapshot)
        .expect("exact runtime content restored");
    prepared->commit().expect("same token retries");
    prepared->commit().expect("idempotent replay");
    for (const auto& member : fixture.request.units) {
        auto actor = factory->inspect(member.subject).expect("actual archived member");
        REQUIRE_EQ(actor.attack, 99.0);
        REQUIRE_EQ(actor.definition.generation.value(), std::uint64_t(2));
    }
    auto again =
        factory->capture(epoch.child("resave"), core.unitReferences()).expect("resave actual restored factory");
    REQUIRE_EQ(again.futureUnits()[0].generation.value(), std::uint64_t(2));
    auto missing                = fixture.request;
    missing.units[0].definition = eve::DefinitionRef::parse("rpg.unit:missing").expect("unknown unit");
    unit.set("id", "missing");
    fixture.units->publish(unit).expect("live-only unit must not satisfy archived future binding");
    // A new factory has no operation receipt; rejection therefore checks its explicit future map.
    auto otherState = std::make_shared<const eve::rpg_spawn::RPGSpawnRestoreState>(
        eve::rpg_spawn::RPGSpawnRestoreState::prepare(archive).expect("independent state"));
    auto other = eve::rpg_spawn::RPGSpawnFactory::prepareRestored(std::make_shared<ecs::Table>(), otherState)
                     .expect("weak table factory");
    REQUIRE(!other->prepare(fixture.request, fixture.positions).ok());
    auto missingFactory =
        eve::rpg_spawn::RPGSpawnFactory::prepareRestored(table, otherState).expect("missing binding probe");
    REQUIRE(!missingFactory->prepare(missing, fixture.positions).ok());
    REQUIRE(!eve::rpg_spawn::RPGSpawnFactory::prepareRestored(table, {}).ok());
}
}  // namespace test_rpg_spawn

namespace test_rpg_spawn {
TEST_CASE("RPG Spawn bundle covers retained lifecycle facts and outlives all live owners") {
    for (unsigned mode : {0u, 1u, 2u, 3u}) {
        auto input  = jointInputs(mode);
        auto bundle = eve::rpg_spawn::RPGSpawnJointRestore::sealBundle(input.core, input.domain, input.epoch,
                                                                       input.pending, archiveTestHash)
                          .expect("sealed joined artifact");
        auto json            = eve::serializeSnapshotEnvelope(bundle).expect("durable bytes");
        input.core.payload   = eve::Value::object({});
        input.domain.payload = eve::Value::object({});
        input.pending.clear();
        eve::rpg::SkillRegistry::clear();
        eve::rpg::EffectRegistry::clear();
        auto envelope = eve::parseSnapshotEnvelope(json, archiveTestHash).expect("wire envelope");
        auto restored = eve::rpg_spawn::RPGSpawnJointRestore::prepareBundle(envelope, input.epoch, archiveTestHash)
                            .expect("no live content or external pending input");
        REQUIRE_EQ(restored.pendingLifecycle().size(), std::size_t(mode == 1 || mode == 2 ? 1 : 0));
        REQUIRE_EQ(restored.core().trackedCount(), std::size_t(mode == 3 ? 1 : 2));
        REQUIRE_EQ(envelope.schemaVersion.value(), std::uint32_t(1));
        if (!restored.pendingLifecycle().empty()) {
            REQUIRE(restored.pendingLifecycle()[0].reason == eve::spawn::RemovalReason::Death);
            REQUIRE_EQ(restored.pendingLifecycle()[0].sequence, std::uint64_t(1));
        }
    }
}
TEST_CASE("RPG Spawn bundle rejects corrupt pending payloads unknown fields and mismatched participant boundaries") {
    auto input    = jointInputs(2);
    auto original = eve::rpg_spawn::RPGSpawnJointRestore::sealBundle(input.core, input.domain, input.epoch,
                                                                     input.pending, archiveTestHash)
                        .expect("sealed retained deletion");
    auto tampered = original;
    tampered.payload.find("pending")->getIf<eve::Value::Array>()->clear();
    REQUIRE(!eve::rpg_spawn::RPGSpawnJointRestore::prepareBundle(tampered, input.epoch, archiveTestHash).ok());
    REQUIRE(
        !eve::rpg_spawn::RPGSpawnJointRestore::prepareBundle(resealJoint(tampered), input.epoch, archiveTestHash).ok());
    for (unsigned mode = 0; mode < 9; ++mode) {
        auto  invalid = original;
        auto& row     = invalid.payload.find("pending")->getIf<eve::Value::Array>()->at(0);
        switch (mode) {
            case 0: invalid.payload.set("unknown", true); break;
            case 1: row.set("unknown", true); break;
            case 2: row.set("reason", "revived"); break;
            case 3: row.set("sequence", "01"); break;
            case 4: row.set("sequence", 1); break;
            case 5: row.find("source")->set("slot", "4294967296"); break;
            case 6: row.find("source")->set("unknown", true); break;
            case 7: invalid.schemaVersion = eve::SchemaVersion(2); break;
            case 8: invalid.tick = eve::SimulationTick(99); break;
        }
        REQUIRE(!eve::rpg_spawn::RPGSpawnJointRestore::prepareBundle(resealJoint(invalid), input.epoch, archiveTestHash)
                     .ok());
    }
    REQUIRE(!eve::rpg_spawn::RPGSpawnJointRestore::prepareBundle(original, input.epoch.child("other"), archiveTestHash)
                 .ok());
}
TEST_CASE("RPG Spawn bundle snapshots every input before digest callbacks and retains retry after hash failure") {
    auto input        = jointInputs(1);
    auto original     = input;
    bool changed      = false;
    auto mutatingHash = [&](std::string_view bytes) {
        if (!changed) {
            changed              = true;
            input.core.payload   = eve::Value::object({});
            input.domain.payload = eve::Value::object({});
            input.pending.clear();
        }
        return archiveTestHash(bytes);
    };
    auto bundle = eve::rpg_spawn::RPGSpawnJointRestore::sealBundle(input.core, input.domain, input.epoch, input.pending,
                                                                   mutatingHash)
                      .expect("all writer inputs copied before callback");
    REQUIRE(changed);
    bool mutatedBundle = false;
    auto readingHash   = [&](std::string_view bytes) {
        if (!mutatedBundle) {
            mutatedBundle  = true;
            bundle.payload = eve::Value::object({});
        }
        return archiveTestHash(bytes);
    };
    auto state = eve::rpg_spawn::RPGSpawnJointRestore::prepareBundle(bundle, original.epoch, readingHash)
                     .expect("whole bundle copied before callback");
    REQUIRE_EQ(state.pendingLifecycle().size(), std::size_t(1));
    auto failHash = [](std::string_view) {
        return eve::Result<eve::ContentId>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected digest failure"));
    };
    REQUIRE(!eve::rpg_spawn::RPGSpawnJointRestore::sealBundle(original.core, original.domain, original.epoch,
                                                              original.pending, failHash)
                 .ok());
    REQUIRE(eve::rpg_spawn::RPGSpawnJointRestore::sealBundle(original.core, original.domain, original.epoch,
                                                             original.pending, archiveTestHash)
                .ok());
}
}  // namespace test_rpg_spawn

namespace test_rpg_spawn {
TEST_CASE("Restored core and archived RPG factory drive real creation death handoff and replacement") {
    Fixture fixture;
    auto    unit = Fixture::asset();
    unit.set("attack", 99);
    fixture.units->publish(unit).expect("save-time future recipe");
    const auto epoch          = fixture.request.operation.child("core-factory-composition");
    auto       frozen         = fixture.sourceWorld->capture(epoch, eve::Revision(1)).expect("empty saved core");
    auto       core           = frozen.seal(archiveTestHash).expect("core envelope");
    auto       domainEnvelope = fixture.factory->capture(epoch, frozen.unitReferences())
                              .expect("future archive")
                              .seal(eve::Revision(1), eve::SimulationTick(0), archiveTestHash)
                              .expect("domain envelope");
    auto archive = eve::rpg_spawn::RPGSpawnArchive::parse(domainEnvelope, epoch, archiveTestHash).expect("archive");
    auto domain  = std::make_shared<const eve::rpg_spawn::RPGSpawnRestoreState>(
        eve::rpg_spawn::RPGSpawnRestoreState::prepare(archive).expect("admit exact domain"));
    struct Units final : eve::spawn::IUnitDefinitionResolver {
        const eve::rpg_spawn::RPGSpawnRestoreState& domain;
        explicit Units(const eve::rpg_spawn::RPGSpawnRestoreState& value) : domain(value) {}
        eve::Result<void> validate(const eve::DefinitionRef& reference) const override {
            auto recipe = domain.futureUnit(reference);
            if (!recipe) return eve::Result<void>::failure(recipe.status());
            return eve::Result<void>::success();
        }
    } units(*domain);
    auto admitted = eve::spawn::SpawnRestoreState::decode(core, epoch, archiveTestHash, units).expect("core admit");
    const auto                                       saved = admitted.sources()[0];
    auto                                             table = std::make_shared<ecs::Table>();
    std::shared_ptr<eve::rpg_spawn::RPGSpawnFactory> factory =
        eve::rpg_spawn::RPGSpawnFactory::prepareRestored(table, domain).expect("actual archive-backed factory");
    auto providers =
        std::make_shared<eve::spawn::SpawnProviders>(eve::spawn::SpawnProviders{std::make_shared<Space>(), factory});
    auto restored = eve::spawn::SpawnWorld::restore(admitted, providers).expect("bind reconstructed sources");
    auto source   = restored.resolveSavedSource(saved).expect("new lease").source();
    REQUIRE(source.incarnation != fixture.request.source.incarnation);
    unit.set("attack", 123);
    fixture.units->publish(unit).expect("new live recipe cannot replace saved content");
    auto report = restored.advance({eve::SimulationTick(1), eve::Duration(0)}).expect("actual restored creation");
    REQUIRE_EQ(report.units, std::uint32_t(2));
    auto snapshot = restored.inspect(source).expect("new membership");
    REQUIRE_EQ(snapshot.alive, std::uint32_t(2));
    for (const auto& subject : snapshot.subjects) {
        auto actor = factory->inspect(subject).expect("real actor");
        REQUIRE_EQ(actor.attack, 99.0);
        REQUIRE(actor.origin == source);
    }
    REQUIRE(!factory->prepare(fixture.request, fixture.positions).ok());
    REQUIRE_EQ(fixture.sourceWorld->inspect(fixture.request.source).expect("untouched old core").alive,
               std::uint32_t(0));
    {
        ecs::ScopedTable current(*table);
        auto             view = ecs::View<eve::rpg::RPGActor>(*table);
        eve::rpg::VitalsSystem::takeDamage(view.begin().entity(), "hp", 100);
    }
    std::vector<eve::rpg::VitalsEvent> events;
    eve::rpg::VitalsSystem::pollEvents(events);
    auto lifecycle = factory->collectLifecycle(events).expect("actual new-incarnation death handoff");
    REQUIRE_EQ(lifecycle.size(), std::size_t(1));
    REQUIRE(lifecycle[0].source == source);
    restored.advance({eve::SimulationTick(2), eve::Duration(0)}, lifecycle).expect("consume restored death");
    auto replacement = restored.advance({eve::SimulationTick(3), eve::Duration(1000000000)})
                           .expect("saved population schedule replacement");
    REQUIRE_EQ(replacement.units, std::uint32_t(1));
    REQUIRE_EQ(restored.inspect(source).expect("population restored").alive, std::uint32_t(2));
}
}  // namespace test_rpg_spawn

namespace test_rpg_spawn {
namespace {
struct MaterializedFixture {
    std::unique_ptr<eve::rpg_spawn::RPGSpawnJointRestore>       admitted;
    std::shared_ptr<const eve::rpg_spawn::RPGSpawnRestoreState> domain;
    std::shared_ptr<ecs::Table>                                 table = std::make_shared<ecs::Table>();
    std::shared_ptr<eve::rpg_spawn::RPGSpawnFactory>            factory;
    std::shared_ptr<eve::spawn::SpawnProviders>                 providers;
    std::unique_ptr<eve::spawn::SpawnWorld>                     core;
    explicit MaterializedFixture(const JointInputs& input) {
        admitted = std::make_unique<eve::rpg_spawn::RPGSpawnJointRestore>(
            eve::rpg_spawn::RPGSpawnJointRestore::prepare(input.core, input.domain, input.epoch, archiveTestHash,
                                                          input.pending)
                .expect("checked joint candidate"));
        domain    = admitted->domainOwner();
        factory   = eve::rpg_spawn::RPGSpawnFactory::prepareRestored(table, domain).expect("domain factory");
        providers = std::make_shared<eve::spawn::SpawnProviders>(
            eve::spawn::SpawnProviders{std::make_shared<Space>(), factory});
        struct Units final : eve::spawn::IUnitDefinitionResolver {
            const eve::rpg_spawn::RPGSpawnRestoreState& state;
            explicit Units(const eve::rpg_spawn::RPGSpawnRestoreState& value) : state(value) {}
            eve::Result<void> validate(const eve::DefinitionRef& reference) const override {
                auto recipe = state.futureUnit(reference);
                if (!recipe) return eve::Result<void>::failure(recipe.status());
                return eve::Result<void>::success();
            }
        } units(*domain);
        auto admitted =
            eve::spawn::SpawnRestoreState::decode(input.core, input.epoch, archiveTestHash, units).expect("core");
        core = std::make_unique<eve::spawn::SpawnWorld>(
            eve::spawn::SpawnWorld::restore(admitted, providers).expect("core binding"));
    }
    std::size_t count() const {
        std::size_t count = 0;
        auto        view  = ecs::View<eve::rpg::RPGActor>(*table);
        for (auto it = view.begin(); it != view.end(); ++it) ++count;
        return count;
    }
};
}  // namespace
TEST_CASE(
    "Saved RPG actors materialize exact checkpoints and reject retired operations including previously prepared "
    "tokens") {
    auto  input           = jointInputs();
    auto& encoded         = input.domain.payload.find("actors")->getIf<eve::Value::Array>()->at(0);
    auto  checkpointValue = eve::Value::fromJson(encoded.find("checkpoint")->asString()).expect("actor facts");
    checkpointValue.find("vitals")->set("hp", 5.0);
    checkpointValue.find("cooldowns")->at(0).set("remainingSeconds", 1.25);
    encoded.set("checkpoint", checkpointValue.toJson().expect("modified canonical facts"));
    input.domain = resealJoint(input.domain);
    MaterializedFixture      restored(input);
    auto                     source = restored.core->restoreBindings().expect("bound sources").sources[0].lifetime;
    eve::spawn::BatchRequest old;
    old.source    = source.source();
    old.lifetime  = source;
    old.ordinal   = eve::spawn::OperationOrdinal(1);
    old.operation = source.operationIdentity(old.ordinal).expect("historical operation");
    for (std::uint32_t i = 0; i < 2; ++i)
        old.units.push_back({source.unitIdentity(old.ordinal, i).expect("historical member"),
                             eve::DefinitionRef::parse("rpg.unit:wolf").expect("unit")});
    std::vector<eve::spawn::Point> positions{{1, 2}, {1, 2}};
    auto preparedBefore = restored.factory->prepare(old, positions).expect("unpublished token before materialization");
    auto alternate      = eve::rpg_spawn::RPGSpawnJointRestore::prepare(input.core, input.domain, input.epoch,
                                                                        archiveTestHash, input.pending)
                         .expect("separately admitted facts");
    REQUIRE(!restored.factory->materializeSavedActors(*restored.core, alternate).ok());
    REQUIRE_EQ(restored.count(), std::size_t(0));
    const auto originalSkill    = *eve::rpg::SkillRegistry::find("spawn.basic");
    const auto originalRegistry = eve::rpg::SkillRegistry::definitionRegistry().snapshotJson();
    eve::rpg::SkillRegistry::clear();
    REQUIRE(!restored.factory->materializeSavedActors(*restored.core, *restored.admitted).ok());
    REQUIRE_EQ(restored.count(), std::size_t(0));
    eve::rpg::SkillRegistry::registerSkill(originalSkill);
    eve::rpg::SkillRegistry::definitionRegistry().restoreJson(originalRegistry).expect("exact host content restored");
    restored.factory->materializeSavedActors(*restored.core, *restored.admitted)
        .expect("same factory retries saved actor publication");
    REQUIRE_EQ(restored.count(), std::size_t(2));
    REQUIRE(!restored.factory->materializeSavedActors(*restored.core, *restored.admitted).ok());
    restored.admitted.reset();
    restored.domain.reset();
    REQUIRE(!restored.factory->prepare(old, positions).ok());
    REQUIRE(!std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(preparedBefore)->commit().ok());
    auto frozen =
        restored.factory->capture(input.epoch, std::array{old.units[0].definition}).expect("actual restored actors");
    REQUIRE_EQ(frozen.actors().size(), std::size_t(2));
    for (std::size_t i = 0; i < frozen.actors().size(); ++i) {
        REQUIRE_EQ(frozen.actors()[i].checkpointJson,
                   input.domain.payload.find("actors")->at(i).find("checkpoint")->asString());
        REQUIRE(frozen.actors()[i].origin == source.source());
        REQUIRE(restored.factory->inspect(frozen.actors()[i].subject).ok());
    }
    REQUIRE_EQ(restored.core->inspect(source.source()).expect("unchanged core ledger").alive, std::uint32_t(2));
}
TEST_CASE("Saved RPG retained actors and pending removals resume lifecycle after the saved high water mark") {
    for (unsigned mode : {1u, 2u, 3u}) {
        auto                input = jointInputs(mode);
        MaterializedFixture restored(input);
        restored.factory->materializeSavedActors(*restored.core, *restored.admitted)
            .expect("saved live and retained actors");
        REQUIRE_EQ(restored.count(), std::size_t(mode == 2 ? 1 : 2));
        auto source = restored.core->restoreBindings().expect("core binding").sources[0].lifetime.source();
        {
            ecs::ScopedTable current(*restored.table);
            auto             view = ecs::View<eve::rpg::RPGActor>(*restored.table);
            for (auto it = view.begin(); it != view.end(); ++it)
                if (it.entity()->getCurrent("hp") > 0) eve::rpg::VitalsSystem::takeDamage(it.entity(), "hp", 100);
        }
        std::vector<eve::rpg::VitalsEvent> events;
        eve::rpg::VitalsSystem::pollEvents(events);
        auto next = restored.factory->collectLifecycle(events).expect("new actual death");
        REQUIRE_EQ(next.size(), std::size_t(1));
        REQUIRE_EQ(next[0].sequence, std::uint64_t(2));
        REQUIRE(next[0].source == source);
        std::vector<eve::spawn::LifecycleInput> frame;
        for (const auto& pending : input.pending)
            frame.push_back(
                {pending.subject, pending.sequence, pending.reason,
                 restored.core->resolveSavedSource(pending.source).expect("pending source remap").source()});
        frame.insert(frame.end(), next.begin(), next.end());
        const std::uint64_t tick = mode == 3 ? 3 : 2;
        restored.core->advance({eve::SimulationTick(tick), eve::Duration(0)}, frame)
            .expect("accept pending and new sequence");
        REQUIRE_EQ(restored.core->inspect(source).expect("all deaths consumed").alive, std::uint32_t(0));
        auto replacement = restored.core->advance({eve::SimulationTick(tick + 1), eve::Duration(1000000000)})
                               .expect("restored replacements");
        REQUIRE_EQ(replacement.units, std::uint32_t(1));
        auto second =
            restored.core->advance({eve::SimulationTick(tick + 2), eve::Duration(0)}).expect("second due replacement");
        REQUIRE_EQ(second.units, std::uint32_t(1));
        REQUIRE_EQ(restored.core->inspect(source).expect("all due population restored").alive, std::uint32_t(2));
    }
}
TEST_CASE("Saved RPG materialization preserves a pending operation and rejects a substitute core factory binding") {
    Fixture        fixture;
    struct Blocked final : eve::spawn::ISpawnPlacement {
        eve::Result<void> validate(const eve::spawn::PlacementSpec&, const eve::spawn::Region&) const override {
            return eve::Result<void>::success();
        }
        eve::Result<eve::spawn::PlacementOutcome> plan(const eve::spawn::BatchRequest&,
                                                       const eve::spawn::PlacementSpec&, const eve::spawn::Region&,
                                                       std::uint64_t&) override {
            return eve::Result<eve::spawn::PlacementOutcome>::success(eve::spawn::PlacementBlocked{});
        }
    };
    fixture.providers->placement = std::make_shared<Blocked>();
    fixture.sourceWorld->advance({eve::SimulationTick(1), eve::Duration(0)}).expect("saved pending attempt");
    auto                epoch  = fixture.request.operation.child("pending-operation");
    auto                frozen = fixture.sourceWorld->capture(epoch, eve::Revision(1)).expect("pending core");
    JointInputs         input{frozen.seal(archiveTestHash).expect("core envelope"),
                      fixture.factory->capture(epoch, frozen.unitReferences())
                          .expect("domain")
                          .seal(eve::Revision(1), eve::SimulationTick(1), archiveTestHash)
                          .expect("domain envelope"),
                      epoch,
                              {}};
    MaterializedFixture restored(input);
    auto                sources = restored.core->restoreBindings().expect("pending source");
    REQUIRE_EQ(sources.sources[0].retiredThrough.value(), std::uint64_t(0));
    auto substitute =
        eve::rpg_spawn::RPGSpawnFactory::prepareRestored(restored.table, restored.domain).expect("substitute");
    REQUIRE(!substitute->materializeSavedActors(*restored.core, *restored.admitted).ok());
    restored.factory->materializeSavedActors(*restored.core, *restored.admitted)
        .expect("empty domain and pending ordinal");
    auto created =
        restored.core->advance({eve::SimulationTick(2), eve::Duration(1000000000)}).expect("retry original ordinal");
    REQUIRE_EQ(created.units, std::uint32_t(2));
    REQUIRE_EQ(restored.count(), std::size_t(2));
}
}  // namespace test_rpg_spawn

namespace test_rpg_spawn {
namespace {
eve::SnapshotEnvelope sessionBundle(const JointInputs& input) {
    return eve::rpg_spawn::RPGSpawnJointRestore::sealBundle(input.core, input.domain, input.epoch, input.pending,
                                                            archiveTestHash)
        .expect("complete session artifact");
}
}  // namespace
TEST_CASE(
    "RPG session prepares without actor publication and exchanges the owner only after retryable materialization") {
    auto       input     = jointInputs();
    auto       bundle    = sessionBundle(input);
    const auto queries   = eve::cap::listenerCount<eve::IStateQuery>();
    auto       candidate = eve::rpg_spawn::PreparedRPGSpawnSession::prepare(bundle, input.epoch, archiveTestHash,
                                                                            std::make_shared<Space>())
                         .expect("CPU-only prepare");
    REQUIRE_EQ(eve::cap::listenerCount<eve::IStateQuery>(), queries);
    std::unique_ptr<eve::rpg_spawn::RPGSpawnSession> target;
    auto                                             empty = candidate.commit(target).expect("initial publication");
    REQUIRE(!empty);
    REQUIRE(target != nullptr);
    const auto*              previous   = target.get();
    const auto               source     = target->sources()[0];
    auto                     lease      = target->spawning().sourceLease(source).expect("old current lease");
    auto                     oldFactory = target->spawning().restoreBindings().expect("actual old factory").factory;
    eve::spawn::BatchRequest future;
    future.source    = source;
    future.lifetime  = lease;
    future.ordinal   = eve::spawn::OperationOrdinal(2);
    future.operation = lease.operationIdentity(future.ordinal).expect("future operation");
    future.units.push_back({lease.unitIdentity(future.ordinal, 0).expect("future member"),
                            eve::DefinitionRef::parse("rpg.unit:wolf").expect("unit")});
    std::array<eve::spawn::Point, 1> positions{{{1, 2}}};
    auto                             token = oldFactory->prepare(future, positions).expect("old prepared operation");
    auto next = eve::rpg_spawn::PreparedRPGSpawnSession::prepare(bundle, input.epoch, archiveTestHash,
                                                                 std::make_shared<Space>())
                    .expect("replacement prepare");
    bool replacementRejected = false;
    auto guardedHash         = [&](std::string_view text) {
        replacementRejected = !next.commit(target).ok();
        return archiveTestHash(text);
    };
    target->capture(input.epoch, eve::Revision(7), guardedHash).expect("capture guards owner replacement");
    REQUIRE(replacementRejected);
    REQUIRE(target.get() == previous);
    const auto skill    = *eve::rpg::SkillRegistry::find("spawn.basic");
    const auto registry = eve::rpg::SkillRegistry::definitionRegistry().snapshotJson();
    eve::rpg::SkillRegistry::clear();
    REQUIRE(!next.commit(target).ok());
    REQUIRE(target.get() == previous);
    REQUIRE(lease.isCurrent());
    eve::rpg::SkillRegistry::registerSkill(skill);
    eve::rpg::SkillRegistry::definitionRegistry().restoreJson(registry).expect("exact host content repair");
    auto old = next.commit(target).expect("same prepared candidate retries");
    REQUIRE(old.get() == previous);
    REQUIRE(target.get() != previous);
    REQUIRE(!lease.isCurrent());
    REQUIRE(target->sources()[0].incarnation != source.incarnation);
    REQUIRE(!old->advance({eve::SimulationTick(2), eve::Duration(0)}).ok());
    REQUIRE(!old->capture(input.epoch, eve::Revision(8), archiveTestHash).ok());
    REQUIRE(!std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(token)->commit().ok());
    REQUIRE(!next.commit(target).ok());
    REQUIRE_EQ(target->spawning().inspect(target->sources()[0]).expect("new live core").alive, std::uint32_t(2));
}
TEST_CASE("Restored RPG session owns pending removals across rejected frames and reseals them before retry") {
    auto input    = jointInputs(2);
    auto bundle   = sessionBundle(input);
    auto prepared = eve::rpg_spawn::PreparedRPGSpawnSession::prepare(bundle, input.epoch, archiveTestHash,
                                                                     std::make_shared<Space>())
                        .expect("retained deletion prepare");
    std::unique_ptr<eve::rpg_spawn::RPGSpawnSession> session;
    auto old = prepared.commit(session).expect("publish restored pending owner");
    REQUIRE(!old);
    auto saved = session->capture(input.epoch.child("pending"), eve::Revision(8), archiveTestHash)
                     .expect("pending is in new owner");
    auto admitted =
        eve::rpg_spawn::RPGSpawnJointRestore::prepareBundle(saved, input.epoch.child("pending"), archiveTestHash)
            .expect("verified pending resave");
    REQUIRE_EQ(admitted.pendingLifecycle().size(), std::size_t(1));
    REQUIRE(!session->advance({eve::SimulationTick(1), eve::Duration(0)}).ok());
    session->advance({eve::SimulationTick(2), eve::Duration(0)}).expect("no second event poll needed");
    REQUIRE_EQ(session->spawning().inspect(session->sources()[0]).expect("retained death consumed").alive,
               std::uint32_t(1));
    auto replacement =
        session->advance({eve::SimulationTick(3), eve::Duration(1000000000)}).expect("actual replacement");
    REQUIRE_EQ(replacement.units, std::uint32_t(1));
    {
        auto             world = session->world();
        ecs::ScopedTable current(*world);
        auto             view = ecs::View<eve::rpg::RPGActor>(*world);
        eve::rpg::VitalsSystem::takeDamage(view.begin().entity(), "hp", 100);
    }
    std::vector<eve::rpg::VitalsEvent> events;
    eve::rpg::VitalsSystem::pollEvents(events);
    REQUIRE(!session->advance({eve::SimulationTick(3), eve::Duration(0)}, events).ok());
    auto retained = session->capture(input.epoch.child("next"), eve::Revision(9), archiveTestHash)
                        .expect("new translated event retained");
    auto next =
        eve::rpg_spawn::RPGSpawnJointRestore::prepareBundle(retained, input.epoch.child("next"), archiveTestHash)
            .expect("new event artifact");
    REQUIRE_EQ(next.pendingLifecycle().size(), std::size_t(1));
    REQUIRE_EQ(next.pendingLifecycle()[0].sequence, std::uint64_t(2));
    session->advance({eve::SimulationTick(4), eve::Duration(0)}).expect("retry owning translated batch");
    REQUIRE_EQ(session->spawning().inspect(session->sources()[0]).expect("accepted once").alive, std::uint32_t(1));
}
TEST_CASE(
    "RPG session preparation snapshots the bundle before callbacks and rejects absent or throwing spatial owners") {
    auto       input        = jointInputs();
    auto       bundle       = sessionBundle(input);
    const auto queries      = eve::cap::listenerCount<eve::IStateQuery>();
    auto       mutatingHash = [&](std::string_view text) {
        bundle.payload = eve::Value::object({});
        return archiveTestHash(text);
    };
    {
        auto prepared = eve::rpg_spawn::PreparedRPGSpawnSession::prepare(bundle, input.epoch, mutatingHash,
                                                                         std::make_shared<Space>())
                            .expect("copied whole artifact before callback");
        REQUIRE_EQ(eve::cap::listenerCount<eve::IStateQuery>(), queries);
    }
    REQUIRE_EQ(eve::cap::listenerCount<eve::IStateQuery>(), queries);
    bundle = sessionBundle(input);
    REQUIRE(!eve::rpg_spawn::PreparedRPGSpawnSession::prepare(bundle, input.epoch, archiveTestHash, {}).ok());
    struct Throwing final : eve::spawn::ISpawnPlacement {
        eve::Result<void> validate(const eve::spawn::PlacementSpec&, const eve::spawn::Region&) const override {
            throw std::runtime_error("spatial admission");
        }
        eve::Result<eve::spawn::PlacementOutcome> plan(const eve::spawn::BatchRequest&,
                                                       const eve::spawn::PlacementSpec&, const eve::spawn::Region&,
                                                       std::uint64_t&) override {
            return eve::Result<eve::spawn::PlacementOutcome>::success(eve::spawn::PlacementBlocked{});
        }
    };
    REQUIRE(!eve::rpg_spawn::PreparedRPGSpawnSession::prepare(bundle, input.epoch, archiveTestHash,
                                                              std::make_shared<Throwing>())
                 .ok());
    REQUIRE_EQ(eve::cap::listenerCount<eve::IStateQuery>(), queries);
}
}  // namespace test_rpg_spawn

namespace test_rpg_spawn {
TEST_CASE(
    "RPG active status archive preserves modifiers periodic progress and clone continuation after source retirement") {
    using namespace eve;
    using namespace eve::rpg;
    EffectDefinition direct;
    direct.id             = "archive.active.direct";
    direct.durationPolicy = "duration";
    direct.duration       = 2;
    direct.modifiers.push_back({"attack", "add", 5, 0});
    EffectDefinition periodic;
    periodic.id             = "archive.active.periodic";
    periodic.durationPolicy = "duration";
    periodic.duration       = 3;
    periodic.period         = 1;
    EffectRegistry::registerEffect(direct);
    EffectRegistry::registerEffect(periodic);
    const auto epoch = PersistentId::parse("01900000-0000-7000-8000-000000000009")->child("active-effects");
    std::vector<std::vector<std::string>> reference;
    auto                                  envelope = [&] {
        Fixture source;
        source.commit();
        eve::rpg::detail::SimulationEvents     sourceEvents;
        eve::rpg::detail::SimulationEventScope eventScope(sourceEvents);
        ecs::ScopedTable                       scope(*source.table);
        {
            auto view = ecs::View<RPGActor>(*source.table);
            for (auto it = view.begin(); it != view.end(); ++it) {
                auto*      actor = it.entity();
                const auto id = StatusSystem::apply(actor, direct.id, "direct-source").expect("actual modifier effect");
                REQUIRE(StatusSystem::setProp(actor, id, "icon", "buff.png"));
                StatusSystem::apply(actor, periodic.id, "periodic-source").expect("actual periodic effect");
                REQUIRE_EQ(actor->getFinalAttribute("attack"), 12.0);
            }
        }
        StatusSystem::update(0.375).expect("partial period before capture");
        auto captured = rpg_spawn::RPGSpawnCapture::capture(*source.table, epoch).expect("active actor archive");
        REQUIRE_EQ(captured.effects().size(), std::size_t(2));
        auto saved = captured.seal(Revision(1), SimulationTick(1), archiveTestHash).expect("active file");
        for (double delta : {0.625, 1.0}) {
            auto summary = StatusSystem::update(delta).expect("uninterrupted continuation");
            REQUIRE_EQ(summary.ticks, std::uint32_t(2));
            auto state = rpg_spawn::RPGSpawnCapture::capture(*source.table, epoch).expect("continued actors");
            std::vector<std::string> rows;
            for (const auto& actor : state.actors()) rows.push_back(actor.checkpointJson);
            reference.push_back(std::move(rows));
        }
        return saved;
    }();
    auto file    = serializeSnapshotEnvelope(envelope).expect("independent file bytes");
    auto archive = rpg_spawn::RPGSpawnArchive::parse(parseSnapshotEnvelope(file, archiveTestHash).takeValue(), epoch,
                                                     archiveTestHash)
                       .expect("read active archive");
    auto content = std::make_shared<const SkillContent>(
        SkillContent::prepareWithEffects(archive.skills(), archive.effects(), archive.implementations())
            .expect("exact active content"));
    auto admitted = rpg_spawn::RPGSpawnRestoreState::prepare(archive).expect("full domain admission without entities");
    REQUIRE_EQ(admitted.actorCount(), std::size_t(2));
    ecs::Table                             target;
    eve::rpg::detail::SimulationEvents     targetEvents;
    eve::rpg::detail::SimulationEventScope eventScope(targetEvents);
    ecs::ScopedTable                       scope(target);
    std::vector<ecs::EntityHandle>         actors;
    for (const auto& saved : archive.actors()) {
        auto recipe =
            rpg_spawn::RPGUnitCatalogue::prepareCaptured(archive.units()[saved.unitRevision], *content).takeValue();
        auto prepared = PreparedMemberActor::prepareCaptured(*recipe.member, content).takeValue();
        prepared.restoreCheckpoint(saved.checkpointJson).expect("restore active prototype");
        auto legacy             = Value::fromJson(saved.checkpointJson).takeValue();
        auto originalAttributes = attributes::AttributeSet::prepareState(*legacy.find("attributes")).takeValue();
        Value::Array bases;
        for (int i = 0; i < originalAttributes.view().attributeCount(); ++i) {
            const auto id = originalAttributes.view().attributeAt(i);
            bases.push_back(Value::object({{"id", id}, {"value", originalAttributes.view().getBase(id)}}));
        }
        legacy.set("version", 1);
        legacy.set("baseAttributes", Value(std::move(bases)));
        legacy.getIf<Value::Object>()->erase("attributes");
        legacy.getIf<Value::Object>()->erase("statuses");
        REQUIRE(!prepared.restoreCheckpoint(legacy.toJson().takeValue()).ok());
        auto retry                 = prepared.clone().expect("clone all status facts");
        auto changed               = direct;
        changed.modifiers[0].value = 99;
        EffectRegistry::registerEffect(changed);
        REQUIRE(!retry.create().ok());
        EffectRegistry::registerEffect(direct);
        auto  handle = retry.create().expect("same candidate retries actual content binding");
        auto* actor  = dynamic_cast<RPGActor*>(ecs::try_get(handle));
        REQUIRE(actor != nullptr);
        REQUIRE_EQ(actor->checkpointJson().takeValue(), saved.checkpointJson);
        REQUIRE_EQ(actor->getFinalAttribute("attack"), 12.0);
        actors.push_back(handle);
    }
    std::size_t phase = 0;
    for (double delta : {0.625, 1.0}) {
        auto summary = StatusSystem::update(delta).expect("restored periodic continuation");
        REQUIRE_EQ(summary.ticks, std::uint32_t(2));
        for (std::size_t i = 0; i < actors.size(); ++i) {
            auto* actor = dynamic_cast<RPGActor*>(ecs::try_get(actors[i]));
            REQUIRE_EQ(actor->checkpointJson().takeValue(), reference[phase][i]);
        }
        ++phase;
    }
    for (auto handle : actors) {
        auto* actor = dynamic_cast<RPGActor*>(ecs::try_get(handle));
        REQUIRE_EQ(actor->getFinalAttribute("attack"), 7.0);
        const auto next = StatusSystem::apply(actor, direct.id, "next").expect("post-restore legacy id allocation");
        REQUIRE_EQ(next, 3);
        ecs::DestroyEntity(actor);
    }
    EffectRegistry::remove(direct.id);
    EffectRegistry::remove(periodic.id);
}
}  // namespace test_rpg_spawn

namespace test_rpg_spawn {
TEST_CASE("RPG actor v3 rejects corrupt status ownership atomically and invalidates old local effect handles") {
    using namespace eve;
    using namespace eve::rpg;
    ecs::Table                             table;
    eve::rpg::detail::SimulationEvents     events;
    eve::rpg::detail::SimulationEventScope eventScope(events);
    ecs::ScopedTable                       scope(table);
    EffectDefinition                       effect;
    effect.id             = "actor.v3.owner";
    effect.durationPolicy = "duration";
    effect.duration       = 3;
    effect.modifiers.push_back({"attack", "add", 5, 0});
    EffectRegistry::registerEffect(effect);
    auto* source = RPGActor::createActor();
    auto* target = RPGActor::createActor();
    for (auto* actor : {source, target}) actor->setBaseAttribute("attack", 7);
    const auto sourceId  = StatusSystem::apply(source, effect.id).takeValue();
    const auto oldId     = StatusSystem::apply(target, effect.id).takeValue();
    const auto canonical = target->statuses()->effectByLegacyId.at(oldId);
    const auto oldHandle = target->statuses()->container.handleFor(canonical).takeValue();
    StatusSystem::remove(target, oldId).expect("clean receiver retains historical handle generation");
    auto       snapshot = Value::fromJson(source->checkpointJson().takeValue()).takeValue();
    const auto before   = target->checkpointJson().takeValue();
    auto       reject   = [&](Value bad) {
        REQUIRE(!target->restoreCheckpointJson(bad.toJson().takeValue()).ok());
        REQUIRE_EQ(target->checkpointJson().takeValue(), before);
    };
    auto bad                                                       = snapshot;
    bad.find("statuses")->find("members")->at(0).at(3).at(0).at(1) = "foreign.modifier";
    reject(std::move(bad));
    bad = snapshot;
    bad.find("statuses")->set("next", sourceId);
    reject(std::move(bad));
    bad                                                = snapshot;
    bad.find("statuses")->find("members")->at(0).at(2) = -1.0;
    reject(std::move(bad));
    bad = snapshot;
    bad.find("statuses")->set("unknown", 0);
    reject(std::move(bad));
    bad = snapshot;
    bad.find("class")->set("unknown", 0);
    reject(std::move(bad));
    bad = snapshot;
    bad.set("unknown", 0);
    reject(std::move(bad));
    target->restoreCheckpointJson(snapshot.toJson().takeValue()).expect("publish complete actor facts");
    REQUIRE_EQ(target->checkpointJson().takeValue(), source->checkpointJson().takeValue());
    REQUIRE_EQ(target->getFinalAttribute("attack"), 12.0);
    REQUIRE(!target->statuses()->container.resolve(oldHandle).ok());
    StatusSystem::remove(target, sourceId).expect("restored modifier removal uses exact canonical binding");
    REQUIRE_EQ(target->getFinalAttribute("attack"), 7.0);
    int customCalls = 0;
    AttributeSystem::customOps()
        .registerVersionedOperation("actor.vital.bound", "bound:v1",
                                    [&](double a, double b) {
                                        ++customCalls;
                                        return a + b;
                                    })
        .expect("actual custom bound callback");
    source->setBaseAttribute("hp", 20);
    source->vitals()->current["hp"] = 10;
    attributes::AttributeModifier custom;
    custom.id        = "external-bound";
    custom.attribute = "hp";
    custom.source    = "external";
    custom.operation = attributes::AttributeOperation::Custom;
    custom.policyId  = "actor.vital.bound";
    custom.value     = 5;
    source->attributes()->values.addModifier(custom).expect("actual custom-bound fact");
    auto unsupported = source->checkpointJson();
    REQUIRE(!unsupported.ok());
    REQUIRE(unsupported.status().primaryDiagnostic()->code() == DiagnosticCode::Unsupported);
    auto customFile = snapshot;
    customFile.set("attributes", source->attributes()->values.encodeState().takeValue());
    customFile.find("vitals")->set("hp", 10);
    const auto cleanTarget = target->checkpointJson().takeValue();
    REQUIRE(!target->restoreCheckpointJson(customFile.toJson().takeValue()).ok());
    REQUIRE_EQ(target->checkpointJson().takeValue(), cleanTarget);
    REQUIRE_EQ(customCalls, 0);
    AttributeSystem::customOps().unregisterOperation("actor.vital.bound");
    int callbacks                 = 0;
    target->statuses()->container = effects::EffectContainer([&](std::span<std::uint8_t>) {
        ++callbacks;
        return false;
    });
    REQUIRE(!target->restoreCheckpointJson(snapshot.toJson().takeValue()).ok());
    REQUIRE_EQ(callbacks, 0);
    REQUIRE(!target->statuses()->container.encodeState().ok());
    auto legacy = snapshot;
    legacy.getIf<Value::Object>()->erase("attributes");
    legacy.getIf<Value::Object>()->erase("statuses");
    legacy.set("baseAttributes", Value::array({Value::object({{"id", "attack"}, {"value", 7}})}));
    for (int version : {1, 2}) {
        legacy.set("version", version);
        REQUIRE(!target->restoreCheckpointJson(legacy.toJson().takeValue()).ok());
        REQUIRE(!target->statuses()->container.encodeState().ok());
        REQUIRE_EQ(callbacks, 0);
    }
    target->release();
    source->release();
    EffectRegistry::remove(effect.id);
}
}  // namespace test_rpg_spawn

namespace test_rpg_spawn {
TEST_CASE("RPG active linked casts survive independent files and complete candidate graph rebinding") {
    using namespace eve;
    using namespace eve::rpg;
    JointInputs              input;
    std::vector<std::string> before;
    std::vector<SubjectRef>  referenceOrder;
    {
        Fixture         source;
        SkillDefinition skill;
        skill.id       = "spawn.basic";
        skill.castTime = 2;
        skill.cooldown = 3;
        SkillRegistry::registerSkill(skill);
        source.sourceWorld->advance({SimulationTick(1), Duration(0)}).expect("actual spawned actors");
        ecs::ScopedTable       scope(*source.table);
        std::vector<RPGActor*> actors;
        {
            auto view = ecs::View<RPGActor>(*source.table);
            for (auto it = view.begin(); it != view.end(); ++it) actors.push_back(it.entity());
        }
        REQUIRE_EQ(actors.size(), std::size_t(2));
        const auto high                  = *ActorExecutionKey::parse("01900000-0000-7000-8000-000000000002");
        const auto low                   = *ActorExecutionKey::parse("01900000-0000-7000-8000-000000000001");
        actors[0]->executionOrder()->key = high;
        actors[1]->executionOrder()->key = low;
        actors[0]->executionOrder()->key = low;
        auto duplicateOrder              = rpg_spawn::RPGSpawnCapture::capture(*source.table, source.request.operation);
        REQUIRE(!duplicateOrder.ok());
        REQUIRE(duplicateOrder.error()->code() == DiagnosticCode::Conflict);
        actors[0]->executionOrder()->key.reset();
        REQUIRE(!rpg_spawn::RPGSpawnCapture::capture(*source.table, source.request.operation).ok());
        REQUIRE(!actors[0]->executionOrder()->key);
        actors[0]->executionOrder()->key = high;
        REQUIRE(RPGActor::liveActors()[0] == actors[1]);
        eve::rpg::detail::SimulationEvents     referenceEvents;
        eve::rpg::detail::SimulationEventScope referenceScope(referenceEvents);
        REQUIRE(SkillSystem::beginCast(actors[0], skill.id, actors[1]));
        REQUIRE(SkillSystem::beginCast(actors[1], skill.id, actors[0]));
        SkillSystem::update(0.375f);
        input.epoch   = source.request.operation.child("active-cast-file");
        auto core     = source.sourceWorld->capture(input.epoch, Revision(7)).expect("core boundary");
        input.core    = core.seal(archiveTestHash).expect("core file");
        auto captured = source.factory->capture(input.epoch, core.unitReferences()).expect("mutual target freeze");
        for (const auto& actor : captured.actors()) before.push_back(actor.checkpointJson);
        input.domain = captured.seal(Revision(7), SimulationTick(1), archiveTestHash).expect("domain file");
        SkillSystem::update(1.625f);
        REQUIRE_EQ(referenceEvents.casts.size(), std::size_t(2));
        REQUIRE(referenceEvents.casts.front().caster.resolve().has_value());
        REQUIRE(&referenceEvents.casts.front().caster.resolve()->get() == actors[1]);
        for (const auto& event : referenceEvents.casts) {
            auto caster = event.caster.resolve();
            REQUIRE(caster.has_value());
            referenceOrder.push_back(eve::rpg::actorSubject(caster->get()).value());
        }
        auto* external = RPGActor::createActor();
        SkillSystem::cancelCast(actors[0]);
        actors[0]->setSkillCooldown(skill.id, 0.f);
        REQUIRE(SkillSystem::beginCast(actors[0], skill.id, external));
        REQUIRE(!source.factory->capture(input.epoch, core.unitReferences()).ok());
        external->release();
        auto* sourceStates = actors[0]->getComponentManager().getComponentBuffer<rpg_spawn::SpawnedActorState>();
        auto  provenance   = *sourceStates->ptr(actors[0]->id);
        provenance.origin.reset();
        auto*                                           incomplete = RPGActor::create();
        ecs::ComponentRef<rpg_spawn::SpawnedActorState> incompleteState(incomplete);
        bindActorIdentity(*incomplete, SubjectRef::fromPersistentId(input.epoch.child("incomplete-root")))
            .expect("explicit incomplete-root registration");
        incompleteState.emplace(std::move(provenance));
        REQUIRE(!source.factory->capture(input.epoch, core.unitReferences()).ok());
        REQUIRE(!incomplete->statuses().has());
        REQUIRE(!incomplete->skills().has());
        REQUIRE(!incomplete->attributes().has());
        incomplete->release();
    }
    auto bytes   = serializeSnapshotEnvelope(input.domain).expect("independent actor bytes");
    input.domain = parseSnapshotEnvelope(bytes, archiveTestHash).expect("source world retired before parse");
    auto bad     = input.domain;
    auto state   = Value::fromJson(bad.payload.find("actors")->at(0).find("checkpoint")->asString()).takeValue();
    state.find("casting")->find("target")->set("subject", input.epoch.child("absent").format());
    bad.payload.find("actors")->at(0).set("checkpoint", state.toJson().takeValue());
    REQUIRE(!rpg_spawn::RPGSpawnJointRestore::prepare(input.core, resealJoint(bad), input.epoch, archiveTestHash).ok());
    auto duplicate = input.domain;
    auto firstState =
        Value::fromJson(duplicate.payload.find("actors")->at(0).find("checkpoint")->asString()).takeValue();
    auto secondState =
        Value::fromJson(duplicate.payload.find("actors")->at(1).find("checkpoint")->asString()).takeValue();
    firstState.set("executionOrder", *secondState.find("executionOrder"));
    duplicate.payload.find("actors")->at(0).set("checkpoint", firstState.toJson().takeValue());
    REQUIRE(!rpg_spawn::RPGSpawnJointRestore::prepare(input.core, resealJoint(duplicate), input.epoch, archiveTestHash)
                 .ok());
    {
        auto legacy = input;
        for (auto& actor : *legacy.domain.payload.find("actors")->getIf<Value::Array>()) {
            auto state = Value::fromJson(actor.find("checkpoint")->asString()).takeValue();
            state.set("version", 4);
            state.getIf<Value::Object>()->erase("executionOrder");
            actor.set("checkpoint", state.toJson().takeValue());
        }
        legacy.domain = resealJoint(legacy.domain);
        MaterializedFixture imported(legacy);
        imported.factory->materializeSavedActors(*imported.core, *imported.admitted)
            .expect("explicit v4 coordinated import");
        auto captured = rpg_spawn::RPGSpawnCapture::capture(*imported.table, input.epoch)
                            .expect("legacy import has complete order facts");
        for (const auto& actor : captured.actors()) {
            auto state = Value::fromJson(actor.checkpointJson).takeValue();
            REQUIRE_EQ(state.find("version")->asInt(), 5);
            REQUIRE_EQ(state.find("executionOrder")->asString(), actor.subject.format());
        }
    }
    MaterializedFixture restored(input);
    {
        ecs::ScopedTable scope(*restored.table);
        auto*            a = RPGActor::createActor();
        auto*            b = RPGActor::createActor();
        a->release();
        b->release();
    }
    restored.factory->materializeSavedActors(*restored.core, *restored.admitted).expect("bind complete actor graph");
    ecs::ScopedTable scope(*restored.table);
    auto             stateAfter = rpg_spawn::RPGSpawnCapture::capture(*restored.table, input.epoch)
                          .expect("capture reconstructed native targets");
    REQUIRE_EQ(stateAfter.actors().size(), before.size());
    for (std::size_t i = 0; i < before.size(); ++i) REQUIRE_EQ(stateAfter.actors()[i].checkpointJson, before[i]);
    eve::rpg::detail::SimulationEvents     events;
    eve::rpg::detail::SimulationEventScope eventScope(events);
    REQUIRE(events.casts.empty());
    SkillSystem::update(1.f);
    REQUIRE(events.casts.empty());
    SkillSystem::update(0.625f);
    REQUIRE_EQ(events.casts.size(), std::size_t(2));
    std::vector<SubjectRef> actualOrder;
    for (const auto& event : events.casts) {
        auto caster = event.caster.resolve();
        auto target = event.target.resolve();
        REQUIRE(caster.has_value());
        REQUIRE(target.has_value());
        actualOrder.push_back(eve::rpg::actorSubject(caster->get()).value());
        REQUIRE(&caster->get() != &target->get());
        REQUIRE(caster->get().table == restored.table.get());
        REQUIRE(target->get().table == restored.table.get());
        REQUIRE_EQ(event.skillId, "spawn.basic");
        REQUIRE(!caster->get().isCastingSkill());
        REQUIRE_EQ(caster->get().getSkillCooldown("spawn.basic"), 1.f);
    }
    REQUIRE(actualOrder == referenceOrder);
}
}  // namespace test_rpg_spawn
