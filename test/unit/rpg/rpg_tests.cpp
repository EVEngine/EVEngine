// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "rpg.cpp",
    "rpg_actor_checkpoint.cpp",
    "rpg_battle.cpp",
    "rpg_battle_tactics.cpp",
    "rpg_class.cpp",
    "rpg_dialect.cpp",
    "rpg_gamestate.cpp",
    "gameplay_control_rpg.cpp",
    "rpg_encounter_catalogue.cpp",
    "rpg_party.cpp",
    "rpg_state_canonical.cpp",
    "rpg_progression.cpp",
    "rpg_quest.cpp",
    "rpg_reload.cpp",
    "rpg_skill_condition.cpp",
    "rpg_story_event.cpp",
    "rpg_trait.cpp",
    "status_effect_adapter.cpp",
    "rpg_equipment_upgrade.cpp"
  ]
}
*/
// clang-format on

#include "rpg/ActorEventRef.h"
#include "zeroerr/unittest.h"
namespace {
// Immediate borrowed projection for assertions only; callers never retain across callbacks.
[[maybe_unused]] eve::rpg::RPGActor* eventActor(const eve::rpg::ActorEventRef& reference) {
    auto actor = reference.resolve();
    return actor ? &actor->get() : nullptr;
}
}  // namespace


#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg)
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>
#include "PrivateTestScope.h"
#include "rpg/ActorInitialization.h"
#include "rpg/AttributeSystem.h"
#include "rpg/AttributeTypes.h"
#include "rpg/Battle.h"
#include "rpg/Effect.h"
#include "rpg/EffectDefinitionCodec.h"
#include "rpg/LevelSystem.h"
#include "rpg/RPG.h"
#include "rpg/RPGActor.h"
#include "rpg/Settlement.h"
#include "rpg/SettlementAdapter.h"
#include "rpg/SimulationEventArchive.h"
#include "rpg/SimulationEvents.h"
#include "rpg/Skill.h"
#include "rpg/SkillContent.h"
#include "rpg/SkillSystem.h"
#include "rpg/StatusSystem.h"

