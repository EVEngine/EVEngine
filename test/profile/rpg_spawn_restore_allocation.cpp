// eve-test-policy:
// {"schema":"evengine.test-policy","version":1,"role":"compile-contract","default":"module-composition","module":"rpg_spawn"}
// OBJECT-only ordinary new/new[] interception around exact archived domain admission.
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <optional>
#include "common/Capability.h"
#include "common/ConditionKeys.h"
#include "common/StateAccess.h"
#include "rpg/AttributeSystem.h"
#include "rpg/EffectDefinitionCodec.h"
#include "rpg/ImplementationRequirement.h"
#include "rpg/SimulationEvents.h"
#include "rpg/SkillConditionBindings.h"
#include "rpg/SkillSystem.h"
#include "rpg/StatusSystem.h"
#include "rpg/VitalsSystem.h"
#include "rpg/spawn/RPGSpawnArchive.h"
#include "rpg/spawn/RPGSpawnCapture.h"
#include "rpg/spawn/RPGSpawnJointRestore.h"
#include "rpg/spawn/RPGSpawnRestoreState.h"
#include "rpg/spawn/RPGSpawnSession.h"
#include "spawn/SpawnRestoreState.h"
#include "unit/rpg_spawn/RPGSpawnFixture.h"
namespace {
thread_local bool           measuring = false, injected = false;
thread_local std::size_t    allocations = 0, failureIndex = std::numeric_limits<std::size_t>::max();
eve::Result<eve::ContentId> hash(std::string_view text) {
    std::uint64_t state = 14695981039346656037ULL;
    for (const unsigned char byte : text) state = (state ^ byte) * 1099511628211ULL;
    eve::ContentId::Bytes bytes{};
    for (auto& byte : bytes) {
        state = state * 6364136223846793005ULL + 1;
        byte  = static_cast<std::uint8_t>(state >> 32);
    }
    return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
}
}  // namespace
void* operator new(std::size_t size) {
    if (measuring && allocations++ == failureIndex) {
        injected = true;
        throw std::bad_alloc();
    }
    if (auto* result = std::malloc(size ? size : 1)) return result;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void  operator delete(void* value) noexcept { std::free(value); }
void  operator delete[](void* value) noexcept { std::free(value); }
void  operator delete(void* value, std::size_t) noexcept { std::free(value); }
void  operator delete[](void* value, std::size_t) noexcept { std::free(value); }
int   checkAdmission(unsigned mode) {
    const bool                             withEffects         = mode == 1 || mode == 2 || mode == 4 || mode == 5;
    const bool                             withImplementations = mode == 2 || mode == 5;
    unsigned                               calls               = 0;
    test_rpg_spawn::Fixture                fixture;
    eve::rpg::detail::SimulationEvents     simulationQueues;
    eve::rpg::detail::SimulationEventScope eventScope(simulationQueues);
    fixture.commit();
    if (withEffects) {
        eve::rpg::SkillDefinition skill;
        skill.id             = "spawn.basic";
        skill.grantedEffects = {"allocation.archived.effect"};
        eve::rpg::SkillRegistry::registerSkill(skill);
        eve::rpg::EffectDefinition effect;
        effect.id             = "allocation.archived.effect";
        effect.durationPolicy = "duration";
        effect.duration       = 3;
        effect.modifiers.push_back({"attack", withImplementations ? "allocation.external" : "add", -2, 0});
        if (withImplementations) {
            effect.stackPolicy = "allocation.external";
            eve::rpg::AttributeSystem::customOps()
                .registerVersionedOperation("allocation.external", "operation:v1",
                                              [&](double a, double b) {
                                                ++calls;
                                                return a + b;
                                            })
                .expect("actual operation");
            eve::rpg::StatusSystem::registerVersionedStackPolicy(
                "allocation.external", "stack:v1",
                [&](eve::rpg::RPGActor*, eve::rpg::StatusInstance&, const eve::rpg::EffectDefinition&,
                    const std::string&) {
                    ++calls;
                    return -1;
                })
                .expect("actual stack policy");
        }
        effect.tags  = {"debuff"};
        effect.extra = {{"icon", "allocation/effect.png"}};
        eve::rpg::EffectRegistry::registerEffect(effect);
    }

    std::shared_ptr<const eve::rpg::SkillConditionBindings> conditions;
    if (mode == 3) {
        eve::condition::ScriptConditionDeclaration declaration{
            "allocation.condition", {"state.zone"}, eve::condition::DeterminismLevel::TickDeterministic};
        eve::rpg::SkillDefinition skill;
        skill.id            = "spawn.basic";
        skill.castCondition = eve::condition::Condition::all(
            {eve::condition::Condition::policyCall("allocation.condition", {}, declaration),
               eve::condition::authorityAllow("restore"), eve::condition::authorityAllow("inspect")});
        eve::rpg::SkillRegistry::registerSkill(skill);
        std::vector<eve::rpg::SkillConditionPolicy> policies{
              {"allocation.condition", "policy:v1", declaration,
               [&](const eve::rpg::RPGActor*, const eve::Value&) {
                 ++calls;
                 return std::optional<eve::condition::ConditionResult>(eve::condition::ConditionResult::success());
             }},
              {"authority.allow",
               "authority:v1",
               {},
               eve::rpg::SkillConditionPolicy::Authority([&](const eve::rpg::RPGActor*, std::string_view) {
                 ++calls;
                 return std::optional<bool>(true);
             })}};

        conditions = eve::rpg::SkillConditionBindings::prepare(policies).expect("actual immutable policy owner");
        auto view  = ecs::View<eve::rpg::RPGActor>(*fixture.table);
        for (auto it = view.begin(); it != view.end(); ++it) it.entity()->skills()->conditionBindings = conditions;
    }
    if (mode == 4 || mode == 5) {
        eve::rpg::EffectDefinition periodic;
        periodic.id             = "allocation.active.periodic";
        periodic.durationPolicy = "duration";
        periodic.duration       = 4;
        periodic.period         = 1;
        eve::rpg::EffectRegistry::registerEffect(periodic);
        ecs::ScopedTable scope(*fixture.table);
        {
            auto view = ecs::View<eve::rpg::RPGActor>(*fixture.table);
            for (auto it = view.begin(); it != view.end(); ++it) {
                eve::rpg::StatusSystem::apply(it.entity(), "allocation.archived.effect", "active")
                    .expect("actual direct modifier");
                eve::rpg::StatusSystem::apply(it.entity(), periodic.id, "periodic").expect("actual periodic effect");
            }
        }
        eve::rpg::StatusSystem::update(0.375).expect("actual partial period");
        calls = 0;
    }
    if (mode == 6) {
        eve::rpg::SkillDefinition skill;
        skill.id       = "spawn.basic";
        skill.castTime = 2;
        eve::rpg::SkillRegistry::registerSkill(skill);
        ecs::ScopedTable                 scope(*fixture.table);
        std::vector<eve::rpg::RPGActor*> actors;
        {
            auto view = ecs::View<eve::rpg::RPGActor>(*fixture.table);
            for (auto it = view.begin(); it != view.end(); ++it) actors.push_back(it.entity());
        }
        if (!eve::rpg::SkillSystem::beginCast(actors[0], skill.id, actors[1]) ||
            !eve::rpg::SkillSystem::beginCast(actors[1], skill.id, actors[0]))
            return 5;
        eve::rpg::SkillSystem::update(0.375f);
    }
    auto epoch   = fixture.request.operation;
    auto frozen  = eve::rpg_spawn::RPGSpawnCapture::capture(*fixture.table, epoch).expect("freeze real actors");
    auto saved   = frozen.seal(eve::Revision(1), eve::SimulationTick(1), hash).expect("seal participant");
    auto archive = eve::rpg_spawn::RPGSpawnArchive::parse(saved, epoch, hash).expect("structural archive");
    {
        auto view = ecs::View<eve::rpg::RPGActor>(*fixture.table);
        for (auto it = view.begin(); it != view.end(); ++it) it.entity()->skills()->conditionBindings.reset();
    }
    // Pure archive admission must remain independent of live skill content.
    eve::rpg::SkillRegistry::clear();
    eve::rpg::EffectRegistry::clear();
    eve::rpg::AttributeSystem::customOps().unregisterOperation("allocation.external");
    eve::rpg::StatusSystem::unregisterStackPolicy("allocation.external");
    const auto queries     = eve::cap::listenerCount<eve::IStateQuery>();
    const auto mutations   = eve::cap::listenerCount<eve::IStateMutation>();
    const auto sourceUnit  = archive.units()[0].json;
    const auto checkpoint  = archive.actors()[0].checkpointJson;
    const auto effectBytes = withEffects ? eve::rpg::encodeEffectDefinition(archive.effects()[0])
                                               .expect("effect source")
                                               .toJson()
                                               .expect("effect bytes")
                                           : std::string{};

    if (fixture.count() != 2) return 1;
    allocations      = 0;
    measuring        = true;
    auto baseline    = eve::rpg_spawn::RPGSpawnRestoreState::prepare(archive, conditions);
    measuring        = false;
    const auto count = allocations;
    if (!baseline || baseline.value().actorCount() != 2 || count == 0) return 2;
    for (std::size_t fail = 0; fail < count; ++fail) {
        allocations   = 0;
        failureIndex  = fail;
        injected      = false;
        measuring     = true;
        bool rejected = false;
        try {
            auto result = eve::rpg_spawn::RPGSpawnRestoreState::prepare(archive, conditions);
            measuring   = false;
            rejected    = !result.ok();
        } catch (const std::bad_alloc&) {
            measuring = false;
            rejected  = true;
        }
        failureIndex = std::numeric_limits<std::size_t>::max();
        if (!injected || !rejected || fixture.count() != 2 || eve::rpg::SkillRegistry::count() != 0 ||
            eve::rpg::EffectRegistry::count() != 0 || calls != 0 ||
            eve::rpg::AttributeSystem::customOps().has("allocation.external") ||
            eve::rpg::StatusSystem::hasStackPolicy("allocation.external") ||
            (withImplementations &&
             (archive.implementations().size() != 2 || archive.implementations()[0].revision != "stack:v1" ||
              archive.implementations()[1].revision != "operation:v1")) ||
            (withEffects && eve::rpg::encodeEffectDefinition(archive.effects()[0])
                                    .expect("unchanged effect")
                                    .toJson()
                                    .expect("effect projection") != effectBytes) ||
            eve::cap::listenerCount<eve::IStateQuery>() != queries ||
            eve::cap::listenerCount<eve::IStateMutation>() != mutations || archive.units()[0].json != sourceUnit ||
            archive.actors()[0].checkpointJson != checkpoint ||
            fixture.sourceWorld->inspect(fixture.request.source).expect("unchanged ledger").alive != 0) {
            std::fprintf(stderr, "RPG archive admission failpoint %zu violated isolation\n", fail);
            return 3;
        }
        auto retry = eve::rpg_spawn::RPGSpawnRestoreState::prepare(archive, conditions).expect("same archive retry");
        if (retry.actorCount() != 2 || retry.unitRevisionCount() != 1 || retry.saveEpoch() != epoch) return 4;
    }
    std::printf(
        "RPG archive domain admission (%s): %zu ordinary allocation failpoints; roots/content/capabilities/core ledger "
          "unchanged; same archive retry succeeds\n",
        mode == 6             ? "active linked casts"
          : mode == 5           ? "active custom and periodic effects"
          : mode == 4           ? "active builtin and periodic effects"
          : mode == 3           ? "actor condition bindings"
          : withImplementations ? "versioned custom implementations"
          : withEffects         ? "direct effects"
                                : "no effects",
        count);
    return 0;
}

int checkJointAdmission() {
    test_rpg_spawn::Fixture fixture;
    fixture.sourceWorld->advance({eve::SimulationTick(1), eve::Duration(0)}).expect("real core/domain batch");
    const auto epoch      = fixture.request.operation.child("joint-epoch");
    auto       frozenCore = fixture.sourceWorld->capture(epoch, eve::Revision(1)).expect("core freeze");
    auto       core       = frozenCore.seal(hash).expect("core envelope");
    auto       domain     = fixture.factory->capture(epoch, frozenCore.unitReferences())
                      .expect("domain freeze")
                      .seal(eve::Revision(1), eve::SimulationTick(1), hash)
                      .expect("domain envelope");
    const auto coreBytes   = core.payload.toJson().expect("core source");
    const auto domainBytes = domain.payload.toJson().expect("domain source");
    eve::rpg::SkillRegistry::clear();
    eve::rpg::EffectRegistry::clear();
    const auto queries   = eve::cap::listenerCount<eve::IStateQuery>();
    const auto mutations = eve::cap::listenerCount<eve::IStateMutation>();
    allocations          = 0;
    measuring            = true;
    auto baseline        = eve::rpg_spawn::RPGSpawnJointRestore::prepare(core, domain, epoch, hash);
    measuring            = false;
    const auto count     = allocations;
    if (!baseline || baseline.value().core().trackedCount() != 2 || count == 0) return 10;
    for (std::size_t fail = 0; fail < count; ++fail) {
        allocations   = 0;
        failureIndex  = fail;
        injected      = false;
        measuring     = true;
        bool rejected = false;
        try {
            auto result = eve::rpg_spawn::RPGSpawnJointRestore::prepare(core, domain, epoch, hash);
            measuring   = false;
            rejected    = !result.ok();
        } catch (const std::bad_alloc&) {
            measuring = false;
            rejected  = true;
        }
        failureIndex = std::numeric_limits<std::size_t>::max();
        if (!injected || !rejected || fixture.count() != 2 || eve::rpg::SkillRegistry::count() != 0 ||
            eve::rpg::EffectRegistry::count() != 0 || eve::cap::listenerCount<eve::IStateQuery>() != queries ||
            eve::cap::listenerCount<eve::IStateMutation>() != mutations ||
            core.payload.toJson().expect("unchanged core") != coreBytes ||
            domain.payload.toJson().expect("unchanged domain") != domainBytes ||
            fixture.sourceWorld->inspect(fixture.request.source).expect("old core remains live").alive != 2) {
            std::fprintf(stderr, "Joint restore admission failpoint %zu violated isolation\n", fail);
            return 11;
        }
        auto retry =
            eve::rpg_spawn::RPGSpawnJointRestore::prepare(core, domain, epoch, hash).expect("same bundle retry");
        if (retry.core().trackedCount() != 2 || retry.domain().actorCount() != 2 || retry.saveEpoch() != epoch)
            return 12;
    }
    std::printf(
        "Joint core/RPG admission: %zu ordinary allocation failpoints; original world/content/capabilities/envelopes "
        "unchanged; same bundle retries\n",
        count);
    return 0;
}

int checkBundleAdmission() {
    test_rpg_spawn::Fixture fixture;
    fixture.sourceWorld->advance({eve::SimulationTick(1), eve::Duration(0)}).expect("real batch");
    std::vector<eve::spawn::SavedLifecycleInput> pending;
    {
        ecs::ScopedTable current(*fixture.table);
        auto             view = ecs::View<eve::rpg::RPGActor>(*fixture.table);
        eve::rpg::VitalsSystem::takeDamage(view.begin().entity(), "hp", 100);
    }
    std::vector<eve::rpg::VitalsEvent> events;
    eve::rpg::VitalsSystem::pollEvents(events);
    auto retained = fixture.factory->collectLifecycle(events).expect("retained death");
    for (const auto& input : retained) pending.push_back(eve::spawn::savedLifecycleInput(input));
    if (pending.size() != 1) return 30;
    auto epoch  = fixture.request.operation.child("bundle-epoch");
    auto frozen = fixture.sourceWorld->capture(epoch, eve::Revision(1)).expect("core");
    auto core   = frozen.seal(hash).expect("core envelope");
    auto domain = fixture.factory->capture(epoch, frozen.unitReferences())
                      .expect("domain")
                      .seal(eve::Revision(1), eve::SimulationTick(1), hash)
                      .expect("domain envelope");
    auto bundle = eve::rpg_spawn::RPGSpawnJointRestore::sealBundle(core, domain, epoch, pending, hash).expect("bundle");
    const auto coreBytes   = eve::serializeSnapshotEnvelope(core).expect("core bytes");
    const auto domainBytes = eve::serializeSnapshotEnvelope(domain).expect("domain bytes");
    const auto bundleBytes = eve::serializeSnapshotEnvelope(bundle).expect("bundle bytes");
    eve::rpg::SkillRegistry::clear();
    eve::rpg::EffectRegistry::clear();
    const auto queries   = eve::cap::listenerCount<eve::IStateQuery>();
    const auto mutations = eve::cap::listenerCount<eve::IStateMutation>();
    for (bool sealing : {false, true}) {
        auto run = [&]() -> eve::Result<void> {
            if (sealing) {
                auto result = eve::rpg_spawn::RPGSpawnJointRestore::sealBundle(core, domain, epoch, pending, hash);
                if (!result) return eve::Result<void>::failure(result.status());
            } else {
                auto result = eve::rpg_spawn::RPGSpawnJointRestore::prepareBundle(bundle, epoch, hash);
                if (!result) return eve::Result<void>::failure(result.status());
                if (result.value().pendingLifecycle().size() != 1 || result.value().core().trackedCount() != 2)
                    return eve::Result<void>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "Invalid restored bundle"));
            }
            return eve::Result<void>::success();
        };
        // Snapshot's shared envelope parser initializes its exact-field set once. Measure repeatable
        // per-attempt work after that protocol initialization, as with the native factory probes.
        run().expect("initialize common envelope protocol before measuring restore attempts");
        allocations   = 0;
        measuring     = true;
        auto baseline = run();
        measuring     = false;
        if (!baseline || allocations == 0) return 31;
        const auto count = allocations;
        for (std::size_t fail = 0; fail < count; ++fail) {
            allocations   = 0;
            failureIndex  = fail;
            injected      = false;
            measuring     = true;
            bool rejected = false;
            try {
                auto result = run();
                measuring   = false;
                rejected    = !result.ok();
            } catch (const std::bad_alloc&) {
                measuring = false;
                rejected  = true;
            }
            failureIndex = std::numeric_limits<std::size_t>::max();
            if (!injected || !rejected || fixture.count() != 2 || eve::rpg::SkillRegistry::count() != 0 ||
                eve::rpg::EffectRegistry::count() != 0 || eve::cap::listenerCount<eve::IStateQuery>() != queries ||
                eve::cap::listenerCount<eve::IStateMutation>() != mutations ||
                eve::serializeSnapshotEnvelope(core).expect("unchanged core") != coreBytes ||
                eve::serializeSnapshotEnvelope(domain).expect("unchanged domain") != domainBytes ||
                eve::serializeSnapshotEnvelope(bundle).expect("unchanged bundle") != bundleBytes ||
                pending.size() != 1 ||
                fixture.sourceWorld->inspect(fixture.request.source).expect("old core").alive != 2) {
                std::fprintf(stderr,
                             "Bundle %s failpoint %zu violated isolation: injected=%d rejected=%d roots=%zu "
                             "queries=%zu/%zu mutations=%zu/%zu\n",
                             sealing ? "seal" : "admit", fail, int(injected), int(rejected), fixture.count(),
                             eve::cap::listenerCount<eve::IStateQuery>(), queries,
                             eve::cap::listenerCount<eve::IStateMutation>(), mutations);
                return 32;
            }
            run().expect("same bundle inputs retry");
        }
        std::printf(
            "RPG Spawn bundle %s: %zu ordinary allocation failpoints; retained lifecycle, original "
            "world/content/capabilities/envelopes unchanged; same inputs retry\n",
            sealing ? "seal" : "admit", count);
    }
    return 0;
}

