// eve-test-policy:
// {"schema":"evengine.test-policy","version":1,"role":"compile-contract","default":"module-composition","module":"rpg"}
// OBJECT-only process-local interception of ordinary new/new[] for canonical actor preconfiguration.
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <string>
#include <type_traits>
#include "attributes/AttributeSet.h"
#include "common/Capability.h"
#include "common/StateAccess.h"
#include "rpg/ActorInitialization.h"
#include "rpg/Effect.h"
#include "rpg/EffectDefinitionCodec.h"
#include "rpg/EncounterCatalogue.h"
#include "rpg/LevelSystem.h"
#include "rpg/RPGActor.h"
#include "rpg/SimulationEventArchive.h"
#include "rpg/SimulationEvents.h"
#include "rpg/Skill.h"
#include "rpg/SkillSystem.h"
#include "rpg/StatusSystem.h"
namespace {
thread_local bool        measuring    = false;
thread_local std::size_t allocations  = 0;
thread_local std::size_t failureIndex = std::numeric_limits<std::size_t>::max();
std::size_t              actors() {
    std::size_t count = 0;
    auto        view  = ecs::View<eve::rpg::RPGActor>();
    for (auto it = view.begin(); it != view.end(); ++it) ++count;
    return count;
}
bool configured(eve::rpg::RPGActor& actor) {
    return actor.attributes().has() && actor.statuses().has() && actor.skills().has() && actor.progression().has() &&
           actor.vitals().has() && actor.traits().has() && actor.classInfo().has() && actor.executionOrder().has() &&
           actor.identity().has();
}
}  // namespace
void* operator new(std::size_t size) {
    if (measuring && allocations++ == failureIndex) throw std::bad_alloc();
    if (auto* result = std::malloc(size ? size : 1)) return result;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void  operator delete(void* value) noexcept { std::free(value); }
void  operator delete[](void* value) noexcept { std::free(value); }
void  operator delete(void* value, std::size_t) noexcept { std::free(value); }
void  operator delete[](void* value, std::size_t) noexcept { std::free(value); }
int   checkPreparedMember() {
    eve::rpg::SkillDefinition skill;
    skill.id = "prepared.member.allocation";
    eve::rpg::SkillRegistry::registerSkill(skill);
    eve::rpg::EncounterMemberDefinition member;
    member.id            = "member";
    member.displayName   = "Member";
    member.skillId       = skill.id;
    member.attack        = 7;
    member.defense       = 2;
    member.maxHp         = 40;
    member.speed         = 3;
    const auto queries   = eve::cap::listenerCount<eve::IStateQuery>();
    const auto mutations = eve::cap::listenerCount<eve::IStateMutation>();
    allocations          = 0;
    measuring            = true;
    { auto prepared = eve::rpg::PreparedMemberActor::prepare(member).expect("prepare allocation baseline"); }
    measuring                         = false;
    const auto preparationAllocations = allocations;
    for (std::size_t fail = 0; fail < preparationAllocations; ++fail) {
        failureIndex = fail;
        allocations  = 0;
        measuring    = true;
        bool threw   = false;
        try {
            auto prepared = eve::rpg::PreparedMemberActor::prepare(member).expect("prepare failpoint");
        } catch (const std::bad_alloc&) {
            threw = true;
        }
        measuring    = false;
        failureIndex = std::numeric_limits<std::size_t>::max();
        if (!threw || eve::cap::listenerCount<eve::IStateQuery>() != queries ||
            eve::cap::listenerCount<eve::IStateMutation>() != mutations)
            return 10;
    }
    std::string checkpoint;
    {
        ecs::Table       table;
        ecs::ScopedTable scope(table);
        auto             handle = eve::rpg::createInitializedActor(member).expect("checkpoint source");
        auto*            actor  = dynamic_cast<eve::rpg::RPGActor*>(ecs::try_get(handle));
        if (!actor) return 15;
        actor->setCurrent("hp", 17);
        actor->setSkillCooldown(skill.id, 1.5f);
        checkpoint = actor->checkpointJson().expect("checkpoint facts");
        ecs::DestroyEntity(actor);
    }
    std::size_t restoreAllocations = 0;
    {
        auto prepared      = eve::rpg::PreparedMemberActor::prepare(member).expect("pure restore baseline");
        allocations        = 0;
        measuring          = true;
        auto restored      = prepared.restoreCheckpoint(checkpoint);
        measuring          = false;
        restoreAllocations = allocations;
        if (!restored) return 16;
    }
    for (std::size_t fail = 0; fail < restoreAllocations; ++fail) {
        ecs::Table       table;
        ecs::ScopedTable scope(table);
        auto             prepared = eve::rpg::PreparedMemberActor::prepare(member).expect("prepare restore failpoint");
        failureIndex  = fail;
        allocations   = 0;
        measuring     = true;
        bool rejected = false;
        try {
            auto restored = prepared.restoreCheckpoint(checkpoint);
            measuring     = false;
            rejected      = !restored.ok();
        } catch (const std::bad_alloc&) {
            measuring = false;
            rejected  = true;
        }
        failureIndex = std::numeric_limits<std::size_t>::max();
        if (!rejected || actors() != 0 || eve::cap::listenerCount<eve::IStateQuery>() != queries ||
            eve::cap::listenerCount<eve::IStateMutation>() != mutations)
            return 17;
        auto  handle = prepared.create().expect("unchanged candidate after restore failure");
        auto* actor  = dynamic_cast<eve::rpg::RPGActor*>(ecs::try_get(handle));
        if (!actor || actor->getCurrent("hp") != 40 || actor->getSkillCooldown(skill.id) != 0) return 18;
        ecs::DestroyEntity(actor);
    }
    std::size_t creationAllocations = 0;
    {
        ecs::Table       table;
        ecs::ScopedTable scope(table);
        auto             prepared = eve::rpg::PreparedMemberActor::prepare(member).expect("pure creation baseline");
        allocations               = 0;
        measuring                 = true;
        auto created              = prepared.create();
        measuring                 = false;
        creationAllocations       = allocations;
        if (!created || !ecs::try_get(created.value())) return 11;
        ecs::DestroyEntity(ecs::try_get(created.value()));
    }
    for (std::size_t fail = 0; fail < creationAllocations; ++fail) {
        ecs::Table       table;
        ecs::ScopedTable scope(table);
        auto             prepared = eve::rpg::PreparedMemberActor::prepare(member).expect("prepare creation failpoint");
        failureIndex = fail;
        allocations  = 0;
        measuring    = true;
        auto created = prepared.create();
        measuring    = false;
        failureIndex = std::numeric_limits<std::size_t>::max();
        if (created) {
            ecs::DestroyEntity(ecs::try_get(created.value()));
            return 12;
        }
        if (actors() != 0 || eve::cap::listenerCount<eve::IStateQuery>() != queries ||
            eve::cap::listenerCount<eve::IStateMutation>() != mutations)
            return 13;
        auto  retried = prepared.create().expect("retry same pure candidate");
        auto* actor   = dynamic_cast<eve::rpg::RPGActor*>(ecs::try_get(retried));
        if (!actor || !configured(*actor) || actor->getCurrent("hp") != 40 || actor->getBaseAttribute("attack") != 7)
            return 14;
        ecs::DestroyEntity(actor);
    }
    eve::rpg::SkillRegistry::remove(skill.id);
    std::printf(
        "Pure RPG member: %zu prepare, %zu restore and %zu cold-create ordinary allocation failpoints; no publication "
          "during "
          "prepare; same candidate retries\n",
        preparationAllocations, restoreAllocations, creationAllocations);
    return 0;
}
int checkEffectRegistration() {
    eve::rpg::EffectDefinition original;
    original.id             = "allocation.effect.original.long-key";
    original.durationPolicy = "infinite";
    original.modifiers.push_back({"attack", "add", 2, 0});
    const auto originalBytes =
        eve::rpg::encodeEffectDefinition(original).expect("old effect").toJson().expect("old bytes");
    eve::rpg::EffectDefinition replacement = original;
    replacement.modifiers[0].value         = 99;
    for (int i = 0; i < 16; ++i) {
        replacement.tags.push_back("allocation.effect.long-tag-" + std::to_string(i));
        replacement.extra.emplace("allocation.effect.long-extra-key-" + std::to_string(i),
                                  "allocation.effect.long-extra-value-" + std::to_string(i));
    }
    const auto queries   = eve::cap::listenerCount<eve::IStateQuery>();
    const auto mutations = eve::cap::listenerCount<eve::IStateMutation>();
    for (bool replace : {true, false}) {
        auto next = replacement;
        if (!replace) next.id = "allocation.effect.insert.long-key";
        const auto expected = eve::rpg::encodeEffectDefinition(next).expect("new effect").toJson().expect("new bytes");
        eve::rpg::EffectRegistry::registerEffect(original);
        const auto baseline = eve::rpg::EffectRegistry::count();
        allocations         = 0;
        measuring           = true;
        eve::rpg::EffectRegistry::registerEffect(next);
        measuring        = false;
        const auto count = allocations;
        if (!count) return 30;
        if (!replace && !eve::rpg::EffectRegistry::remove(next.id)) return 31;
        for (std::size_t fail = 0; fail < count; ++fail) {
            eve::rpg::EffectRegistry::registerEffect(original);
            allocations  = 0;
            failureIndex = fail;
            measuring    = true;
            bool threw   = false;
            try {
                eve::rpg::EffectRegistry::registerEffect(next);
            } catch (const std::bad_alloc&) {
                measuring = false;
                threw     = true;
            }
            measuring       = false;
            failureIndex    = std::numeric_limits<std::size_t>::max();
            const auto* old = eve::rpg::EffectRegistry::find(original.id);
            if (!threw || !old || eve::rpg::EffectRegistry::count() != baseline ||
                (!replace && eve::rpg::EffectRegistry::find(next.id)) ||
                eve::rpg::encodeEffectDefinition(*old)
                        .expect("retained old record")
                        .toJson()
                        .expect("old projection") != originalBytes ||
                eve::cap::listenerCount<eve::IStateQuery>() != queries ||
                eve::cap::listenerCount<eve::IStateMutation>() != mutations)
                return 32;
            eve::rpg::EffectRegistry::registerEffect(next);
            const auto* actual = eve::rpg::EffectRegistry::find(next.id);
            if (!actual ||
                eve::rpg::encodeEffectDefinition(*actual).expect("retry record").toJson().expect("retry bytes") !=
                    expected)
                return 33;
            if (!replace && !eve::rpg::EffectRegistry::remove(next.id)) return 34;
        }
        if (!eve::rpg::EffectRegistry::remove(original.id)) return 35;
        std::printf(
            "Canonical RPG effect %s: %zu ordinary allocation failpoints; prior definition/count/capabilities "
            "unchanged; retry succeeds\n",
            replace ? "replacement" : "insertion", count);
    }
    return 0;
}
int checkOperationRegistration() {
    using Registry                      = eve::attributes::AttributeOperationRegistry;
    const std::string      key          = "allocation.attribute.operation.long-key";
    const std::string      oldRevision  = "source:original.attribute.implementation.long-revision";
    const std::string      nextRevision = "source:replacement.attribute.implementation.long-revision";
    std::array<double, 64> payload{};
    payload[0]              = 20;
    Registry::Function next = [payload](double a, double b) { return payload[0] + a + b; };
    for (bool replace : {false, true}) {
        Registry baseline;
        if (replace)
            baseline.registerVersionedOperation(key, oldRevision, [](double a, double b) { return a - b; })
                .expect("baseline registration");
        allocations = 0;
        measuring   = true;
        baseline.registerVersionedOperation(key, nextRevision, next).expect("measure registration");
        measuring        = false;
        const auto count = allocations;
        if (!count) return 40;
        for (std::size_t fail = 0; fail < count; ++fail) {
            Registry operations;
            if (replace)
                operations.registerVersionedOperation(key, oldRevision, [](double a, double b) { return a - b; })
                    .expect("old registration");
            allocations  = 0;
            failureIndex = fail;
            measuring    = true;
            bool threw   = false;
            try {
                operations.registerVersionedOperation(key, nextRevision, next).expect("injected registration");
            } catch (const std::bad_alloc&) {
                measuring = false;
                threw     = true;
            }
            measuring    = false;
            failureIndex = std::numeric_limits<std::size_t>::max();
            if (!threw) return 41;
            if (replace) {
                auto old = operations.implementationRevision(key);
                if (!old || old.value() != oldRevision || operations.apply(key, 10, 2) != 8) return 42;
            } else if (operations.has(key) || operations.implementationRevision(key).ok())
                return 43;
            operations.registerVersionedOperation(key, nextRevision, next).expect("same input retry");
            if (operations.implementationRevision(key).expect("new revision") != nextRevision ||
                operations.apply(key, 10, 2) != 32)
                return 44;
        }
        std::printf(
            "Versioned attribute operation %s: %zu ordinary allocation failpoints; callback/revision atomic; retry "
            "succeeds\n",
            replace ? "replacement" : "insertion", count);
    }
    return 0;
}
int checkStackPolicyRegistration() {
    using Status                      = eve::rpg::StatusSystem;
    const std::string     key         = "allocation.custom.stack.policy.long-key";
    const std::string     original    = "source:original.stack.implementation.long-revision";
    const std::string     replacement = "source:replacement.stack.implementation.long-revision";
    std::array<int, 64>   payload{};
    Status::StackPolicyFn next = [payload](eve::rpg::RPGActor*, eve::rpg::StatusInstance&,
                                           const eve::rpg::EffectDefinition&,
                                           const std::string&) { return payload[0]; };
    Status::registerVersionedStackPolicy(key, original, next).expect("warm registry");
    Status::unregisterStackPolicy(key);
    for (bool replace : {false, true}) {
        if (replace) Status::registerVersionedStackPolicy(key, original, next).expect("baseline");
        allocations = 0;
        measuring   = true;
        Status::registerVersionedStackPolicy(key, replacement, next).expect("measure stack registry");
        measuring        = false;
        const auto count = allocations;
        if (!count) return 50;
        Status::unregisterStackPolicy(key);
        for (std::size_t fail = 0; fail < count; ++fail) {
            if (replace) Status::registerVersionedStackPolicy(key, original, next).expect("original policy");
            allocations  = 0;
            failureIndex = fail;
            measuring    = true;
            bool threw   = false;
            try {
                Status::registerVersionedStackPolicy(key, replacement, next).expect("injected stack registry");
            } catch (const std::bad_alloc&) {
                measuring = false;
                threw     = true;
            }
            measuring    = false;
            failureIndex = std::numeric_limits<std::size_t>::max();
            if (!threw) return 51;
            auto retained = Status::stackPolicyRevision(key);
            if (replace) {
                if (!retained || retained.value() != original) return 52;
            } else if (retained.ok() || Status::hasStackPolicy(key))
                return 53;
            Status::registerVersionedStackPolicy(key, replacement, next).expect("retry registration");
            if (Status::stackPolicyRevision(key).expect("new policy revision") != replacement) return 54;
            Status::unregisterStackPolicy(key);
        }
        std::printf(
            "Versioned RPG stack policy %s: %zu ordinary allocation failpoints; revision replacement atomic; retry "
            "succeeds\n",
            replace ? "replacement" : "insertion", count);
    }
    return 0;
}
int checkLethalDamagePublication() {
    ecs::Table       table;
    ecs::ScopedTable scope(table);
    auto*            actor = eve::rpg::RPGActor::createActor();
    actor->setBaseAttribute("hp", 40);
    const std::string source(128, 'd');
    std::size_t       count = 0;
    for (std::size_t attempt = 0; attempt <= count; ++attempt) {
        eve::rpg::detail::SimulationEvents     events;
        eve::rpg::detail::SimulationEventScope eventScope(events);
        actor->setCurrent("hp", 40);
        allocations  = 0;
        failureIndex = attempt == 0 ? std::numeric_limits<std::size_t>::max() : attempt - 1;
        measuring    = true;
        bool threw   = false;
        try {
            eve::rpg::VitalsSystem::takeDamage(actor, "hp", 40, source);
        } catch (const std::bad_alloc&) {
            threw = true;
        }
        measuring    = false;
        failureIndex = std::numeric_limits<std::size_t>::max();
        if (attempt == 0) {
            count = allocations;
            if (count == 0 || threw || actor->getCurrent("hp") != 0 || events.vitals.size() != 2) return 60;
        } else {
            if (!threw || actor->getCurrent("hp") != 40 || !events.vitals.empty()) return 61;
            eve::rpg::VitalsSystem::takeDamage(actor, "hp", 40, source);
            if (actor->getCurrent("hp") != 0 || events.vitals.size() != 2 || events.vitals[0].action != "damage" ||
                events.vitals[1].action != "death" || ecs::try_get(events.vitals[1].actor.handle()) != actor)
                return 62;
        }
    }
    actor->release();
    std::printf("Canonical lethal damage: %zu ordinary allocation failures preserve HP/events; retry publishes once\n",
                count);
    return 0;
}
template <class Event>
int checkAppendPollPublication(std::vector<Event> eve::rpg::detail::SimulationEvents::*member,
                               void (*poll)(std::vector<Event>&), const char* name) {
    auto label = [](Event& event) -> std::string& {
        if constexpr (std::is_same_v<Event, eve::rpg::SkillCastEvent>)
            return event.skillId;
        else
            return event.effectId;
    };
    auto reference = [](Event& event) -> eve::rpg::ActorEventRef& {
        if constexpr (std::is_same_v<Event, eve::rpg::SkillCastEvent>)
            return event.caster;
        else
            return event.actor;
    };
    const std::string prefix(128, 'p'), first(128, 'a'), second(128, 'b');
    std::size_t       count = 0;
    for (std::size_t pass = 0; pass <= count; ++pass) {
        ecs::Table                             table;
        ecs::ScopedTable                       scope(table);
        eve::rpg::detail::SimulationEvents     events;
        eve::rpg::detail::SimulationEventScope eventScope(events);
        auto*                                  actor = eve::rpg::RPGActor::createActor();
        auto&                                  queue = events.*member;
        queue.resize(2);
        label(queue[0])     = first;
        label(queue[1])     = second;
        reference(queue[0]) = eve::rpg::ActorEventRef(*actor);
        reference(queue[1]) = eve::rpg::ActorEventRef(*actor);
        std::vector<Event> out(1);
        label(out[0]) = prefix;
        allocations   = 0;
        failureIndex  = pass == 0 ? std::numeric_limits<std::size_t>::max() : pass - 1;
        measuring     = true;
        bool threw    = false;
        try {
            poll(out);
        } catch (const std::bad_alloc&) {
            threw = true;
        }
        measuring    = false;
        failureIndex = std::numeric_limits<std::size_t>::max();
        if (pass == 0) {
            count = allocations;
            if (count == 0 || threw) return 110;
        } else {
            if (!threw || out.size() != 1 || label(out[0]) != prefix || queue.size() != 2 || label(queue[0]) != first ||
                label(queue[1]) != second || !reference(queue[0]).resolve() || !reference(queue[1]).resolve())
                return 111;
            poll(out);
        }
        if (out.size() != 3 || !queue.empty() || label(out[0]) != prefix || label(out[1]) != first ||
            label(out[2]) != second)
            return 112;
        for (std::size_t i = 1; i < 3; ++i) {
            auto resolved = reference(out[i]).resolve();
            if (!resolved || &resolved->get() != actor) return 113;
        }
        std::size_t growthAllocations = 0;
        for (int delivery = 0; delivery < 32; ++delivery) {
            queue.resize(1);
            label(queue[0])     = first;
            reference(queue[0]) = eve::rpg::ActorEventRef(*actor);
            allocations         = 0;
            measuring           = true;
            poll(out);
            measuring = false;
            growthAllocations += allocations;
            if (!queue.empty()) return 114;
        }
        if (out.size() != 35 || label(out[0]) != prefix || growthAllocations > 6) return 115;
    }
    std::printf("%s polling: %zu allocation failures preserve both owners and prefix; retry drains once\n", name,
                count);
    return 0;
}

int checkProgressionPublication() {
    std::size_t count = 0;
    {
        ecs::Table                             table;
        ecs::ScopedTable                       scope(table);
        eve::rpg::detail::SimulationEvents     events;
        eve::rpg::detail::SimulationEventScope eventScope(events);
        auto*                                  actor = eve::rpg::RPGActor::createActor();
        actor->setXpToNext(10);
        events.levels.emplace_back();
        auto prepared  = eve::rpg::LevelSystem::prepareGainXp(actor, 35, 1).expect("progression baseline");
        allocations    = 0;
        failureIndex   = std::numeric_limits<std::size_t>::max();
        measuring      = true;
        auto committed = eve::rpg::LevelSystem::commitGainXp(std::move(prepared));
        measuring      = false;
        count          = allocations;
        if (!committed || committed.value() != 3 || count == 0) return 100;
    }
    for (std::size_t fail = 0; fail < count; ++fail) {
        ecs::Table                             table;
        ecs::ScopedTable                       scope(table);
        eve::rpg::detail::SimulationEvents     events;
        eve::rpg::detail::SimulationEventScope eventScope(events);
        auto*                                  actor = eve::rpg::RPGActor::createActor();
        actor->setXpToNext(10);
        events.levels.emplace_back();
        events.levels.back().newLevel = 99;
        auto prepared  = eve::rpg::LevelSystem::prepareGainXp(actor, 35, 1).expect("progression failpoint");
        allocations    = 0;
        failureIndex   = fail;
        measuring      = true;
        auto committed = eve::rpg::LevelSystem::commitGainXp(std::move(prepared));
        measuring      = false;
        failureIndex   = std::numeric_limits<std::size_t>::max();
        if (committed.ok() || actor->getLevel() != 1 || actor->getXp() != 0 || actor->getXpToNext() != 10 ||
            events.levels.size() != 1 || events.levels[0].newLevel != 99)
            return 101;
        auto retry = eve::rpg::LevelSystem::prepareGainXp(actor, 35, 1).expect("progression retry");
        if (eve::rpg::LevelSystem::commitGainXp(std::move(retry)).expect("publish all transitions") != 3 ||
            actor->getLevel() != 4 || actor->getXp() != 5 || events.levels.size() != 4)
            return 102;
        for (std::size_t i = 1; i < 4; ++i) {
            const auto& event     = events.levels[i];
            auto        reference = event.actor.resolve();
            if (!reference || &reference->get() != actor || event.previousLevel != static_cast<int>(i) ||
                event.newLevel != static_cast<int>(i + 1))
                return 103;
        }
    }
    std::printf(
        "Progression publication: %zu allocation failures preserve state and event prefix; retry publishes all "
        "transitions\n",
        count);
    return 0;
}

int checkActorIdentityRegistration() {
    const auto id =
        eve::SubjectRef::fromPersistentId(*eve::PersistentId::parse("00000000-0000-7000-8000-000000000151"));
    std::size_t count = 0;
    {
        ecs::Table       table;
        ecs::ScopedTable scope(table);
        auto*            actor = eve::rpg::RPGActor::create();
        allocations            = 0;
        failureIndex           = std::numeric_limits<std::size_t>::max();
        measuring              = true;
        auto registered        = eve::rpg::bindActorIdentity(*actor, id);
        measuring              = false;
        count                  = allocations;
        if (!registered || count == 0) return 120;
    }
    for (std::size_t fail = 0; fail < count; ++fail) {
        ecs::Table              table;
        ecs::ScopedTable        scope(table);
        auto*                   actor = eve::rpg::RPGActor::create();
        eve::rpg::ActorEventRef before(*actor);
        allocations  = 0;
        failureIndex = fail;
        measuring    = true;
        bool threw = false, published = false;
        try {
            auto registered = eve::rpg::bindActorIdentity(*actor, id);
            published       = registered.ok();
        } catch (const std::bad_alloc&) {
            threw = true;
        }
        measuring    = false;
        failureIndex = std::numeric_limits<std::size_t>::max();
        if (!threw || published || eve::rpg::actorSubject(*actor) || before.capturedSubject() ||
            actor->attributes().has() || actor->skills().has())
            return 121;
        eve::rpg::bindActorIdentity(*actor, id).expect("retry identity registration");
        if (eve::rpg::actorSubject(*actor) != std::optional<eve::SubjectRef>{id} || before.capturedSubject())
            return 122;
        eve::rpg::ActorEventRef after(*actor);
        if (after.capturedSubject() != std::optional<eve::SubjectRef>{id}) return 123;
    }
    std::printf("Actor identity: %zu registration allocation failures preserve unregistered facts and allow retry\n",
                count);
    return 0;
}

int checkEventArchiveFailures() {
    using namespace eve;
    using namespace eve::rpg;
    const auto epoch   = *PersistentId::parse("00000000-0000-7000-8000-000000000152");
    const auto subject = SubjectRef::fromPersistentId(epoch);
    const auto hash    = [](std::string_view input) {
        ContentId::Bytes bytes{};
        std::uint64_t    n = 14695981039346656037ULL;
        for (unsigned char c : input) n = (n ^ c) * 1099511628211ULL;
        for (auto& byte : bytes) {
            n ^= n >> 12;
            n ^= n << 25;
            n ^= n >> 27;
            byte = static_cast<std::uint8_t>(n >> 24);
        }
        return Result<ContentId>::success(ContentId(bytes));
    };
    ecs::Table source, destination;
    auto*      actor  = RPGActor::create(source);
    auto*      target = RPGActor::create(destination);
    bindActorIdentity(*actor, subject).expect("source identity");
    bindActorIdentity(*target, subject).expect("target identity");
    const ActorEventRef                ref(*actor);
    eve::rpg::detail::SimulationEvents queues;
    queues.casts.push_back({ref, {}, "skill"});
    queues.ticks.push_back({ref, 1, "burn", "skill", 1});
    queues.changes.push_back({ref, 1, "burn", "skill", "expire", 0, ""});
    queues.vitals.push_back({ref, "hp", "death", 8, "burn", 0, 8});
    queues.levels.push_back({ref, 1, 2});
    auto archive  = SimulationEventArchive::capture(queues, epoch, Revision(1), SimulationTick(7)).expect("archive");
    auto envelope = archive.seal(hash).expect("envelope");
    for (int stage = 0; stage != 4; ++stage) {
        auto attempt = [&]() {
            if (stage == 0) return SimulationEventArchive::capture(queues, epoch, Revision(1), SimulationTick(7)).ok();
            if (stage == 1) return archive.seal(hash).ok();
            if (stage == 2) return SimulationEventArchive::parse(envelope, epoch, hash).ok();
            return archive.prepareRestore(destination).ok();
        };
        failureIndex        = std::numeric_limits<std::size_t>::max();
        allocations         = 0;
        measuring           = true;
        const bool baseline = attempt();
        measuring           = false;
        const auto count    = allocations;
        if (!baseline || !count) return 130;
        for (std::size_t fail = 0; fail < count; ++fail) {
            allocations  = 0;
            failureIndex = fail;
            measuring    = true;
            bool threw = false, published = false;
            try {
                published = attempt();
            } catch (const std::bad_alloc&) {
                threw = true;
            }
            measuring    = false;
            failureIndex = std::numeric_limits<std::size_t>::max();
            if (!threw || published || queues.casts.size() != 1 || queues.ticks.size() != 1 ||
                queues.changes.size() != 1 || queues.vitals.size() != 1 || queues.levels.size() != 1 ||
                actorSubject(*actor) != subject || actorSubject(*target) != subject || target->attributes().has())
                return 131;
            if (!attempt()) return 132;
        }
        std::printf(
            "Event archive stage %d: %zu ordinary allocation failures preserve source/target and permit retry\n", stage,
            count);
    }
    return 0;
}

int main() {
    {
        ecs::Table       warmTable;
        ecs::ScopedTable warmScope(warmTable);
        auto*            warm = eve::rpg::RPGActor::createActor();
        if (!warm || !configured(*warm)) return 1;
        warm->release();
    }
    const auto  queries   = eve::cap::listenerCount<eve::IStateQuery>();
    const auto  mutations = eve::cap::listenerCount<eve::IStateMutation>();
    std::size_t count     = 0;
    {
        ecs::Table       table;
        ecs::ScopedTable scope(table);
        allocations    = 0;
        measuring      = true;
        auto* baseline = eve::rpg::RPGActor::createActor();
        measuring      = false;
        count          = allocations;
        if (!baseline || !configured(*baseline) || count == 0) return 2;
        baseline->release();
    }
    for (std::size_t fail = 0; fail < count; ++fail) {
        ecs::Table       table;
        ecs::ScopedTable scope(table);
        allocations               = 0;
        failureIndex              = fail;
        measuring                 = true;
        bool                threw = false;
        eve::rpg::RPGActor* actor = nullptr;
        try {
            actor = eve::rpg::RPGActor::createActor();
        } catch (const std::bad_alloc&) {
            threw = true;
        }
        measuring    = false;
        failureIndex = std::numeric_limits<std::size_t>::max();
        if (actor) actor->release();
        if (!threw || actors() != 0 || eve::cap::listenerCount<eve::IStateQuery>() != queries ||
            eve::cap::listenerCount<eve::IStateMutation>() != mutations) {
            std::fprintf(stderr, "RPG actor allocation %zu left partial publication\n", fail);
            return 3;
        }
        auto* retry = eve::rpg::RPGActor::createActor();
        if (!retry || !configured(*retry)) return 4;
        retry->release();
    }
    std::printf(
        "Canonical RPG actor creation: %zu cold-table ordinary allocation failpoints; all configured components ready; "
        "no "
        "roots or capability leaks\n",
        count);
    const auto archive = checkEventArchiveFailures();
    if (archive != 0) return archive;
    const auto identity = checkActorIdentityRegistration();
    if (identity != 0) return identity;
    const auto casts = checkAppendPollPublication(&eve::rpg::detail::SimulationEvents::casts,
                                                  eve::rpg::SkillSystem::pollCastEvents, "Cast");
    if (casts != 0) return casts;
    const auto ticks = checkAppendPollPublication(&eve::rpg::detail::SimulationEvents::ticks,
                                                  eve::rpg::StatusSystem::pollTicks, "Tick");
    if (ticks != 0) return ticks;
    const auto changes = checkAppendPollPublication(&eve::rpg::detail::SimulationEvents::changes,
                                                    eve::rpg::StatusSystem::pollChanges, "Change");
    if (changes != 0) return changes;
    const auto progression = checkProgressionPublication();
    if (progression != 0) return progression;
    const auto damage = checkLethalDamagePublication();
    if (damage != 0) return damage;
    const auto member = checkPreparedMember();
    if (member != 0) return member;
    const auto effects = checkEffectRegistration();
    if (effects != 0) return effects;
    const auto operations = checkOperationRegistration();
    return operations == 0 ? checkStackPolicyRegistration() : operations;
}