namespace test_rpg_contract {
using namespace eve::rpg;
class IdentityChild final : public RPGActor {
public:
    ENTITY(IdentityChild, RPGActor)
};

TEST_CASE("Vitals events retain facts while actor and world incarnations retire") {
    detail::SimulationEvents     events;
    detail::SimulationEventScope eventScope(events);
    ActorEventRef                absent;
    CHECK(absent.presence() == ActorEventPresence::None);
    CHECK(!(absent.resolve().has_value()));
    alignas(ecs::Table) unsigned char storage[sizeof(ecs::Table)];
    auto*                             world = new (storage) ecs::Table();
    auto*                             actor = RPGActor::create(*world);
    actor->setBaseAttribute("hp", 10.0);
    VitalsSystem::setCurrent(actor, "hp", 10.0);
    REQUIRE_EQ(VitalsSystem::takeDamage(actor, "hp", 10.0, "test"), 10.0);
    REQUIRE_EQ(events.vitals.size(), std::size_t(2));
    VitalsSystem::pollEvents(events.vitals);
    REQUIRE_EQ(events.vitals.size(), std::size_t(2));
    auto& death = events.vitals.back();
    REQUIRE(death.actor.presence() == ActorEventPresence::Live);
    REQUIRE(death.actor.resolve().has_value());
    CHECK(&death.actor.resolve()->get() == actor);
    CHECK(death.actor.isFromWorld(world->lifetime()));
    const auto oldHandle     = death.actor.handle();
    const auto originalWorld = world->lifetime();
    actor->release();
    CHECK(death.actor.presence() == ActorEventPresence::Retired);
    CHECK(!(death.actor.resolve().has_value()));
    CHECK(death.actor.isFromWorld(world->lifetime()));
    auto* replacement = RPGActor::create(*world);
    REQUIRE_EQ(replacement->id, oldHandle.id);
    CHECK(!(death.actor.resolve().has_value()));
    world->~Table();
    CHECK(death.actor.isFromWorld(originalWorld));
    CHECK(!(death.actor.resolve().has_value()));
    world          = new (storage) ecs::Table();
    auto* newActor = RPGActor::create(*world);
    REQUIRE_EQ(newActor->id, oldHandle.id);
    REQUIRE_EQ(newActor->generation, oldHandle.generation);
    CHECK(!(death.actor.isFromWorld(world->lifetime())));
    CHECK(!(death.actor.resolve().has_value()));
    CHECK_EQ(death.action, std::string("death"));
    CHECK_EQ(death.source, std::string("test"));
    CHECK_EQ(death.amount, 10.0);
    CHECK_EQ(death.current, 0.0);
    world->~Table();
    CHECK(death.actor.presence() == ActorEventPresence::Retired);
}

TEST_CASE("RPG simulation event families keep facts after actor and world retirement") {
    detail::SimulationEvents     events;
    detail::SimulationEventScope eventScope(events);
    EffectDefinition             effect;
    effect.id                       = "event.lifetime.periodic";
    effect.durationPolicy           = "duration";
    effect.duration                 = 2.f;
    effect.period                   = 0.5f;
    effect.extra["settlement.tick"] = R"({"kind":"damage","resource":"hp","magnitude":3})";
    EffectRegistry::registerEffect(effect);
    SkillDefinition skill;
    skill.id = "event.lifetime.instant";
    SkillRegistry::registerSkill(skill);
    alignas(ecs::Table) unsigned char storage[sizeof(ecs::Table)];
    auto*                             world = new (storage) ecs::Table();
    ecs::EntityHandle                 original;
    {
        ecs::ScopedTable tableScope(*world);
        auto*            actor = RPGActor::createActor();
        original               = ecs::handle_of(actor);
        actor->learnSkill(skill.id);
        actor->setBaseAttribute("hp", 20);
        actor->setCurrent("hp", 20);
        REQUIRE(SkillSystem::beginCast(actor, skill.id));
        REQUIRE(SkillSystem::beginCast(actor, skill.id, actor));
        StatusSystem::apply(actor, effect.id, "historical.source").expect("periodic status");
        StatusSystem::update(0.5).expect("periodic tick");
        REQUIRE(LevelSystem::gainXp(actor, 100, 1));
        REQUIRE_EQ(events.casts.size(), std::size_t(2));
        REQUIRE_EQ(events.ticks.size(), std::size_t(1));
        REQUIRE_EQ(events.changes.size(), std::size_t(1));
        REQUIRE_EQ(events.levels.size(), std::size_t(1));
        SkillSystem::pollCastEvents(events.casts);
        StatusSystem::pollTicks(events.ticks);
        StatusSystem::pollChanges(events.changes);
        LevelSystem::pollLevelUps(events.levels);
        REQUIRE_EQ(events.casts.size(), std::size_t(2));
        REQUIRE_EQ(events.ticks.size(), std::size_t(1));
        REQUIRE_EQ(events.changes.size(), std::size_t(1));
        REQUIRE_EQ(events.levels.size(), std::size_t(1));
        REQUIRE(events.casts[0].target.presence() == ActorEventPresence::None);
        REQUIRE(eventActor(events.casts[1].target) == actor);
        REQUIRE(eventActor(events.ticks[0].actor) == actor);
        {
            Battle liveBattle;
            liveBattle.addActor(actor, BattleSide::Party);
            auto outcomes = liveBattle.settleStatusTick(events.ticks[0], {}, eve::SimulationTick(1))
                                .expect("live periodic tick settlement");
            REQUIRE_EQ(outcomes.size(), std::size_t(1));
            REQUIRE(outcomes[0].result.has_value());
            REQUIRE_EQ(outcomes[0].result->applied, 3.0);
            REQUIRE_EQ(actor->getCurrent("hp"), 17.0);
            REQUIRE_EQ(events.vitals.size(), std::size_t(1));
            REQUIRE(eventActor(events.vitals[0].actor) == actor);
        }
        actor->release();
        auto* replacement = RPGActor::createActor();
        REQUIRE_EQ(replacement->id, original.id);
        REQUIRE(replacement->generation != original.generation);
        for (const auto& event : events.casts) CHECK(event.caster.presence() == ActorEventPresence::Retired);
        CHECK(events.casts[1].target.presence() == ActorEventPresence::Retired);
        CHECK(events.ticks[0].actor.presence() == ActorEventPresence::Retired);
        CHECK(events.changes[0].actor.presence() == ActorEventPresence::Retired);
        CHECK(events.levels[0].actor.presence() == ActorEventPresence::Retired);
        CHECK(events.vitals[0].actor.presence() == ActorEventPresence::Retired);
    }
    world->~Table();
    world        = new (storage) ecs::Table();
    auto* reused = RPGActor::create(*world);
    REQUIRE_EQ(reused->id, original.id);
    REQUIRE_EQ(reused->generation, original.generation);
    for (const auto& event : events.casts) CHECK(!event.caster.resolve());
    CHECK(!events.casts[1].target.resolve());
    CHECK(events.casts[0].target.presence() == ActorEventPresence::None);
    CHECK(!events.ticks[0].actor.resolve());
    CHECK(!events.changes[0].actor.resolve());
    CHECK(!events.levels[0].actor.resolve());
    CHECK(!events.vitals[0].actor.resolve());
    CHECK_EQ(events.ticks[0].source, "historical.source");
    CHECK_EQ(events.changes[0].effectId, effect.id);
    CHECK_EQ(events.levels[0].previousLevel, 1);
    CHECK_EQ(events.levels[0].newLevel, 2);
    const auto id = eve::PersistentId::parse("00000000-0000-7000-8000-000000000123");
    REQUIRE(id.has_value());
    auto projected = makeStatusTickSettlementRequest(events.ticks[0], {}, eve::SubjectRef::fromPersistentId(*id),
                                                     eve::SimulationTick(1));
    REQUIRE(!projected.ok());
    CHECK(projected.diagnostics()[0].code() == eve::DiagnosticCode::StaleHandle);
    Battle battle;
    auto   settled = battle.settleStatusTick(events.ticks[0], {}, eve::SimulationTick(1));
    REQUIRE(!settled.ok());
    CHECK(settled.diagnostics()[0].code() == eve::DiagnosticCode::StaleHandle);
    world->~Table();
    EffectRegistry::remove(effect.id);
    SkillRegistry::remove(skill.id);
}

TEST_CASE("RPG lifecycle hooks resolve the original incarnation after an earlier hook retires it") {
    ecs::Table                   world;
    ecs::ScopedTable             tableScope(world);
    detail::SimulationEvents     events;
    detail::SimulationEventScope eventScope(events);
    EffectDefinition             effect;
    effect.id             = "event.lifetime.hooks";
    effect.durationPolicy = "infinite";
    EffectRegistry::registerEffect(effect);
    auto*      actor       = RPGActor::createActor();
    const auto original    = ecs::handle_of(actor);
    RPGActor*  replacement = nullptr;
    int        calls       = 0;
    const auto observe     = [&](const StatusChangeEvent& event) {
        if (event.effectId != effect.id) return;
        ++calls;
        REQUIRE_EQ(event.actor.handle().id, original.id);
        REQUIRE_EQ(event.actor.handle().generation, original.generation);
        if (calls == 1) {
            REQUIRE(eventActor(event.actor) == actor);
            actor->release();
            replacement = RPGActor::createActor();
            REQUIRE_EQ(replacement->id, original.id);
        } else {
            CHECK(event.actor.presence() == ActorEventPresence::Retired);
            CHECK(!event.actor.resolve());
            CHECK_EQ(event.action, "apply");
        }
    };
    StatusSystem::registerLifecycleHook("event.lifetime.first", observe);
    StatusSystem::registerLifecycleHook("event.lifetime.second", observe);
    StatusSystem::apply(actor, effect.id).expect("publish before hooks");
    REQUIRE_EQ(calls, 2);
    REQUIRE_EQ(events.changes.size(), std::size_t(1));
    CHECK(events.changes[0].actor.presence() == ActorEventPresence::Retired);
    CHECK(replacement != nullptr);
    StatusSystem::unregisterLifecycleHook("event.lifetime.first");
    StatusSystem::unregisterLifecycleHook("event.lifetime.second");
    replacement->release();
    EffectRegistry::remove(effect.id);
}

TEST_CASE("RPG progression queues follow nested simulation owners and prepared gains reject retired actors") {
    static_assert(!std::is_copy_constructible_v<PreparedProgressionGain>);
    detail::SimulationEvents     outer, inner;
    detail::SimulationEventScope outerScope(outer);
    ecs::Table                   outerWorld;
    ecs::ScopedTable             tableScope(outerWorld);
    auto*                        incomplete = RPGActor::create(outerWorld);
    REQUIRE(!incomplete->progression().has());
    auto unprepared = LevelSystem::prepareGainXp(incomplete, 100, 1);
    REQUIRE(!unprepared.ok());
    CHECK(unprepared.diagnostics()[0].code() == eve::DiagnosticCode::PreconditionViolation);
    CHECK(!incomplete->progression().has());
    CHECK(outer.levels.empty());
    incomplete->release();
    auto* outerActor = RPGActor::createActor();
    REQUIRE(LevelSystem::gainXp(outerActor, 100, 1));
    {
        detail::SimulationEventScope innerScope(inner);
        ecs::Table                   innerWorld;
        ecs::ScopedTable             innerTable(innerWorld);
        auto*                        actor = RPGActor::createActor();
        REQUIRE(LevelSystem::gainXp(actor, 100, 1));
        std::vector<LevelUpEvent> polled;
        LevelSystem::pollLevelUps(polled);
        REQUIRE_EQ(polled.size(), std::size_t(1));
        REQUIRE(eventActor(polled[0].actor) == actor);
        REQUIRE(inner.levels.empty());
        REQUIRE_EQ(outer.levels.size(), std::size_t(1));
    }
    std::vector<LevelUpEvent> outerPolled;
    LevelSystem::pollLevelUps(outerPolled);
    REQUIRE_EQ(outerPolled.size(), std::size_t(1));
    REQUIRE(eventActor(outerPolled[0].actor) == outerActor);
    auto conflict = LevelSystem::prepareGainXp(outerActor, 100, 1).expect("prepare conflict");
    LevelSystem::setLevel(outerActor, 3);
    auto rejected = LevelSystem::commitGainXp(std::move(conflict));
    REQUIRE(!rejected.ok());
    CHECK(rejected.diagnostics()[0].code() == eve::DiagnosticCode::Conflict);
    CHECK(outer.levels.empty());
    outerActor->restoreProgression(std::numeric_limits<int>::max(), 0, 1).expect("valid terminal level");
    auto overflow = LevelSystem::prepareGainXp(outerActor, 1, 1);
    REQUIRE(!overflow.ok());
    CHECK_EQ(outerActor->getLevel(), std::numeric_limits<int>::max());
    CHECK(outer.levels.empty());

    alignas(ecs::Table) unsigned char storage[sizeof(ecs::Table)];
    auto*                             world = new (storage) ecs::Table();
    PreparedProgressionGain           afterEntity, afterWorld;
    ecs::EntityHandle                 original;
    {
        ecs::ScopedTable sourceScope(*world);
        auto*            actor = RPGActor::createActor();
        original               = ecs::handle_of(actor);
        afterEntity            = LevelSystem::prepareGainXp(actor, 100, 1).expect("entity-bound gain");
        afterWorld             = LevelSystem::prepareGainXp(actor, 100, 1).expect("world-bound gain");
        actor->release();
        auto* replacement = RPGActor::createActor();
        REQUIRE_EQ(replacement->id, original.id);
        auto stale = LevelSystem::commitGainXp(std::move(afterEntity));
        REQUIRE(!stale.ok());
        CHECK(stale.diagnostics()[0].code() == eve::DiagnosticCode::StaleHandle);
        CHECK_EQ(replacement->getLevel(), 1);
    }
    world->~Table();
    world       = new (storage) ecs::Table();
    auto* actor = RPGActor::create(*world);
    REQUIRE_EQ(actor->id, original.id);
    REQUIRE_EQ(actor->generation, original.generation);
    auto stale = LevelSystem::commitGainXp(std::move(afterWorld));
    REQUIRE(!stale.ok());
    CHECK(stale.diagnostics()[0].code() == eve::DiagnosticCode::StaleHandle);
    CHECK(!actor->progression().has());
    CHECK(outer.levels.empty());
    world->~Table();
    outerActor->release();
}

TEST_CASE("Pinned RPG execution content resolves exclusively and nested scopes restore their context") {
    SkillDefinition archived;
    archived.id       = "scope.archived";
    archived.cooldown = 3;
    SkillRegistry::registerSkill(archived);
    auto             record = SkillRegistry::captureDefinition(archived.id).expect("source record");
    EffectDefinition effect;
    effect.id     = "scope.effect";
    effect.period = 1;
    EffectRegistry::registerEffect(effect);
    std::vector<eve::definitions::Definition> records{record};
    std::vector<EffectDefinition>             effects{effect};
    auto                                      content = std::make_shared<const SkillContent>(
        SkillContent::prepareWithEffects(records, effects).expect("admit content"));
    archived.cooldown = 99;
    SkillRegistry::registerSkill(archived);
    effect.period = 99;
    EffectRegistry::registerEffect(effect);
    SkillDefinition outside;
    outside.id = "scope.outside";
    SkillRegistry::registerSkill(outside);
    REQUIRE(!SkillContentScope::current());
    {
        SkillContentScope scope(content);
        REQUIRE(SkillContentScope::current().has_value());
        CHECK_EQ(SkillRegistry::find(archived.id)->cooldown, 3.f);
        CHECK_EQ(EffectRegistry::find(effect.id)->period, 1.f);
        CHECK(!SkillRegistry::find(outside.id));
        CHECK_EQ(SkillRegistry::captureDefinition(archived.id).expect("pinned record").json, record.json);
        {
            SkillContentScope ordinary({});
            CHECK(!SkillContentScope::current());
            CHECK_EQ(SkillRegistry::find(archived.id)->cooldown, 99.f);
            CHECK_EQ(EffectRegistry::find(effect.id)->period, 99.f);
            CHECK(SkillRegistry::find(outside.id) != nullptr);
        }
        CHECK_EQ(SkillRegistry::find(archived.id)->cooldown, 3.f);
        SkillRegistry::clear();
        EffectRegistry::clear();
        CHECK_EQ(SkillRegistry::find(archived.id)->cooldown, 3.f);
        CHECK_EQ(EffectRegistry::find(effect.id)->period, 1.f);
        CHECK(!SkillRegistry::find(outside.id));
    }
    CHECK(!SkillContentScope::current());
    CHECK(!SkillRegistry::find(archived.id));
    CHECK(!EffectRegistry::find(effect.id));
}

TEST_CASE("RPG event archive preserves all queues and never resurrects retired history") {
    const auto epoch   = *eve::PersistentId::parse("00000000-0000-7000-8000-000000000151");
    const auto subject = eve::SubjectRef::fromPersistentId(epoch);
    const auto hash    = [](std::string_view input) {
        eve::ContentId::Bytes bytes{};
        std::uint64_t         state = 14695981039346656037ULL;
        for (unsigned char c : input) state = (state ^ c) * 1099511628211ULL;
        for (auto& byte : bytes) {
            state ^= state >> 12;
            state ^= state << 25;
            state ^= state >> 27;
            byte = static_cast<std::uint8_t>(state >> 24);
        }
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
    std::optional<SimulationEventArchive> liveArchive, retiredArchive;
    detail::SimulationEvents              source;
    {
        ecs::Table    world;
        auto*         actor = RPGActor::create(world);
        ActorEventRef anonymous(*actor);
        source.casts.push_back({anonymous, {}, "skill"});
        REQUIRE(!SimulationEventArchive::capture(source, epoch, eve::Revision(2), eve::SimulationTick(9)).ok());
        bindActorIdentity(*actor, subject).expect("register source");
        REQUIRE(!SimulationEventArchive::capture(source, epoch, eve::Revision(2), eve::SimulationTick(9)).ok());
        source.casts.clear();
        const ActorEventRef ref(*actor);
        source.casts.push_back({ref, {}, "skill"});
        source.ticks.push_back({ref, 3, "burn", "skill", 2});
        source.changes.push_back({ref, 3, "burn", "skill", "expire", 0, "done"});
        source.vitals.push_back({ref, "hp", "death", 8, "burn", 0, 8});
        source.levels.push_back({ref, 2, 3});
        liveArchive.emplace(SimulationEventArchive::capture(source, epoch, eve::Revision(2), eve::SimulationTick(9))
                                .expect("freeze live"));
    }
    retiredArchive.emplace(SimulationEventArchive::capture(source, epoch, eve::Revision(2), eve::SimulationTick(9))
                               .expect("freeze retired"));
    auto envelope = liveArchive->seal(hash).expect("seal live");
    auto json     = eve::serializeSnapshotEnvelope(envelope).expect("write file");
    auto input    = eve::parseSnapshotEnvelope(json, hash).expect("read file");
    auto parsed   = SimulationEventArchive::parse(input, epoch, hash).expect("admit file");
    REQUIRE_EQ(parsed.linkedSubjects().size(), std::size_t(1));
    ecs::Table destination;
    REQUIRE(!parsed.prepareRestore(destination).ok());
    auto* replacement = RPGActor::create(destination);
    bindActorIdentity(*replacement, subject).expect("register destination");
    auto restored = parsed.prepareRestore(destination).expect("rebind queues");
    REQUIRE_EQ(restored.casts.size(), std::size_t(1));
    REQUIRE_EQ(restored.ticks.size(), std::size_t(1));
    REQUIRE_EQ(restored.changes.size(), std::size_t(1));
    REQUIRE_EQ(restored.vitals.size(), std::size_t(1));
    REQUIRE_EQ(restored.levels.size(), std::size_t(1));
    CHECK(eventActor(restored.vitals[0].actor) == replacement);
    CHECK(restored.casts[0].target.presence() == ActorEventPresence::None);
    CHECK_EQ(restored.ticks[0].stacks, 2);
    auto historical = retiredArchive->prepareRestore(destination).expect("restore history");
    CHECK(historical.vitals[0].actor.presence() == ActorEventPresence::Retired);
    CHECK(!historical.vitals[0].actor.resolve());
    CHECK(!historical.vitals[0].actor.handle().table);
    CHECK(historical.vitals[0].actor.capturedSubject() == subject);
    CHECK(SimulationEventArchive::capture(historical, epoch, eve::Revision(2), eve::SimulationTick(9)).ok());
    input.schemaVersion = eve::SchemaVersion(2);
    REQUIRE(!SimulationEventArchive::parse(input, epoch, hash).ok());
    input = envelope;
    input.payload.getIf<eve::Value::Object>()->emplace("extra", 0);
    REQUIRE(!SimulationEventArchive::parse(input, epoch, hash).ok());
    auto invalid = eve::makeSnapshotEnvelope(input.type, input.schema, input.schemaVersion, input.instanceId,
                                             input.revision, input.tick, input.payload, hash)
                       .expect("seal invalid shape");
    REQUIRE(!SimulationEventArchive::parse(invalid, epoch, hash).ok());
    bool rejected  = false;
    auto reentrant = [&](std::string_view bytes) {
        rejected = !liveArchive->seal(hash).ok();
        return hash(bytes);
    };
    REQUIRE(liveArchive->seal(reentrant).ok());
    CHECK(rejected);
}

TEST_CASE("RPG identity registration freezes historical event subjects without copying body identities") {
    const auto firstId  = eve::PersistentId::parse("00000000-0000-7000-8000-000000000141");
    const auto secondId = eve::PersistentId::parse("00000000-0000-7000-8000-000000000142");
    REQUIRE(firstId.has_value());
    REQUIRE(secondId.has_value());
    const auto    first  = eve::SubjectRef::fromPersistentId(*firstId);
    const auto    second = eve::SubjectRef::fromPersistentId(*secondId);
    ActorEventRef historical, anonymous;
    std::string   body;
    {
        ecs::Table       world;
        ecs::ScopedTable scope(world);
        auto*            actor = RPGActor::createActor();
        REQUIRE(!actorSubject(*actor));
        anonymous = ActorEventRef(*actor);
        REQUIRE(!bindActorIdentity(*actor, eve::SubjectRef::nil()).ok());
        REQUIRE(!actorSubject(*actor));
        bindActorIdentity(*actor, first).expect("first registration");
        historical = ActorEventRef(*actor);
        bindActorIdentity(*actor, first).expect("idempotent registration");
        REQUIRE(historical.capturedSubject() == std::optional<eve::SubjectRef>{first});
        REQUIRE(!anonymous.capturedSubject());
        REQUIRE(eventActor(anonymous) == actor);
        REQUIRE(!bindActorIdentity(*actor, second).ok());
        REQUIRE(actorSubject(*actor) == std::optional<eve::SubjectRef>{first});
        auto* other = RPGActor::createActor();
        REQUIRE(!bindActorIdentity(*other, first).ok());
        REQUIRE(!actorSubject(*other));
        {
            auto view = ecs::View<RPGActor>(world);
            REQUIRE(!bindActorIdentity(*other, second).ok());
        }
        REQUIRE(!actorSubject(*other));
        auto* child = IdentityChild::create();
        initializeActorComponents(*child);
        REQUIRE(!bindActorIdentity(*child, first).ok());
        bindActorIdentity(*child, second).expect("derived RPG family registration");
        REQUIRE(actorSubject(*child) == std::optional<eve::SubjectRef>{second});
        child->release();
        actor->setBaseAttribute("hp", 20);
        actor->setCurrent("hp", 17);
        body           = actor->checkpointJson().expect("identity-independent body");
        const auto old = ecs::handle_of(actor);
        actor->release();
        auto* replacement = RPGActor::createActor();
        REQUIRE_EQ(replacement->id, old.id);
        bindActorIdentity(*replacement, second).expect("replacement gets its own identity");
        REQUIRE(!historical.resolve());
        REQUIRE(historical.capturedSubject() == std::optional<eve::SubjectRef>{first});
        REQUIRE(actorSubject(*replacement) == std::optional<eve::SubjectRef>{second});
    }
    REQUIRE(!historical.resolve());
    REQUIRE(historical.capturedSubject() == std::optional<eve::SubjectRef>{first});
    REQUIRE(!anonymous.capturedSubject());
    ecs::Table       destination;
    ecs::ScopedTable destinationScope(destination);
    auto*            target = RPGActor::createActor();
    bindActorIdentity(*target, second).expect("destination primary key");
    target->restoreCheckpointJson(body).expect("copy body into another registered actor");
    REQUIRE(actorSubject(*target) == std::optional<eve::SubjectRef>{second});
    REQUIRE_EQ(target->getCurrent("hp"), 17.0);
    target->release();
}

TEST_CASE("Attribute evaluation applies modifier phases in order and admits custom operations") {
    AttributeValue value;
    value.base = 100;
    SUB_CASE("add then percent add then percent multiply") {
        value.modifiers = {
            {"flat", "gear", "add", 10, 0}, {"percent", "gear", "mulAdd", .5, 0}, {"factor", "gear", "mulMul", .1, 0}};
        CHECK(std::fabs(computeAttributeValue(value) - 181.5) < 1e-9);
    };
    SUB_CASE("higher priority override takes precedence") {
        value.modifiers = {{"high", "script", "override", 50, 5}, {"low", "script", "override", 20, 1}};
        CHECK_EQ(computeAttributeValue(value), 50.0);
    };
    SUB_CASE("range clamps apply to final value") {
        value.base      = 200;
        value.modifiers = {{"max", "rule", "clampMax", 100, 0}, {"min", "rule", "clampMin", 10, 0}};
        CHECK_EQ(computeAttributeValue(value), 100.0);
    };
    SUB_CASE("registered operation is used and unknown operation leaves value unchanged") {
        AttributeOpTable ops;
        ops.registerOp("square", [](double, double amount) { return amount * amount; });
        value.base      = 7;
        value.modifiers = {{"custom", "rule", "square", 4, 0}};
        CHECK_EQ(computeAttributeValue(value, &ops), 16.0);
        value.modifiers = {{"missing", "rule", "unknown", 4, 0}};
        REQUIRE_EQ(computeAttributeValue(value, &ops), 7.0);
    };
}

TEST_CASE("Actor attributes reflect base edits and source scoped modifier removal") {
    auto* actor = RPGActor::createActor();
    REQUIRE(actor != nullptr);
    actor->setBaseAttribute("power", 10);
    actor->setBaseAttribute("mana", 50);
    CHECK(actor->hasAttribute("power"));
    CHECK(!actor->hasAttribute("unknown"));
    const auto gear = actor->addAttributeModifier("power", "gear", "add", 5);
    actor->addAttributeModifier("power", "spell", "add", 2);
    actor->addAttributeModifier("mana", "spell", "add", 20);
    CHECK_EQ(actor->getFinalAttribute("power"), 17.0);
    actor->modifyBaseAttribute("power", 3);
    CHECK_EQ(actor->getFinalAttribute("power"), 20.0);
    CHECK(actor->removeAttributeModifier("power", gear));
    CHECK(!actor->removeAttributeModifier("power", gear));
    CHECK_EQ(actor->getFinalAttribute("power"), 15.0);
    const int removedSpellModifiers = actor->removeAllAttributeModifiersBySource("spell");
    CHECK_EQ(removedSpellModifiers, 2);
    CHECK_EQ(actor->getFinalAttribute("power"), 13.0);
    CHECK_EQ(actor->getFinalAttribute("mana"), 50.0);
    actor->release();
}

TEST_CASE("Effect registry publishes definitions and JSON payloads then removes by id") {
    EffectDefinition effect;
    effect.id             = "rewrite.rpg.effect.registry";
    effect.durationPolicy = "duration";
    effect.duration       = 3;
    effect.tags           = {"buff"};
    EffectRegistry::registerEffect(effect);
    const auto* found = EffectRegistry::find(effect.id);
    REQUIRE(found != nullptr);
    CHECK_EQ(found->duration, 3.f);
    CHECK(found->hasTag("buff"));
    CHECK(EffectRegistry::remove(effect.id));
    CHECK(EffectRegistry::find(effect.id) == nullptr);
    std::string error;
    const int   loadedEffects = EffectRegistry::loadFromJson(
        R"([{"id":"rewrite.rpg.effect.json","durationPolicy":"infinite","tags":["poison"]}])", &error);
    CHECK_EQ(loadedEffects, 1);
    CHECK(error.empty());
    found = EffectRegistry::find("rewrite.rpg.effect.json");
    REQUIRE(found != nullptr);
    CHECK(found->hasTag("poison"));
    CHECK(EffectRegistry::remove("rewrite.rpg.effect.json"));
}

TEST_CASE("Effect lifetime distinguishes instant application from expiring modifiers") {
    EffectDefinition instant;
    instant.id             = "rewrite.rpg.effect.instant";
    instant.durationPolicy = "instant";
    instant.modifiers.push_back({"health", "add", 25, 0});
    EffectRegistry::registerEffect(instant);
    EffectDefinition duration;
    duration.id             = "rewrite.rpg.effect.duration";
    duration.durationPolicy = "duration";
    duration.duration       = 1;
    duration.modifiers.push_back({"attack", "add", 10, 0});
    EffectRegistry::registerEffect(duration);
    auto* actor = RPGActor::createActor();
    actor->setBaseAttribute("health", 50);
    actor->setBaseAttribute("attack", 20);
    const int instantApplication = actor->applyEffect(instant.id);
    CHECK_EQ(instantApplication, 0);
    CHECK_EQ(actor->getBaseAttribute("health"), 75.0);
    CHECK_EQ(actor->getStatusCount(), 0);
    REQUIRE(actor->applyEffect(duration.id) > 0);
    CHECK_EQ(actor->getFinalAttribute("attack"), 30.0);
    CHECK(StatusSystem::update(.5).ok());
    CHECK_EQ(actor->getStatusCount(), 1);
    CHECK(StatusSystem::update(.6).ok());
    CHECK_EQ(actor->getStatusCount(), 0);
    CHECK_EQ(actor->getFinalAttribute("attack"), 20.0);
    actor->release();
    EffectRegistry::remove(instant.id);
    EffectRegistry::remove(duration.id);
}

TEST_CASE("Effect stack policies reject duplicates refresh duration or cap modifier stacks") {
    SUB_CASE("none rejects duplicate") {
        EffectDefinition def;
        def.id             = "rewrite.rpg.stack.none";
        def.durationPolicy = "infinite";
        def.stackPolicy    = "none";
        EffectRegistry::registerEffect(def);
        auto* actor = RPGActor::createActor();
        REQUIRE(actor->applyEffect(def.id) > 0);
        const int duplicate = actor->applyEffect(def.id);
        CHECK_EQ(duplicate, -1);
        CHECK_EQ(actor->getStatusCount(), 1);
        actor->release();
        EffectRegistry::remove(def.id);
    };
    SUB_CASE("refresh restores full remaining time") {
        EffectDefinition def;
        def.id             = "rewrite.rpg.stack.refresh";
        def.durationPolicy = "duration";
        def.duration       = 2;
        def.stackPolicy    = "refresh";
        EffectRegistry::registerEffect(def);
        auto*     actor = RPGActor::createActor();
        const int id    = actor->applyEffect(def.id);
        REQUIRE(id > 0);
        CHECK(StatusSystem::update(1.5).ok());
        const int refreshed = actor->applyEffect(def.id);
        CHECK_EQ(refreshed, id);
        CHECK(std::fabs(actor->getStatusRemaining(0) - 2) < 1e-4);
        actor->release();
        EffectRegistry::remove(def.id);
    };
    SUB_CASE("stack caps count and its attribute contribution") {
        EffectDefinition def;
        def.id             = "rewrite.rpg.stack.cap";
        def.durationPolicy = "infinite";
        def.stackPolicy    = "stack";
        def.maxStacks      = 2;
        def.modifiers.push_back({"armor", "add", 5, 0});
        EffectRegistry::registerEffect(def);
        auto* actor = RPGActor::createActor();
        actor->setBaseAttribute("armor", 0);
        const int id = actor->applyEffect(def.id);
        REQUIRE(id > 0);
        const int stacked = actor->applyEffect(def.id);
        CHECK_EQ(stacked, id);
        actor->applyEffect(def.id);
        CHECK_EQ(actor->getStatusStacks(0), 2);
        CHECK_EQ(actor->getFinalAttribute("armor"), 10.0);
        CHECK(actor->removeStatus(id));
        CHECK_EQ(actor->getFinalAttribute("armor"), 0.0);
        actor->release();
        EffectRegistry::remove(def.id);
    };
}

TEST_CASE("Periodic effects emit due ticks and stop emitting after expiry") {
    EffectDefinition def;
    def.id             = "rewrite.rpg.periodic";
    def.durationPolicy = "duration";
    def.duration       = 2.5f;
    def.period         = 1.f;
    EffectRegistry::registerEffect(def);
    auto* actor = RPGActor::createActor();
    REQUIRE(actor->applyEffect(def.id, "caster") > 0);
    std::vector<StatusTickEvent> ticks;
    CHECK(StatusSystem::update(1).ok());
    StatusSystem::pollTicks(ticks);
    REQUIRE_EQ(ticks.size(), 1u);
    CHECK_EQ(eventActor(ticks.front().actor), actor);
    CHECK_EQ(ticks.front().source, "caster");
    ticks.clear();
    CHECK(StatusSystem::update(1).ok());
    StatusSystem::pollTicks(ticks);
    CHECK_EQ(ticks.size(), 1u);
    ticks.clear();
    CHECK(StatusSystem::update(1).ok());
    StatusSystem::pollTicks(ticks);
    CHECK_EQ(actor->getStatusCount(), 0);
    ticks.clear();
    CHECK(StatusSystem::update(5).ok());
    StatusSystem::pollTicks(ticks);
    CHECK(ticks.empty());
    actor->release();
    EffectRegistry::remove(def.id);
}

TEST_CASE("Skill admission reports prerequisites then instant cast spends resource and applies effect") {
    EffectDefinition effect;
    effect.id             = "rewrite.rpg.skill.effect";
    effect.durationPolicy = "duration";
    effect.duration       = 5;
    effect.modifiers.push_back({"defense", "add", -3, 0});
    EffectRegistry::registerEffect(effect);
    SkillDefinition skill;
    skill.id       = "rewrite.rpg.skill.instant";
    skill.cooldown = 3;
    auto cost      = eve::resource::CostSpec::single("mana", 10);
    REQUIRE(cost.hasValue());
    skill.cost = std::move(cost).takeValue();
    skill.grantedEffects.push_back(effect.id);
    SkillRegistry::registerSkill(skill);
    auto* caster = RPGActor::createActor();
    auto* target = RPGActor::createActor();
    caster->setBaseAttribute("mana", 5);
    target->setBaseAttribute("defense", 10);
    CHECK_EQ(caster->canCastSkillReason(skill.id), "not_learned");
    caster->learnSkill(skill.id);
    CHECK_EQ(caster->canCastSkillReason(skill.id), "insufficient_cost");
    caster->setBaseAttribute("mana", 20);
    REQUIRE(caster->beginCastSkill(skill.id, target));
    CHECK_EQ(caster->getBaseAttribute("mana"), 10.0);
    CHECK_EQ(caster->getSkillCooldown(skill.id), 3.f);
    CHECK_EQ(target->getFinalAttribute("defense"), 7.0);
    CHECK_EQ(caster->canCastSkillReason(skill.id), "on_cooldown");
    std::vector<SkillCastEvent> casts;
    SkillSystem::pollCastEvents(casts);
    REQUIRE_EQ(casts.size(), 1u);
    CHECK_EQ(eventActor(casts.front().target), target);
    SkillSystem::update(3.1f);
    CHECK(caster->canCastSkill(skill.id));
    caster->release();
    target->release();
    SkillRegistry::remove(skill.id);
    EffectRegistry::remove(effect.id);
}

TEST_CASE("Timed skill emits only when completed and cancellation suppresses resolution") {
    SkillDefinition skill;
    skill.id       = "rewrite.rpg.skill.timed";
    skill.castTime = 2;
    SkillRegistry::registerSkill(skill);
    auto* actor = RPGActor::createActor();
    actor->learnSkill(skill.id);
    REQUIRE(actor->beginCastSkill(skill.id));
    SkillSystem::update(1);
    CHECK(actor->isCastingSkill());
    CHECK(std::fabs(actor->getCastProgress() - .5f) < 1e-4f);
    std::vector<SkillCastEvent> casts;
    SkillSystem::pollCastEvents(casts);
    CHECK(casts.empty());
    actor->cancelCastSkill();
    SkillSystem::update(5);
    SkillSystem::pollCastEvents(casts);
    CHECK(casts.empty());
    REQUIRE(actor->beginCastSkill(skill.id));
    SkillSystem::update(2.1f);
    SkillSystem::pollCastEvents(casts);
    REQUIRE_EQ(casts.size(), 1u);
    CHECK_EQ(casts.front().skillId, skill.id);
    actor->release();
    SkillRegistry::remove(skill.id);
}

TEST_CASE("Timed RPG cast rejects retired target slot reuse without redirecting effects to the caster") {
    ecs::Table                             table;
    ecs::ScopedTable                       scope(table);
    eve::rpg::detail::SimulationEvents     events;
    eve::rpg::detail::SimulationEventScope eventScope(events);
    EffectDefinition                       effect;
    effect.id             = "cast.retired.effect";
    effect.durationPolicy = "duration";
    effect.duration       = 3;
    effect.modifiers.push_back({"attack", "add", 4, 0});
    EffectRegistry::registerEffect(effect);
    SkillDefinition skill;
    skill.id             = "cast.retired.skill";
    skill.castTime       = 1;
    skill.cooldown       = 0.5f;
    skill.grantedEffects = {effect.id};
    SkillRegistry::registerSkill(skill);
    auto* caster = RPGActor::createActor();
    auto* target = RPGActor::createActor();
    caster->setBaseAttribute("attack", 5);
    caster->learnSkill(skill.id);
    REQUIRE(SkillSystem::beginCast(caster, skill.id, target));
    auto old = caster->skills()->casting.target.entity;
    target->release();
    auto* replacement = RPGActor::createActor();
    REQUIRE_EQ(replacement->id, old.id);
    REQUIRE(replacement->generation != old.generation);
    replacement->setBaseAttribute("attack", 9);
    SkillSystem::update(0.25f);
    REQUIRE(caster->skills()->casting.target.intent == CastTargetIntent::Retired);
    REQUIRE_EQ(caster->skills()->casting.remaining, 0.75f);
    REQUIRE(caster->skills()->casting.target.entity.table == nullptr);
    SkillSystem::update(1.75f);
    REQUIRE(!caster->isCastingSkill());
    REQUIRE_EQ(caster->getFinalAttribute("attack"), 5.0);
    REQUIRE_EQ(replacement->getFinalAttribute("attack"), 9.0);
    REQUIRE_EQ(caster->getSkillCooldown(skill.id), 0.f);
    REQUIRE(caster->skills()->casting.skillId.empty());
    REQUIRE(caster->skills()->casting.target.entity.table == nullptr);
    REQUIRE(events.casts.empty());
    REQUIRE(caster->checkpointJson().ok());
    REQUIRE(SkillSystem::beginCast(caster, skill.id, replacement));
    caster->release();
    SkillSystem::update(2);
    REQUIRE(events.casts.empty());
    REQUIRE_EQ(replacement->getFinalAttribute("attack"), 9.0);
    replacement->release();
    SkillRegistry::remove(skill.id);
    EffectRegistry::remove(effect.id);
}

TEST_CASE("Timed RPG cast rejects foreign worlds before spending and never dereferences a retired foreign table") {
    ecs::Table                             table;
    ecs::ScopedTable                       scope(table);
    eve::rpg::detail::SimulationEvents     events;
    eve::rpg::detail::SimulationEventScope eventScope(events);
    SkillDefinition                        skill;
    skill.id       = "cast.world.skill";
    skill.castTime = 1;
    skill.cooldown = 2;
    skill.cost     = eve::resource::CostSpec::single("mana", 4).takeValue();
    SkillRegistry::registerSkill(skill);
    auto* caster = RPGActor::createActor();
    caster->learnSkill(skill.id);
    caster->setBaseAttribute("mana", 10);
    {
        ecs::Table       foreign;
        ecs::ScopedTable other(foreign);
        auto*            target = RPGActor::createActor();
        std::string      reason;
        REQUIRE(!SkillSystem::beginCast(caster, skill.id, target, &reason));
        REQUIRE_EQ(reason, "target_world_mismatch");
        REQUIRE_EQ(caster->getBaseAttribute("mana"), 10.0);
        REQUIRE_EQ(caster->getSkillCooldown(skill.id), 0.f);
        caster->skills()->casting = {true, skill.id, 1, 1, {CastTargetIntent::Linked, ecs::handle_of(target)}};
    }
    SkillSystem::update(1);
    REQUIRE(!caster->isCastingSkill());
    REQUIRE(events.casts.empty());
    caster->skills()->casting = {true, skill.id, 1, 1, {CastTargetIntent::Retired, {}}};
    SkillSystem::update(1);
    REQUIRE(!caster->isCastingSkill());
    REQUIRE(events.casts.empty());
    caster->release();
    SkillRegistry::remove(skill.id);
}

TEST_CASE("RPG skill clocks ignore invalid deltas clamp cooldowns and preserve explicit self target intent") {
    ecs::Table                             table;
    ecs::ScopedTable                       scope(table);
    eve::rpg::detail::SimulationEvents     events;
    eve::rpg::detail::SimulationEventScope eventScope(events);
    SkillDefinition                        skill;
    skill.id       = "cast.clock.skill";
    skill.castTime = 1;
    skill.cooldown = 0.25f;
    SkillRegistry::registerSkill(skill);
    auto* actor = RPGActor::createActor();
    actor->learnSkill(skill.id);
    REQUIRE(SkillSystem::beginCast(actor, skill.id));
    for (float delta : {0.f, -1.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        SkillSystem::update(delta);
        REQUIRE_EQ(actor->skills()->casting.remaining, 1.f);
        REQUIRE_EQ(actor->getSkillCooldown(skill.id), 0.25f);
        REQUIRE(events.casts.empty());
    }
    SkillSystem::update(2);
    REQUIRE_EQ(actor->getSkillCooldown(skill.id), 0.f);
    REQUIRE(!actor->isCastingSkill());
    REQUIRE(actor->getCastingSkillId().empty());
    REQUIRE_EQ(events.casts.size(), std::size_t(1));
    REQUIRE(events.casts[0].target.presence() == ActorEventPresence::None);
    events.casts.clear();
    REQUIRE(SkillSystem::beginCast(actor, skill.id, actor));
    SkillSystem::update(2);
    REQUIRE_EQ(events.casts.size(), std::size_t(1));
    REQUIRE(eventActor(events.casts[0].target) == actor);
    REQUIRE_EQ(actor->skills()->casting.totalCastTime, 0.f);
    REQUIRE(actor->checkpointJson().ok());
    actor->release();
    SkillRegistry::remove(skill.id);
}

TEST_CASE("RPG skill update excludes actor replacements created by an earlier completion callback") {
    ecs::Table                             table;
    ecs::ScopedTable                       scope(table);
    eve::rpg::detail::SimulationEvents     events;
    eve::rpg::detail::SimulationEventScope eventScope(events);
    EffectDefinition                       effect;
    effect.id             = "cast.callback.effect";
    effect.durationPolicy = "duration";
    effect.duration       = 1;
    EffectRegistry::registerEffect(effect);
    SkillDefinition skill;
    skill.id             = "cast.callback.skill";
    skill.castTime       = 1;
    skill.grantedEffects = {effect.id};
    SkillRegistry::registerSkill(skill);
    auto*      first         = RPGActor::createActor();
    auto*      retired       = RPGActor::createActor();
    const auto retiredHandle = ecs::handle_of(retired);
    for (auto* actor : {first, retired}) {
        actor->learnSkill(skill.id);
        REQUIRE(SkillSystem::beginCast(actor, skill.id));
    }
    RPGActor* replacement = nullptr;
    StatusSystem::registerLifecycleHook("cast.callback", [&](const StatusChangeEvent& event) {
        if (eventActor(event.actor) != first || event.action != "apply" || replacement) return;
        retired->release();
        replacement = RPGActor::createActor();
        replacement->learnSkill(skill.id);
        REQUIRE(SkillSystem::beginCast(replacement, skill.id));
    });
    SkillSystem::update(1);
    REQUIRE(replacement != nullptr);
    REQUIRE_EQ(replacement->id, retiredHandle.id);
    REQUIRE(replacement->generation != retiredHandle.generation);
    REQUIRE_EQ(events.casts.size(), std::size_t(1));
    REQUIRE(eventActor(events.casts[0].caster) == first);
    REQUIRE(replacement->isCastingSkill());
    REQUIRE_EQ(replacement->skills()->casting.remaining, 1.f);
    SkillSystem::update(1);
    REQUIRE_EQ(events.casts.size(), std::size_t(2));
    REQUIRE(eventActor(events.casts[1].caster) == replacement);
    StatusSystem::unregisterLifecycleHook("cast.callback");
    first->release();
    replacement->release();
    SkillRegistry::remove(skill.id);
    EffectRegistry::remove(effect.id);
}

TEST_CASE("RPG cast completion pins skill facts across lifecycle content replacement") {
    ecs::Table                             table;
    ecs::ScopedTable                       scope(table);
    eve::rpg::detail::SimulationEvents     events;
    eve::rpg::detail::SimulationEventScope eventScope(events);
    EffectDefinition                       first;
    first.id             = "cast.pin.first";
    first.durationPolicy = "duration";
    first.duration       = 2;
    first.modifiers.push_back({"attack", "add", 1, 0});
    auto second               = first;
    second.id                 = "cast.pin.second";
    second.modifiers[0].value = 2;
    auto third                = first;
    third.id                  = "cast.pin.replacement";
    third.modifiers[0].value  = 100;
    for (const auto& effect : {first, second, third}) EffectRegistry::registerEffect(effect);
    SkillDefinition skill;
    skill.id             = "cast.pin.skill";
    skill.castTime       = 1;
    skill.grantedEffects = {first.id, second.id};
    SkillRegistry::registerSkill(skill);
    auto* actor = RPGActor::createActor();
    actor->setBaseAttribute("attack", 5);
    actor->learnSkill(skill.id);
    REQUIRE(SkillSystem::beginCast(actor, skill.id));
    StatusSystem::registerLifecycleHook("cast.pin", [&](const StatusChangeEvent& event) {
        if (eventActor(event.actor) != actor || event.effectId != first.id || event.action != "apply") return;
        SkillRegistry::remove(skill.id);
        auto replacement           = skill;
        replacement.grantedEffects = {third.id};
        SkillRegistry::registerSkill(replacement);
    });
    SkillSystem::update(1);
    REQUIRE_EQ(actor->getFinalAttribute("attack"), 8.0);
    REQUIRE_EQ(actor->getStatusCount(), 2);
    REQUIRE_EQ(events.casts.size(), std::size_t(1));
    REQUIRE_EQ(events.casts[0].skillId, skill.id);
    StatusSystem::unregisterLifecycleHook("cast.pin");
    actor->release();
    SkillRegistry::remove(skill.id);
    for (const auto& effect : {first, second, third}) EffectRegistry::remove(effect.id);
}

TEST_CASE("RPG cast lifecycle target retirement suppresses remaining effects and stale event publication") {
    ecs::Table                             table;
    ecs::ScopedTable                       scope(table);
    eve::rpg::detail::SimulationEvents     events;
    eve::rpg::detail::SimulationEventScope eventScope(events);
    EffectDefinition                       first;
    first.id             = "cast.recipient.first";
    first.durationPolicy = "duration";
    first.duration       = 2;
    first.modifiers.push_back({"attack", "add", 1, 0});
    auto second               = first;
    second.id                 = "cast.recipient.second";
    second.modifiers[0].value = 2;
    EffectRegistry::registerEffect(first);
    EffectRegistry::registerEffect(second);
    SkillDefinition skill;
    skill.id             = "cast.recipient.skill";
    skill.grantedEffects = {first.id, second.id};
    SkillRegistry::registerSkill(skill);
    auto* caster = RPGActor::createActor();
    auto* target = RPGActor::createActor();
    caster->learnSkill(skill.id);
    const auto oldTarget   = ecs::handle_of(target);
    RPGActor*  replacement = nullptr;
    StatusSystem::registerLifecycleHook("cast.recipient", [&](const StatusChangeEvent& event) {
        if (eventActor(event.actor) != target || event.effectId != first.id || event.action != "apply" || replacement)
            return;
        target->release();
        replacement = RPGActor::createActor();
        replacement->setBaseAttribute("attack", 9);
    });
    REQUIRE(SkillSystem::beginCast(caster, skill.id, target));
    REQUIRE(replacement != nullptr);
    REQUIRE_EQ(replacement->id, oldTarget.id);
    REQUIRE(replacement->generation != oldTarget.generation);
    REQUIRE_EQ(replacement->getFinalAttribute("attack"), 9.0);
    REQUIRE_EQ(caster->getStatusCount(), 0);
    REQUIRE(events.casts.empty());
    StatusSystem::unregisterLifecycleHook("cast.recipient");
    caster->release();
    replacement->release();
    SkillRegistry::remove(skill.id);
    EffectRegistry::remove(first.id);
    EffectRegistry::remove(second.id);
}

TEST_CASE("Settlement stages execute by priority and cancellation prevents later mutation") {
    const std::string pipeline = "rewrite.rpg.settlement";
    SettlementPipeline::clearPipeline(pipeline);
    std::vector<std::string> order;
    SettlementPipeline::registerStage(pipeline, "later", 10, [&](SettlementContext&) { order.push_back("later"); });
    SettlementPipeline::registerStage(pipeline, "first", 0, [&](SettlementContext&) { order.push_back("first"); });
    SettlementContext context;
    SettlementPipeline::run(pipeline, context);
    REQUIRE_EQ(order.size(), 2u);
    CHECK_EQ(order[0], "first");
    CHECK_EQ(order[1], "later");
    SettlementPipeline::clearPipeline(pipeline);
    SettlementPipeline::registerStage(pipeline, "cancel", 0, [](SettlementContext& ctx) { ctx.cancelled = true; });
    SettlementPipeline::registerStage(pipeline, "later", 10,
                                      [&](SettlementContext&) { order.push_back("unexpected"); });
    SettlementPipeline::run(pipeline, context);
    CHECK(context.cancelled);
    CHECK_EQ(order.size(), 2u);
    SettlementPipeline::clearPipeline(pipeline);
}

TEST_CASE("RPG effect codec round trips detached facts without consulting custom runtime implementations") {
    EffectDefinition original;
    original.id             = "codec.original";
    original.durationPolicy = "duration";
    original.duration       = 1.5f;
    original.period         = 0.25f;
    original.stackPolicy    = "codec.custom.stack";
    original.maxStacks      = 7;
    original.modifiers.push_back({"attack", "codec.custom.op", 2.5, -3});
    original.tags               = {"buff", "fire"};
    original.extra              = {{"icon", "ui/fire.png"}, {"sound", "fire.wav"}};
    const auto definitions      = EffectRegistry::count();
    auto       value            = encodeEffectDefinition(original).expect("owning effect source");
    auto       text             = value.toJson().expect("canonical bytes");
    original.modifiers[0].value = 999;
    original.extra.clear();
    auto parsed = eve::Value::fromJson(text).expect("source parse");
    parsed.set("futureField", true);
    parsed.find("modifiers")->at(0).set("futureField", true);
    auto restored = decodeEffectDefinition(parsed).expect("pure canonical admission");
    REQUIRE_EQ(restored.modifiers.size(), std::size_t(1));
    CHECK_EQ(restored.modifiers[0].value, 2.5);
    CHECK_EQ(restored.modifiers[0].priority, -3);
    CHECK_EQ(restored.duration, 1.5f);
    CHECK_EQ(restored.period, 0.25f);
    CHECK_EQ(restored.maxStacks, 7);
    CHECK_EQ(restored.extra.at("icon"), std::string("ui/fire.png"));
    CHECK(restored.hasTag("fire"));
    CHECK_EQ(encodeEffectDefinition(restored).expect("canonical projection").toJson().expect("bytes"), text);
    CHECK_EQ(EffectRegistry::count(), definitions);
    CHECK(!StatusSystem::hasStackPolicy("codec.custom.stack"));
    CHECK(!AttributeSystem::customOps().has("codec.custom.op"));
}
TEST_CASE("RPG effect codec and legacy import reject malformed replacements without losing live definitions") {
    EffectDefinition stable;
    stable.id             = "codec.stable";
    stable.durationPolicy = "infinite";
    stable.modifiers.push_back({"attack", "add", 3, 0});
    EffectRegistry::registerEffect(stable);
    auto valid   = encodeEffectDefinition(stable).expect("valid source");
    auto reject  = [&](eve::Value value) { REQUIRE(!decodeEffectDefinition(value).ok()); };
    auto invalid = valid;
    invalid.set("duration", -1);
    reject(invalid);
    invalid = valid;
    invalid.set("duration", "1");
    reject(invalid);
    invalid = valid;
    invalid.set("period", -1);
    reject(invalid);
    invalid = valid;
    invalid.set("durationPolicy", "unknown");
    reject(invalid);
    invalid = valid;
    invalid.set("maxStacks", 1.5);
    reject(invalid);
    invalid = valid;
    invalid.set("maxStacks", -1);
    reject(invalid);
    invalid = valid;
    invalid.set("modifiers", eve::Value::array({eve::Value::object({{"attribute", ""}})}));
    reject(invalid);
    invalid = valid;
    invalid.find("modifiers")->at(0).set("priority", std::int64_t(std::numeric_limits<int>::max()) + 1);
    reject(invalid);
    invalid = valid;
    invalid.set("tags", eve::Value::array({1}));
    reject(invalid);
    invalid = valid;
    invalid.set("extra", eve::Value::object({{"bad", true}}));
    reject(invalid);
    reject(eve::Value(7));
    auto nonfinite               = stable;
    nonfinite.modifiers[0].value = std::numeric_limits<double>::infinity();
    REQUIRE(!encodeEffectDefinition(nonfinite).ok());
    std::string error;
    REQUIRE_EQ(EffectRegistry::loadFromJson(R"({"id":"codec.stable","maxStacks":-1})", &error), 0);
    CHECK(!error.empty());
    REQUIRE(EffectRegistry::find(stable.id) != nullptr);
    CHECK_EQ(EffectRegistry::find(stable.id)->modifiers[0].value, 3.0);
    CHECK(EffectRegistry::remove(stable.id));
}
TEST_CASE("RPG canonical effect facts feed real status application and missing custom bindings reject unchanged") {
    ecs::Table       table;
    ecs::ScopedTable current(table);
    auto*            actor = RPGActor::createActor();
    actor->setBaseAttribute("attack", 7);
    auto source = eve::Value::fromJson(R"({"id":"codec.status","durationPolicy":"duration","duration":2,
        "modifiers":[{"attribute":"attack","op":"add","value":3}]})")
                      .expect("effect fields");
    auto effect = decodeEffectDefinition(source).expect("canonical definition");
    EffectRegistry::registerEffect(effect);
    auto applied = StatusSystem::apply(actor, effect.id).expect("real domain apply");
    REQUIRE(applied > 0);
    CHECK_EQ(actor->getFinalAttribute("attack"), 10.0);
    StatusSystem::remove(actor, applied).expect("canonical cleanup");
    REQUIRE_EQ(actor->getFinalAttribute("attack"), 7.0);
    source.set("stackPolicy", "codec.absent.stack");
    auto missing = decodeEffectDefinition(source).expect("unbound shape remains pure");
    EffectRegistry::registerEffect(missing);
    const auto events = actor->statuses()->container.eventCount();
    REQUIRE(!StatusSystem::apply(actor, effect.id).ok());
    CHECK_EQ(actor->getFinalAttribute("attack"), 7.0);
    CHECK_EQ(actor->statuses()->container.effectCount(), std::size_t(0));
    CHECK_EQ(actor->statuses()->container.eventCount(), events);
    source.set("stackPolicy", "none");
    source.find("modifiers")->at(0).set("op", "codec.absent.op");
    EffectRegistry::registerEffect(decodeEffectDefinition(source).expect("unbound modifier shape"));
    REQUIRE(!StatusSystem::apply(actor, effect.id).ok());
    CHECK_EQ(actor->statuses()->container.effectCount(), std::size_t(0));
    CHECK_EQ(actor->getFinalAttribute("attack"), 7.0);
    int  evaluations = 0;
    auto registered  = AttributeSystem::customOps().registerVersionedOperation("codec.absent.op", "codec.op:v1",
                                                                               [&](double value, double modifier) {
                                                                                  ++evaluations;
                                                                                  return value + modifier;
                                                                              });
    REQUIRE(registered.ok());
    REQUIRE_EQ(AttributeSystem::customOps().implementationRevision("codec.absent.op").expect("executor identity"),
               "codec.op:v1");
    auto rebound = decodeEffectDefinition(source).expect("shape parsing with implementation present");
    CHECK_EQ(evaluations, 0);
    EffectRegistry::registerEffect(rebound);
    auto custom = StatusSystem::apply(actor, effect.id).expect("actual custom operation binding");
    REQUIRE(custom > 0);
    CHECK_EQ(actor->getFinalAttribute("attack"), 10.0);
    CHECK(evaluations > 0);
    StatusSystem::remove(actor, custom).expect("custom modifier cleanup");
    AttributeSystem::customOps().unregisterOperation("codec.absent.op");
    CHECK_EQ(actor->getFinalAttribute("attack"), 7.0);
    REQUIRE(EffectRegistry::remove(effect.id));
    ecs::DestroyEntity(actor);
}
}  // namespace test_rpg_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_actor_checkpoint)
#include <limits>
#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "common/StateAccess.h"
#include "common/Value.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "rpg/GameState.h"
#include "rpg/RPGActor.h"
#include "rpg/RPGSaveSession.h"
#include "rpg/Skill.h"
#include "rpg/Tracker.h"
namespace test_rpg_actor_checkpoint_contract {
using namespace eve::rpg;

TEST_CASE("Actor checkpoint restores attributes vitals and progression while rejecting malformed replacement") {
    SkillDefinition skill;
    skill.id = "rewrite.rpg.checkpoint.skill";
    SkillRegistry::registerSkill(skill);
    auto* source   = RPGActor::createActor();
    auto* restored = RPGActor::createActor();
    REQUIRE(source != nullptr);
    REQUIRE(restored != nullptr);
    source->setBaseAttribute("attack", 18);
    source->setBaseAttribute("hp", 100);
    source->setCurrent("hp", 47);
    source->learnSkill(skill.id);
    source->setSkillCooldown(skill.id, 2.75f);
    REQUIRE(source->restoreProgression(4, 25, 120).ok());
    const auto snapshot = source->checkpointJson();
    REQUIRE(snapshot.ok());
    restored->setBaseAttribute("attack", 1);
    restored->setBaseAttribute("hp", 100);
    REQUIRE(restored->restoreCheckpointJson(snapshot.value()).ok());
    CHECK_EQ(restored->getBaseAttribute("attack"), 18.0);
    CHECK_EQ(restored->getCurrent("hp"), 47.0);
    CHECK_EQ(restored->getLevel(), 4);
    CHECK(restored->knowsSkill(skill.id));
    REQUIRE_EQ(restored->getSkillCooldown(skill.id), 2.75f);
    const auto rejected = restored->restoreCheckpointJson(
        R"({"schema":"eve.rpg.actor-checkpoint","version":1,"baseAttributes":[],"class":{"id":"","skillsSyncedUpTo":0},"learnedSkills":[],"progression":{"level":0,"xp":0,"xpToNext":1},"traits":[],"vitals":{}})");
    CHECK(!rejected.ok());
    CHECK_EQ(restored->getBaseAttribute("attack"), 18.0);
    CHECK_EQ(restored->getLevel(), 4);
    source->release();
    restored->release();
    SkillRegistry::remove(skill.id);
}

TEST_CASE("Actor checkpoint preserves explicit self cast timing and event intent") {
    SkillDefinition skill;
    skill.id       = "rewrite.rpg.checkpoint.channel";
    skill.castTime = 2;
    SkillRegistry::registerSkill(skill);
    auto* actor = RPGActor::createActor();
    REQUIRE(actor != nullptr);
    actor->learnSkill(skill.id);
    REQUIRE(actor->beginCastSkill(skill.id, actor));
    SkillSystem::update(0.25f);
    auto saved = actor->checkpointJson().expect("active explicit-self cast");
    actor->cancelCastSkill();
    actor->restoreCheckpointJson(saved).expect("restore explicit-self cast");
    REQUIRE(actor->skills()->casting.target.intent == CastTargetIntent::Linked);
    REQUIRE_EQ(actor->skills()->casting.target.entity.id, actor->id);
    REQUIRE_EQ(actor->skills()->casting.remaining, 1.75f);
    eve::rpg::detail::SimulationEvents     events;
    eve::rpg::detail::SimulationEventScope scope(events);
    SkillSystem::update(2.f);
    REQUIRE_EQ(events.casts.size(), std::size_t(1));
    REQUIRE(eventActor(events.casts.front().target) == actor);
    CHECK(actor->checkpointJson().ok());
    actor->release();
    SkillRegistry::remove(skill.id);
}
TEST_CASE("Actor active cast checkpoint validates stable bindings clocks and retirement without partial writes") {
    using namespace eve;
    SkillDefinition skill;
    skill.id       = "actor.cast.checkpoint";
    skill.castTime = 2;
    SkillRegistry::registerSkill(skill);
    ecs::Table        source;
    std::string       json;
    ecs::EntityHandle foreign;
    const auto subject = SubjectRef::fromPersistentId(*PersistentId::parse("01900000-0000-7000-8000-000000000099"));
    {
        ecs::ScopedTable scope(source);
        auto*            caster = RPGActor::createActor();
        auto*            target = RPGActor::createActor();
        bindActorIdentity(*target, subject).expect("source target registration");
        caster->learnSkill(skill.id);
        REQUIRE(SkillSystem::beginCast(caster, skill.id, target));
        SkillSystem::update(0.25f);
        REQUIRE(!caster->checkpointJson().ok());
        auto wrong = CastTargetBinding{subject, ecs::handle_of(caster)};
        REQUIRE(!caster->checkpointJson(wrong).ok());
        auto mismatched = CastTargetBinding{SubjectRef::fromPersistentId(subject.persistentId().child("wrong")),
                                            ecs::handle_of(target)};
        REQUIRE(!caster->checkpointJson(mismatched).ok());
        json    = caster->checkpointJson(CastTargetBinding{subject, ecs::handle_of(target)}).expect("linked file");
        foreign = ecs::handle_of(target);
        target->release();
        auto retired = caster->checkpointJson().expect("retired link captured without retargeting");
        SkillSystem::cancelCast(caster);
        caster->restoreCheckpointJson(retired).expect("restore retired intent");
        REQUIRE(caster->skills()->casting.target.intent == CastTargetIntent::Retired);
        eve::rpg::detail::SimulationEvents     events;
        eve::rpg::detail::SimulationEventScope eventScope(events);
        SkillSystem::update(2.f);
        REQUIRE(events.casts.empty());
        caster->release();
    }
    ecs::Table       destination;
    ecs::ScopedTable scope(destination);
    auto*            caster = RPGActor::createActor();
    auto*            target = RPGActor::createActor();
    caster->learnSkill(skill.id);
    const auto before = caster->checkpointJson().takeValue();
    bindActorIdentity(*target, subject).expect("destination target registration");
    const CastTargetBinding binding{subject, ecs::handle_of(target)};
    REQUIRE(!caster->restoreCheckpointJson(json).ok());
    auto wrong    = binding;
    wrong.subject = SubjectRef::fromPersistentId(subject.persistentId().child("wrong"));
    REQUIRE(!caster->restoreCheckpointJson(json, wrong).ok());
    wrong        = binding;
    wrong.entity = foreign;
    REQUIRE(!caster->restoreCheckpointJson(json, wrong).ok());
    auto* otherLive = RPGActor::createActor();
    bindActorIdentity(*otherLive, SubjectRef::fromPersistentId(subject.persistentId().child("other")))
        .expect("different live logical actor");
    wrong        = binding;
    wrong.entity = ecs::handle_of(otherLive);
    REQUIRE(!caster->restoreCheckpointJson(json, wrong).ok());
    REQUIRE_EQ(caster->checkpointJson().takeValue(), before);
    otherLive->release();
    auto* temporary = RPGActor::createActor();
    wrong           = binding;
    wrong.entity    = ecs::handle_of(temporary);
    temporary->release();
    auto* reused = RPGActor::createActor();
    REQUIRE_EQ(reused->id, wrong.entity.id);
    REQUIRE(!caster->restoreCheckpointJson(json, wrong).ok());
    REQUIRE_EQ(caster->checkpointJson().takeValue(), before);
    reused->release();
    const auto value  = Value::fromJson(json).takeValue();
    auto       reject = [&](Value bad) {
        REQUIRE(!caster->restoreCheckpointJson(bad.toJson().takeValue(), binding).ok());
        REQUIRE_EQ(caster->checkpointJson().takeValue(), before);
    };
    auto bad = value;
    bad.find("casting")->set("unknown", 0);
    reject(std::move(bad));
    for (double time : {-1., 0., 0.1, 3., 1e100}) {
        bad = value;
        bad.find("casting")->set("remaining", time);
        reject(std::move(bad));
    }
    bad = value;
    bad.find("casting")->find("target")->set("kind", "unknown");
    reject(std::move(bad));
    bad = value;
    bad.find("casting")->find("target")->set("subject", "not-an-id");
    reject(std::move(bad));
    caster->restoreCheckpointJson(json, binding).expect("bind fresh target incarnation");
    REQUIRE_EQ(caster->skills()->casting.remaining, 1.75f);
    REQUIRE(caster->skills()->casting.target.entity.table == &destination);
    REQUIRE_EQ(caster->skills()->casting.target.entity.id, target->id);
    eve::rpg::detail::SimulationEvents     events;
    eve::rpg::detail::SimulationEventScope eventScope(events);
    SkillSystem::update(2.f);
    REQUIRE_EQ(events.casts.size(), std::size_t(1));
    REQUIRE(eventActor(events.casts.front().target) == target);
    caster->release();
    target->release();
    SkillRegistry::remove(skill.id);
}
TEST_CASE("Actor checkpoint v2 validates cooldown roster atomically and migrates legacy v1") {
    SkillDefinition skill;
    skill.id = "spawn.checkpoint.cooldown";
    SkillRegistry::registerSkill(skill);
    auto* actor = RPGActor::createActor();
    REQUIRE(actor != nullptr);
    actor->learnSkill(skill.id);
    actor->setSkillCooldown(skill.id, 3.25f);
    const auto text  = actor->checkpointJson().expect("v2 capture");
    const auto valid = eve::Value::fromJson(text).expect("checkpoint value");
    REQUIRE_EQ(valid.find("version")->asInt(), 5);
    auto legacy4 = valid;
    legacy4.set("version", 4);
    legacy4.getIf<eve::Value::Object>()->erase("executionOrder");
    const auto existingOrder     = *ActorExecutionKey::parse("01900000-0000-7000-8000-000000000009");
    actor->executionOrder()->key = existingOrder;
    actor->restoreCheckpointJson(legacy4.toJson().expect("v4 JSON")).expect("v4 preserves preconfigured order");
    REQUIRE(actor->executionOrder()->key == existingOrder);
    actor->executionOrder()->key.reset();
    auto legacy3 = valid;
    legacy3.set("version", 3);
    legacy3.getIf<eve::Value::Object>()->erase("casting");
    legacy3.getIf<eve::Value::Object>()->erase("executionOrder");
    actor->restoreCheckpointJson(legacy3.toJson().expect("v3 JSON")).expect("v3 explicit inactive migration");
    REQUIRE(!actor->skills()->casting.active);
    REQUIRE_EQ(actor->getSkillCooldown(skill.id), 3.25f);
    auto*      raw                     = RPGActor::create();
    const auto queriesBeforeRawRestore = eve::cap::listenerCount<eve::IStateQuery>();
    REQUIRE(!raw->checkpointJson().ok());
    REQUIRE(!raw->restoreCheckpointJson(text).ok());
    REQUIRE_EQ(eve::cap::listenerCount<eve::IStateQuery>(), queriesBeforeRawRestore);
    REQUIRE(!raw->attributes().has());
    raw->release();
    auto reject = [&](eve::Value bad) {
        REQUIRE(!actor->restoreCheckpointJson(bad.toJson().expect("corrupt JSON")).ok());
        REQUIRE_EQ(actor->getSkillCooldown(skill.id), 3.25f);
    };
    auto bad = valid;
    bad.set("cooldowns", eve::Value::array({}));
    reject(std::move(bad));
    for (double seconds : {-1.0, 0.1, 1e100}) {
        bad = valid;
        bad.find("cooldowns")->at(0).set("remainingSeconds", seconds);
        reject(std::move(bad));
    }
    bad = valid;
    bad.find("cooldowns")->at(0).set("id", "missing");
    reject(std::move(bad));
    bad = valid;
    bad.set("version", 6);
    reject(std::move(bad));
    for (const auto& order :
         {eve::Value("invalid-order"), eve::Value("00000000-0000-0000-0000-000000000000"), eve::Value(1)}) {
        bad = valid;
        bad.set("executionOrder", order);
        reject(std::move(bad));
    }
    SkillDefinition secondSkill;
    secondSkill.id = "spawn.checkpoint.second";
    SkillRegistry::registerSkill(secondSkill);
    bad = valid;
    bad.set("learnedSkills", eve::Value::array({skill.id, secondSkill.id}));
    bad.set("cooldowns", eve::Value::array({valid.find("cooldowns")->at(0), valid.find("cooldowns")->at(0)}));
    reject(std::move(bad));
    for (float remaining : {0.1f, std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::max()}) {
        actor->setSkillCooldown(skill.id, remaining);
        const auto precise = actor->checkpointJson().expect("float cooldown capture");
        actor->setSkillCooldown(skill.id, 0.f);
        REQUIRE(actor->restoreCheckpointJson(precise).ok());
        REQUIRE_EQ(actor->getSkillCooldown(skill.id), remaining);
    }
    actor->setSkillCooldown(skill.id, 3.25f);
    bad = valid;
    bad.find("progression")->set("level", std::numeric_limits<std::int64_t>::max());
    reject(std::move(bad));
    bad = valid;
    bad.find("class")->set("skillsSyncedUpTo", std::numeric_limits<std::int64_t>::max());
    reject(std::move(bad));
    auto              legacy = valid;
    eve::Value::Array bases;
    auto              originalAttributes =
        eve::attributes::AttributeSet::prepareState(*valid.find("attributes")).expect("v3 canonical attributes");
    for (int i = 0; i < originalAttributes.view().attributeCount(); ++i) {
        const auto id = originalAttributes.view().attributeAt(i);
        bases.push_back(eve::Value::object({{"id", id}, {"value", originalAttributes.view().getBase(id)}}));
    }
    legacy.set("baseAttributes", eve::Value(std::move(bases)));
    legacy.getIf<eve::Value::Object>()->erase("attributes");
    legacy.getIf<eve::Value::Object>()->erase("statuses");
    auto legacy2 = legacy;
    legacy2.set("version", 2);
    actor->setSkillCooldown(skill.id, 0.f);
    REQUIRE(actor->restoreCheckpointJson(legacy2.toJson().expect("v2 JSON")).ok());
    REQUIRE_EQ(actor->getSkillCooldown(skill.id), 3.25f);
    legacy.set("version", 1);
    legacy.getIf<eve::Value::Object>()->erase("cooldowns");
    REQUIRE(actor->restoreCheckpointJson(legacy.toJson().expect("v1 JSON")).ok());
    REQUIRE_EQ(actor->getSkillCooldown(skill.id), 0.f);
    actor->setSkillCooldown(skill.id, -1.f);
    REQUIRE(!actor->checkpointJson().ok());
    actor->setSkillCooldown(skill.id, std::numeric_limits<float>::quiet_NaN());
    REQUIRE(!actor->checkpointJson().ok());
    actor->release();
    SkillRegistry::remove(skill.id);
    SkillRegistry::remove(secondSkill.id);
}
TEST_CASE("Actor checkpoint cooldown identity follows canonical save session content migration") {
    SkillDefinition oldSkill;
    oldSkill.id       = "spawn.save.old";
    oldSkill.castTime = 2;
    SkillRegistry::registerSkill(oldSkill);
    auto* source = RPGActor::createActor();
    auto* target = RPGActor::createActor();
    REQUIRE(source != nullptr);
    REQUIRE(target != nullptr);
    source->learnSkill(oldSkill.id);
    REQUIRE(SkillSystem::beginCast(source, oldSkill.id));
    SkillSystem::update(0.25f);
    source->setSkillCooldown(oldSkill.id, 1.5f);
    GameState                    state;
    Tracker                      tracker;
    eve::inventory::Bag          bag(2);
    eve::inventory::EquipmentSet equipment;
    RPGSaveSession               original;
    original.bind(state, tracker, *source, bag, equipment);
    original.setContentVersion("cooldown.v1").expect("source content");
    auto snapshot = original.snapshotJson().expect("save session capture");
    SkillRegistry::remove(oldSkill.id);
    SkillDefinition currentSkill;
    currentSkill.id = "spawn.save.new";
    SkillRegistry::registerSkill(currentSkill);
    RPGSaveSession migrated;
    migrated.bind(state, tracker, *target, bag, equipment);
    migrated.setContentVersion("cooldown.v2").expect("destination content");
    migrated.addIdRenameMigration("cooldown.v1", RPGSaveIdDomain::Skill, oldSkill.id, currentSkill.id)
        .expect("rename route");
    const auto queries   = eve::cap::listenerCount<eve::IStateQuery>();
    const auto mutations = eve::cap::listenerCount<eve::IStateMutation>();
    {
        auto cancelled = migrated.prepareRestoreSnapshotJson(snapshot).expect("unpublished migrated candidate");
        REQUIRE_EQ(eve::cap::listenerCount<eve::IStateQuery>(), queries);
        REQUIRE_EQ(eve::cap::listenerCount<eve::IStateMutation>(), mutations);
        REQUIRE(!target->knowsSkill(currentSkill.id));
    }
    auto prepared = migrated.prepareRestoreSnapshotJson(snapshot).expect("prepare migrated cooldown");
    REQUIRE_EQ(eve::cap::listenerCount<eve::IStateQuery>(), queries);
    REQUIRE_EQ(eve::cap::listenerCount<eve::IStateMutation>(), mutations);
    migrated.commitPrepared(std::move(prepared));
    REQUIRE_EQ(eve::cap::listenerCount<eve::IStateQuery>(), queries);
    REQUIRE_EQ(eve::cap::listenerCount<eve::IStateMutation>(), mutations);
    REQUIRE(target->knowsSkill(currentSkill.id));
    REQUIRE(!target->knowsSkill(oldSkill.id));
    REQUIRE_EQ(target->getSkillCooldown(currentSkill.id), 1.5f);
    REQUIRE(target->skills()->casting.active);
    REQUIRE_EQ(target->skills()->casting.skillId, currentSkill.id);
    REQUIRE_EQ(target->skills()->casting.remaining, 1.75f);
    REQUIRE(target->skills()->casting.target.intent == CastTargetIntent::DefaultSelf);
    source->release();
    target->release();
    SkillRegistry::remove(currentSkill.id);
}
}  // namespace test_rpg_actor_checkpoint_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_battle)
#include <cstdint>
#include <vector>
#include "PrivateTestScope.h"
#include "rpg/Battle.h"
#include "rpg/BattleSystem.h"
#include "rpg/RPGActor.h"
#include "rpg/SettlementAdapter.h"
#include "rpg/VitalsSystem.h"
namespace test_rpg_battle_contract {
using namespace eve::rpg;
namespace {
RPGActor* fighter(double attack, double defense, double hp, double speed) {
    auto* actor = RPGActor::createActor();
    actor->setBaseAttribute("attack", attack);
    actor->setBaseAttribute("defense", defense);
    actor->setBaseAttribute("hp", hp);
    actor->setBaseAttribute("speed", speed);
    actor->setCurrent("hp", hp);
    return actor;
}
}  // namespace

TEST_CASE("Damage formula evaluates actor attributes and rejects malformed expressions") {
    auto*      attacker = fighter(10, 0, 100, 5);
    auto*      target   = fighter(0, 2, 100, 1);
    const auto value    = BattleSystem::evaluateFormulaChecked("a.attack * 4 - b.defense * 2", attacker, target);
    REQUIRE(value.ok());
    CHECK_EQ(value.value(), 36.0);
    const auto unary = BattleSystem::evaluateFormulaChecked("-(a.attack - b.defense)", attacker, target);
    REQUIRE(unary.ok());
    CHECK_EQ(unary.value(), -8.0);
    SUB_CASE("missing close parenthesis is rejected") {
        CHECK(!BattleSystem::evaluateFormulaChecked("(a.attack + 2", attacker, target).ok());
    };
    SUB_CASE("trailing tokens are rejected") {
        CHECK(!BattleSystem::evaluateFormulaChecked("a.attack garbage", attacker, target).ok());
    };
    SUB_CASE("division by zero is rejected") {
        CHECK(!BattleSystem::evaluateFormulaChecked("a.attack / 0", attacker, target).ok());
    };
    attacker->release();
    target->release();
}

TEST_CASE("Invalid damage registration retains the previously published skill rule") {
    BattleSystem::clearSkillDamage();
    SkillDamageSpec valid{"hp", "a.attack / b.defense", "", 0, 100};
    REQUIRE(BattleSystem::registerSkillDamageChecked("rewrite.rpg.strike", valid).ok());
    SkillDamageSpec invalid{"hp", "a.attack + )", "", 0, 100};
    CHECK(!BattleSystem::registerSkillDamageChecked("rewrite.rpg.strike", invalid).ok());
    const auto* retained = BattleSystem::findSkillDamage("rewrite.rpg.strike");
    REQUIRE(retained != nullptr);
    CHECK_EQ(retained->formula, std::string("a.attack / b.defense"));
    BattleSystem::clearSkillDamage();
}

TEST_CASE("Battle resolves legal actions into damage victory and life state") {
    auto*  hero  = fighter(30, 0, 100, 10);
    auto*  enemy = fighter(5, 0, 20, 1);
    Battle battle;
    battle.addActor(hero, BattleSide::Party);
    battle.addActor(enemy, BattleSide::Enemies);
    REQUIRE(battle.setActionChecked(hero, "", enemy).ok());
    battle.autoEnemyActions();
    battle.startRound();
    REQUIRE(battle.executeNextAction());
    REQUIRE_EQ(VitalsSystem::getCurrent(enemy, "hp"), 0.0);
    CHECK_EQ(VitalsSystem::getMax(enemy, "hp"), 20.0);
    CHECK(battle.isFinished());
    CHECK(battle.isVictory());
    CHECK(!battle.isActorAlive(enemy));
    battle.pollEvents();
    bool damage  = false;
    bool victory = false;
    for (int i = 0; i < battle.getEventCount(); ++i) {
        const auto event = battle.getEvent(i);
        damage |= event.action == "damage";
        victory |= event.action == "victory";
    }
    CHECK(damage);
    REQUIRE(victory);
    hero->release();
    enemy->release();
}

TEST_CASE("Battle damage passes through mitigation rules before changing vitals") {
    auto*                           hero  = fighter(20, 0, 100, 10);
    auto*                           enemy = fighter(0, 0, 100, 1);
    eve::settlement::SettlementRule mitigation;
    mitigation.id                  = "rewrite.rpg.guard";
    mitigation.source              = "effect:guard";
    mitigation.stage               = eve::settlement::StageKind::TargetMitigation;
    mitigation.operation           = eve::settlement::RuleOperation::ResistPercent;
    mitigation.value               = .5;
    mitigation.filter.kinds        = {"damage"};
    mitigation.filter.requiredTags = {"rpg:damage"};
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({mitigation}).ok());
    Battle battle;
    REQUIRE(battle.configureSettlementRules(rules).ok());
    battle.addActor(hero, BattleSide::Party);
    battle.addActor(enemy, BattleSide::Enemies);
    battle.setAction(hero, "", enemy);
    battle.startRound();
    REQUIRE(battle.executeNextAction());
    CHECK_EQ(enemy->getCurrent("hp"), 90.0);
    std::vector<VitalsEvent> events;
    VitalsSystem::pollEvents(events);
    REQUIRE(!events.empty());
    CHECK_EQ(events.front().amount, 10.0);
    hero->release();
    enemy->release();
}
}  // namespace test_rpg_battle_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_class)
#include "PrivateTestScope.h"
#include "rpg/Class.h"
#include "rpg/RPG.h"
#include "rpg/RPGActor.h"
namespace test_rpg_class_contract {
using namespace eve::rpg;

TEST_CASE("Class catalogue loads level gated skills and class change replaces trait effects") {
    auto* rpg = RPG::create();
    rpg->clearClassDefinitions();
    rpg->clearSkillDefinitions();
    rpg->clearTraitDefinitions();
    const int loadedSkills = rpg->registerSkillsFromJson(R"([{"id":"rewrite.rpg.strike"},{"id":"rewrite.rpg.fire"}])");
    REQUIRE_EQ(loadedSkills, 2);
    const int loadedTraits = rpg->registerTraitsFromJson(
        R"([{"id":"rewrite.rpg.mighty","traits":[{"kind":"paramRate","target":"attack","value":1.5}]}])");
    REQUIRE_EQ(loadedTraits, 1);
    const int loadedClasses = rpg->registerClassesFromJson(R"([
      {"id":"rewrite.rpg.warrior","traits":["rewrite.rpg.mighty"],"learnSkills":[{"skillId":"rewrite.rpg.strike","level":2}]},
      {"id":"rewrite.rpg.mage","learnSkills":[{"skillId":"rewrite.rpg.fire","level":1}]}
    ])");
    REQUIRE_EQ(loadedClasses, 2);
    auto* actor = rpg->newActor();
    actor->setBaseAttribute("attack", 100);
    REQUIRE(actor->setClass("rewrite.rpg.warrior"));
    CHECK_EQ(actor->getFinalAttribute("attack"), 150.0);
    CHECK(!actor->knowsSkill("rewrite.rpg.strike"));
    REQUIRE(actor->restoreProgression(2, 0, 10).ok());
    const int learnedOnLevelUp = actor->checkLevelSkills();
    CHECK_EQ(learnedOnLevelUp, 1);
    CHECK(actor->knowsSkill("rewrite.rpg.strike"));
    const int learnedAgain = actor->checkLevelSkills();
    CHECK_EQ(learnedAgain, 0);
    REQUIRE(actor->setClass("rewrite.rpg.mage"));
    CHECK_EQ(actor->getFinalAttribute("attack"), 100.0);
    CHECK(actor->knowsSkill("rewrite.rpg.fire"));
    actor->release();
    rpg->clearClassDefinitions();
    rpg->clearTraitDefinitions();
    rpg->clearSkillDefinitions();
}
}  // namespace test_rpg_class_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_gamestate)
#include "PrivateTestScope.h"
#include "common/Value.h"
#include "rpg/GameState.h"
#include "rpg/RPG.h"
namespace test_rpg_game_state_contract {
using namespace eve::rpg;

TEST_CASE("Game state keeps global switches variables and per subject values in distinct scopes") {
    GameState state;
    CHECK(!state.isSwitchOn("door"));
    state.switchOn("door");
    state.setVariable("gold", 100);
    state.addVariable("gold", 25);
    state.setSelfVariable("map:1:event:3", "visits", 2);
    state.setSelfString("story.one", "cursor", "active");
    CHECK(state.isSwitchOn("door"));
    CHECK_EQ(state.getVariable("gold"), 125.0);
    CHECK_EQ(state.getSelfVariable("map:1:event:3", "visits"), 2.0);
    CHECK(!state.hasSelfVariable("map:1:event:4", "visits"));
    CHECK_EQ(state.getSelfString("story.one", "cursor"), std::string("active"));
    state.clearSelfString("story.one", "cursor");
    CHECK(!state.hasSelfString("story.one", "cursor"));
    state.clear();
    CHECK(!state.isSwitchOn("door"));
    REQUIRE_EQ(state.getVariable("gold"), 0.0);
}

TEST_CASE("Game state snapshot round trips and invalid schema or data leaves published state intact") {
    GameState source;
    source.switchOn("door.open");
    source.setVariable("gold", 125.5);
    source.setSelfVariable("map:1", "visits", 2);
    const std::string longKey = "story-key-" + std::string(96, 'k');
    source.setSelfVariable("map:1", longKey, 42.25);
    source.setSelfString("story.scope", longKey, "未完成的章节");
    const auto encoded = source.snapshotJson();
    REQUIRE(encoded.ok());
    GameState target;
    target.setVariable("sentinel", 7);
    REQUIRE(target.restoreSnapshotJson(encoded.value()).ok());
    CHECK(target.isSwitchOn("door.open"));
    CHECK_EQ(target.getVariable("gold"), 125.5);
    CHECK_EQ(target.getSelfVariable("map:1", "visits"), 2.0);
    CHECK_EQ(target.getSelfVariable("map:1", longKey), 42.25);
    CHECK_EQ(target.getSelfString("story.scope", longKey), std::string("未完成的章节"));
    CHECK_EQ(target.getVariable("sentinel"), 0.0);
    {
        CHECK(
            !target
                 .restoreSnapshotJson(
                     R"({"schema":"eve.rpg.game-state","version":1,"switches":{},"variables":{"gold":"bad"},"selfVariables":{}})")
                 .ok());
        CHECK_EQ(target.getVariable("gold"), 125.5);
    };
    {
        CHECK(!target
                   .restoreSnapshotJson(
                       R"({"schema":"eve.rpg.game-state","version":2,"switches":{},"variables":{},"selfVariables":{}})")
                   .ok());
        CHECK_EQ(target.getVariable("gold"), 125.5);
    };
    auto invalid = eve::Value::fromJson(encoded.value());
    REQUIRE(invalid.ok());
    auto& invalidRoot   = *invalid.value().getIf<eve::Value::Object>();
    auto& scopes        = *invalidRoot.at("selfStrings").getIf<eve::Value::Object>();
    auto& strings       = *scopes.at("story.scope").getIf<eve::Value::Object>();
    strings.at(longKey) = eve::Value(7);
    auto invalidJson    = invalid.value().toJson();
    REQUIRE(invalidJson.ok());
    auto rejected = target.restoreSnapshotJson(invalidJson.value());
    REQUIRE(!rejected.ok());
    REQUIRE(rejected.error() != nullptr);
    CHECK(rejected.error()->code() == eve::DiagnosticCode::ParseError);
    CHECK_EQ(rejected.error()->path(), "$.selfStrings.story.scope." + longKey);
    auto unchanged = target.snapshotJson();
    REQUIRE(unchanged.ok());
    CHECK_EQ(unchanged.value(), encoded.value());
}
}  // namespace test_rpg_game_state_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_progression)
#include "PrivateTestScope.h"
#include "rpg/RPGActor.h"
#include "rpg/VitalsSystem.h"
namespace test_rpg_progression_contract {
using namespace eve::rpg;

TEST_CASE("Experience carries overflow across level thresholds and rejects invalid restoration atomically") {
    auto* actor = RPGActor::createActor();
    REQUIRE(actor != nullptr);
    actor->setXpToNext(100);
    CHECK(!actor->gainXp(50));
    CHECK_EQ(actor->getLevel(), 1);
    CHECK(actor->gainXp(60));
    CHECK_EQ(actor->getLevel(), 2);
    CHECK_EQ(actor->getXp(), 10.0);
    CHECK_EQ(actor->getXpToNext(), 120.0);
    REQUIRE(actor->restoreProgression(4, 12, 86.4).ok());
    CHECK(!actor->restoreProgression(0, 100, 10).ok());
    CHECK_EQ(actor->getLevel(), 4);
    CHECK_EQ(actor->getXp(), 12.0);
    actor->release();
}

TEST_CASE("Vitals clamp writes damage and healing and revival restores capacity") {
    auto* actor = RPGActor::createActor();
    REQUIRE(actor != nullptr);
    actor->setBaseAttribute("hp", 100);
    actor->setCurrent("hp", 999);
    CHECK_EQ(actor->getCurrent("hp"), 100.0);
    actor->setCurrent("hp", 50);
    const double firstDamage = actor->takeDamage("hp", 30, "enemy");
    CHECK_EQ(firstDamage, 30.0);
    CHECK_EQ(actor->getCurrent("hp"), 20.0);
    const double lethalDamage = actor->takeDamage("hp", 999, "enemy");
    CHECK_EQ(lethalDamage, 20.0);
    CHECK(actor->isDead("hp"));
    const double postMortemDamage = actor->takeDamage("hp", 10, "enemy");
    CHECK_EQ(postMortemDamage, 0.0);
    actor->revive("hp");
    CHECK_EQ(actor->getCurrent("hp"), 100.0);
    actor->setCurrent("hp", 30);
    const double restoredHealth = actor->heal("hp", 999);
    CHECK_EQ(restoredHealth, 70.0);
    CHECK_EQ(actor->getCurrent("hp"), 100.0);
    actor->release();
}
}  // namespace test_rpg_progression_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_quest)
#include "PrivateTestScope.h"
#include "rpg/Quest.h"
#include "rpg/QuestGeneration.h"
#include "rpg/Tracker.h"
namespace test_rpg_quest_contract {
using namespace eve::rpg;

TEST_CASE("Quest catalogue parses objectives rewards and defaults while excluding invalid cycles") {
    QuestRegistry::clear();
    const int accepted = QuestRegistry::loadFromJson(R"([
      {"id":"rewrite.rpg.quest.hunt","completePolicy":"claim","objectives":[{"id":"kill","topic":"kill","target":"slime","count":2}],"rewards":[{"type":"item","id":"potion","amount":3}]},
      {"id":"rewrite.rpg.quest.bad.a","requires":["rewrite.rpg.quest.bad.b"]},
      {"id":"rewrite.rpg.quest.bad.b","requires":["rewrite.rpg.quest.bad.a"]}
    ])");
    CHECK_EQ(accepted, 1);
    const auto* hunt = QuestRegistry::find("rewrite.rpg.quest.hunt");
    REQUIRE(hunt != nullptr);
    CHECK_EQ(hunt->startPolicy, std::string("manual"));
    CHECK_EQ(hunt->completePolicy, std::string("claim"));
    REQUIRE_EQ(hunt->objectives.size(), 1u);
    CHECK_EQ(hunt->objectives.front().count, 2);
    REQUIRE_EQ(hunt->rewards.size(), 1u);
    CHECK_EQ(hunt->rewards.front().amount, 3.0);
    CHECK(QuestRegistry::find("rewrite.rpg.quest.bad.a") == nullptr);
    QuestRegistry::clear();
}