int checkCoreBinding() {
    test_rpg_spawn::Fixture fixture;
    fixture.sourceWorld->advance({eve::SimulationTick(1), eve::Duration(0)}).expect("original actual actors");
    auto epoch          = fixture.request.operation.child("core-binding");
    auto frozen         = fixture.sourceWorld->capture(epoch, eve::Revision(1)).expect("core freeze");
    auto core           = frozen.seal(hash).expect("core envelope");
    auto domainEnvelope = fixture.factory->capture(epoch, frozen.unitReferences())
                              .expect("domain freeze")
                              .seal(eve::Revision(1), eve::SimulationTick(1), hash)
                              .expect("domain envelope");
    auto archive = eve::rpg_spawn::RPGSpawnArchive::parse(domainEnvelope, epoch, hash).expect("archive");
    auto domain  = std::make_shared<const eve::rpg_spawn::RPGSpawnRestoreState>(
        eve::rpg_spawn::RPGSpawnRestoreState::prepare(archive).expect("admitted domain"));
    struct Units final : eve::spawn::IUnitDefinitionResolver {
        const eve::rpg_spawn::RPGSpawnRestoreState& domain;
        explicit Units(const eve::rpg_spawn::RPGSpawnRestoreState& value) : domain(value) {}
        eve::Result<void> validate(const eve::DefinitionRef& reference) const override {
            auto recipe = domain.futureUnit(reference);
            if (!recipe) return eve::Result<void>::failure(recipe.status());
            return eve::Result<void>::success();
        }
    } units(*domain);
    auto                                       table = std::make_shared<ecs::Table>();
    std::shared_ptr<eve::spawn::ISpawnFactory> factory =
        eve::rpg_spawn::RPGSpawnFactory::prepareRestored(table, domain).expect("restored factory");
    auto providers = std::make_shared<eve::spawn::SpawnProviders>(
        eve::spawn::SpawnProviders{std::make_shared<test_rpg_spawn::Space>(), factory});
    auto decode = [&] {
        return eve::spawn::SpawnRestoreState::decode(core, epoch, hash, units).expect("same saved core");
    };
    const auto fileBytes = eve::serializeSnapshotEnvelope(core).expect("frozen file bytes");
    allocations          = 0;
    measuring            = true;
    auto parsedFile      = eve::spawn::SpawnCheckpoint::parse(core, epoch, hash, units);
    measuring            = false;
    if (!parsedFile || allocations == 0) return 93;
    const auto fileCount = allocations;
    for (std::size_t fail = 0; fail < fileCount; ++fail) {
        allocations   = 0;
        failureIndex  = fail;
        injected      = false;
        measuring     = true;
        bool rejected = false;
        try {
            auto result = eve::spawn::SpawnCheckpoint::parse(core, epoch, hash, units);
            measuring   = false;
            rejected    = !result.ok();
        } catch (const std::bad_alloc&) {
            measuring = false;
            rejected  = true;
        }
        failureIndex = std::numeric_limits<std::size_t>::max();
        if (!injected || !rejected || fixture.count() != 2 ||
            eve::serializeSnapshotEnvelope(core).expect("unchanged core file") != fileBytes ||
            fixture.sourceWorld->inspect(fixture.request.source).expect("source still owns members").alive != 2) {
            std::fprintf(stderr, "Core checkpoint file admission failpoint %zu violated isolation\n", fail);
            return 94;
        }
        auto retry = eve::spawn::SpawnCheckpoint::parse(core, epoch, hash, units).expect("same file retries");
        for (const auto& member : retry.membership())
            if (!member.source.incarnation.isZero()) return 95;
        auto sealed = retry.seal(hash).expect("detached file reseals");
        if (eve::serializeSnapshotEnvelope(sealed).expect("exact file retry") != fileBytes) return 96;
    }
    std::printf("Core checkpoint file admission: checked %zu ordinary allocation failpoints\n", fileCount);
    auto retained = decode();
    allocations   = 0;
    measuring     = true;
    auto copied   = retained.clone();
    measuring     = false;
    if (!copied || allocations == 0) return 90;
    const auto copyCount       = allocations;
    const auto originalBytes   = eve::serializeSnapshotEnvelope(core).expect("original core bytes");
    const auto beforeQueries   = eve::cap::listenerCount<eve::IStateQuery>();
    const auto beforeMutations = eve::cap::listenerCount<eve::IStateMutation>();
    for (std::size_t fail = 0; fail < copyCount; ++fail) {
        allocations   = 0;
        failureIndex  = fail;
        injected      = false;
        measuring     = true;
        bool rejected = false;
        try {
            auto result = retained.clone();
            measuring   = false;
            rejected    = !result.ok();
        } catch (const std::bad_alloc&) {
            measuring = false;
            rejected  = true;
        }
        failureIndex = std::numeric_limits<std::size_t>::max();
        if (!injected || !rejected || retained.saveEpoch() != epoch || retained.trackedCount() != 2 ||
            fixture.count() != 2 || eve::cap::listenerCount<eve::IStateQuery>() != beforeQueries ||
            eve::cap::listenerCount<eve::IStateMutation>() != beforeMutations)
            return 91;
        auto retry    = retained.clone().expect("same CPU facts after failed copy");
        auto world    = eve::spawn::SpawnWorld::restore(retry, providers).expect("bind successful retry");
        auto artifact = world.capture(epoch, eve::Revision(1)).expect("retry freeze").seal(hash).expect("retry bytes");
        if (eve::serializeSnapshotEnvelope(artifact).expect("cloned artifact") != originalBytes) return 92;
    }
    std::printf("Core restore clone: checked %zu ordinary allocation failpoints\n", copyCount);
    auto candidate = decode();
    allocations    = 0;
    measuring      = true;
    auto baseline  = eve::spawn::SpawnWorld::restore(candidate, providers);
    measuring      = false;
    if (!baseline || allocations == 0) return 40;
    const auto count     = allocations;
    const auto queries   = eve::cap::listenerCount<eve::IStateQuery>();
    const auto mutations = eve::cap::listenerCount<eve::IStateMutation>();
    for (std::size_t fail = 0; fail < count; ++fail) {
        auto state    = decode();
        auto source   = state.sources()[0];
        allocations   = 0;
        failureIndex  = fail;
        injected      = false;
        measuring     = true;
        bool rejected = false;
        try {
            auto result = eve::spawn::SpawnWorld::restore(state, providers);
            measuring   = false;
            rejected    = !result.ok();
        } catch (const std::bad_alloc&) {
            measuring = false;
            rejected  = true;
        }
        failureIndex = std::numeric_limits<std::size_t>::max();
        if (!injected || !rejected || state.saveEpoch() != epoch || state.trackedCount() != 2 ||
            state.sources().size() != 1 || state.sources()[0] != source || fixture.count() != 2 ||
            eve::cap::listenerCount<eve::IStateQuery>() != queries ||
            eve::cap::listenerCount<eve::IStateMutation>() != mutations ||
            fixture.sourceWorld->inspect(fixture.request.source).expect("original core").alive != 2) {
            std::fprintf(stderr, "Core binding failpoint %zu violated isolation\n", fail);
            return 41;
        }
        auto restored = eve::spawn::SpawnWorld::restore(state, providers).expect("same admitted state retries");
        auto lease    = restored.resolveSavedSource(source).expect("reconstructed source");
        if (!state.saveEpoch().isNil() || !lease.isCurrent() ||
            lease.source().incarnation == fixture.request.source.incarnation ||
            restored.inspect(lease.source()).expect("restored ledger").alive != 2)
            return 42;
        auto snapshot =
            restored.capture(epoch, eve::Revision(1)).expect("restored capture").seal(hash).expect("restored seal");
        if (eve::serializeSnapshotEnvelope(snapshot).expect("restored bytes") !=
            eve::serializeSnapshotEnvelope(core).expect("original bytes"))
            return 43;
    }
    std::printf(
        "Spawn core provider/source binding: %zu ordinary allocation failpoints; admitted state and original actual "
        "world unchanged; same candidate retries\n",
        count);
    return 0;
}