TEST_CASE("Quest tracker filters topic and target then caps progress before claim") {
    QuestRegistry::clear();
    REQUIRE_EQ(
        QuestRegistry::loadFromJson(
            R"([{"id":"rewrite.rpg.quest.track","startPolicy":"manual","completePolicy":"claim","objectives":[{"id":"slime","topic":"kill","target":"slime","count":2},{"id":"any","topic":"kill","count":3}]}])"),
        1);
    Tracker tracker;
    CHECK_EQ(tracker.getState("rewrite.rpg.quest.track"), std::string("inactive"));
    REQUIRE(tracker.activate("rewrite.rpg.quest.track"));
    tracker.notify("talk", "slime", 1);
    CHECK_EQ(tracker.getObjectiveCurrent("rewrite.rpg.quest.track", 0), 0);
    tracker.notify("kill", "slime", 1);
    tracker.notify("kill", "goblin", 8);
    CHECK_EQ(tracker.getObjectiveCurrent("rewrite.rpg.quest.track", 0), 1);
    CHECK_EQ(tracker.getObjectiveCurrent("rewrite.rpg.quest.track", 1), 3);
    tracker.notify("kill", "slime", 1);
    CHECK_EQ(tracker.getState("rewrite.rpg.quest.track"), std::string("ready"));
    REQUIRE(tracker.claim("rewrite.rpg.quest.track"));
    CHECK_EQ(tracker.getState("rewrite.rpg.quest.track"), std::string("completed"));
    QuestRegistry::clear();
}

TEST_CASE("Quest prerequisite unlocks only after claim and failed quests require reset") {
    QuestRegistry::clear();
    const int loadedQuests = QuestRegistry::loadFromJson(R"([
      {"id":"rewrite.rpg.quest.first","startPolicy":"manual","completePolicy":"claim","objectives":[{"id":"x","topic":"kill","count":1}]},
      {"id":"rewrite.rpg.quest.next","startPolicy":"auto","requires":["rewrite.rpg.quest.first"],"objectives":[{"id":"y","topic":"talk","count":1}]}
    ])");
    REQUIRE_EQ(loadedQuests, 2);
    Tracker tracker;
    CHECK_EQ(tracker.getState("rewrite.rpg.quest.next"), std::string("locked"));
    REQUIRE(tracker.activate("rewrite.rpg.quest.first"));
    tracker.notify("kill", "slime", 1);
    CHECK_EQ(tracker.getState("rewrite.rpg.quest.first"), std::string("ready"));
    CHECK_EQ(tracker.getState("rewrite.rpg.quest.next"), std::string("locked"));
    REQUIRE(tracker.claim("rewrite.rpg.quest.first"));
    CHECK_EQ(tracker.getState("rewrite.rpg.quest.next"), std::string("active"));
    REQUIRE(tracker.abandon("rewrite.rpg.quest.next"));
    CHECK_EQ(tracker.getState("rewrite.rpg.quest.next"), std::string("failed"));
    CHECK(!tracker.activate("rewrite.rpg.quest.next"));
    REQUIRE(tracker.reset("rewrite.rpg.quest.next"));
    CHECK_EQ(tracker.getState("rewrite.rpg.quest.next"), std::string("active"));
    QuestRegistry::clear();
}