int checkSavedActorMaterialization(bool active) {
    test_rpg_spawn::Fixture                fixture;
    eve::rpg::detail::SimulationEvents     simulationQueues;
    eve::rpg::detail::SimulationEventScope eventScope(simulationQueues);
    fixture.sourceWorld->advance({eve::SimulationTick(1), eve::Duration(0)}).expect("original actual population");
    {
        ecs::ScopedTable current(*fixture.table);
        auto             view = ecs::View<eve::rpg::RPGActor>(*fixture.table);
        eve::rpg::VitalsSystem::takeDamage(view.begin().entity(), "hp", 100);
    }
    std::vector<eve::rpg::VitalsEvent> events;
    eve::rpg::VitalsSystem::pollEvents(events);
    auto retired = fixture.factory->collectLifecycle(events).expect("retained death before save");
    std::vector<eve::spawn::SavedLifecycleInput> pending;
    for (const auto& input : retired) pending.push_back(eve::spawn::savedLifecycleInput(input));
    if (active) {
        eve::rpg::EffectDefinition effect;
        effect.id             = "allocation.materialized.active";
        effect.durationPolicy = "duration";
        effect.duration       = 3;
        effect.modifiers.push_back({"attack", "add", 4, 0});
        eve::rpg::EffectRegistry::registerEffect(effect);
        ecs::ScopedTable scope(*fixture.table);
        {
            auto view = ecs::View<eve::rpg::RPGActor>(*fixture.table);
            for (auto it = view.begin(); it != view.end(); ++it)
                eve::rpg::StatusSystem::apply(it.entity(), effect.id, "active").expect("materialized status authority");
        }
        eve::rpg::StatusSystem::update(0.375).expect("materialized status partial duration");
        eve::rpg::SkillDefinition skill;
        skill.id       = "spawn.basic";
        skill.castTime = 2;
        skill.cooldown = 3;
        eve::rpg::SkillRegistry::registerSkill(skill);
        std::vector<eve::rpg::RPGActor*> actors;
        {
            auto view = ecs::View<eve::rpg::RPGActor>(*fixture.table);
            for (auto it = view.begin(); it != view.end(); ++it) actors.push_back(it.entity());
        }
        if (!eve::rpg::SkillSystem::beginCast(actors[1], skill.id, actors[0])) return 55;
        eve::rpg::SkillSystem::update(0.375f);
    }
    auto epoch          = fixture.request.operation.child("saved-actor-materialization");
    auto frozen         = fixture.sourceWorld->capture(epoch, eve::Revision(1)).expect("core freeze");
    auto core           = frozen.seal(hash).expect("core envelope");
    auto domainEnvelope = fixture.factory->capture(epoch, frozen.unitReferences())
                              .expect("domain freeze")
                              .seal(eve::Revision(1), eve::SimulationTick(1), hash)
                              .expect("domain envelope");
    auto admitted = eve::rpg_spawn::RPGSpawnJointRestore::prepare(core, domainEnvelope, epoch, hash, pending)
                        .expect("checked joint candidate");
    auto         domain = admitted.domainOwner();
    struct Units final : eve::spawn::IUnitDefinitionResolver {
        const eve::rpg_spawn::RPGSpawnRestoreState& state;
        explicit Units(const eve::rpg_spawn::RPGSpawnRestoreState& value) : state(value) {}
        eve::Result<void> validate(const eve::DefinitionRef& reference) const override {
            auto recipe = state.futureUnit(reference);
            if (!recipe) return eve::Result<void>::failure(recipe.status());
            return eve::Result<void>::success();
        }
    } units(*domain);
    struct Stage {
        std::shared_ptr<ecs::Table>                      table;
        std::shared_ptr<eve::rpg_spawn::RPGSpawnFactory> factory;
        std::shared_ptr<eve::spawn::SpawnProviders>      providers;
        std::unique_ptr<eve::spawn::SpawnWorld>          core;
    };
    auto stage = [&] {
        Stage result;
        result.table     = std::make_shared<ecs::Table>();
        result.factory   = eve::rpg_spawn::RPGSpawnFactory::prepareRestored(result.table, domain).expect("factory");
        result.providers = std::make_shared<eve::spawn::SpawnProviders>(
            eve::spawn::SpawnProviders{std::make_shared<test_rpg_spawn::Space>(), result.factory});
        auto state  = eve::spawn::SpawnRestoreState::decode(core, epoch, hash, units).expect("core candidate");
        result.core = std::make_unique<eve::spawn::SpawnWorld>(
            eve::spawn::SpawnWorld::restore(state, result.providers).expect("bind sources"));
        return result;
    };
    auto roots = [](const std::shared_ptr<ecs::Table>& table) {
        std::size_t count = 0;
        auto        view  = ecs::View<eve::rpg::RPGActor>(*table);
        for (auto it = view.begin(); it != view.end(); ++it) ++count;
        return count;
    };
    const auto  domainBytes   = eve::serializeSnapshotEnvelope(domainEnvelope).expect("original domain bytes");
    const auto  coreBytes     = eve::serializeSnapshotEnvelope(core).expect("original core bytes");
    const auto  registryBytes = eve::rpg::SkillRegistry::definitionRegistry().snapshotJson();
    std::size_t count         = 0;
    {
        // Old and restored actors coexist during preparation. Warm the common capability listener lists
        // to that simultaneous population before measuring repeatable per-attempt allocations.
        auto warm = stage();
        warm.factory->materializeSavedActors(*warm.core, admitted).expect("warm old/new capability capacity");
        if (roots(warm.table) != 2) return 50;
    }
    {
        auto target   = stage();
        allocations   = 0;
        measuring     = true;
        auto baseline = target.factory->materializeSavedActors(*target.core, admitted);
        measuring     = false;
        count         = allocations;
        if (!baseline || count == 0 || roots(target.table) != 2) return 50;
    }
    const auto queries   = eve::cap::listenerCount<eve::IStateQuery>();
    const auto mutations = eve::cap::listenerCount<eve::IStateMutation>();
    for (std::size_t fail = 0; fail < count; ++fail) {
        auto target   = stage();
        allocations   = 0;
        failureIndex  = fail;
        injected      = false;
        measuring     = true;
        bool rejected = false;
        try {
            auto result = target.factory->materializeSavedActors(*target.core, admitted);
            measuring   = false;
            rejected    = !result.ok();
        } catch (const std::bad_alloc&) {
            measuring = false;
            rejected  = true;
        }
        failureIndex = std::numeric_limits<std::size_t>::max();
        if (!injected || !rejected || roots(target.table) != 0 || fixture.count() != 2 ||
            eve::cap::listenerCount<eve::IStateQuery>() != queries ||
            eve::cap::listenerCount<eve::IStateMutation>() != mutations ||
            eve::rpg::SkillRegistry::definitionRegistry().snapshotJson() != registryBytes ||
            eve::serializeSnapshotEnvelope(core).expect("unchanged source core") != coreBytes ||
            eve::serializeSnapshotEnvelope(domainEnvelope).expect("unchanged source domain") != domainBytes ||
            fixture.sourceWorld->inspect(fixture.request.source).expect("original core alive").alive != 2) {
            std::fprintf(stderr,
                         "Saved actor materialization failpoint %zu violated isolation: injected=%d rejected=%d "
                         "targetRoots=%zu\n",
                         fail, int(injected), int(rejected), roots(target.table));
            return 51;
        }
        target.factory->materializeSavedActors(*target.core, admitted).expect("same factory/canonical archive retries");
        if (roots(target.table) != 2) return 52;
        auto actual = target.factory->capture(epoch, frozen.unitReferences())
                          .expect("actual materialized capture")
                          .seal(eve::Revision(1), eve::SimulationTick(1), hash)
                          .expect("materialized envelope");
        if (eve::serializeSnapshotEnvelope(actual).expect("actual bytes") != domainBytes) return 53;
    }
    if (eve::cap::listenerCount<eve::IStateQuery>() != queries ||
        eve::cap::listenerCount<eve::IStateMutation>() != mutations)
        return 54;
    std::printf(
        "Saved RPG actor materialization (%s): %zu ordinary allocation failpoints; every prefix rolled back, old "
        "actual "
        "world/content/capabilities/envelopes unchanged; same factory retries exact actor bytes\n",
        active ? "active effects and linked cast" : "empty effects", count);
    return 0;
}

int checkSessionRestoration() {
    test_rpg_spawn::Fixture fixture;
    fixture.sourceWorld->advance({eve::SimulationTick(1), eve::Duration(0)}).expect("original world");
    auto epoch  = fixture.request.operation.child("session-restoration");
    auto frozen = fixture.sourceWorld->capture(epoch, eve::Revision(1)).expect("core");
    auto core   = frozen.seal(hash).expect("core envelope");
    auto domain = fixture.factory->capture(epoch, frozen.unitReferences())
                      .expect("domain")
                      .seal(eve::Revision(1), eve::SimulationTick(1), hash)
                      .expect("domain envelope");
    auto bundle      = eve::rpg_spawn::RPGSpawnJointRestore::sealBundle(core, domain, epoch, {}, hash).expect("bundle");
    const auto bytes = eve::serializeSnapshotEnvelope(bundle).expect("original artifact");
    auto       prepare = [&] {
        return eve::rpg_spawn::PreparedRPGSpawnSession::prepare(bundle, epoch, hash,
                                                                      std::make_shared<test_rpg_spawn::Space>())
            .expect("prepare full session");
    };
    auto makeSession = [&] {
        auto                                             prepared = prepare();
        std::unique_ptr<eve::rpg_spawn::RPGSpawnSession> target;
        auto                                             previous = prepared.commit(target).expect("initial session");
        if (previous) std::abort();
        return target;
    };
    {
        auto target   = makeSession();
        auto prepared = prepare();
        auto previous = prepared.commit(target).expect("warm old/new simultaneous publication capacity");
    }
    const auto idleQueries   = eve::cap::listenerCount<eve::IStateQuery>();
    const auto idleMutations = eve::cap::listenerCount<eve::IStateMutation>();
    for (bool committing : {false, true}) {
        std::size_t count = 0;
        {
            auto                                                   target = makeSession();
            std::optional<eve::rpg_spawn::PreparedRPGSpawnSession> candidate;
            if (committing) candidate.emplace(prepare());
            allocations = 0;
            measuring   = true;
            if (committing) {
                auto result = candidate->commit(target);
                measuring   = false;
                if (!result) return 60;
            } else {
                auto result =
                    eve::rpg_spawn::PreparedRPGSpawnSession::prepare(bundle, epoch, hash, fixture.providers->placement);
                measuring = false;
                if (!result) return 60;
                candidate.emplace(std::move(result).takeValue());
            }
            count = allocations;
            if (count == 0) return 61;
        }
        for (std::size_t fail = 0; fail < count; ++fail) {
            auto        target   = makeSession();
            const auto* original = target.get();
            auto        lease    = target->spawning().sourceLease(target->sources()[0]).expect("old authority");
            std::optional<eve::rpg_spawn::PreparedRPGSpawnSession> candidate;
            if (committing) candidate.emplace(prepare());
            const auto queries   = eve::cap::listenerCount<eve::IStateQuery>();
            const auto mutations = eve::cap::listenerCount<eve::IStateMutation>();
            allocations          = 0;
            failureIndex         = fail;
            injected             = false;
            measuring            = true;
            bool rejected        = false;
            try {
                if (committing) {
                    auto result = candidate->commit(target);
                    measuring   = false;
                    rejected    = !result.ok();
                } else {
                    auto result = eve::rpg_spawn::PreparedRPGSpawnSession::prepare(bundle, epoch, hash,
                                                                                   fixture.providers->placement);
                    measuring   = false;
                    rejected    = !result.ok();
                    if (result) candidate.emplace(std::move(result).takeValue());
                }
            } catch (const std::bad_alloc&) {
                measuring = false;
                rejected  = true;
            }
            failureIndex = std::numeric_limits<std::size_t>::max();
            if (!injected || !rejected || target.get() != original || !lease.isCurrent() || fixture.count() != 2 ||
                eve::cap::listenerCount<eve::IStateQuery>() != queries ||
                eve::cap::listenerCount<eve::IStateMutation>() != mutations ||
                target->spawning().inspect(target->sources()[0]).expect("old root ledger").alive != 2 ||
                eve::serializeSnapshotEnvelope(bundle).expect("unchanged artifact") != bytes) {
                std::fprintf(stderr, "Session %s failpoint %zu violated isolation: injected=%d rejected=%d\n",
                             committing ? "commit" : "prepare", fail, int(injected), int(rejected));
                return 62;
            }
            auto originalSnapshot = target->capture(epoch, eve::Revision(1), hash).expect("old session still saves");
            if (eve::serializeSnapshotEnvelope(originalSnapshot).expect("old session bytes") != bytes) return 63;
            if (!committing) candidate.emplace(prepare());
            auto retired = candidate->commit(target).expect("same candidate retries complete owner exchange");
            if (retired.get() != original || target.get() == original || lease.isCurrent()) return 64;
            auto actual = target->capture(epoch, eve::Revision(1), hash).expect("new session exact save");
            if (eve::serializeSnapshotEnvelope(actual).expect("new artifact") != bytes) return 65;
        }
        std::printf(
            "RPG session %s: %zu ordinary allocation failpoints; active owner/leases/actors/capabilities/artifact "
            "unchanged; same inputs or candidate retry complete replacement\n",
            committing ? "commit" : "prepare", count);
    }
    if (eve::cap::listenerCount<eve::IStateQuery>() != idleQueries ||
        eve::cap::listenerCount<eve::IStateMutation>() != idleMutations)
        return 66;
    return 0;
}

int main() {
    for (unsigned mode : {0u, 1u, 2u, 3u, 4u, 5u, 6u}) {
        const auto result = checkAdmission(mode);
        if (result != 0) return result;
    }
    const auto joint = checkJointAdmission();
    if (joint != 0) return joint;
    const auto bundle = checkBundleAdmission();
    if (bundle != 0) return bundle;
    const auto binding = checkCoreBinding();
    if (binding != 0) return binding;
    const auto actors = checkSavedActorMaterialization(false);
    if (actors != 0) return actors;
    const auto activeActors = checkSavedActorMaterialization(true);
    if (activeActors != 0) return activeActors;
    return checkSessionRestoration();
}