TEST_CASE("Quest objectivePolicy any completes on first required objective") {
    QuestRegistry::clear();
    REQUIRE_EQ(QuestRegistry::loadFromJson(R"([{
      "id":"rewrite.rpg.quest.any",
      "startPolicy":"manual",
      "completePolicy":"claim",
      "objectivePolicy":"any",
      "objectives":[
        {"id":"kill","topic":"kill","target":"slime","count":1},
        {"id":"talk","topic":"talk","target":"npc","count":1}
      ]
    }])"),
               1);
    Tracker tracker;
    REQUIRE(tracker.activate("rewrite.rpg.quest.any"));
    CHECK_EQ(tracker.getObjectivePolicy("rewrite.rpg.quest.any"), std::string("any"));
    tracker.notify("kill", "slime", 1);
    CHECK(tracker.isObjectivesSatisfied("rewrite.rpg.quest.any"));
    CHECK_EQ(tracker.getState("rewrite.rpg.quest.any"), std::string("ready"));
    CHECK(!tracker.isObjectiveDone("rewrite.rpg.quest.any", 1));
    QuestRegistry::clear();
}

TEST_CASE("Quest ordered policy gates later required objectives while optional still listens") {
    QuestRegistry::clear();
    REQUIRE_EQ(QuestRegistry::loadFromJson(R"([{
      "id":"rewrite.rpg.quest.ordered",
      "startPolicy":"manual",
      "completePolicy":"auto",
      "objectivePolicy":"ordered",
      "objectives":[
        {"id":"first","topic":"kill","target":"a","count":1},
        {"id":"bonus","topic":"loot","target":"chest","count":1,"optional":true},
        {"id":"second","topic":"kill","target":"b","count":1}
      ]
    }])"),
               1);
    Tracker tracker;
    REQUIRE(tracker.activate("rewrite.rpg.quest.ordered"));
    CHECK(tracker.isObjectiveOptional("rewrite.rpg.quest.ordered", 1));
    tracker.notify("kill", "b", 1);
    CHECK_EQ(tracker.getObjectiveCurrent("rewrite.rpg.quest.ordered", 2), 0);
    tracker.notify("loot", "chest", 1);
    CHECK(tracker.isObjectiveDone("rewrite.rpg.quest.ordered", 1));
    CHECK_EQ(tracker.getState("rewrite.rpg.quest.ordered"), std::string("active"));
    tracker.notify("kill", "a", 1);
    tracker.notify("kill", "b", 1);
    CHECK_EQ(tracker.getState("rewrite.rpg.quest.ordered"), std::string("completed"));
    QuestRegistry::clear();
}

TEST_CASE("Quest dialogue phase maps tracker state and clock refresh hooks work") {
    QuestRegistry::clear();
    REQUIRE_EQ(QuestRegistry::loadFromJson(R"([{
      "id":"rewrite.rpg.quest.timed",
      "startPolicy":"manual",
      "completePolicy":"claim",
      "timeLimitSeconds":10,
      "refreshKey":"daily",
      "objectives":[{"id":"x","topic":"kill","count":1}]
    }])"),
               1);
    Tracker tracker;
    CHECK_EQ(tracker.dialoguePhase("rewrite.rpg.quest.timed"), std::string("offer"));
    CHECK_EQ(tracker.dialogueConversationId("rewrite.rpg.quest.timed", "npc.giver"), std::string("npc.giver.offer"));
    REQUIRE(tracker.activate("rewrite.rpg.quest.timed"));
    CHECK_EQ(tracker.dialoguePhase("rewrite.rpg.quest.timed"), std::string("active"));
    CHECK_EQ(tracker.applyClock(100.0), 0);
    CHECK_EQ(tracker.getDeadlineAt("rewrite.rpg.quest.timed"), 110.0);
    CHECK_EQ(tracker.applyClock(110.0), 1);
    CHECK_EQ(tracker.getState("rewrite.rpg.quest.timed"), std::string("failed"));
    CHECK_EQ(tracker.dialoguePhase("rewrite.rpg.quest.timed"), std::string("failed"));
    REQUIRE_EQ(tracker.refreshByKey("daily", "2026-11-06"), 1);
    CHECK_EQ(tracker.getRefreshStamp("rewrite.rpg.quest.timed"), std::string("2026-11-06"));
    CHECK_EQ(tracker.getState("rewrite.rpg.quest.timed"), std::string("inactive"));
    CHECK_EQ(tracker.refreshByKey("daily", "2026-11-06"), 0);
    QuestRegistry::clear();
}

TEST_CASE("Quest replaceFromJsonStrict rejects unknown objectivePolicy") {
    QuestRegistry::clear();
    auto rejected = QuestRegistry::replaceFromJsonStrict(
        R"([{"id":"rewrite.rpg.quest.bad.policy","objectivePolicy":"xor","objectives":[{"id":"a","topic":"kill","count":1}]}])");
    CHECK(!rejected.ok());
    CHECK_EQ(QuestRegistry::count(), 0);
    auto accepted = QuestRegistry::replaceFromJsonStrict(
        R"([{"id":"rewrite.rpg.quest.ok","objectivePolicy":"all","objectives":[{"id":"a","topic":"kill","count":1,"optional":true}]}])");
    REQUIRE(accepted.ok());
    CHECK_EQ(accepted.value(), 1);
    const auto* def = QuestRegistry::find("rewrite.rpg.quest.ok");
    REQUIRE(def != nullptr);
    CHECK(def->objectives.front().optional);
    QuestRegistry::clear();
}

TEST_CASE("Quest generator selects urgent entity and registers a playable definition") {
    QuestRegistry::clear();
    QuestGenerationCatalogue::clear();
    auto archetypes = QuestGenerationCatalogue::replaceFromJsonStrict(R"([{
      "id":"archetype.slay",
      "weight":1.0,
      "requiresEntityTags":["enemy"],
      "completePolicy":"claim",
      "idPattern":"gen.slay.{entity}",
      "objectives":[{"id":"kill","topic":"kill","targetSlot":"entity","countVar":"count"}],
      "rewards":[{"type":"attribute","id":"gold","amountVar":"gold"}],
      "extra":{"title":"Hunt {entity}","giver":"{giver}"}
    }])");
    REQUIRE(archetypes.ok());
    CHECK_EQ(archetypes.value(), 1);

    auto registered = QuestGenerator::generateAndRegister(R"({
      "schema":"eve.rpg.quest-generate",
      "version":1,
      "seed":7,
      "maxQuests":1,
      "selection":"mostUrgent",
      "giverId":"npc.elder",
      "entities":[
        {"id":"slime","tags":["enemy"],"urgency":10,"vars":{"count":2,"gold":20}},
        {"id":"wolf","tags":["enemy"],"urgency":90,"vars":{"count":3,"gold":55}}
      ]
    })");
    REQUIRE(registered.ok());
    CHECK_EQ(registered.value(), 1);
    const auto* quest = QuestRegistry::find("gen.slay.wolf");
    REQUIRE(quest != nullptr);
    CHECK(quest->hasTag("generated"));
    REQUIRE_EQ(quest->objectives.size(), 1u);
    CHECK_EQ(quest->objectives.front().target, std::string("wolf"));
    CHECK_EQ(quest->objectives.front().count, 3);
    REQUIRE_EQ(quest->rewards.size(), 1u);
    CHECK_EQ(quest->rewards.front().amount, 55.0);
    CHECK_EQ(quest->getExtra("title"), std::string("Hunt wolf"));
    CHECK_EQ(quest->getExtra("giver"), std::string("npc.elder"));

    Tracker tracker;
    REQUIRE(tracker.activate("gen.slay.wolf"));
    tracker.notify("kill", "wolf", 3);
    CHECK_EQ(tracker.getState("gen.slay.wolf"), std::string("ready"));

    auto again = QuestGenerator::generateAndRegister(R"({
      "schema":"eve.rpg.quest-generate","version":1,"seed":7,"maxQuests":1,"selection":"mostUrgent",
      "giverId":"npc.elder",
      "entities":[{"id":"wolf","tags":["enemy"],"urgency":90,"vars":{"count":1,"gold":10}}]
    })");
    REQUIRE(again.ok());
    CHECK_EQ(again.value(), 1);
    CHECK(QuestRegistry::find("gen.slay.wolf") != nullptr);

    QuestGenerationCatalogue::clear();
    QuestRegistry::clear();
}

TEST_CASE("Quest generator preview json is deterministic for a fixed seed") {
    QuestRegistry::clear();
    QuestGenerationCatalogue::clear();
    REQUIRE(QuestGenerationCatalogue::replaceFromJsonStrict(R"([{
      "id":"archetype.fetch",
      "requiresEntityTags":["item"],
      "objectives":[{"id":"get","topic":"loot","targetSlot":"entity","count":1}],
      "rewards":[{"type":"attribute","id":"xp","amount":5}]
    }])")
                .ok());
    const char* request = R"({
      "schema":"eve.rpg.quest-generate","version":1,"seed":99,"maxQuests":1,"selection":"weighted",
      "entities":[
        {"id":"herb","tags":["item"],"urgency":40},
        {"id":"ore","tags":["item"],"urgency":40}
      ]
    })";
    auto        first   = QuestGenerator::generateJson(request);
    auto        second  = QuestGenerator::generateJson(request);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(first.value(), second.value());
    CHECK(first.value().find("\"schema\":\"eve.rpg.quest-generate-result\"") != std::string::npos);
    QuestGenerationCatalogue::clear();
    QuestRegistry::clear();
}
}  // namespace test_rpg_quest_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_reload)
#include "PrivateTestScope.h"
#include "common/StateValue.h"
#include "rpg/RPGActor.h"
#include "rpg/RpgState.h"
#include "rpg/Skill.h"
#include "rpg/SkillSystem.h"
namespace test_rpg_reload_contract {
using namespace eve::rpg;

TEST_CASE("Runtime RPG state restores an in flight cast and cooldown then reset clears casting") {
    SkillDefinition skill;
    skill.id       = "rewrite.rpg.reload.channel";
    skill.castTime = 1.5f;
    SkillRegistry::registerSkill(skill);
    auto* actor = RPGActor::createActor();
    REQUIRE(actor != nullptr);
    actor->learnSkill(skill.id);
    REQUIRE(SkillSystem::beginCast(actor, skill.id));
    actor->setSkillCooldown(skill.id, 2.5f);
    eve::StateValue snapshot;
    REQUIRE(captureRpgState(snapshot));
    SkillSystem::cancelCast(actor);
    actor->setSkillCooldown(skill.id, 0);
    std::string error;
    REQUIRE(restoreRpgState(snapshot, &error));
    CHECK(error.empty());
    CHECK(SkillSystem::isCasting(actor));
    CHECK_EQ(SkillSystem::getCastingSkillId(actor), skill.id);
    CHECK_EQ(SkillSystem::getCooldownRemaining(actor, skill.id), 2.5f);
    REQUIRE(resetRpgState());
    CHECK(!SkillSystem::isCasting(actor));
    CHECK(!restoreRpgState(eve::StateValue::object(), &error));
    CHECK(!error.empty());
    actor->release();
    SkillRegistry::remove(skill.id);
}
TEST_CASE("RPG reload admits explicit self casts and rejects ambiguous target contexts without partial publication") {
    SkillDefinition skill;
    skill.id       = "cast.reload.intent";
    skill.castTime = 2;
    SkillRegistry::registerSkill(skill);
    auto* caster = RPGActor::createActor();
    auto* target = RPGActor::createActor();
    caster->learnSkill(skill.id);
    REQUIRE(SkillSystem::beginCast(caster, skill.id));
    auto snapshot = captureRpgState().takeValue();
    auto reject   = [&](eve::Value bad) {
        const auto before = captureRpgState().takeValue().toJson().takeValue();
        REQUIRE(!restoreRpgState(bad).ok());
        REQUIRE_EQ(captureRpgState().takeValue().toJson().takeValue(), before);
    };
    auto bad = snapshot;
    bad.at(0).find("casting")->getIf<eve::Value::Object>()->erase("target");
    reject(std::move(bad));
    bad = snapshot;
    bad.at(0).find("casting")->set("version", 3);
    reject(std::move(bad));
    bad = snapshot;
    bad.at(0).find("casting")->set("schema", "foreign.casting");
    reject(std::move(bad));
    bad = snapshot;
    bad.at(0).find("casting")->set("active", false);
    bad.at(0).find("casting")->set("target", "foreign-target");
    reject(std::move(bad));
    bad = snapshot;
    bad.at(0).getIf<eve::Value::Object>()->erase("skills");
    reject(std::move(bad));
    bad = snapshot;
    bad.at(0).set("skills", "bad");
    reject(std::move(bad));
    bad = snapshot;
    bad.at(0).find("casting")->set("remaining", -1.0);
    reject(std::move(bad));
    bad = snapshot;
    bad.at(0).find("skills")->set(skill.id, -1.0);
    reject(std::move(bad));
    auto* incomplete = RPGActor::create();
    incomplete->attributes().emplace();
    REQUIRE(!incomplete->skills().has());
    REQUIRE(!captureRpgState().ok());
    auto malformedWorld = snapshot;
    malformedWorld.getIf<eve::Value::Array>()->push_back(snapshot.at(1));
    malformedWorld.at(0).find("skills")->set(skill.id, 99.0);
    REQUIRE(!restoreRpgState(malformedWorld).ok());
    REQUIRE(!incomplete->skills().has());
    REQUIRE_EQ(caster->getSkillCooldown(skill.id), 0.f);
    REQUIRE_EQ(caster->skills()->casting.remaining, 2.f);
    incomplete->release();
    REQUIRE_EQ(captureRpgState().takeValue().toJson().takeValue(), snapshot.toJson().takeValue());
    SkillSystem::cancelCast(caster);
    REQUIRE(restoreRpgState(snapshot).ok());
    REQUIRE(caster->skills()->casting.target.intent == CastTargetIntent::DefaultSelf);
    SkillSystem::cancelCast(caster);
    REQUIRE(SkillSystem::beginCast(caster, skill.id, target));
    REQUIRE(!captureRpgState().ok());
    REQUIRE(caster->skills()->casting.target.entity.id == target->id);
    target->release();
    SkillSystem::update(0.25f);
    REQUIRE(caster->skills()->casting.target.intent == CastTargetIntent::Retired);
    REQUIRE(!caster->skills()->casting.target.entity.table);
    auto retired = captureRpgState();
    REQUIRE(retired.ok());
    REQUIRE_EQ(retired.value().at(0).find("casting")->find("target")->asString(), "retired");
    SkillSystem::cancelCast(caster);
    REQUIRE(restoreRpgState(retired.value()).ok());
    REQUIRE(caster->skills()->casting.target.intent == CastTargetIntent::Retired);
    REQUIRE(!caster->skills()->casting.target.entity.table);
    REQUIRE_EQ(caster->skills()->casting.remaining, 1.75f);
    std::vector<SkillCastEvent> events;
    SkillSystem::pollCastEvents(events);
    events.clear();
    SkillSystem::update(2.f);
    REQUIRE(!caster->skills()->casting.active);
    SkillSystem::pollCastEvents(events);
    REQUIRE(events.empty());
    caster->release();
    SkillRegistry::remove(skill.id);
}

}  // namespace test_rpg_reload_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_skill_condition)
#include <optional>
#include "PrivateTestScope.h"
#include "common/ConditionKeys.h"
#include "rpg/RPGActor.h"
#include "rpg/Skill.h"
#include "rpg/SkillCondition.h"
#include "rpg/SkillConditionBindings.h"
#include "rpg/SkillSystem.h"
namespace test_rpg_skill_condition_contract {
using namespace eve::rpg;
using namespace eve::condition;

TEST_CASE("Skill condition reads canonical actor attributes tags phase and cooldown") {
    auto* actor = RPGActor::createActor();
    REQUIRE(actor != nullptr);
    actor->setBaseAttribute("mana", 20);
    actor->learnSkill("rewrite.rpg.condition.fire");
    SkillDefinition skill;
    skill.id             = "rewrite.rpg.condition.fire";
    skill.tags           = {"fire"};
    const auto condition = Condition::all({tagPresent("fire"), attributeExists("mana"), resourceExists("mana"),
                                           Condition::compare("skill.cooldown", CompareOperator::Equal, 0),
                                           stateEquals("skill.phase", "idle")});
    CHECK(SkillConditionAdapter::evaluate(actor, skill, condition).passed());
    actor->setSkillCooldown(skill.id, 2);
    const auto blocked =
        SkillConditionAdapter::evaluate(actor, skill, Condition::compare("skill.cooldown", CompareOperator::Equal, 0));
    CHECK(!blocked.passed());
    CHECK_EQ(static_cast<int>(blocked.reasonCode()), static_cast<int>(ConditionReasonCode::ValueMismatch));
    actor->release();
}

TEST_CASE("Skill condition authority and policy query providers are called without mutating actor") {
    auto* actor = RPGActor::createActor();
    REQUIRE(actor != nullptr);
    SkillDefinition skill;
    skill.id                             = "rewrite.rpg.condition.policy";
    int                   authorityCalls = 0;
    int                   policyCalls    = 0;
    SkillConditionQueries queries;
    queries.authority = [&](std::string_view scope) -> std::optional<bool> {
        ++authorityCalls;
        return scope == "cast";
    };
    queries.policy = [&](std::string_view name, const eve::Value&) -> std::optional<ConditionResult> {
        ++policyCalls;
        return name == "safe" ? std::optional<ConditionResult>(ConditionResult::success()) : std::nullopt;
    };
    const auto result = SkillConditionAdapter::evaluate(
        actor, skill, Condition::all({authorityAllow("cast"), Condition::policyCall("safe")}), queries);
    CHECK(result.passed());
    CHECK_EQ(authorityCalls, 1);
    CHECK_EQ(policyCalls, 1);
    actor->release();
}
TEST_CASE("Versioned skill policies drive formal casting and preserve actor ownership through checkpoints") {
    SkillDefinition skill;
    skill.id = "binding.formal.cast";
    ScriptConditionDeclaration declaration{"binding.check", {"attr.mana"}, DeterminismLevel::TickDeterministic};
    skill.castCondition =
        Condition::all({Condition::policyCall("binding.check", eve::Value::object({{"minimum", 3}}), declaration),
                        authorityAllow("cast"), authorityAllow("inspect")});
    SkillRegistry::registerSkill(skill);
    int                               calls = 0, legacyCalls = 0;
    std::vector<SkillConditionPolicy> policies{
        {"binding.check", "source:check:v1", declaration,
         [&](const RPGActor*, const eve::Value& args) {
             ++calls;
             REQUIRE_EQ(args.find("minimum")->asInt(), 3);
             return std::optional<ConditionResult>(ConditionResult::success());
         }},
        {"authority.allow",
         "source:authority:v1",
         {},
         SkillConditionPolicy::Authority([&](const RPGActor*, std::string_view scope) {
             ++calls;
             return std::optional<bool>(scope == "cast" || scope == "inspect");
         })}};
    auto bindings = SkillConditionBindings::prepare(policies).expect("actual code/metadata owner");
    policies.clear();
    REQUIRE_EQ(calls, 0);
    auto* actor = RPGActor::createActor();
    REQUIRE(actor != nullptr);
    actor->learnSkill(skill.id);
    actor->skills()->conditionBindings = bindings;
    SkillConditionQueries queries;
    queries.authority = [&](std::string_view) {
        ++legacyCalls;
        return std::optional<bool>(true);
    };
    queries.policy = [&](std::string_view, const eve::Value&) {
        ++legacyCalls;
        return std::optional<ConditionResult>(ConditionResult::success());
    };
    REQUIRE(SkillConditionAdapter::evaluate(actor, skill, skill.castCondition, queries).passed());
    REQUIRE_EQ(legacyCalls, 0);
    REQUIRE(SkillSystem::canCast(actor, skill.id));
    REQUIRE(SkillSystem::beginCast(actor, skill.id));
    std::vector<SkillCastEvent> events;
    SkillSystem::pollCastEvents(events);
    REQUIRE_EQ(events.size(), std::size_t(1));
    REQUIRE_EQ(events[0].skillId, skill.id);
    const auto checkpoint = actor->checkpointJson().expect("provider-free actor JSON");
    REQUIRE(checkpoint.find("source:check:v1") == std::string::npos);
    REQUIRE(actor->restoreCheckpointJson(checkpoint).ok());
    REQUIRE(actor->skills()->conditionBindings == bindings);
    queries.bindings = SkillConditionBindings::prepare({}).expect("explicit empty executor set");
    REQUIRE(!SkillConditionAdapter::evaluate(actor, skill, skill.castCondition, queries).passed());
    REQUIRE(!SkillConditionAdapter::evaluate(
                 actor, skill, Condition::any({Condition::all({}), Condition::policyCall("unbound")}), queries)
                 .passed());
    REQUIRE_EQ(legacyCalls, 0);
    const auto beforeDeniedScope = calls;
    REQUIRE(!SkillConditionAdapter::evaluate(actor, skill, authorityAllow("admin")).passed());
    REQUIRE_EQ(calls, beforeDeniedScope + 1);
    actor->release();
    SkillRegistry::remove(skill.id);
}
TEST_CASE("Condition binding admission owns code and rejects duplicate or mismatched declarations without execution") {
    int                               calls = 0;
    ScriptConditionDeclaration        declaration{"binding.contract", {"state.zone"}, DeterminismLevel::BitExact};
    std::vector<SkillConditionPolicy> policies{
        {"binding.contract", "source:v1", declaration, [&](const RPGActor*, const eve::Value&) {
             ++calls;
             return std::optional<ConditionResult>(ConditionResult::success());
         }}};
    auto bindings        = SkillConditionBindings::prepare(policies).expect("immutable owner");
    policies[0].revision = "source:v2";
    policies[0].function = {};
    REQUIRE_EQ(bindings->revision("binding.contract").expect("owned revision"), "source:v1");
    REQUIRE(!SkillConditionBindings::prepare(policies).ok());
    policies[0].function = [](const RPGActor*, const eve::Value&) {
        return std::optional<ConditionResult>(ConditionResult::success());
    };
    policies.push_back(policies[0]);
    REQUIRE(!SkillConditionBindings::prepare(policies).ok());
    policies.pop_back();
    policies[0].declaration->name = "other";
    REQUIRE(!SkillConditionBindings::prepare(policies).ok());
    policies[0].declaration               = declaration;
    policies[0].declaration->dependencies = {""};
    REQUIRE(!SkillConditionBindings::prepare(policies).ok());
    REQUIRE(bindings->validateDeclaration("binding.contract", declaration).ok());
    auto changed        = declaration;
    changed.determinism = DeterminismLevel::ExplicitlyNondeterministic;
    REQUIRE(!bindings->validateDeclaration("binding.contract", changed).ok());
    REQUIRE(!bindings->validateDeclaration("binding.contract", {}).ok());
    REQUIRE(!bindings->revision("missing").ok());
    REQUIRE(!bindings->evaluate("missing", {}).has_value());
    REQUIRE_EQ(calls, 0);
    REQUIRE(bindings->evaluate("binding.contract", {})->passed());
    REQUIRE_EQ(calls, 1);
}
TEST_CASE("Condition policy reentry pins executable records when the last external owner is released") {
    std::shared_ptr<const SkillConditionBindings> owner;
    auto                                          resource = std::make_shared<int>(7);
    std::weak_ptr<int>                            observed = resource;
    int                                           calls    = 0;
    std::vector<SkillConditionPolicy>             policies{
                    {"binding.reentrant", "source:v1", {}, [&, retained = resource](const RPGActor*, const eve::Value&) {
             ++calls;
             REQUIRE_EQ(*retained, 7);
             if (calls == 1) {
                 REQUIRE(owner->evaluate("binding.reentrant", {})->passed());
                 owner.reset();
                 REQUIRE(!observed.expired());
             }
             return std::optional<ConditionResult>(ConditionResult::success());
         }}};
    owner = SkillConditionBindings::prepare(policies).expect("owning executable");
    policies.clear();
    resource.reset();
    REQUIRE(owner->evaluate("binding.reentrant", {})->passed());
    REQUIRE(!owner);
    REQUIRE_EQ(calls, 2);
    REQUIRE(observed.expired());
}
}  // namespace test_rpg_skill_condition_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_story_event)
#include "PrivateTestScope.h"
#include "rpg/GameState.h"
#include "rpg/StoryEvent.h"
namespace test_rpg_story_event_contract {
using namespace eve::rpg;

TEST_CASE("Story event catalogue replaces atomically and session resumes from persisted cursor") {
    StoryEventCatalogue::clear();
    const std::string content = R"({"schema":"eve.rpg.story-events","version":1,"events":[
      {"id":"rewrite.rpg.story.once","repeatable":false,"steps":[{"kind":"dialogue","reference":"arrival","actorId":"","x":0,"y":0,"duration":0},{"kind":"message","reference":"done","actorId":"","x":0,"y":0,"duration":0}]},
      {"id":"rewrite.rpg.story.repeat","repeatable":true,"steps":[{"kind":"wait","reference":"","actorId":"","x":0,"y":0,"duration":0.25}]}
    ]})";
    REQUIRE(StoryEventCatalogue::replaceFromJsonStrict(content).ok());
    CHECK_EQ(StoryEventCatalogue::count(), 2);
    CHECK(
        !StoryEventCatalogue::replaceFromJsonStrict(
             R"({"schema":"eve.rpg.story-events","version":1,"events":[{"id":"bad","repeatable":false,"steps":[{"kind":"wait","reference":"","actorId":"","x":0,"y":0,"duration":0}]}]})")
             .ok());
    CHECK_EQ(StoryEventCatalogue::count(), 2);
    GameState         state;
    StoryEventSession session;
    REQUIRE(session.begin("rewrite.rpg.story.once", &state).ok());
    CHECK_EQ(session.getStepKind(), std::string("dialogue"));
    REQUIRE(session.advance(&state).ok());
    const auto snapshot = state.snapshotJson();
    REQUIRE(snapshot.ok());
    GameState restored;
    REQUIRE(restored.restoreSnapshotJson(snapshot.value()).ok());
    StoryEventSession resumed;
    REQUIRE(resumed.begin("rewrite.rpg.story.once", &restored).ok());
    CHECK_EQ(resumed.getStepIndex(), 1);
    CHECK_EQ(resumed.getStepKind(), std::string("message"));
    REQUIRE(resumed.advance(&restored).ok());
    CHECK(resumed.isFinished());
    CHECK(!StoryEventSession().begin("rewrite.rpg.story.once", &restored).ok());
    StoryEventSession repeat;
    REQUIRE(repeat.begin("rewrite.rpg.story.repeat", &restored).ok());
    REQUIRE(repeat.advance(&restored).ok());
    CHECK(repeat.begin("rewrite.rpg.story.repeat", &restored).ok());
    StoryEventCatalogue::clear();
}
}  // namespace test_rpg_story_event_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_trait)
#include "PrivateTestScope.h"
#include "rpg/RPG.h"
#include "rpg/RPGActor.h"
#include "rpg/Trait.h"
namespace test_rpg_trait_contract {
using namespace eve::rpg;

TEST_CASE("Trait projection changes combat attributes and source removal retracts only owned effects") {
    auto* rpg = RPG::create();
    rpg->clearTraitDefinitions();
    const int loadedTraits = rpg->registerTraitsFromJson(R"([
      {"id":"rewrite.rpg.trait.mighty","traits":[{"kind":"paramRate","target":"attack","value":1.5},{"kind":"exParam","target":"critRate","value":0.3}]},
      {"id":"rewrite.rpg.trait.resist","traits":[{"kind":"elementRate","target":"fire","value":0.5}]}
    ])");
    REQUIRE_EQ(loadedTraits, 2);
    auto* actor = rpg->newActor();
    actor->setBaseAttribute("attack", 100);
    REQUIRE(actor->applyTrait("rewrite.rpg.trait.mighty", "class") > 0);
    REQUIRE(actor->applyTrait("rewrite.rpg.trait.resist", "equipment") > 0);
    CHECK_EQ(actor->getFinalAttribute("attack"), 150.0);
    CHECK(std::fabs(actor->getExParam("critRate") - .3) < 1e-9);
    CHECK_EQ(actor->getElementRate("fire"), .5);
    const int removedEquipmentTraits = actor->removeTraitsBySource("equipment");
    CHECK_EQ(removedEquipmentTraits, 1);
    CHECK_EQ(actor->getElementRate("fire"), 1.0);
    CHECK_EQ(actor->getFinalAttribute("attack"), 150.0);
    const int removedMightyTraits = actor->removeTraitsByTrait("rewrite.rpg.trait.mighty");
    CHECK_EQ(removedMightyTraits, 1);
    CHECK_EQ(actor->getFinalAttribute("attack"), 100.0);
    actor->release();
    rpg->clearTraitDefinitions();
}
}  // namespace test_rpg_trait_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_status_effect_adapter)
#include "PrivateTestScope.h"
#include "rpg/Effect.h"
#include "rpg/RPGActor.h"
#include "rpg/StatusSystem.h"
namespace test_rpg_status_adapter_contract {
using namespace eve::rpg;

TEST_CASE("Legacy status ids project one canonical effect across stack replacement") {
    EffectDefinition effect;
    effect.id             = "rewrite.rpg.adapter.stack";
    effect.durationPolicy = "infinite";
    effect.stackPolicy    = "stack";
    effect.maxStacks      = 2;
    effect.modifiers.push_back({"armor", "add", 4, 0});
    EffectRegistry::registerEffect(effect);
    auto* actor = RPGActor::createActor();
    actor->setBaseAttribute("armor", 10);
    const int legacy = actor->applyEffect(effect.id, "source");
    REQUIRE(legacy > 0);
    REQUIRE_EQ(actor->statuses()->container.effectCount(), 1u);
    const auto* canonical = actor->statuses()->container.effectAt(0);
    REQUIRE(canonical != nullptr);
    const std::string canonicalId = canonical->id;
    CHECK_EQ(actor->statuses()->effectByLegacyId.at(legacy), canonicalId);
    CHECK_EQ(actor->getFinalAttribute("armor"), 14.0);
    const int stackedLegacy = actor->applyEffect(effect.id, "source");
    CHECK_EQ(stackedLegacy, legacy);
    CHECK_EQ(actor->statuses()->container.effectCount(), 1u);
    const auto* stacked = actor->statuses()->container.find(canonicalId);
    REQUIRE(stacked != nullptr);
    CHECK_EQ(stacked->stackCount, 2u);
    CHECK_EQ(actor->getFinalAttribute("armor"), 18.0);
    actor->release();
    EffectRegistry::remove(effect.id);
}

TEST_CASE("Rejected custom stack candidate leaves canonical state and emitted events unchanged") {
    EffectDefinition effect;
    effect.id             = "rewrite.rpg.adapter.rollback";
    effect.durationPolicy = "duration";
    effect.duration       = 4;
    effect.stackPolicy    = "rewriteReject";
    effect.modifiers.push_back({"attack", "add", 2, 0});
    EffectRegistry::registerEffect(effect);
    auto registered = StatusSystem::registerVersionedStackPolicy(
        "rewriteReject", "rewrite:v1",
        [](RPGActor*, StatusInstance& candidate, const EffectDefinition&, const std::string&) {
            candidate.stacks += 1;
            candidate.remaining += 10;
            return -1;
        });
    REQUIRE(registered.ok());
    REQUIRE_EQ(StatusSystem::stackPolicyRevision("rewriteReject").expect("actual executor revision"), "rewrite:v1");
    REQUIRE(!StatusSystem::registerVersionedStackPolicy("rewriteReject", "invalid", {}).ok());
    REQUIRE_EQ(StatusSystem::stackPolicyRevision("rewriteReject").expect("invalid replacement preserved"),
               "rewrite:v1");
    auto* actor = RPGActor::createActor();
    actor->setBaseAttribute("attack", 5);
    REQUIRE(actor->applyEffect(effect.id) > 0);
    const int events        = actor->statuses()->container.eventCount();
    const int rejectedStack = actor->applyEffect(effect.id);
    CHECK_EQ(rejectedStack, -1);
    CHECK_EQ(actor->getStatusStacks(0), 1);
    CHECK_EQ(actor->getStatusRemaining(0), 4.f);
    CHECK_EQ(actor->getFinalAttribute("attack"), 7.0);
    CHECK_EQ(actor->statuses()->container.eventCount(), events);
    actor->release();
    StatusSystem::registerStackPolicy(
        "rewriteReject", [](RPGActor*, StatusInstance&, const EffectDefinition&, const std::string&) { return -1; });
    REQUIRE(!StatusSystem::stackPolicyRevision("rewriteReject").ok());
    StatusSystem::unregisterStackPolicy("rewriteReject");
    REQUIRE(!StatusSystem::stackPolicyRevision("rewriteReject").ok());
    EffectRegistry::remove(effect.id);
}
TEST_CASE("RPG status phase follows durable keys and freezes generation roster before expiry hooks") {
    ecs::Table                             table;
    ecs::ScopedTable                       scope(table);
    eve::rpg::detail::SimulationEvents     events;
    eve::rpg::detail::SimulationEventScope eventScope(events);
    auto                                   high   = *ActorExecutionKey::parse("01900000-0000-7000-8000-000000000002");
    auto                                   low    = *ActorExecutionKey::parse("01900000-0000-7000-8000-000000000001");
    auto*                                  legacy = RPGActor::createActor();
    auto*                                  later  = RPGActor::createActor();
    auto*                                  first  = RPGActor::createActor();
    later->executionOrder()->key                  = high;
    first->executionOrder()->key                  = low;
    REQUIRE(RPGActor::liveActors()[0] == legacy);
    REQUIRE(RPGActor::liveActors()[1] == first);
    EffectDefinition effect;
    effect.id             = "order.status.expiry";
    effect.durationPolicy = "duration";
    effect.duration       = 1;
    EffectRegistry::registerEffect(effect);
    StatusSystem::apply(later, effect.id).expect("later actor status");
    StatusSystem::apply(first, effect.id).expect("first actor status");
    events.changes.clear();
    RPGActor*  replacement = nullptr;
    const auto retired     = ecs::handle_of(later);
    StatusSystem::registerLifecycleHook("order.freeze.roster", [&](const StatusChangeEvent& event) {
        if (event.action != "expire" || eventActor(event.actor) != first) return;
        REQUIRE(RPGActor::liveActors()[0] == legacy);
        REQUIRE(RPGActor::liveActors()[1] == first);
        later->release();
        later       = nullptr;
        replacement = RPGActor::createActor();
        REQUIRE_EQ(replacement->id, retired.id);
        replacement->executionOrder()->key = high;
        StatusSystem::apply(replacement, effect.id).expect("replacement status in callback");
    });
    auto summary = StatusSystem::update(1).expect("ordered frozen phase");
    REQUIRE_EQ(summary.expired, std::uint32_t(1));
    REQUIRE(replacement != nullptr);
    REQUIRE_EQ(replacement->getStatusRemaining(0), 1.f);
    REQUIRE_EQ(eventActor(events.changes.front().actor), first);
    StatusSystem::unregisterLifecycleHook("order.freeze.roster");
    StatusSystem::update(1).expect("replacement enters next phase");
    REQUIRE_EQ(replacement->getStatusCount(), 0);
    REQUIRE(!legacy->executionOrder()->key);
    legacy->release();
    first->release();
    replacement->release();
    EffectRegistry::remove(effect.id);
}
}  // namespace test_rpg_status_adapter_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_dialect)
#include "PrivateTestScope.h"
#include "rpg/GameState.h"
#include "rpg/Party.h"
#include "rpg/RPGActor.h"
#include "rpg/RpgDialect.h"
namespace test_rpg_dialect_contract {
using namespace eve::rpg;

TEST_CASE("Dnut story parses a domain step then persists nonrepeatable completion") {
    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story rewrite_once {
    flag name=gate.open state=true
    variable name=gold op=add value=25
    message text=Done
}
)",
                                                     "rewrite.dnut")
                .ok());
    GameState       state;
    RpgStoryBinding binding;
    binding.gameState = &state;
    RpgStorySession session;
    REQUIRE(session.begin("rewrite_once", &binding).ok());
    CHECK(state.isSwitchOn("gate.open"));
    CHECK_EQ(state.getVariable("gold"), 25.0);
    CHECK(session.isBlocked());
    CHECK_EQ(session.getStepKind(), std::string("message"));
    REQUIRE(session.advance().ok());
    CHECK(!session.isActive());
    CHECK(state.hasSelfVariable("story.rewrite_once", "completed"));
    CHECK(!session.begin("rewrite_once", &binding).ok());
    RpgStoryCatalogue::clear();
}

TEST_CASE("Dnut strict replacement rejects invalid step payload without dropping published story") {
    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict("story good {\n    message text=ok\n}\n", "good.dnut").ok());
    CHECK(!RpgStoryCatalogue::replaceFromDnutStrict("story bad {\n    teleport actor=hero\n}\n", "bad.dnut").ok());
    CHECK(RpgStoryCatalogue::contains("good"));
    CHECK(!RpgStoryCatalogue::contains("bad"));
    RpgStoryCatalogue::clear();
}

TEST_CASE("Dnut repeatable branch reads current game state on each run") {
    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story rewrite_branch repeatable {
    if switch.gate == true {
        variable name=path op=set value=1
    } else {
        variable name=path op=set value=2
    }
}
)",
                                                     "branch.dnut")
                .ok());
    GameState       state;
    RpgStoryBinding binding;
    binding.gameState = &state;
    RpgStorySession session;
    REQUIRE(session.begin("rewrite_branch", &binding).ok());
    CHECK_EQ(state.getVariable("path"), 2.0);
    state.switchOn("gate");
    REQUIRE(session.begin("rewrite_branch", &binding).ok());
    CHECK_EQ(state.getVariable("path"), 1.0);
    RpgStoryCatalogue::clear();
}
}  // namespace test_rpg_dialect_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_battle_tactics)
#include <vector>
#include "PrivateTestScope.h"
#include "rpg/Battle.h"
#include "rpg/BattleSystem.h"
#include "rpg/BattleTactics.h"
#include "rpg/BattleVictory.h"
#include "rpg/GameState.h"
#include "rpg/LevelSystem.h"
#include "rpg/Quest.h"
#include "rpg/RPGActor.h"
#include "rpg/Skill.h"
#include "rpg/Tracker.h"
#include "rpg/WorldState.h"
namespace test_rpg_battle_tactics_contract {
using namespace eve::rpg;
namespace {
RPGActor* combatant(double attack, double hp) {
    auto* actor = RPGActor::createActor();
    actor->setBaseAttribute("attack", attack);
    actor->setBaseAttribute("defense", 0);
    actor->setBaseAttribute("hp", hp);
    actor->setCurrent("hp", hp);
    return actor;
}
BattleVictoryRequest victoryRequest() {
    BattleVictoryRequest request;
    request.mapId                 = "village";
    request.objectId              = "slime_west";
    request.requiredQuestId       = "rewrite.rpg.victory.quest";
    request.xpAmount              = 30;
    request.xpGrowth              = 1.2;
    request.attributeId           = "gold";
    request.attributeAmount       = 10;
    request.notifyTopic           = "kill";
    request.notifyTarget          = "slime";
    request.notifyAmount          = 1;
    request.defeatCounterId       = "slime_defeats";
    request.defeatCounterAmount   = 1;
    request.levelPointAttributeId = "skill_points";
    request.pointsPerLevel        = 1;
    return request;
}
}  // namespace

TEST_CASE("Tactics chooses injured ally for healing and rejects incompatible replacement") {
    SkillDefinition aid;
    aid.id         = "rewrite.rpg.tactics.aid";
    aid.targetType = "allySingle";
    SkillRegistry::registerSkill(aid);
    SkillDamageSpec healing;
    healing.damageType = "hpHeal";
    healing.formula    = "20";
    BattleSystem::registerSkillDamage(aid.id, healing);
    BattleTacticsCatalogue::clear();
    REQUIRE(
        BattleTacticsCatalogue::replaceFromJsonStrict(
            R"([{"id":"rewrite.rpg.tactics.healer","rules":[{"skillId":"rewrite.rpg.tactics.aid","targetPolicy":"lowestHealthAlly","conditionResource":"hp","belowRatio":0.4},{"skillId":"rewrite.rpg.tactics.aid","targetPolicy":"lowestHealthAlly","conditionResource":"","belowRatio":1.0}]}])")
            .ok());
    CHECK(
        !BattleTacticsCatalogue::replaceFromJsonStrict(
             R"([{"id":"bad","rules":[{"skillId":"rewrite.rpg.tactics.aid","targetPolicy":"lowestHealthEnemy","conditionResource":"","belowRatio":1}]}])")
             .ok());
    CHECK_EQ(BattleTacticsCatalogue::count(), 1);
    auto* hero   = combatant(20, 100);
    auto* healer = combatant(10, 80);
    auto* enemy  = combatant(5, 100);
    hero->setCurrent("hp", 25);
    healer->learnSkill(aid.id);
    Battle battle;
    battle.addActor(hero, BattleSide::Party);
    battle.addActor(healer, BattleSide::Party);
    battle.addActor(enemy, BattleSide::Enemies);
    const auto queued = BattleTacticsCatalogue::queueAction(&battle, healer, "rewrite.rpg.tactics.healer");
    REQUIRE(queued.ok());
    CHECK_EQ(queued.value(), aid.id);
    battle.startRound();
    REQUIRE(battle.executeNextAction());
    CHECK_EQ(hero->getCurrent("hp"), 45.0);
    hero->release();
    healer->release();
    enemy->release();
    BattleTacticsCatalogue::clear();
    SkillRegistry::remove(aid.id);
    BattleSystem::clearSkillDamage();
}

TEST_CASE("Victory settlement commits progression quest reward and encounter consumption together") {
    QuestRegistry::clear();
    QuestDefinition quest;
    quest.id             = "rewrite.rpg.victory.quest";
    quest.startPolicy    = "manual";
    quest.completePolicy = "claim";
    quest.objectives.push_back({"kill", "kill", "slime", 1});
    QuestRegistry::registerQuest(quest);
    Tracker tracker;
    REQUIRE(tracker.activate(quest.id));
    GameState  state;
    WorldState world(state);
    auto*      actor = RPGActor::createActor();
    REQUIRE(actor->restoreProgression(1, 80, 100).ok());
    const auto settled = BattleVictory::settle(*actor, state, tracker, victoryRequest());
    REQUIRE(settled.ok());
    CHECK_EQ(settled.value().levelsGained, 1);
    CHECK_EQ(actor->getLevel(), 2);
    CHECK_EQ(state.getVariable("gold"), 10.0);
    CHECK_EQ(tracker.getState(quest.id), std::string("ready"));
    CHECK(world.isObjectConsumed("village", "slime_west"));
    actor->release();
    QuestRegistry::clear();
}

TEST_CASE("Duplicate encounter rejects victory before mutating any owner") {
    QuestRegistry::clear();
    QuestDefinition quest;
    quest.id          = "rewrite.rpg.victory.quest";
    quest.startPolicy = "manual";
    quest.objectives.push_back({"kill", "kill", "slime", 1});
    QuestRegistry::registerQuest(quest);
    Tracker tracker;
    REQUIRE(tracker.activate(quest.id));
    GameState  state;
    WorldState world(state);
    REQUIRE(world.consumeObject("village", "slime_west").ok());
    auto* actor = RPGActor::createActor();
    REQUIRE(actor->restoreProgression(1, 80, 100).ok());
    CHECK(!BattleVictory::settle(*actor, state, tracker, victoryRequest()).ok());
    CHECK_EQ(actor->getLevel(), 1);
    CHECK_EQ(actor->getXp(), 80.0);
    CHECK_EQ(state.getVariable("gold"), 0.0);
    CHECK_EQ(tracker.getState(quest.id), std::string("active"));
    actor->release();
    QuestRegistry::clear();
}
}  // namespace test_rpg_battle_tactics_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_gameplay_control_rpg) ||     \
    defined(EVE_TEST_ORIGIN_rpg_encounter_catalogue) || defined(EVE_TEST_ORIGIN_rpg_party) || \
    defined(EVE_TEST_ORIGIN_rpg_state_canonical)
#include "PrivateTestScope.h"
#include "common/ECS.h"
#include "common/GameplayControl.h"
#include "common/Value.h"
#include "rpg/Battle.h"
#include "rpg/BattleControl.h"
#include "rpg/EncounterCatalogue.h"
#include "rpg/Party.h"
#include "rpg/Quest.h"
#include "rpg/RPGActor.h"
#include "rpg/RpgState.h"
#include "rpg/Skill.h"
#include "rpg/SkillSystem.h"
namespace test_rpg_composition_contract {
using namespace eve::rpg;
namespace {
eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}
RPGActor* fighter(double attack, double hp) {
    auto* actor = RPGActor::createActor();
    actor->setBaseAttribute("attack", attack);
    actor->setBaseAttribute("defense", 0);
    actor->setBaseAttribute("hp", hp);
    actor->setBaseAttribute("speed", 10);
    actor->setCurrent("hp", hp);
    return actor;
}
}  // namespace

TEST_CASE("Battle control checks revision before queue mutation and executes an authorized attack") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    auto*            hero  = fighter(50, 100);
    auto*            enemy = fighter(1, 20);
    Battle           battle;
    battle.addActor(hero, BattleSide::Party);
    battle.addActor(enemy, BattleSide::Enemies);
    const auto    arena   = subject("00000000-0000-7000-8000-000000000701");
    const auto    heroId  = subject("00000000-0000-7000-8000-000000000702");
    const auto    enemyId = subject("00000000-0000-7000-8000-000000000703");
    BattleControl control(battle, arena);
    REQUIRE(control.bindParticipant(heroId, hero).ok());
    REQUIRE(control.bindParticipant(enemyId, enemy).ok());
    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {heroId}};
    const auto           observed = control.observeGameplay(player, arena);
    REQUIRE(observed.ok());
    eve::GameplayCommand command;
    command.id        = "rewrite-rpg-attack";
    const auto action = eve::LogicalId::parse("rpg:attack");
    REQUIRE(action.has_value());
    command.action           = *action;
    command.subject          = heroId;
    command.observedTick     = observed.value().tick;
    command.expectedRevision = observed.value().revision;
    command.parameters       = eve::Value(eve::Value::Object{{"target", eve::Value(enemyId.format())}});
    {
        command.expectedRevision = 0;
        const auto rejected      = control.submitGameplay(player, arena, command);
        CHECK(!rejected.ok());
        CHECK_EQ(rejected.code(), eve::StatusCode::Conflict);
        CHECK_EQ(enemy->getCurrent("hp"), 20.0);
    };
    {
        command.expectedRevision = observed.value().revision;
        REQUIRE(control.submitGameplay(player, arena, command).ok());
        REQUIRE(
            control.advanceGameplay(player, arena, {eve::SimulationTick(1), eve::Duration::fromNanoseconds(1)}).ok());
        CHECK(battle.isVictory());
    };
    hero->release();
    enemy->release();
}

TEST_CASE("Encounter catalogue validates dependencies before publishing actors") {
    SkillDefinition skill;
    skill.id = "rewrite.rpg.encounter.claw";
    SkillRegistry::registerSkill(skill);
    QuestDefinition quest;
    quest.id = "rewrite.rpg.encounter.quest";
    QuestRegistry::registerQuest(quest);
    EncounterCatalogue::clear();
    const std::string valid =
        R"([{"id":"rewrite.rpg.encounter.slime","displayName":"Slime","members":[{"id":"slime","displayName":"Slime","skillId":"rewrite.rpg.encounter.claw","attack":12,"defense":2.8,"maxHp":42,"speed":5}],"xpReward":40,"xpGrowth":1.2,"goldReward":10,"requiredQuestId":"rewrite.rpg.encounter.quest","notifyTopic":"kill","notifyTarget":"slime","notifyAmount":1,"defeatCounterId":"kills","defeatCounterAmount":1,"levelPointAttributeId":"statPoints","pointsPerLevel":3}])";
    REQUIRE(EncounterCatalogue::replaceFromJsonStrict(valid).ok());
    auto* actor = EncounterCatalogue::createActor("rewrite.rpg.encounter.slime");
    REQUIRE(actor != nullptr);
    CHECK_EQ(actor->getFinalAttribute("attack"), 12.0);
    CHECK_EQ(actor->getCurrent("hp"), 42.0);
    CHECK(actor->knowsSkill(skill.id));
    actor->release();
    const std::string invalid =
        R"([{"id":"bad","displayName":"Bad","members":[{"id":"x","displayName":"X","skillId":"missing","attack":1,"defense":0,"maxHp":1,"speed":0}],"xpReward":1,"xpGrowth":1.2,"goldReward":0,"requiredQuestId":"rewrite.rpg.encounter.quest","notifyTopic":"kill","notifyTarget":"slime","notifyAmount":1,"defeatCounterId":"kills","defeatCounterAmount":1,"levelPointAttributeId":"statPoints","pointsPerLevel":0}])";
    CHECK(!EncounterCatalogue::replaceFromJsonStrict(invalid).ok());
    CHECK(EncounterCatalogue::find("rewrite.rpg.encounter.slime") != nullptr);
    EncounterCatalogue::clear();
    SkillRegistry::remove(skill.id);
    QuestRegistry::remove(quest.id);
}

TEST_CASE("Party preserves roster order and refuses duplicate identity or stale battle composition") {
    Party party;
    auto* hero      = fighter(10, 100);
    auto* companion = fighter(5, 100);
    REQUIRE(party.addMember("hero", hero).ok());
    REQUIRE(party.addMember("companion", companion).ok());
    CHECK_EQ(party.getMemberId(0), std::string("hero"));
    CHECK(!party.addMember("hero", companion).ok());
    CHECK(!party.addMember("alias", hero).ok());
    CHECK_EQ(party.count(), 2);
    companion->release();
    CHECK(party.hasStaleMembers());
    Battle battle;
    CHECK(!party.addToBattle(&battle, BattleSide::Party).ok());
    CHECK_EQ(battle.getActorCount(), 0);
    party.clear();
    REQUIRE(party.addMember("hero", hero).ok());
    REQUIRE(party.addToBattle(&battle, BattleSide::Party).ok());
    CHECK_EQ(battle.getActorCount(), 1);
    hero->release();
}

TEST_CASE("Party checkpoint validates full roster before mutating any actor") {
    Party party;
    auto* hero      = fighter(20, 100);
    auto* companion = fighter(12, 100);
    REQUIRE(party.addMember("hero", hero).ok());
    REQUIRE(party.addMember("companion", companion).ok());
    const auto encoded = party.checkpointJson();
    REQUIRE(encoded.ok());
    hero->setBaseAttribute("attack", 1);
    companion->setBaseAttribute("attack", 2);
    REQUIRE(party.restoreCheckpointJson(encoded.value()).ok());
    CHECK_EQ(hero->getBaseAttribute("attack"), 20.0);
    CHECK_EQ(companion->getBaseAttribute("attack"), 12.0);
    REQUIRE(party.removeMember("companion").ok());
    hero->setBaseAttribute("attack", 7);
    CHECK(!party.restoreCheckpointJson(encoded.value()).ok());
    CHECK_EQ(hero->getBaseAttribute("attack"), 7.0);
    hero->release();
    companion->release();
}

TEST_CASE("Canonical RPG restore rejects malformed later actor without applying earlier actor changes") {
    SkillDefinition skill;
    skill.id = "rewrite.rpg.canonical.fire";
    SkillRegistry::registerSkill(skill);
    auto* first  = RPGActor::createActor();
    auto* second = RPGActor::createActor();
    first->learnSkill(skill.id);
    first->setSkillCooldown(skill.id, 3);
    const auto captured = captureRpgState();
    REQUIRE(captured.ok());
    auto state = captured.value();
    state.at(0).find("skills")->set(skill.id, eve::Value::number(9));
    state.at(1) = eve::Value::integer(42);
    CHECK(!restoreRpgState(state).ok());
    CHECK_EQ(SkillSystem::getCooldownRemaining(first, skill.id), 3.f);
    second->release();
    first->release();
    SkillRegistry::remove(skill.id);
}
}  // namespace test_rpg_composition_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_equipment_upgrade)
#include "PrivateTestScope.h"

#include <cmath>
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/InventoryResourceAccount.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#include "production/Production.h"
#include "rpg/EquipmentSystem.h"
#include "rpg/EquipmentUpgrade.h"
#include "rpg/ForgeBench.h"
#include "rpg/RPG.h"
#include "rpg/RPGActor.h"

namespace test_rpg_equipment_upgrade {
using namespace eve;

TEST_CASE("rpg.equipmentUpgradePaysQueuesAndMutatesEnhanceOnce") {
    inventory::ItemRegistry::clear();
    inventory::InventorySystem::clearEvents();
    inventory::InventorySystem::ensureBuiltins();
    inventory::ItemDefinition blade;
    blade.id        = "sword.ember";
    blade.maxStack  = 1;
    blade.equipSlot = "weapon";
    blade.tags      = {"weapon"};
    inventory::ItemRegistry::registerItem(blade);
    inventory::ItemDefinition stone;
    stone.id       = "emberstone";
    stone.maxStack = 99;
    inventory::ItemRegistry::registerItem(stone);

    inventory::Bag bag(4);
    bag.setId("forge.bag");
    REQUIRE_EQ(bag.addItem("sword.ember", 1), 1);
    REQUIRE_EQ(bag.addItem("emberstone", 3), 3);
    const int slot = bag.findItem("sword.ember");
    REQUIRE(slot >= 0);
    const int instanceId = bag.getSlotInstanceId(slot);
    CHECK_EQ(bag.getSlotProp(slot, rpg::kEquipmentEnhanceProp, "0"), "0");

    auto cost = resource::CostSpec::single("emberstone", 1);
    REQUIRE(cost.ok());
    rpg::EquipmentUpgradeRecipe recipe;
    recipe.id          = "enhance.ember.0";
    recipe.process     = "upgrade";
    recipe.itemId      = "sword.ember";
    recipe.fromLevel   = 0;
    recipe.toLevel     = 1;
    recipe.ingredients = std::move(cost).takeValue();
    recipe.duration    = Duration::fromSeconds(1.0).expect("upgrade duration");

    production::WorkQueue               queue;
    inventory::InventoryResourceAccount account(bag);
    rpg::EquipmentUpgradeRequest        request;
    request.production         = &queue;
    request.ingredients        = &account;
    request.bag                = &bag;
    request.bagSlot            = slot;
    request.expectedInstanceId = instanceId;
    request.owner              = "forge";
    request.recipe             = recipe;
    auto begun                 = rpg::EquipmentUpgrade::begin(std::move(request));
    REQUIRE(begun.ok());
    CHECK_EQ(bag.countItem("emberstone"), 2);
    const std::string taskId = begun.value().productionTaskId;
    REQUIRE(queue.advance({SimulationTick(1), recipe.duration}).ok());
    CHECK_EQ(queue.find(taskId)->get().state, production::TaskState::ReadyToSettle);

    auto settled = rpg::EquipmentUpgrade::settle(queue, bag, taskId);
    REQUIRE(settled.ok());
    CHECK_EQ(settled.value(), 1);
    CHECK_EQ(bag.getSlotProp(slot, rpg::kEquipmentEnhanceProp, "0"), "1");
    CHECK_EQ(bag.getSlotInstanceId(slot), instanceId);

    auto repeated = rpg::EquipmentUpgrade::settle(queue, bag, taskId);
    REQUIRE(repeated.ok());
    CHECK_EQ(repeated.status().code(), StatusCode::NoOp);
    CHECK_EQ(bag.getSlotProp(slot, rpg::kEquipmentEnhanceProp, "0"), "1");
}

TEST_CASE("rpg.equipmentUpgradeRejectsWrongLevelWithoutPayment") {
    inventory::ItemRegistry::clear();
    inventory::InventorySystem::ensureBuiltins();
    inventory::ItemDefinition blade;
    blade.id       = "sword.ember";
    blade.maxStack = 1;
    inventory::ItemRegistry::registerItem(blade);
    inventory::ItemDefinition stone;
    stone.id       = "emberstone";
    stone.maxStack = 99;
    inventory::ItemRegistry::registerItem(stone);

    inventory::Bag bag(2);
    REQUIRE_EQ(bag.addItem("sword.ember", 1), 1);
    REQUIRE_EQ(bag.addItem("emberstone", 2), 2);
    const int slot = bag.findItem("sword.ember");
    bag.setSlotProp(slot, rpg::kEquipmentEnhanceProp, "2");
    auto cost = resource::CostSpec::single("emberstone", 1);
    REQUIRE(cost.ok());
    rpg::EquipmentUpgradeRecipe recipe;
    recipe.id          = "enhance.bad";
    recipe.itemId      = "sword.ember";
    recipe.fromLevel   = 0;
    recipe.toLevel     = 1;
    recipe.ingredients = std::move(cost).takeValue();
    recipe.duration    = Duration::fromSeconds(0.5).expect("upgrade duration");
    production::WorkQueue               queue;
    inventory::InventoryResourceAccount account(bag);
    rpg::EquipmentUpgradeRequest request{&queue, &account, &bag, slot, bag.getSlotInstanceId(slot), "forge", recipe};
    auto                         result = rpg::EquipmentUpgrade::begin(std::move(request));
    CHECK(!result.ok());
    CHECK_EQ(bag.countItem("emberstone"), 2);
    CHECK_EQ(queue.taskCount(), 0);
}

TEST_CASE("rpg.forgeBenchCraftUpgradeAndEnhanceScalesEquipAttack") {
    inventory::ItemRegistry::clear();
    inventory::InventorySystem::ensureBuiltins();
    inventory::ItemDefinition ore;
    ore.id       = "ore.iron";
    ore.maxStack = 99;
    inventory::ItemRegistry::registerItem(ore);
    inventory::ItemDefinition coal;
    coal.id       = "coal";
    coal.maxStack = 99;
    inventory::ItemRegistry::registerItem(coal);
    inventory::ItemDefinition stone;
    stone.id       = "emberstone";
    stone.maxStack = 99;
    inventory::ItemRegistry::registerItem(stone);
    inventory::ItemDefinition blade;
    blade.id        = "sword.ember";
    blade.maxStack  = 1;
    blade.equipSlot = "weapon";
    blade.tags      = {"weapon"};
    inventory::ItemRegistry::registerItem(blade);

    auto* rpgModule = rpg::RPG::create();
    rpgModule->clearAllItemStats();
    REQUIRE_EQ(
        rpgModule->registerItemStatsFromJson("sword.ember", R"([{"attribute":"attack","op":"add","value":12.0}])"), 1);

    inventory::Bag bag(8);
    bag.setId("forge.bag");
    REQUIRE_EQ(bag.addItem("ore.iron", 3), 3);
    REQUIRE_EQ(bag.addItem("coal", 1), 1);
    REQUIRE_EQ(bag.addItem("emberstone", 2), 2);
    inventory::EquipmentSet gear;
    gear.defineSlot("weapon");
    gear.addSlotAllowedTag("weapon", "weapon");
    rpg::RPGActor* hero = rpgModule->newActor();
    REQUIRE(hero != nullptr);
    hero->setBaseAttribute("attack", 8.0);

    rpg::ForgeBench forge;
    REQUIRE(forge.bind(&bag, &gear, hero).ok());
    auto crafted = forge.craftFromJson(R"({
      "id":"forge.ember_blade",
      "process":"craft",
      "ingredients":{"ore.iron":3,"coal":1},
      "outputs":[{"item":"sword.ember","quantity":1}],
      "duration":0.5
    })");
    REQUIRE(crafted.ok());
    REQUIRE(forge.advance(SimulationTick(1), Duration::fromSeconds(0.5).expect("dt")).ok());
    auto settledCraft = forge.settleReady();
    REQUIRE(settledCraft.ok());
    CHECK_EQ(settledCraft.value(), 1);
    CHECK_EQ(bag.countItem("sword.ember"), 1);

    const int slot = bag.findItem("sword.ember");
    REQUIRE(slot >= 0);
    auto upgraded = forge.upgradeFromJson(R"({
      "id":"enhance.ember.0",
      "process":"upgrade",
      "itemId":"sword.ember",
      "fromLevel":0,
      "toLevel":1,
      "ingredients":{"emberstone":1},
      "duration":0.4
    })",
                                          slot, bag.getSlotInstanceId(slot));
    REQUIRE(upgraded.ok());
    REQUIRE(forge.advance(SimulationTick(2), Duration::fromSeconds(0.4).expect("dt")).ok());
    auto settledUpgrade = forge.settleReady();
    REQUIRE(settledUpgrade.ok());
    CHECK_EQ(settledUpgrade.value(), 1);
    CHECK_EQ(bag.getSlotProp(slot, rpg::kEquipmentEnhanceProp, "0"), "1");
    CHECK_EQ(bag.countItem("emberstone"), 1);

    REQUIRE(gear.equipFromBag("weapon", &bag, slot));
    CHECK_EQ(forge.syncEquipment(), 1);
    // base 8 + blade 12 + enhance bonus max(1, floor(12*0.25+0.5))=3 → 23
    CHECK(std::abs(hero->getFinalAttribute("attack") - 23.0) < 1e-9);
}
}  // namespace test_rpg_equipment_upgrade
#endif
