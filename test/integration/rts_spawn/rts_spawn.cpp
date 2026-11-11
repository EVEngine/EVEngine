// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"role":"tests","default":"module-composition","module":"rts_spawn","task":"rts_spawn","modules":["rts_spawn","rts","spawn","combat","weapon","sensing","action","attributes"]}
*/
// clang-format on
#include <algorithm>
#include <cmath>
#include <limits>
#include "RTSSpawnFixture.h"
#include "RTSSpawnRestoreFixture.h"
#include "SpawnFactoryContract.h"
#include "action/Action.h"
#include "attributes/AttributeProjectionCapture.h"
#include "combat/Damage.h"
#include "common/Capability.h"
#include "common/StateAccess.h"
#include "rts/RTSAttributeCapture.h"
#include "rts/RTSCombatArchive.h"
#include "rts/RTSCombatRestore.h"
#include "rts/RTSFrameArchive.h"
#include "rts/RTSFrameRestore.h"
#include "rts/RTSWeaponCapture.h"
#include "rts/spawn/RTSSpawnCapture.h"
#include "rts/spawn/RTSSpawnOccupancy.h"
#include "sensing/Sensing.h"
#include "sensing/SensingCapture.h"
#include "weapon/AmmoPoolRestore.h"
#include "weapon/WeaponAttributes.h"
#include "weapon/WeaponCapture.h"
#include "weapon/WeaponGraphRestore.h"
#include "weapon/WeaponRestore.h"
#include "weapon/WeaponSystem.h"
#include "zeroerr/unittest.h"
namespace test_rts_spawn {
namespace {
void roundTripProvenance(SavedRTSSpawn& saved) {
    const auto epoch    = saved.provenance->saveEpoch();
    auto       envelope = saved.provenance
                        ->seal(eve::Revision(7), eve::SimulationTick(1),
                               [&](std::string_view bytes) {
                                   saved.provenance.reset();
                                   return restoreHash(bytes);
                               })
                        .expect("complete source seal while digest retires capture");
    REQUIRE(!saved.provenance);
    const auto text   = eve::serializeSnapshotEnvelope(envelope).expect("complete source wire");
    auto       input  = eve::parseSnapshotEnvelope(text, restoreHash).expect("owning source input");
    auto       parsed = eve::rts_spawn::RTSSpawnCapture::parse(input, epoch, [&](std::string_view bytes) {
                      input.payload = eve::Value{};
                      return restoreHash(bytes);
                  }).expect("complete detached source admission");
    REQUIRE(parsed.sourceIncarnation().isZero());
    for (const auto& unit : parsed.units())
        if (unit.origin) REQUIRE(unit.origin->incarnation.isZero());
    for (const auto& receipt : parsed.receipts()) REQUIRE(receipt.source.incarnation.isZero());
    REQUIRE_EQ(eve::serializeSnapshotEnvelope(
                   parsed.seal(eve::Revision(7), eve::SimulationTick(1), restoreHash).expect("complete source reseal"))
                   .expect("complete source repeat wire"),
               text);
    saved.provenance = std::make_shared<const eve::rts_spawn::RTSSpawnCapture>(std::move(parsed));
}
}  // namespace
TEST_CASE("RTS archetype archive retires registry retains exact sources and materializes old future content") {
    eve::Value                                          value;
    std::weak_ptr<eve::definitions::DefinitionRegistry> registry;
    std::string                                         sourceJson, weaponJson;
    {
        Fixture source;
        registry = source.registry;
        source.registry
            ->replace("unit", "fighter", 1,
                      " {\"health\":40,\"speed\":3,\"weaponType\":\"club\",\"future_field\":[1,2]} ")
            .expect("exact source bytes");
        auto recipe = eve::rts::PreparedUnitArchetype::prepare(*source.registry, *eve::LogicalId::parse("unit:fighter"))
                          .expect("actual prepared recipe");
        sourceJson = recipe.source()->get().json;
        weaponJson = recipe.weaponSource()->get().json;
        value      = recipe.encodeState().expect("owning recipe value");
    }
    REQUIRE(registry.expired());
    auto parsed = eve::rts::PreparedUnitArchetype::prepareState(
                      eve::Value::fromJson(value.toJson().expect("recipe text")).expect("owning input"))
                      .expect("retired source admission");
    REQUIRE_EQ(parsed.source()->get().json, sourceJson);
    REQUIRE_EQ(parsed.weaponSource()->get().json, weaponJson);
    REQUIRE_EQ(parsed.encodeState().expect("repeat recipe").toJson().expect("repeat wire"),
               value.toJson().expect("wire"));
    Fixture target;
    eve::rts::RTSContentLoader::load(
        *target.registry,
        R"({"units":[{"id":"fighter","weaponType":"club","health":700,"speed":9}],"weapons":[{"id":"club","damage":90,"range":2}]})")
        .expect("unrelated latest registry");
    ecs::ScopedTable targetScope(*target.table);
    auto*            faction = target.domain->findFaction(
        eve::SubjectRef::fromPersistentId(*eve::PersistentId::parse("01900000-0000-7000-8000-000000000889")));
    REQUIRE(faction);
    const auto       id = eve::SubjectRef::fromPersistentId(target.request.source.owner.child("archived-future"));
    const std::array requests{eve::rts::UnitBatchRequest{id, parsed, 12, 12, {}}};
    auto             batch = target.domain->prepareUnitBatch(*faction, requests).expect("parsed recipe native batch");
    REQUIRE_EQ(target.count(), std::size_t(0));
    REQUIRE(batch.commit().expect("canonical future materialization").state == eve::rts::UnitBatchState::Committed);
    ecs::ScopedTable scope(*target.table);
    auto*            unit = target.domain->findUnit(id);
    REQUIRE(unit);
    REQUIRE_EQ(unit->durability()->state.maxHealth, 40.f);
    REQUIRE_EQ(unit->motion()->speed, 3.f);
    auto* weapon = dynamic_cast<eve::weapon::WeaponEntity*>(unit->weapon()->link.resolve());
    REQUIRE(weapon);
    REQUIRE_EQ(weapon->definition()->def->damage, 10.f);
    REQUIRE(weapon->definitionBinding()->identity.definitionGeneration == parsed.weaponSource()->get().generation);
}
TEST_CASE("RTS archetype archive rejects malformed or mismatched content without publishing roots") {
    Fixture source;
    auto    recipe = eve::rts::PreparedUnitArchetype::prepare(*source.registry, *eve::LogicalId::parse("unit:fighter"))
                      .expect("actual recipe");
    const auto valid   = recipe.encodeState().expect("recipe value");
    const auto queries = eve::cap::listenerCount<eve::IStateQuery>();
    auto       reject  = [&](eve::Value candidate) {
        REQUIRE(!eve::rts::PreparedUnitArchetype::prepareState(candidate).ok());
    };
    auto candidate                                      = valid;
    (*candidate.getIf<eve::Value::Object>())["unknown"] = 1;
    reject(candidate);
    candidate                                           = valid;
    (*candidate.getIf<eve::Value::Object>())["version"] = 2;
    reject(candidate);
    candidate = valid;
    candidate.getIf<eve::Value::Object>()->erase("weapon");
    reject(candidate);
    for (const auto generation : {"0", "01", "18446744073709551616"}) {
        candidate                                                                          = valid;
        (*candidate.getIf<eve::Value::Object>()->at("unit").getIf<eve::Value::Array>())[3] = generation;
        reject(candidate);
    }
    candidate                                                                          = valid;
    (*candidate.getIf<eve::Value::Object>()->at("unit").getIf<eve::Value::Array>())[2] = 2;
    reject(candidate);
    candidate                                                                            = valid;
    (*candidate.getIf<eve::Value::Object>()->at("weapon").getIf<eve::Value::Array>())[1] = "other";
    reject(candidate);
    candidate                                                                            = valid;
    (*candidate.getIf<eve::Value::Object>()->at("weapon").getIf<eve::Value::Array>())[4] = "[]";
    reject(candidate);
    candidate                                          = valid;
    (*candidate.getIf<eve::Value::Object>())["weapon"] = eve::Value{};
    reject(candidate);
    candidate                                                                          = valid;
    (*candidate.getIf<eve::Value::Object>()->at("unit").getIf<eve::Value::Array>())[4] = R"({"health":-1})";
    reject(candidate);
    candidate                                                                          = valid;
    (*candidate.getIf<eve::Value::Object>()->at("unit").getIf<eve::Value::Array>())[4] = R"({"health":14})";
    reject(candidate);
    (*candidate.getIf<eve::Value::Object>())["weapon"] = eve::Value{};
    auto unarmed = eve::rts::PreparedUnitArchetype::prepareState(candidate).expect("explicit unarmed recipe");
    REQUIRE(!unarmed.weaponSource());
    candidate = valid;
    for (const auto key : {"unit", "weapon"})
        (*candidate.getIf<eve::Value::Object>()->at(key).getIf<eve::Value::Array>())[3] = "18446744073709551615";
    auto exact = eve::rts::PreparedUnitArchetype::prepareState(candidate).expect("full-width generations");
    REQUIRE_EQ(exact.source()->get().generation.value(), std::numeric_limits<std::uint64_t>::max());
    REQUIRE_EQ(exact.weaponSource()->get().generation.value(), std::numeric_limits<std::uint64_t>::max());
    auto moved = std::move(exact);
    REQUIRE(!exact.encodeState().ok());
    REQUIRE(!exact.source());
    REQUIRE_EQ(source.count(), std::size_t(0));
    REQUIRE_EQ(eve::cap::listenerCount<eve::IStateQuery>(), queries);
    REQUIRE_EQ(recipe.encodeState().expect("source unchanged").toJson().expect("wire"),
               valid.toJson().expect("valid wire"));
}
TEST_CASE("RTS source archive rejects malformed content and leaves receipt authority to actual core leases") {
    SavedRTSSpawn saved;
    {
        auto command             = std::make_shared<eve::rts::CommandSpec>();
        command->kind            = eve::rts::OrderKind::Patrol;
        command->target          = {16, 8};
        command->secondaryTarget = {2, 3};
        command->definitionId    = "test.patrol";
        command->priority        = -4;
        command->timeoutSeconds  = 12.5;
        command->radius          = 1.25;
        command->append          = true;
        Fixture source(command);
        source.sourceWorld->advance({eve::SimulationTick(1), eve::Duration(100000000)}).expect("source publication");
        saved = freezeSpawn(source, source.request.source.owner.child("source-wire"));
    }
    const auto epoch = saved.provenance->saveEpoch();
    const auto base =
        saved.provenance->seal(eve::Revision(11), eve::SimulationTick(1), restoreHash).expect("source envelope");
    auto admit = [&](eve::Value payload) {
        auto envelope = eve::makeSnapshotEnvelope(base.type, base.schema, base.schemaVersion, epoch, base.revision,
                                                  base.tick, std::move(payload), restoreHash)
                            .expect("reseal test source");
        return eve::rts_spawn::RTSSpawnCapture::parse(std::move(envelope), epoch, restoreHash);
    };
    auto parsed = admit(base.payload).expect("actual command archive");
    REQUIRE(parsed.initialCommand());
    REQUIRE(parsed.initialCommand()->kind == eve::rts::OrderKind::Patrol);
    REQUIRE_EQ(parsed.initialCommand()->secondaryTarget.y, 3.f);
    REQUIRE_EQ(parsed.initialCommand()->priority, -4);
    REQUIRE_EQ(parsed.initialCommand()->timeoutSeconds, 12.5);
    REQUIRE_EQ(parsed.initialCommand()->radius, 1.25f);
    REQUIRE(parsed.initialCommand()->append);
    REQUIRE(!parsed.initialCommand()->targetEntity.table);
    auto reject = [&](eve::Value payload) { REQUIRE(!admit(std::move(payload)).ok()); };
    auto value  = base.payload;
    (*value.getIf<eve::Value::Object>())["incarnation"] = "123";
    reject(value);
    value                                            = base.payload;
    (*value.getIf<eve::Value::Object>())["sequence"] = "01";
    reject(value);
    value                                                                            = base.payload;
    (*value.getIf<eve::Value::Object>()->at("source").getIf<eve::Value::Array>())[0] = eve::Value{};
    reject(value);
    value                                                                             = base.payload;
    (*value.getIf<eve::Value::Object>()->at("command").getIf<eve::Value::Array>())[0] = "unknown";
    reject(value);
    value                                                                             = base.payload;
    (*value.getIf<eve::Value::Object>()->at("command").getIf<eve::Value::Array>())[4] = -1;
    reject(value);
    value           = base.payload;
    auto& duplicate = *value.getIf<eve::Value::Object>()->at("units").getIf<eve::Value::Array>();
    duplicate.push_back(duplicate.front());
    reject(value);
    value = base.payload;
    auto& member =
        *value.getIf<eve::Value::Object>()->at("units").getIf<eve::Value::Array>()->front().getIf<eve::Value::Array>();
    member[2] = false;
    reject(value);
    value           = base.payload;
    auto& sourceKey = *(*value.getIf<eve::Value::Object>()
                             ->at("units")
                             .getIf<eve::Value::Array>()
                             ->front()
                             .getIf<eve::Value::Array>())[1]
                           .getIf<eve::Value::Array>();
    sourceKey.push_back(99);
    reject(value);
    value        = base.payload;
    auto& future = *value.getIf<eve::Value::Object>()->at("future").getIf<eve::Value::Array>();
    (*future.front().getIf<eve::Value::Array>())[0] = "unit:other";
    reject(value);
    value         = base.payload;
    auto& receipt = *value.getIf<eve::Value::Object>()
                         ->at("receipts")
                         .getIf<eve::Value::Array>()
                         ->front()
                         .getIf<eve::Value::Array>();
    receipt[2] = "0";
    reject(value);
    value           = base.payload;
    auto& positions = *(*value.getIf<eve::Value::Object>()
                             ->at("receipts")
                             .getIf<eve::Value::Array>()
                             ->front()
                             .getIf<eve::Value::Array>())[4]
                           .getIf<eve::Value::Array>();
    positions.pop_back();
    reject(value);
    value        = base.payload;
    auto& tooFar = *(*value.getIf<eve::Value::Object>()
                          ->at("receipts")
                          .getIf<eve::Value::Array>()
                          ->front()
                          .getIf<eve::Value::Array>())[4]
                        .getIf<eve::Value::Array>();
    (*tooFar.front().getIf<eve::Value::Array>())[0] = 1e300;
    reject(value);
    REQUIRE(!eve::rts_spawn::RTSSpawnCapture::parse(base, epoch.child("wrong"), restoreHash).ok());
    auto wrongVersion = eve::makeSnapshotEnvelope(base.type, base.schema, eve::SchemaVersion(2), epoch, base.revision,
                                                  base.tick, base.payload, restoreHash)
                            .expect("future version");
    REQUIRE(!eve::rts_spawn::RTSSpawnCapture::parse(std::move(wrongVersion), epoch, restoreHash).ok());
    value                                 = base.payload;
    (*value.getIf<eve::Value::Object>()
          ->at("receipts")
          .getIf<eve::Value::Array>()
          ->front()
          .getIf<eve::Value::Array>())[1] = epoch.child("forged-operation").format();
    auto          forged                  = admit(value).expect("structural UUID is not receipt authority");
    SavedRTSSpawn bad                     = saved;
    bad.provenance                        = std::make_shared<const eve::rts_spawn::RTSSpawnCapture>(std::move(forged));
    RestoredRTSSpawn target(bad);
    const auto       before = target.domain->canonicalStateJson().expect("before invalid receipt");
    REQUIRE(!target.factory->restoreSources(*target.core, {}, {}).ok());
    REQUIRE(!target.factory->capture(epoch, {}).ok());
    REQUIRE_EQ(target.domain->canonicalStateJson().expect("after invalid receipt"), before);
    auto moved = std::move(parsed);
    REQUIRE(!parsed.seal(eve::Revision(1), eve::SimulationTick(1), restoreHash).ok());
    REQUIRE(moved.sourceIncarnation().isZero());
}
TEST_CASE("RTS source archive admits an unbound factory with explicit future content and no live authority") {
    auto command    = std::make_shared<eve::rts::CommandSpec>();
    command->target = {2, 4};
    Fixture          source(command);
    const auto       epoch = source.request.source.owner.child("unbound-source");
    const std::array future{eve::DefinitionRef::parse("unit:fighter").expect("future reference")};
    auto             captured = source.factory->capture(epoch, future).expect("unbound actual capture");
    auto envelope = captured.seal(eve::Revision(1), eve::SimulationTick(0), restoreHash).expect("unbound envelope");
    auto parsed =
        eve::rts_spawn::RTSSpawnCapture::parse(std::move(envelope), epoch, restoreHash).expect("unbound admission");
    REQUIRE(parsed.sourceOwner().isNil());
    REQUIRE_EQ(parsed.sourceEpoch(), std::uint64_t(0));
    REQUIRE(parsed.sourceIncarnation().isZero());
    REQUIRE(parsed.units().empty());
    REQUIRE(parsed.receipts().empty());
    REQUIRE_EQ(parsed.futureUnits().size(), std::size_t(1));
    REQUIRE(parsed.resolveFuture(future[0]).ok());
    auto saved       = freezeSpawn(source, epoch);
    saved.provenance = std::make_shared<const eve::rts_spawn::RTSSpawnCapture>(std::move(parsed));
    RestoredRTSSpawn target(saved);
    ecs::ScopedTable targetScope(*target.table);
    target.factory->restoreSources(*target.core, {}, {}).expect("bind unbound saved source with fresh leases");
    target.core->advance({eve::SimulationTick(1), eve::Duration(100000000)})
        .expect("first publication from saved future content");
    REQUIRE_EQ(target.domain->unitCount(), std::size_t(2));
    REQUIRE(!target.factory->prepare(source.request, source.positions).ok());
}
TEST_CASE("RTS restored source factory pins future recipes rebuilds authority and replays receipts without creation") {
    SavedRTSSpawn             saved;
    std::weak_ptr<ecs::Table> retired;
    {
        Fixture source;
        source.sourceWorld->advance({eve::SimulationTick(1), eve::Duration(100000000)}).expect("real core publication");
        retired = source.table;
        saved   = freezeSpawn(source, source.request.source.owner.child("source-restore"));
        eve::rts::RTSContentLoader::load(
            *source.registry,
            R"({"units":[{"id":"fighter","weaponType":"club","health":700,"speed":3,"radius":0.35}]})")
            .expect("replace latest unit");
    }
    REQUIRE(retired.expired());
    roundTripProvenance(saved);
    RestoredRTSSpawn target(saved);
    ecs::ScopedTable targetScope(*target.table);
    REQUIRE(!target.factory->capture(saved.provenance->saveEpoch(), {}).ok());
    REQUIRE(!target.factory->prepare({}, {}).ok());
    REQUIRE(!target.factory->collectLifecycle({}).ok());
    auto pending = target.factory->restoreSources(*target.core, {}, {}).expect("restore native origin graph");
    REQUIRE(pending.empty());
    REQUIRE(!target.factory->restoreSources(*target.core, {}, {}).ok());
    const auto& receipt = saved.provenance->receipts()[0];
    auto        lease =
        target.core->resolveSavedSource(eve::spawn::savedSpawnerKey(receipt.source)).expect("fresh source lease");
    REQUIRE(lease.source().incarnation != receipt.source.incarnation);
    REQUIRE(lease.operationIdentity(receipt.ordinal).expect("durable canonical operation") == receipt.operation);
    eve::spawn::BatchRequest replay{receipt.operation, receipt.units, lease.source(), receipt.ordinal, lease};
    const auto               before = target.domain->unitCount();
    auto  replayed                  = target.factory->prepare(replay, receipt.positions).expect("exact receipt replay");
    auto& token                     = std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(replayed);
    REQUIRE(token->isPublished());
    token->commit().expect("idempotent confirmation");
    REQUIRE_EQ(target.domain->unitCount(), before);
    auto changed = receipt.positions;
    changed[0].x += 1;
    REQUIRE(!target.factory->prepare(replay, changed).ok());
    auto frozen = target.factory->capture(saved.provenance->saveEpoch(), {}).expect("recapture restored source state");
    REQUIRE(frozen.sourceIncarnation() == lease.source().incarnation);
    REQUIRE_EQ(frozen.units().size(), saved.provenance->units().size());
    ecs::ScopedTable           scope(*target.table);
    auto                       subject = receipt.units[0].subject;
    auto*                      unit    = target.domain->findUnit(subject);
    eve::combat::DamageRuntime damage;
    eve::combat::DamageRequest request;
    request.target       = subject;
    request.healthDamage = 100;
    request.damageType   = "damage.physical";
    request.tick         = eve::SimulationTick(2);
    auto outcome         = damage.apply(unit->durability()->state, request).expect("actual restored death");
    target.domain->cleanupDestroyed().expect("native casualty cleanup");
    const std::array events{
        eve::rts::RTSFrameDamageEvent{1, request.tick, eve::rts::DamageChannel::Weapon, request, outcome}};
    auto lifecycle = target.factory->collectLifecycle(events).expect("fresh source lifecycle");
    REQUIRE(lifecycle[0].source == lease.source());
    target.core->advance({eve::SimulationTick(2), eve::Duration(1000000000)}, lifecycle)
        .expect("consume restored death");
    target.core->advance({eve::SimulationTick(3), eve::Duration(1000000000)}).expect("replacement uses pinned recipe");
    REQUIRE_EQ(target.domain->unitCount(), std::size_t(2));
    const auto current     = target.factory->capture(saved.provenance->saveEpoch(), {}).expect("actual replacement");
    bool       replacement = false;
    for (const auto& member : current.units())
        if (member.subject != receipt.units[1].subject) {
            REQUIRE_EQ(target.domain->findUnit(member.subject)->durability()->state.maxHealth, 40.0);
            REQUIRE(member.recipe->source()->get().generation ==
                    saved.provenance->futureUnits()[0].recipe->source()->get().generation);
            replacement = true;
        }
    REQUIRE(replacement);
}
TEST_CASE("RTS restored source factory resumes raw and translated lifecycle handoffs with remapped sources") {
    for (bool translated : {false, true}) {
        SavedRTSSpawn saved;
        {
            Fixture source;
            source.sourceWorld->advance({eve::SimulationTick(1), eve::Duration(100000000)}).expect("core publication");
            ecs::ScopedTable           scope(*source.table);
            eve::combat::DamageRuntime damage;
            eve::combat::DamageRequest request;
            request.target       = source.request.units[0].subject;
            request.healthDamage = 100;
            request.damageType   = "damage.physical";
            request.tick         = eve::SimulationTick(2);
            auto outcome         = damage.apply(source.domain->findUnit(request.target)->durability()->state, request)
                               .expect("death before core handoff");
            source.domain->cleanupDestroyed().expect("native cleanup");
            std::vector<eve::rts::RTSFrameDamageEvent> raw{
                {1, request.tick, eve::rts::DamageChannel::Weapon, request, outcome}};
            std::vector<eve::spawn::SavedLifecycleInput> pending;
            if (translated)
                for (const auto& input : source.factory->collectLifecycle(raw).expect("translate only"))
                    pending.push_back(eve::spawn::savedLifecycleInput(input));
            saved         = freezeSpawn(source, source.request.source.owner.child(translated ? "translated" : "raw"));
            saved.pending = std::move(pending);
            if (!translated) saved.damage = std::move(raw);
        }
        roundTripProvenance(saved);
        RestoredRTSSpawn target(saved);
        ecs::ScopedTable targetScope(*target.table);
        REQUIRE(!target.factory->restoreSources(*target.core, {}, {}).ok());
        auto pending = target.factory->restoreSources(*target.core, saved.pending, saved.damage)
                           .expect("full retained handoff admission");
        if (!translated) pending = target.factory->collectLifecycle(saved.damage).expect("translate raw only once");
        REQUIRE_EQ(pending.size(), std::size_t(1));
        REQUIRE(pending[0].reason == eve::spawn::RemovalReason::Death);
        REQUIRE(pending[0].source.incarnation != saved.provenance->receipts()[0].source.incarnation);
        target.core->advance({eve::SimulationTick(2), eve::Duration(1000000000)}, pending)
            .expect("restored translated handoff");
        target.core->advance({eve::SimulationTick(3), eve::Duration(1000000000)}).expect("restored replacement timer");
        REQUIRE_EQ(target.domain->unitCount(), std::size_t(2));
    }
}
TEST_CASE("RTS restored source retired floors reject a skipped interval attempt without fabricating a receipt") {
    Fixture          source;
    struct Templates final : eve::spawn::ITemplateResolver {
        eve::spawn::CompiledTemplate content;
        explicit Templates(const eve::spawn::IUnitDefinitionResolver& units)
            : content(eve::spawn::compileTemplate(
                          eve::Value::fromJson(R"({"schema":"eve.spawn-template","version":1,"id":"wolves",
                "content":{"kind":"weighted","selection":"per_unit","entries":[{"unit":{"type":"unit","id":"fighter"},"weight":1}]},
                "schedule":{"kind":"interval","period_seconds":1,"initial_delay_seconds":0,"count_per_batch":1,"on_missed":"skip"},
                "limits":{"alive":4,"total":{"kind":"unlimited"},"budget_group":"wolves"},
                "placement":{"kind":"random_in_region","constraints":[{"kind":"walkable"}],"attempts_per_unit":2},"batch_policy":"atomic"})")
                              .expect("interval asset"),
                          units)
                          .expect("interval compilation")) {}
        eve::Result<eve::spawn::CompiledTemplate> resolve(const eve::DefinitionRef&) const override {
            return eve::Result<eve::spawn::CompiledTemplate>::success(content);
        }
    } templates(*source.factory);
    auto map = eve::spawn::compileMap(eve::Value::fromJson(R"({"schema":"eve.map-spawns","version":1,"map":"forest",
        "budgets":[{"id":"wolves","alive_limit":4}],"instances":[{"id":"wolves","template":{"type":"spawn.template","id":"wolves"},
        "region":{"kind":"circle","space":"world2d","center":[0,0],"radius":10},"activation":{"kind":"map_loaded"},
        "inactive_policy":"pause","unload_policy":"persist"}]})")
                                          .expect("interval map"),
                                      templates, *source.factory)
                   .expect("compiled interval map");
    source.sourceWorld = std::make_unique<eve::spawn::SpawnWorld>(source.request.source.owner, 42);
    source.sourceWorld->attach(map, source.providers).expect("interval source");
    source.sourceWorld->advance({eve::SimulationTick(1), eve::Duration(1000000000)})
        .expect("successful first interval");
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
    source.providers->placement = std::make_shared<Blocked>();
    source.sourceWorld->advance({eve::SimulationTick(2), eve::Duration(1000000000)})
        .expect("skip blocked second interval");
    auto saved = freezeSpawn(source, source.request.source.owner.child("retired-floor"));
    REQUIRE_EQ(saved.provenance->receipts()[0].ordinal.value(), std::uint64_t(1));
    RestoredRTSSpawn target(saved);
    ecs::ScopedTable scope(*target.table);
    auto             bindings = target.core->restoreBindings().expect("actual operation floor");
    REQUIRE_EQ(bindings.sources[0].retiredThrough.value(), std::uint64_t(2));
    target.factory->restoreSources(*target.core, {}, {}).expect("source floor admission");
    const auto                         lease = bindings.sources[0].lifetime;
    const eve::spawn::OperationOrdinal skipped(2);
    eve::spawn::BatchRequest           request{
        lease.operationIdentity(skipped).expect("skipped identity"),
                  {{lease.unitIdentity(skipped, 0).expect("skipped subject"), saved.provenance->futureUnits()[0].reference}},
        lease.source(),
        skipped,
        lease};
    const std::array positions{eve::spawn::Point{10, 10}};
    REQUIRE(!target.factory->prepare(request, positions).ok());
    REQUIRE_EQ(target.domain->unitCount(), std::size_t(1));
    target.core->advance({eve::SimulationTick(3), eve::Duration(1000000000)})
        .expect("next interval after restored floor");
    REQUIRE_EQ(target.domain->unitCount(), std::size_t(2));
}
TEST_CASE("RTS native source restore rejects wrong core and member ownership and preserves origin-free bodies") {
    Fixture source;
    source.sourceWorld->advance({eve::SimulationTick(1), eve::Duration(100000000)}).expect("real source members");
    const auto       epoch = source.request.source.owner.child("source-admission");
    auto             saved = freezeSpawn(source, epoch);
    RestoredRTSSpawn first(saved), other(saved);
    REQUIRE(!first.factory->restoreSources(*other.core, {}, {}).ok());
    ecs::ScopedTable scope(*first.table);
    auto*            unit       = first.domain->findUnit(source.request.units[0].subject);
    const auto       definition = unit->definition()->id;
    unit->definition()->id      = *eve::LogicalId::parse("unit:foreign");
    REQUIRE(!first.factory->restoreSources(*first.core, {}, {}).ok());
    REQUIRE(!first.factory->inspect(source.request.units[0].subject).ok());
    unit->definition()->id = definition;
    first.factory->restoreSources(*first.core, {}, {}).expect("same candidate after actual repair");
    auto snapshot = first.factory->inspect(source.request.units[0].subject).expect("restored ownership");
    REQUIRE(snapshot.origin);
    REQUIRE(snapshot.origin->incarnation != source.request.source.incarnation);
    source.sourceWorld->detach(source.request.source, eve::spawn::DetachPolicy::KeepUnits)
        .expect("retire old source keep bodies");
    auto kept = freezeSpawn(source, epoch);
    REQUIRE(kept.provenance->receipts().empty());
    RestoredRTSSpawn retained(kept);
    retained.factory->restoreSources(*retained.core, {}, {}).expect("retired source with origin-free roots");
    REQUIRE(!retained.factory->inspect(source.request.units[0].subject).expect("retained root").origin);
    REQUIRE(!retained.core->resolveSavedSource(eve::spawn::savedSpawnerKey(source.request.source)).ok());
}
TEST_CASE("RTS native restored command resolves actual owned player roots independent of current Table") {
    Fixture         source;
    eve::SubjectRef playerId = eve::SubjectRef::fromPersistentId(source.request.source.owner.child("command-player"));
    {
        ecs::ScopedTable scope(*source.table);
        auto*            player  = source.domain->newPlayer(playerId).expect("real player root");
        auto             command = std::make_shared<eve::rts::CommandSpec>();
        command->kind            = eve::rts::OrderKind::Move;
        command->target          = {10, 20};
        command->targetEntity    = ecs::handle_of(player);
        auto faction             = source.factory->capture(source.request.source.owner.child("initial"), {})
                           .expect("source faction")
                           .faction();
        source.factory =
            std::make_shared<eve::rts_spawn::RTSSpawnFactory>(*source.domain, faction, source.registry, command);
        source.providers->factory = source.factory;
    }
    auto             saved = freezeSpawn(source, source.request.source.owner.child("player-command"));
    RestoredRTSSpawn target(saved);
    ecs::Table       unrelated;
    ecs::ScopedTable foreign(unrelated);
    auto             handle = target.domain->resolveOwnedSubject(playerId).expect("actual player identity");
    REQUIRE(handle.table == target.table.get());
    REQUIRE(!target.domain->resolveOwnedSubject({}).ok());
    target.factory->restoreSources(*target.core, {}, {}).expect("empty ready source binding");
    auto captured = target.factory->capture(saved.provenance->saveEpoch(), {}).expect("retained mapped command");
    REQUIRE(captured.initialCommandTarget() == playerId);
    REQUIRE(!captured.initialCommand()->targetEntity.table);
}
TEST_CASE("RTS full native Unit attributes restore non-combat values revisions and counters atomically") {
    std::optional<eve::rts::RTSUnitAttributeCapture> frozen;
    std::weak_ptr<ecs::Table>                        retired;
    std::string                                      nextId;
    eve::attributes::ModifierSequence                nextSequence = 0;
    {
        Fixture source;
        source.commit();
        retired = source.table;
        ecs::ScopedTable scope(*source.table);
        for (std::size_t i = 0; i < 2; ++i) {
            auto& values = source.domain->findUnit(source.request.units[i].subject)->attributes()->values;
            values.setBase("mana", 20 + double(i)).expect("non-combat authoritative base");
            values.addModifier({"", "mana", "equipment", eve::attributes::AttributeOperation::Add, 3})
                .expect("native modifier");
        }
        ecs::Table       foreign;
        ecs::ScopedTable other(foreign);
        frozen.emplace(source.domain->captureUnitAttributes(source.request.source.owner.child("attributes"))
                           .expect("actual world capture"));
        ecs::ScopedTable back(*source.table);
        auto&            values = source.domain->findUnit(source.request.units[0].subject)->attributes()->values;
        nextId                  = values.addModifier({"", "mana", "later", eve::attributes::AttributeOperation::Add, 2})
                     .expect("expected next identity");
        nextSequence = values.modifierAt(1)->sequence;
        values.setBase("mana", 900).expect("source advances independently");
    }
    REQUIRE(retired.expired());
    const auto                epoch       = frozen->saveEpoch();
    eve::SnapshotHashProvider retireFacts = [&](std::string_view text) {
        frozen.reset();
        return restoreHash(text);
    };
    auto envelope = frozen->seal(eve::Revision(4), eve::SimulationTick(7), retireFacts).takeValue();
    REQUIRE(!frozen);
    const auto text = eve::serializeSnapshotEnvelope(envelope).takeValue();
    frozen.emplace(eve::rts::RTSUnitAttributeCapture::parse(eve::parseSnapshotEnvelope(text, restoreHash).takeValue(),
                                                            epoch, restoreHash)
                       .takeValue());
    REQUIRE_EQ(
        eve::serializeSnapshotEnvelope(frozen->seal(eve::Revision(4), eve::SimulationTick(7), restoreHash).takeValue())
            .takeValue(),
        text);
    REQUIRE_EQ(frozen->members().size(), std::size_t(2));
    Fixture target;
    target.commit();
    ecs::ScopedTable scope(*target.table);
    auto*            first  = target.domain->findUnit(target.request.units[0].subject);
    auto*            second = target.domain->findUnit(target.request.units[1].subject);
    first->attributes()->values.setBase("mana", 99).expect("old first");
    second->attributes()->values.setBase("mana", 98).expect("old second");
    auto pending =
        eve::rts::PreparedRTSUnitAttributeRestore::prepare(*target.domain, *frozen).expect("prepare all projections");
    auto moved = std::move(pending);
    REQUIRE(!target.domain->restoreUnitAttributes(std::move(pending)).ok());
    auto*      earlier          = target.domain->findUnit(frozen->members().front().subject);
    auto*      later            = target.domain->findUnit(frozen->members().back().subject);
    const auto earlierValue     = earlier->attributes()->values.getFinal("mana").expect("earlier original value");
    auto       originalLater    = std::move(later->attributes()->values);
    later->attributes()->values = eve::rts::AttributeComponent{};
    later->attributes()->values.bindOwner(ecs::handle_of(earlier)).expect("wrong later owner");
    REQUIRE(!target.domain->restoreUnitAttributes(std::move(moved)).ok());
    REQUIRE_EQ(earlier->attributes()->values.getFinal("mana").expect("earlier unchanged"), earlierValue);
    later->attributes()->values = std::move(originalLater);
    {
        ecs::View<eve::rts::Unit> view(*target.table);
        REQUIRE(!target.domain->restoreUnitAttributes(std::move(moved)).ok());
    }
    const auto queries = eve::cap::listenerCount<eve::IStateQuery>();
    target.domain->restoreUnitAttributes(std::move(moved)).expect("same candidate complete publication");
    REQUIRE_EQ(eve::cap::listenerCount<eve::IStateQuery>(), queries);
    REQUIRE(!target.domain->restoreUnitAttributes(std::move(moved)).ok());
    REQUIRE_EQ(first->attributes()->values.getFinal("mana").expect("full custom value"), 23.0);
    REQUIRE_EQ(second->attributes()->values.getFinal("mana").expect("other custom value"), 24.0);
    const auto savedFirst = std::find_if(frozen->members().begin(), frozen->members().end(), [&](const auto& value) {
        return value.subject == target.request.units[0].subject;
    });
    auto       restored   = first->attributes()->values.capture().expect("actual restored native metadata");
    REQUIRE(restored.revision() == savedFirst->attributes->revision());
    REQUIRE(restored.initialized() == savedFirst->attributes->initialized());
    REQUIRE(first->attributes()
                ->values.addModifier({"", "mana", "later", eve::attributes::AttributeOperation::Add, 2})
                .expect("continued identity") == nextId);
    REQUIRE_EQ(first->attributes()->values.modifierAt(1)->sequence, nextSequence);
    auto transferred = std::move(*frozen);
    REQUIRE(frozen->saveEpoch().isNil());
    REQUIRE(frozen->members().empty());
    REQUIRE(!eve::rts::PreparedRTSUnitAttributeRestore::prepare(*target.domain, *frozen).ok());
}
TEST_CASE("RTS Unit attribute archive rejects malformed membership and leaves topology admission to native owner") {
    using namespace eve;
    using namespace eve::rts;
    Fixture fixture;
    fixture.commit();
    const auto epoch    = fixture.request.source.owner.child("attribute-archive");
    auto       saved    = fixture.domain->captureUnitAttributes(epoch).takeValue();
    const auto envelope = saved.seal(Revision(1), SimulationTick(1), restoreHash).takeValue();
    auto       reseal   = [&](SnapshotEnvelope value) {
        return makeSnapshotEnvelope(value.type, value.schema, value.schemaVersion, value.instanceId, value.revision,
                                            value.tick, std::move(value.payload), restoreHash)
            .takeValue();
    };
    auto bad = envelope;
    bad.payload.set("extra", true);
    REQUIRE(!RTSUnitAttributeCapture::parse(bad, epoch, restoreHash).ok());
    REQUIRE(!RTSUnitAttributeCapture::parse(reseal(bad), epoch, restoreHash).ok());
    bad               = envelope;
    bad.schemaVersion = SchemaVersion(2);
    REQUIRE(!RTSUnitAttributeCapture::parse(reseal(bad), epoch, restoreHash).ok());
    bad = envelope;
    bad.payload.find("members")->getIf<Value::Array>()->push_back(bad.payload.find("members")->at(0));
    REQUIRE(!RTSUnitAttributeCapture::parse(reseal(bad), epoch, restoreHash).ok());
    bad        = envelope;
    auto& rows = *bad.payload.find("members")->getIf<Value::Array>();
    std::swap(rows[0], rows[1]);
    REQUIRE(!RTSUnitAttributeCapture::parse(reseal(bad), epoch, restoreHash).ok());
    bad                                      = envelope;
    bad.payload.find("members")->at(0).at(0) = Value("not-a-subject");
    REQUIRE(!RTSUnitAttributeCapture::parse(reseal(bad), epoch, restoreHash).ok());
    bad = envelope;
    bad.payload.find("members")->at(0).at(1).set("initialized", false);
    REQUIRE(!RTSUnitAttributeCapture::parse(reseal(bad), epoch, restoreHash).ok());
    bad = envelope;
    bad.payload.find("members")->getIf<Value::Array>()->pop_back();
    auto       missing = RTSUnitAttributeCapture::parse(reseal(bad), epoch, restoreHash).takeValue();
    const auto before  = fixture.domain->canonicalStateJson().takeValue();
    REQUIRE(!PreparedRTSUnitAttributeRestore::prepare(*fixture.domain, missing).ok());
    REQUIRE_EQ(fixture.domain->canonicalStateJson().takeValue(), before);
    REQUIRE(!RTSUnitAttributeCapture::parse(envelope, epoch.child("foreign"), restoreHash).ok());
    REQUIRE(!saved.seal(Revision::zero(), SimulationTick(1), restoreHash).ok());
    auto moved = std::move(saved);
    REQUIRE(!saved.seal(Revision(1), SimulationTick(1), restoreHash).ok());
    REQUIRE(moved.seal(Revision(1), SimulationTick(1), restoreHash).ok());
}
TEST_CASE("RTS native Unit attribute admission rejects changed scope missing components and replacements") {
    Fixture fixture;
    fixture.commit();
    const auto epoch = fixture.request.source.owner.child("attribute-admission");
    REQUIRE(!fixture.domain->captureUnitAttributes({}).ok());
    eve::rts::RTS borrowed;
    REQUIRE(!borrowed.captureUnitAttributes(epoch).ok());
    auto             saved = fixture.domain->captureUnitAttributes(epoch).expect("native scope");
    ecs::ScopedTable scope(*fixture.table);
    {
        ecs::View<eve::rts::Unit> view(*fixture.table);
        REQUIRE(!fixture.domain->captureUnitAttributes(epoch).ok());
        REQUIRE(!eve::rts::PreparedRTSUnitAttributeRestore::prepare(*fixture.domain, saved).ok());
    }
    auto pending =
        eve::rts::PreparedRTSUnitAttributeRestore::prepare(*fixture.domain, saved).expect("original target topology");
    Fixture other;
    other.commit();
    REQUIRE(!other.domain->restoreUnitAttributes(std::move(pending)).ok());
    auto* unit   = fixture.domain->findUnit(fixture.request.units[1].subject);
    auto* buffer = unit->getComponentManager().getComponentBuffer<eve::rts::Unit::Attributes>();
    buffer->reset_slot(unit->id);
    REQUIRE(!fixture.domain->captureUnitAttributes(epoch).ok());
    REQUIRE(!fixture.domain->restoreUnitAttributes(std::move(pending)).ok());
    REQUIRE(!buffer->has(unit->id));
    unit->attributes()->values.bindOwner(ecs::handle_of(unit)).expect("explicit caller repair");
    fixture.domain->restoreUnitAttributes(std::move(pending)).expect("retry after caller repair");
    auto changed     = eve::rts::PreparedRTSUnitAttributeRestore::prepare(*fixture.domain, saved).expect("scope token");
    const auto extra = eve::SubjectRef::fromPersistentId(epoch.child("new-root"));
    fixture.domain->newUnit(extra).expect("topology changed");
    REQUIRE(!fixture.domain->restoreUnitAttributes(std::move(changed)).ok());
    REQUIRE(!eve::rts::PreparedRTSUnitAttributeRestore::prepare(*fixture.domain, saved).ok());
    auto current = fixture.domain->captureUnitAttributes(epoch).expect("complete changed scope");
    auto stale =
        eve::rts::PreparedRTSUnitAttributeRestore::prepare(*fixture.domain, current).expect("generation candidate");
    const auto replaced = ecs::handle_of(unit);
    const auto subject  = unit->identity()->subject;
    unit->release();
    auto* substitute = eve::rts::Unit::createUnit(subject);
    substitute->attributes()->values.bindOwner(ecs::handle_of(substitute)).expect("replacement owner");
    substitute->attributes()->values.setBase("mana", 51).expect("replacement facts");
    REQUIRE(!ecs::try_get(replaced));
    REQUIRE(!fixture.domain->restoreUnitAttributes(std::move(stale)).ok());
    REQUIRE_EQ(substitute->attributes()->values.getFinal("mana").expect("replacement untouched"), 51.0);
}
TEST_CASE("RTS native Unit attributes preserve uninitialized state and do not retain target owners") {
    Fixture fixture;
    fixture.commit();
    const auto                                               epoch = fixture.request.source.owner.child("unseeded");
    std::optional<eve::rts::RTSUnitAttributeCapture>         frozen;
    std::optional<eve::rts::PreparedRTSUnitAttributeRestore> abandoned;
    {
        ecs::ScopedTable scope(*fixture.table);
        auto*            first      = fixture.domain->findUnit(fixture.request.units[0].subject);
        first->attributes()->values = eve::rts::AttributeComponent{};
        first->attributes()->values.bindOwner(ecs::handle_of(first)).expect("uninitialized owner");
        frozen.emplace(fixture.domain->captureUnitAttributes(epoch).expect("native unseeded capture"));
        auto wire = eve::serializeSnapshotEnvelope(
                        frozen->seal(eve::Revision(1), eve::SimulationTick(0), restoreHash).takeValue())
                        .takeValue();
        *frozen = eve::rts::RTSUnitAttributeCapture::parse(eve::parseSnapshotEnvelope(wire, restoreHash).takeValue(),
                                                           epoch, restoreHash)
                      .takeValue();
        eve::rts::RTSUnitAttributeAdapter::ensure(*first).expect("later initialization");
        first->attributes()->values.setBase("mana", 7).expect("target-only value after initialization");
        auto pending =
            eve::rts::PreparedRTSUnitAttributeRestore::prepare(*fixture.domain, *frozen).expect("native candidate");
        fixture.domain->restoreUnitAttributes(std::move(pending)).expect("restore exact seed flag");
        REQUIRE(!first->attributes()->values.initialized());
        REQUIRE(!first->attributes()->values.has("mana"));
        abandoned.emplace(eve::rts::PreparedRTSUnitAttributeRestore::prepare(*fixture.domain, *frozen)
                              .expect("weak target candidate"));
    }
    auto weakWorld = std::weak_ptr<ecs::Table>(fixture.table);
    fixture.domain.reset();
    fixture.providers.reset();
    fixture.factory.reset();
    fixture.table.reset();
    REQUIRE(weakWorld.expired());
    Fixture replacement;
    replacement.commit();
    REQUIRE(!replacement.domain->restoreUnitAttributes(std::move(*abandoned)).ok());
    auto          emptyWorld = std::make_shared<ecs::Table>();
    eve::rts::RTS empty(emptyWorld);
    auto          emptySaved = empty.captureUnitAttributes(epoch).expect("empty scope");
    auto noOp = eve::rts::PreparedRTSUnitAttributeRestore::prepare(empty, emptySaved).expect("empty candidate");
    empty.restoreUnitAttributes(std::move(noOp)).expect("empty publication");
}
TEST_CASE("RTS full weapon closure includes non-generated bodies aliases absence and detached targets") {
    std::optional<eve::rts::RTSWeaponCapture> saved;
    std::weak_ptr<ecs::Table>                 retired;
    eve::SubjectRef                           buildingId, emptyId;
    eve::PersistentId                         epoch;
    {
        Fixture fixture;
        fixture.commit();
        retired    = fixture.table;
        epoch      = fixture.request.source.owner.child("all-weapons");
        buildingId = eve::SubjectRef::fromPersistentId(epoch.child("building"));
        emptyId    = eve::SubjectRef::fromPersistentId(epoch.child("empty"));
        ecs::ScopedTable scope(*fixture.table);
        auto*            building = fixture.domain->newBuilding(buildingId).expect("non-generated building");
        auto*            empty    = fixture.domain->newUnit(emptyId).expect("non-generated unarmed unit");
        REQUIRE(!empty->weapon()->link.isBound());
        auto provenance = fixture.factory->capture(epoch, {}).expect("generated provenance only");
        REQUIRE_EQ(provenance.units().size(), std::size_t(2));
        const auto externalId = eve::SubjectRef::fromPersistentId(epoch.child("native-reinforcement"));
        auto       recipe =
            eve::rts::PreparedUnitArchetype::prepare(*fixture.registry, *eve::LogicalId::parse("unit:fighter"))
                .expect("canonical non-generated recipe");
        const std::array nativeMembers{eve::rts::UnitBatchRequest{externalId, recipe, 20, 20, {}}};
        auto             native =
            fixture.domain->prepareUnitBatch(*fixture.domain->findFaction(provenance.faction()), nativeMembers)
                .expect("native batch outside spawn provenance");
        REQUIRE(native.commit().expect("native reinforcement").state == eve::rts::UnitBatchState::Committed);
        auto* unit               = fixture.domain->findUnit(fixture.request.units[0].subject);
        auto* gun                = dynamic_cast<eve::weapon::WeaponEntity*>(unit->weapon()->link.resolve());
        building->weapon()->link = eve::rts::WeaponLink::bind(ecs::handle_of(gun)).expect("actual body alias");
        gun->state()->lastRequest.targetHandle = ecs::handle_of(building);
        gun->state()->cooldown                 = 0.75f;
        auto* pool                             = eve::weapon::AmmoPoolEntity::createPool();
        pool->state()->count                   = 17;
        gun->state()->ammoPool                 = eve::weapon::AmmoPoolLink::bind(*gun, *pool).expect("shared pool");
        ecs::Table       unrelated;
        ecs::ScopedTable foreign(unrelated);
        saved.emplace(fixture.domain->captureWeapons(epoch).expect("actual owning world closure"));
        REQUIRE_EQ(saved->members().size(), std::size_t(5));
        const auto find = [&](eve::SubjectRef id) -> const eve::rts::RTSWeaponMember& {
            auto found = std::find_if(saved->members().begin(), saved->members().end(),
                                      [&](const auto& value) { return value.body == id; });
            REQUIRE(found != saved->members().end());
            return *found;
        };
        const auto& linked = find(buildingId);
        REQUIRE(linked.weapon);
        REQUIRE(linked.weapon == find(fixture.request.units[0].subject).weapon);
        REQUIRE(!find(emptyId).weapon);
        REQUIRE(find(externalId).weapon);
        REQUIRE(find(externalId).weapon != linked.weapon);
        REQUIRE(linked.weapon->facts()->get().target == buildingId);
        REQUIRE(!linked.weapon->facts()->get().request.targetHandle.table);
        REQUIRE_EQ(linked.weapon->facts()->get().pool->state.count, 17);
        REQUIRE_EQ(linked.weapon->facts()->get().state.cooldown, 0.75f);
        gun->state()->cooldown = 8;
        pool->state()->count   = 1;
    }
    REQUIRE(retired.expired());
    REQUIRE(saved->saveEpoch() == epoch);
    auto building = std::find_if(saved->members().begin(), saved->members().end(),
                                 [&](const auto& value) { return value.body == buildingId; });
    REQUIRE_EQ(building->weapon->facts()->get().pool->state.count, 17);
    REQUIRE_EQ(building->weapon->facts()->get().state.cooldown, 0.75f);
    auto moved = std::move(*saved);
    REQUIRE(saved->saveEpoch().isNil());
    REQUIRE(saved->members().empty());
    REQUIRE_EQ(moved.members().size(), std::size_t(5));
}
TEST_CASE("RTS full weapon closure rejects borrowed busy missing stale and foreign facts without repair") {
    Fixture fixture;
    fixture.commit();
    const auto epoch = fixture.request.source.owner.child("weapon-admission");
    REQUIRE(!fixture.domain->captureWeapons({}).ok());
    eve::rts::RTS borrowed;
    REQUIRE(!borrowed.captureWeapons(epoch).ok());
    ecs::ScopedTable scope(*fixture.table);
    {
        ecs::View<eve::rts::Unit> view(*fixture.table);
        REQUIRE(!fixture.domain->captureWeapons(epoch).ok());
    }
    auto*      unit     = fixture.domain->findUnit(fixture.request.units[0].subject);
    auto*      gun      = dynamic_cast<eve::weapon::WeaponEntity*>(unit->weapon()->link.resolve());
    const auto original = unit->weapon()->link;
    {
        ecs::Table       foreign;
        ecs::ScopedTable other(foreign);
        auto*            otherGun = eve::weapon::WeaponEntity::createWeapon();
        unit->weapon()->link      = eve::rts::WeaponLink::bind(ecs::handle_of(otherGun)).expect("foreign link");
        REQUIRE(!fixture.domain->captureWeapons(epoch).ok());
    }
    unit->weapon()->link = original;
    auto* otherGun       = dynamic_cast<eve::weapon::WeaponEntity*>(
        fixture.domain->findUnit(fixture.request.units[1].subject)->weapon()->link.resolve());
    const auto originalId                              = otherGun->definitionBinding()->identity.instanceId;
    otherGun->definitionBinding()->identity.instanceId = gun->definitionBinding()->identity.instanceId;
    REQUIRE(!fixture.domain->captureWeapons(epoch).ok());
    otherGun->definitionBinding()->identity.instanceId = originalId;
    gun->state()->lastRequest.targetHandle             = ecs::handle_of(gun);
    REQUIRE(!fixture.domain->captureWeapons(epoch).ok());
    gun->state()->lastRequest.targetHandle = {};
    auto* weapons                          = unit->getComponentManager().getComponentBuffer<eve::rts::Unit::Weapon>();
    weapons->reset_slot(unit->id);
    REQUIRE(!fixture.domain->captureWeapons(epoch).ok());
    REQUIRE(!weapons->has(unit->id));
    unit->weapon()->link = original;
    gun->release();
    REQUIRE(!fixture.domain->captureWeapons(epoch).ok());
    REQUIRE(unit->weapon()->link.isBound());
}
TEST_CASE("RTS native weapon graph admits empty scope and rejects duplicate correspondences") {
    Fixture fixture;
    fixture.commit();
    ecs::ScopedTable scope(*fixture.table);
    const auto       epoch = fixture.request.source.owner.child("graph-admission");
    auto             graph =
        eve::weapon::PreparedWeaponGraphRestore::prepare(epoch, fixture.table, {}, {}, {}).expect("empty graph");
    auto receipt = graph.commit().expect("empty publication");
    REQUIRE(receipt.pools.empty());
    REQUIRE_EQ(receipt.weapons, std::size_t(0));
    REQUIRE(receipt.saveEpoch == epoch);
    REQUIRE(!eve::weapon::PreparedWeaponGraphRestore::prepare({}, fixture.table, {}, {}, {}).ok());
    auto* gun = dynamic_cast<eve::weapon::WeaponEntity*>(
        fixture.domain->findUnit(fixture.request.units[0].subject)->weapon()->link.resolve());
    auto bindings = eve::weapon::WeaponPoolBindings::capture(*fixture.table, epoch).expect("empty pool graph");
    auto frozen   = eve::weapon::WeaponCapture::capture(*gun, epoch, {}, bindings).expect("actual member");
    const std::array duplicate{eve::weapon::WeaponGraphMember{std::cref(frozen), ecs::handle_of(gun)},
                               eve::weapon::WeaponGraphMember{std::cref(frozen), ecs::handle_of(gun)}};
    REQUIRE(!eve::weapon::PreparedWeaponGraphRestore::prepare(epoch, fixture.table, duplicate, {}, {}).ok());
    const auto                    first  = ecs::handle_of(eve::weapon::AmmoPoolEntity::createPool());
    const auto                    second = ecs::handle_of(eve::weapon::AmmoPoolEntity::createPool());
    eve::weapon::CapturedAmmoPool saved{{"same", "round"}, {9, 50}, epoch.child("pool")};
    std::array pools{eve::weapon::WeaponGraphPool{saved, first}, eve::weapon::WeaponGraphPool{saved, second}};
    REQUIRE(!eve::weapon::PreparedWeaponGraphRestore::prepare(epoch, fixture.table, {}, pools, {}).ok());
    pools[1].captured.archiveId = epoch.child("other");
    pools[1].target             = first;
    REQUIRE(!eve::weapon::PreparedWeaponGraphRestore::prepare(epoch, fixture.table, {}, pools, {}).ok());
}
TEST_CASE("RTS native pool restoration rebuilds shared weapon aliases and resumes one ammunition authority") {
    std::vector<eve::weapon::WeaponCapture> frozen;
    std::weak_ptr<ecs::Table>               retired;
    {
        Fixture source;
        source.commit();
        retired = source.table;
        ecs::ScopedTable scope(*source.table);
        auto*            pool      = eve::weapon::AmmoPoolEntity::createPool();
        pool->identity()->id       = "shared-name";
        pool->identity()->ammoType = "round";
        pool->state()->count       = 9;
        pool->state()->max         = 50;
        for (std::size_t i = 0; i < 2; ++i) {
            auto* gun = dynamic_cast<eve::weapon::WeaponEntity*>(
                source.domain->findUnit(source.request.units[i].subject)->weapon()->link.resolve());
            gun->state()->ammoPool       = eve::weapon::AmmoPoolLink::bind(*gun, *pool).expect("actual source alias");
            gun->state()->resource.value = 0;
            eve::weapon::WeaponSystem::startReload(*gun);
            eve::weapon::WeaponSystem::update(*gun, i == 0 ? .25f : .5f);
            eve::weapon::WeaponAttributeAdapter::setBase(*gun, "mana", 3 + static_cast<int>(i))
                .expect("source native attributes");
        }
        auto epoch    = source.request.source.owner.child("pool-restore");
        auto bindings = eve::weapon::WeaponPoolBindings::capture(*source.table, epoch).expect("one source pool graph");
        for (std::size_t i = 0; i < 2; ++i) {
            auto* gun = dynamic_cast<eve::weapon::WeaponEntity*>(
                source.domain->findUnit(source.request.units[i].subject)->weapon()->link.resolve());
            frozen.push_back(eve::weapon::WeaponCapture::capture(*gun, epoch, {}, bindings).expect("detached member"));
        }
    }
    REQUIRE(retired.expired());
    const auto saved = *frozen[0].facts()->get().pool;
    REQUIRE(frozen[1].facts()->get().pool->archiveId == saved.archiveId);
    Fixture destination;
    destination.commit();
    ecs::ScopedTable scope(*destination.table);
    auto             gun = [&](std::size_t i) {
        return dynamic_cast<eve::weapon::WeaponEntity*>(
            destination.domain->findUnit(destination.request.units[i].subject)->weapon()->link.resolve());
    };
    auto* pool           = eve::weapon::AmmoPoolEntity::createPool();
    pool->identity()->id = "old-target";
    pool->state()->count = 99;
    auto old             = eve::weapon::AmmoPoolLink::bind(*gun(0), *pool).expect("old physical lifetime");
    auto plan = eve::weapon::PreparedAmmoPoolRestore::prepare(saved, destination.table, *pool).expect("pool candidate");
    REQUIRE_EQ(pool->state()->count, 99);
    REQUIRE(!old.isStale());
    {
        auto view = ecs::View<eve::weapon::AmmoPoolEntity>(*destination.table);
        REQUIRE(!plan.commit().ok());
    }
    REQUIRE(!old.isStale());
    auto receipt = plan.commit().expect("pool facts and lifetime publication");
    REQUIRE(old.isStale());
    REQUIRE(receipt.archiveId == saved.archiveId);
    REQUIRE(receipt.pool.resolve() == pool);
    REQUIRE_EQ(pool->state()->count, 9);
    REQUIRE_EQ(pool->identity()->id, "shared-name");
    REQUIRE(!plan.commit().ok());
    pool->state()->count = 99;
    const auto epoch     = frozen[0].saveEpoch();
    auto       graph     = [&] {
        const std::array members{eve::weapon::WeaponGraphMember{std::cref(frozen[0]), ecs::handle_of(gun(0))},
                                 eve::weapon::WeaponGraphMember{std::cref(frozen[1]), ecs::handle_of(gun(1))}};
        const std::array poolTargets{eve::weapon::WeaponGraphPool{saved, ecs::handle_of(pool)}};
        auto             conflicting = poolTargets;
        ++conflicting[0].captured.state.count;
        REQUIRE(
            !eve::weapon::PreparedWeaponGraphRestore::prepare(epoch, destination.table, members, conflicting, {}).ok());
        REQUIRE(!eve::weapon::PreparedWeaponGraphRestore::prepare(epoch, destination.table, members, {}, {}).ok());
        REQUIRE(!eve::weapon::PreparedWeaponGraphRestore::prepare(epoch.child("wrong"), destination.table, members,
                                                                            poolTargets, {})
                               .ok());
        return eve::weapon::PreparedWeaponGraphRestore::prepare(epoch, destination.table, members, poolTargets, {})
            .expect("entire graph candidate");
    }();
    auto moved = std::move(graph);
    REQUIRE(!graph.commit().ok());
    frozen.clear();
    gun(1)->attributes()->values = eve::attributes::AttributeProjection("wrong-subject");
    const auto queries           = eve::cap::listenerCount<eve::IStateQuery>();
    REQUIRE(!moved.commit().ok());
    REQUIRE_EQ(eve::cap::listenerCount<eve::IStateQuery>(), queries);
    REQUIRE_EQ(pool->state()->count, 99);
    REQUIRE(!receipt.pool.isStale());
    REQUIRE_EQ(gun(0)->state()->resource.value, 1000.f);
    auto* selected = gun(0)->getComponentManager().getComponentBuffer<eve::weapon::WeaponEntity::Attributes>();
    REQUIRE(!selected->has(gun(0)->id));
    selected->reset_slot(gun(1)->id);
    auto restored = moved.commit().expect("joint pool and weapon publication after rejection");
    REQUIRE(!moved.commit().ok());
    REQUIRE_EQ(restored.weapons, std::size_t(2));
    REQUIRE_EQ(restored.pools.size(), std::size_t(1));
    REQUIRE(restored.saveEpoch == epoch);
    REQUIRE(receipt.pool.isStale());
    REQUIRE(restored.pools[0].pool.resolve() == pool);
    REQUIRE(gun(0)->state()->ammoPool.resolve() == pool);
    REQUIRE(gun(1)->state()->ammoPool.resolve() == pool);
    REQUIRE_EQ(pool->state()->count, 9);
    REQUIRE_EQ(gun(0)->state()->resource.value, 0.f);
    REQUIRE_EQ(gun(1)->state()->resource.value, 0.f);
    eve::weapon::WeaponSystem::update(*gun(1), .5f);
    REQUIRE_EQ(gun(1)->state()->resource.value, 9.f);
    REQUIRE_EQ(pool->state()->count, 0);
    eve::weapon::WeaponSystem::update(*gun(0), .75f);
    REQUIRE_EQ(gun(0)->state()->resource.value, 0.f);
    REQUIRE_EQ(pool->state()->count, 0);
    REQUIRE_EQ(gun(0)->attributes()->values.getFinal("mana").expect("first native attributes"), 3.0);
    REQUIRE_EQ(gun(1)->attributes()->values.getFinal("mana").expect("second native attributes"), 4.0);
}
TEST_CASE("RTS native pool restore rejects changed lifetime and replacement and preserves distinct same-name pools") {
    auto             world = std::make_shared<ecs::Table>();
    ecs::ScopedTable scope(*world);
    auto*            gun          = eve::weapon::WeaponEntity::createWeapon();
    auto             firstHandle  = ecs::handle_of(eve::weapon::AmmoPoolEntity::createPool());
    auto             secondHandle = ecs::handle_of(eve::weapon::AmmoPoolEntity::createPool());
    auto*            first        = dynamic_cast<eve::weapon::AmmoPoolEntity*>(ecs::try_get(firstHandle));
    auto*            second       = dynamic_cast<eve::weapon::AmmoPoolEntity*>(ecs::try_get(secondHandle));
    first->identity()->id = second->identity()->id = "same-name";
    first->state()->count                          = 5;
    second->state()->count                         = 7;
    auto                          old   = eve::weapon::AmmoPoolLink::bind(*gun, *first).expect("original link");
    auto                          epoch = *eve::PersistentId::parse("01900000-0000-7000-8000-000000000b35");
    eve::weapon::CapturedAmmoPool saved{*first->identity(), {9, 50}, epoch.child("first")};
    auto                          bad = saved;
    bad.archiveId                     = {};
    REQUIRE(!eve::weapon::PreparedAmmoPoolRestore::prepare(bad, world, *first).ok());
    auto lifetimePlan  = eve::weapon::PreparedAmmoPoolRestore::prepare(saved, world, *first).expect("lifetime guard");
    *first->lifetime() = eve::weapon::AmmoPoolEntity::Lifetime{};
    auto fresh         = eve::weapon::AmmoPoolLink::bind(*gun, *first).expect("explicit new lifetime");
    REQUIRE(old.isStale());
    REQUIRE(!lifetimePlan.commit().ok());
    REQUIRE(!fresh.isStale());
    REQUIRE_EQ(first->state()->count, 5);
    auto generationPlan =
        eve::weapon::PreparedAmmoPoolRestore::prepare(saved, world, *first).expect("generation guard");
    first->release();
    auto  replacementHandle     = ecs::handle_of(eve::weapon::AmmoPoolEntity::createPool());
    auto* replacement           = dynamic_cast<eve::weapon::AmmoPoolEntity*>(ecs::try_get(replacementHandle));
    replacement->identity()->id = "same-name";
    REQUIRE(!generationPlan.commit().ok());
    REQUIRE_EQ(replacement->state()->count, 0);
    second            = dynamic_cast<eve::weapon::AmmoPoolEntity*>(ecs::try_get(secondHandle));
    auto other        = saved;
    other.archiveId   = epoch.child("second");
    other.state.count = 13;
    auto a            = eve::weapon::PreparedAmmoPoolRestore::prepare(saved, world, *replacement)
                 .expect("replacement admission")
                 .commit()
                 .expect("first distinct pool");
    auto b = eve::weapon::PreparedAmmoPoolRestore::prepare(other, world, *second)
                 .expect("other pool admission")
                 .commit()
                 .expect("second distinct pool");
    REQUIRE(a.archiveId != b.archiveId);
    REQUIRE(a.pool.resolve() != b.pool.resolve());
    REQUIRE_EQ(a.pool.resolve()->state()->count, 9);
    REQUIRE_EQ(b.pool.resolve()->state()->count, 13);
    REQUIRE_EQ(a.pool.resolve()->identity()->id, b.pool.resolve()->identity()->id);
}
TEST_CASE("RTS native pool restore does not retain target world or expose its future lifetime") {
    std::optional<eve::weapon::PreparedAmmoPoolRestore> pending;
    std::weak_ptr<ecs::Table>                           retired;
    {
        auto world = std::make_shared<ecs::Table>();
        retired    = world;
        ecs::ScopedTable              scope(*world);
        auto*                         pool  = eve::weapon::AmmoPoolEntity::createPool();
        auto                          epoch = *eve::PersistentId::parse("01900000-0000-7000-8000-000000000b36");
        eve::weapon::CapturedAmmoPool saved{{"saved", "round"}, {3, 10}, epoch.child("pool")};
        pending.emplace(eve::weapon::PreparedAmmoPoolRestore::prepare(saved, world, *pool).expect("weak destination"));
        auto* gun        = eve::weapon::WeaponEntity::createWeapon();
        auto  newBinding = eve::weapon::AmmoPoolLink::bind(*gun, *pool).expect("first binding after prepare");
        REQUIRE(!pending->commit().ok());
        REQUIRE(!newBinding.isStale());
        REQUIRE_EQ(pool->state()->count, 0);
    }
    REQUIRE(retired.expired());
    REQUIRE(!pending->commit().ok());
}
TEST_CASE("RTS native weapon restoration owns stages and exact attributes after source retirement without replay") {
    std::optional<eve::weapon::WeaponCapture> frozen;
    std::weak_ptr<ecs::Table>                 retired;
    eve::SubjectRef                           targetSubject;
    {
        Fixture source;
        source.commit();
        retired = source.table;
        ecs::ScopedTable scope(*source.table);
        auto*            unit   = source.domain->findUnit(source.request.units[0].subject);
        auto*            target = source.domain->findUnit(source.request.units[1].subject);
        targetSubject           = target->identity()->subject;
        auto* gun               = dynamic_cast<eve::weapon::WeaponEntity*>(unit->weapon()->link.resolve());
        eve::rts::RTSContentLoader::load(
            *source.registry,
            R"({"weapons":[{"id":"club","kind":"melee","damageType":"normal","damage":10,"range":1.2,"cooldown":0.2,"resource":{"kind":"ammo","max":1000,"cost":1}}]})")
            .expect("canonical staged definition revision");
        auto stagedRuntime = eve::weapon::WeaponDefinitionRuntime::create(*source.registry, "club",
                                                                          gun->definitionBinding()->identity.instanceId)
                                 .expect("canonical staged runtime");
        stagedRuntime.applyTo(gun).expect("canonical live revision replacement");
        auto* pool                 = eve::weapon::AmmoPoolEntity::createPool();
        pool->identity()->id       = "restored-shared";
        pool->identity()->ammoType = "club";
        pool->state()->count       = 9;
        pool->state()->max         = 40;
        gun->state()->ammoPool     = eve::weapon::AmmoPoolLink::bind(*gun, *pool).expect("source pool");
        eve::weapon::AttackStageSpec stages{.5f, .25f, .75f};
        gun->state()->stages = &stages;
        eve::weapon::AttackRequest request;
        request.targetHandle = ecs::handle_of(target);
        request.targetX      = 17;
        REQUIRE(eve::weapon::WeaponSystem::tryFire(*gun, request));
        eve::weapon::WeaponSystem::update(*gun, .05f);
        gun->state()->resource.value   = 0;
        gun->state()->resource.reserve = 8;
        eve::weapon::WeaponSystem::startReload(*gun);
        gun->state()->burstRemaining = 2;
        gun->state()->burstTimer     = .1f;
        gun->state()->recoilPitch    = .25f;
        gun->aim()->yaw              = 31;
        eve::weapon::WeaponAttributeAdapter::setBase(*gun, "mana", 3).expect("native mana");
        eve::weapon::WeaponAttributeAdapter::addModifier(*gun, "saved", "mana", "source",
                                                         eve::attributes::AttributeOperation::Add, 2)
            .expect("native modifier");
        const auto       epoch = source.request.source.owner.child("weapon-restore");
        auto             pools = eve::weapon::WeaponPoolBindings::capture(*source.table, epoch).expect("source graph");
        const std::array targets{eve::weapon::WeaponCaptureTarget{ecs::handle_of(target), targetSubject}};
        frozen.emplace(eve::weapon::WeaponCapture::capture(*gun, epoch, targets, pools).expect("full source freeze"));
    }
    REQUIRE(retired.expired());
    const auto text = frozen->encodeState().expect("complete weapon value").toJson().expect("complete weapon text");
    frozen.reset();
    const auto input = eve::Value::fromJson(text).expect("owning complete weapon input");
    frozen.emplace(eve::weapon::WeaponCapture::prepareState(input).expect("detached complete weapon admission"));
    REQUIRE_EQ(frozen->encodeState().expect("repeat full weapon").toJson().expect("repeat wire"), text);
    const auto expected = frozen->facts()->get();
    Fixture    destination;
    destination.commit();
    ecs::ScopedTable scope(*destination.table);
    auto*            unit   = destination.domain->findUnit(destination.request.units[0].subject);
    auto*            target = destination.domain->findUnit(targetSubject);
    REQUIRE(target != nullptr);
    auto* gun                 = dynamic_cast<eve::weapon::WeaponEntity*>(unit->weapon()->link.resolve());
    auto* pool                = eve::weapon::AmmoPoolEntity::createPool();
    *pool->identity()         = expected.pool->identity;
    *pool->state()            = expected.pool->state;
    const auto       poolLink = eve::weapon::AmmoPoolLink::bind(*gun, *pool).expect("restored pool relationship");
    const std::array pools{eve::weapon::WeaponRestorePool{expected.pool->archiveId, poolLink}};
    const std::array targets{eve::weapon::WeaponCaptureTarget{ecs::handle_of(target), targetSubject}};
    gun->state()->cooldown = 7;
    REQUIRE(!eve::weapon::PreparedWeaponRestore::prepare(*frozen, destination.table, *gun, {}, pools).ok());
    REQUIRE(!eve::weapon::PreparedWeaponRestore::prepare(*frozen, destination.table, *gun, targets, {}).ok());
    const auto listeners = eve::cap::listenerCount<eve::IStateQuery>();
    auto       plan      = eve::weapon::PreparedWeaponRestore::prepare(*frozen, destination.table, *gun, targets, pools)
                    .expect("full destination candidate");
    frozen.reset();
    REQUIRE_EQ(eve::cap::listenerCount<eve::IStateQuery>(), listeners);
    REQUIRE_EQ(gun->state()->cooldown, 7.f);
    ++pool->state()->count;
    REQUIRE(!plan.commit().ok());
    REQUIRE_EQ(gun->state()->cooldown, 7.f);
    --pool->state()->count;
    {
        auto view = ecs::View<eve::weapon::WeaponEntity>(*destination.table);
        REQUIRE(!plan.commit().ok());
    }
    int events = 0;
    struct SinkReset {
        ~SinkReset() { eve::weapon::WeaponSystem::setEventSink({}); }
    } reset;
    eve::weapon::WeaponSystem::setEventSink([&](const auto&) { ++events; });
    plan.commit().expect("native weapon publication");
    REQUIRE_EQ(events, 0);
    REQUIRE(!plan.commit().ok());
    REQUIRE(gun->definition()->owned != nullptr);
    REQUIRE(gun->definitionBinding()->identity.instanceId == expected.binding.identity.instanceId);
    REQUIRE_EQ(gun->state()->resource.reloadProgress, expected.state.resource.reloadProgress);
    REQUIRE_EQ(gun->state()->resource.reserve, expected.state.resource.reserve);
    REQUIRE(gun->state()->stage == expected.state.stage);
    REQUIRE_EQ(gun->state()->stageTimer, expected.state.stageTimer);
    REQUIRE_EQ(gun->state()->burstRemaining, 2);
    REQUIRE_EQ(gun->state()->recoilPitch, .25f);
    REQUIRE_EQ(gun->aim()->yaw, 31.f);
    REQUIRE(ecs::try_get(gun->state()->lastRequest.targetHandle) == target);
    REQUIRE(gun->state()->ammoPool.resolve() == pool);
    REQUIRE(gun->state()->ownedStages != nullptr);
    REQUIRE(gun->state()->stages == gun->state()->ownedStages.get());
    REQUIRE_EQ(gun->state()->stages->windupTime, .5f);
    REQUIRE(gun->attributes()->values.revision() == expected.attributes->revision);
    REQUIRE_EQ(gun->attributes()->values.getFinal("mana").expect("restored native attributes"), 5.0);
    eve::weapon::WeaponSystem::update(*gun, .01f);
    REQUIRE(gun->state()->stageTimer > expected.state.stageTimer);
    REQUIRE_EQ(pool->state()->count, 9);
}
TEST_CASE("RTS native weapon restoration removes absent attributes and refuses replacement incarnation") {
    Fixture fixture;
    fixture.commit();
    ecs::ScopedTable scope(*fixture.table);
    auto*            gun = dynamic_cast<eve::weapon::WeaponEntity*>(
        fixture.domain->findUnit(fixture.request.units[0].subject)->weapon()->link.resolve());
    const auto epoch      = fixture.request.source.owner.child("weapon-restore-admission");
    auto       pools      = eve::weapon::WeaponPoolBindings::capture(*fixture.table, epoch).expect("graph");
    auto       captured   = eve::weapon::WeaponCapture::capture(*gun, epoch, {}, pools).expect("absent attributes");
    const auto absentText = captured.encodeState().expect("absent attribute value").toJson().expect("absent wire");
    captured = eve::weapon::WeaponCapture::prepareState(eve::Value::fromJson(absentText).expect("absent input"))
                   .expect("absent projection admission");
    REQUIRE(!captured.attributes());
    REQUIRE(!captured.attributes());
    {
        auto             blankWorld = std::make_shared<ecs::Table>();
        ecs::ScopedTable blankScope(*blankWorld);
        auto*            blank = eve::weapon::WeaponEntity::createWeapon();
        blank->identity()->id  = "already-indexed";
        REQUIRE(!eve::weapon::PreparedWeaponRestore::prepare(captured, blankWorld, *blank, {}, {}).ok());
        blank->identity()->id.clear();
        auto candidate = eve::weapon::PreparedWeaponRestore::prepare(captured, blankWorld, *blank, {}, {})
                             .expect("unclaimed placeholder");
        blank->identity()->id = "claimed-after-prepare";
        REQUIRE(!candidate.commit().ok());
        REQUIRE(blank->definition()->def == nullptr);
        blank->identity()->id.clear();
        candidate.commit().expect("claim actual placeholder");
        REQUIRE(blank->definitionBinding()->identity.instanceId == captured.facts()->get().binding.identity.instanceId);
        blank->release();
    }

    eve::weapon::WeaponAttributeAdapter::setBase(*gun, "mana", 99).expect("old target attributes");
    auto plan = eve::weapon::PreparedWeaponRestore::prepare(captured, fixture.table, *gun, {}, {})
                    .expect("exact absence candidate");
    plan.commit().expect("absence publication");
    auto* buffer = gun->getComponentManager().getComponentBuffer<eve::weapon::WeaponEntity::Attributes>();
    REQUIRE(!buffer->has(gun->id));
    std::weak_ptr<const eve::weapon::AttackStageSpec> stageOwner = gun->state()->ownedStages;
    auto runtime = eve::weapon::WeaponDefinitionRuntime::create(*fixture.registry, "club",
                                                                captured.facts()->get().binding.identity.instanceId)
                       .expect("canonical replacement runtime");
    runtime.applyTo(gun).expect("canonical stage owner retirement");
    REQUIRE(stageOwner.expired());
    REQUIRE(!gun->state()->ownedStages);
    REQUIRE(gun->state()->stages == &gun->definition()->def->stages);
    auto stale = eve::weapon::PreparedWeaponRestore::prepare(captured, fixture.table, *gun, {}, {})
                     .expect("original incarnation candidate");
    gun->release();
    auto* replacement                                     = eve::weapon::WeaponEntity::createWeapon();
    replacement->definitionBinding()->identity.instanceId = captured.facts()->get().binding.identity.instanceId;
    REQUIRE(!stale.commit().ok());
    replacement->release();
}
TEST_CASE("RTS complete weapon value rejects malformed closure without authorizing target topology") {
    Fixture fixture;
    fixture.commit();
    ecs::ScopedTable scope(*fixture.table);
    auto*            gun = dynamic_cast<eve::weapon::WeaponEntity*>(
        fixture.domain->findUnit(fixture.request.units[0].subject)->weapon()->link.resolve());
    const auto epoch    = fixture.request.source.owner.child("complete-weapon-wire");
    auto       pools    = eve::weapon::WeaponPoolBindings::capture(*fixture.table, epoch).expect("pool scope");
    auto       captured = eve::weapon::WeaponCapture::capture(*gun, epoch, {}, pools).expect("weapon facts");
    const auto valid    = captured.encodeState().expect("complete value");
    auto       reject   = [&](const char* field, eve::Value value) {
        auto candidate                                  = valid;
        (*candidate.getIf<eve::Value::Object>())[field] = std::move(value);
        REQUIRE(!eve::weapon::WeaponCapture::prepareState(candidate).ok());
    };
    reject("unknown", 0);
    reject("version", 2);
    reject("epoch", "00000000-0000-0000-0000-000000000000");
    reject("target", "bad target");
    reject("aim", eve::Value::Array{0, 0, 0, 0, std::numeric_limits<double>::infinity()});
    reject("request", eve::Value::Array{});
    reject("stages", eve::Value::Array{1, 2});
    auto definition = valid.getIf<eve::Value::Object>()->at("definition");
    REQUIRE_EQ(definition.getIf<eve::Value::Array>()->size(), std::size_t(44));
    (*definition.getIf<eve::Value::Array>())[1] = 5;
    reject("definition", definition);
    auto binding                             = valid.getIf<eve::Value::Object>()->at("binding");
    (*binding.getIf<eve::Value::Array>())[2] = "01";
    reject("binding", binding);
    binding                                  = valid.getIf<eve::Value::Object>()->at("binding");
    (*binding.getIf<eve::Value::Array>())[3] = 4;
    reject("binding", binding);
    auto state                                    = valid.getIf<eve::Value::Object>()->at("state");
    (*state.getIf<eve::Value::Object>())["stage"] = "future-stage";
    reject("state", state);
    state = valid.getIf<eve::Value::Object>()->at("state");
    state.getIf<eve::Value::Object>()->at("resource").getIf<eve::Value::Object>()->erase("reserve");
    reject("state", state);
    state = valid.getIf<eve::Value::Object>()->at("state");
    (*state.getIf<eve::Value::Object>()->at("resource").getIf<eve::Value::Object>())["unknown"] = 1;
    reject("state", state);
    auto missing = valid;
    missing.getIf<eve::Value::Object>()->erase("request");
    REQUIRE(!eve::weapon::WeaponCapture::prepareState(missing).ok());
    auto parsed = eve::weapon::WeaponCapture::prepareState(valid).expect("CPU-only admission");
    auto moved  = std::move(parsed);
    REQUIRE(!parsed.encodeState().ok());
    REQUIRE(!parsed.facts());
    REQUIRE(moved.saveEpoch() == epoch);
    auto foreign = std::make_shared<ecs::Table>();
    REQUIRE(!eve::weapon::PreparedWeaponRestore::prepare(moved, foreign, *gun, {}, {}).ok());
    REQUIRE_EQ(captured.encodeState().expect("original still intact").toJson().expect("wire"),
               valid.toJson().expect("original wire"));
}
TEST_CASE("RTS weapon pool graph preserves aliases independent of names and rejects retired graph observations") {
    auto fixture = std::make_unique<Fixture>();
    fixture->commit();
    auto                                           world = std::weak_ptr<ecs::Table>(fixture->table);
    const auto                                     epoch = fixture->request.source.owner.child("pool-graph");
    std::optional<eve::weapon::WeaponPoolBindings> retainedGraph;
    std::optional<eve::weapon::WeaponCapture>      retainedWeapon;
    eve::PersistentId                              archive;
    {
        ecs::ScopedTable scope(*fixture->table);
        auto             gun = [&](std::size_t i) {
            return dynamic_cast<eve::weapon::WeaponEntity*>(
                fixture->domain->findUnit(fixture->request.units[i].subject)->weapon()->link.resolve());
        };
        const auto aHandle = ecs::handle_of(eve::weapon::AmmoPoolEntity::createPool());
        const auto bHandle = ecs::handle_of(eve::weapon::AmmoPoolEntity::createPool());
        auto*      a       = dynamic_cast<eve::weapon::AmmoPoolEntity*>(ecs::try_get(aHandle));
        auto*      b       = dynamic_cast<eve::weapon::AmmoPoolEntity*>(ecs::try_get(bHandle));
        a->identity()->id = b->identity()->id = "same-descriptive-name";
        a->state()->count = b->state()->count = 7;
        gun(0)->state()->ammoPool             = eve::weapon::AmmoPoolLink::bind(*gun(0), *a).expect("first real link");
        gun(1)->state()->ammoPool             = eve::weapon::AmmoPoolLink::bind(*gun(1), *a).expect("second alias");
        auto aliases = eve::weapon::WeaponPoolBindings::capture(*fixture->table, epoch).expect("one actual pool");
        REQUIRE_EQ(aliases.size(), std::size_t(1));
        auto first  = eve::weapon::WeaponCapture::capture(*gun(0), epoch, {}, aliases).expect("first alias freeze");
        auto second = eve::weapon::WeaponCapture::capture(*gun(1), epoch, {}, aliases).expect("second alias freeze");
        archive     = first.facts()->get().pool->archiveId;
        REQUIRE(!archive.isNil());
        REQUIRE(second.facts()->get().pool->archiveId == archive);
        auto repeat = eve::weapon::WeaponPoolBindings::capture(*fixture->table, epoch).expect("same boundary graph");
        REQUIRE(eve::weapon::WeaponCapture::capture(*gun(0), epoch, {}, repeat)
                    .expect("repeat freeze")
                    .facts()
                    ->get()
                    .pool->archiveId == archive);
        REQUIRE(!eve::weapon::WeaponPoolBindings::capture(*fixture->table, {}).ok());
        {
            auto view = ecs::View<eve::weapon::WeaponEntity>(*fixture->table);
            REQUIRE(!eve::weapon::WeaponPoolBindings::capture(*fixture->table, epoch).ok());
        }
        gun(1)->state()->ammoPool = eve::weapon::AmmoPoolLink::bind(*gun(1), *b).expect("distinct same-name pool");
        REQUIRE(!eve::weapon::WeaponCapture::capture(*gun(1), epoch, {}, aliases).ok());
        auto distinct = eve::weapon::WeaponPoolBindings::capture(*fixture->table, epoch).expect("two actual pools");
        REQUIRE_EQ(distinct.size(), std::size_t(2));
        auto distinctA = eve::weapon::WeaponCapture::capture(*gun(0), epoch, {}, distinct).expect("actual first pool");
        auto distinctB = eve::weapon::WeaponCapture::capture(*gun(1), epoch, {}, distinct).expect("actual second pool");
        REQUIRE(distinctA.facts()->get().pool->archiveId != distinctB.facts()->get().pool->archiveId);
        auto identity                                    = gun(1)->definitionBinding()->identity.instanceId;
        gun(1)->definitionBinding()->identity.instanceId = gun(0)->definitionBinding()->identity.instanceId;
        REQUIRE(!eve::weapon::WeaponPoolBindings::capture(*fixture->table, epoch).ok());
        gun(1)->definitionBinding()->identity.instanceId = identity;
        a->identity()->id.clear();
        REQUIRE(eve::weapon::WeaponCapture::capture(*gun(0), epoch, {}, distinct).ok());
        auto wrongEpoch = epoch.child("other");
        REQUIRE(!eve::weapon::WeaponCapture::capture(*gun(0), wrongEpoch, {}, distinct).ok());
        retainedGraph.emplace(std::move(distinct));
        REQUIRE(distinct.saveEpoch().isNil());
        REQUIRE_EQ(distinct.size(), std::size_t(0));
        REQUIRE(!eve::weapon::WeaponCapture::capture(*gun(0), epoch, {}, distinct).ok());
        retainedWeapon.emplace(std::move(first));
        a->release();
        const auto replacementHandle = ecs::handle_of(eve::weapon::AmmoPoolEntity::createPool());
        auto*      replacement       = dynamic_cast<eve::weapon::AmmoPoolEntity*>(ecs::try_get(replacementHandle));
        replacement->identity()->id  = "same-descriptive-name";
        replacement->state()->count  = 7;
        gun(0)->state()->ammoPool =
            eve::weapon::AmmoPoolLink::bind(*gun(0), *replacement).expect("replacement physical link");
        REQUIRE(!eve::weapon::WeaponCapture::capture(*gun(0), epoch, {}, *retainedGraph).ok());
        REQUIRE_EQ(retainedWeapon->facts()->get().pool->state.count, 7);
    }
    fixture.reset();
    REQUIRE(world.expired());
    REQUIRE(retainedWeapon->facts()->get().pool->archiveId == archive);
    REQUIRE_EQ(retainedGraph->size(), std::size_t(2));
}
TEST_CASE("RTS weapon native attribute capture restores exact revision and modifier counters after source retirement") {
    std::optional<eve::weapon::WeaponCapture> frozen;
    std::weak_ptr<ecs::Table>                 retired;
    eve::Revision                             revision;
    eve::attributes::ModifierSequence         sequence = 0;
    {
        Fixture fixture;
        fixture.commit();
        retired = fixture.table;
        ecs::ScopedTable scope(*fixture.table);
        auto*            unit = fixture.domain->findUnit(fixture.request.units[0].subject);
        auto*            gun  = dynamic_cast<eve::weapon::WeaponEntity*>(unit->weapon()->link.resolve());
        eve::weapon::WeaponAttributeAdapter::setBase(*gun, "mana", 3).expect("canonical base");
        for (int i = 0; i < 2; ++i)
            eve::weapon::WeaponAttributeAdapter::addModifier(*gun, "replaced", "mana", "source",
                                                             eve::attributes::AttributeOperation::Add, 2)
                .expect("canonical replacement leaves sequence gap");
        revision             = gun->attributes()->values.revision();
        sequence             = gun->attributes()->values.modifierAt(0)->sequence;
        const auto listeners = eve::cap::listenerCount<eve::IStateQuery>();
        const auto epoch     = fixture.request.source.owner.child("attributes");
        auto       pools     = eve::weapon::WeaponPoolBindings::capture(*fixture.table, epoch).expect("pool graph");
        frozen.emplace(eve::weapon::WeaponCapture::capture(*gun, epoch, {}, pools)
                           .expect("weapon canonical native attribute freeze"));
        REQUIRE_EQ(eve::cap::listenerCount<eve::IStateQuery>(), listeners);
        REQUIRE(frozen->attributes().has_value());
        REQUIRE(frozen->attributes()->get().revision() == revision);
    }
    REQUIRE(retired.expired());
    const auto text = frozen->attributes()->get().encodeState().takeValue().toJson().takeValue();
    auto       parsed =
        eve::attributes::AttributeProjectionCapture::prepareState(eve::Value::fromJson(text).takeValue()).takeValue();
    auto plan =
        eve::attributes::PreparedAttributeProjection::prepare(parsed).expect("independent persistent candidate");
    frozen.reset();
    ecs::Table       world;
    ecs::ScopedTable scope(world);
    const auto       targetHandle = ecs::handle_of(eve::weapon::WeaponEntity::createWeapon());
    const auto       otherHandle  = ecs::handle_of(eve::weapon::WeaponEntity::createWeapon());
    auto*            target       = dynamic_cast<eve::weapon::WeaponEntity*>(ecs::try_get(targetHandle));
    auto*            other        = dynamic_cast<eve::weapon::WeaponEntity*>(ecs::try_get(otherHandle));
    auto&            projection   = target->attributes()->values;
    projection.bindOwner(ecs::handle_of(target)).expect("actual target");
    const auto listeners = eve::cap::listenerCount<eve::IStateQuery>();
    REQUIRE(!projection.restore(std::move(plan), ecs::handle_of(other)).ok());
    {
        auto view = ecs::View<eve::weapon::WeaponEntity>(world);
        REQUIRE(!projection.restore(std::move(plan), ecs::handle_of(target)).ok());
    }
    projection.restore(std::move(plan), ecs::handle_of(target)).expect("canonical exact restoration");
    REQUIRE_EQ(eve::cap::listenerCount<eve::IStateQuery>(), listeners);
    REQUIRE(projection.revision() == revision);
    REQUIRE(projection.initialized());
    REQUIRE_EQ(projection.modifierAt(0)->sequence, sequence);
    REQUIRE_EQ(projection.getFinal("mana").expect("exact restored final"), 5.0);
    REQUIRE(!projection.restore(std::move(plan), ecs::handle_of(target)).ok());
    projection.addModifier({"next", "mana", "source", eve::attributes::AttributeOperation::Add, 1})
        .expect("continue canonical counter");
    REQUIRE_EQ(projection.modifierAt(1)->sequence, sequence + 1);
    target->release();
    ecs::try_get(otherHandle)->release();
}
TEST_CASE(
    "RTS native attribute restore preserves uninitialized state and rejects foreign subject without publication") {
    eve::attributes::AttributeProjection source;
    auto                                 captured = source.capture().expect("uninitialized facts");
    const auto                           text     = captured.encodeState().takeValue().toJson().takeValue();
    captured =
        eve::attributes::AttributeProjectionCapture::prepareState(eve::Value::fromJson(text).takeValue()).takeValue();
    REQUIRE(captured.initialized() == false);
    auto plan  = eve::attributes::PreparedAttributeProjection::prepare(captured).expect("uninitialized candidate");
    auto moved = std::move(captured);
    REQUIRE(!captured.values());
    REQUIRE(!captured.initialized());
    REQUIRE(!eve::attributes::PreparedAttributeProjection::prepare(captured).ok());
    ecs::Table                           world;
    ecs::ScopedTable                     scope(world);
    auto*                                target = eve::weapon::WeaponEntity::createWeapon();
    eve::attributes::AttributeProjection foreign("other-subject");
    REQUIRE(!foreign.restore(std::move(plan), ecs::handle_of(target)).ok());
    auto& projection = target->attributes()->values;
    projection.setBase("mana", 99).expect("old target values");
    projection.restore(std::move(plan), ecs::handle_of(target)).expect("uninitialized exact state");
    REQUIRE(!projection.initialized());
    REQUIRE(projection.revision().isZero());
    REQUIRE(!projection.has("mana"));
    REQUIRE(moved.values().has_value());
    target->release();
}
TEST_CASE("RTS frame archive preserves full historical audits transport identities and retention without replay") {
    using namespace eve;
    using namespace eve::rts;
    const auto epoch    = *PersistentId::parse("01900000-0000-7000-8000-000000000d40");
    const auto targetId = SubjectRef::fromPersistentId(epoch.child("retired-target"));
    const auto eventId  = *game_event::EventId::parse(epoch.child("historical-event").format());
    auto       archived = [&] {
        combat::DamageRuntime runtime;
        combat::CombatState   body{targetId, 100, 100, 50, 50};
        combat::DamageRequest request;
        request.target          = targetId;
        request.damageType      = "Damage.Physical.Slash";
        request.healthDamage    = 10;
        request.poiseDamage     = 7;
        request.knockback       = {1, -2, 3};
        request.tick            = SimulationTick(12);
        request.actionExecution = action::ActionExecutionId(std::numeric_limits<std::uint64_t>::max());
        auto outcome            = runtime.apply(body, request).takeValue();
        REQUIRE(outcome.settlementResult.event);
        outcome.settlementResult.event->eventId  = eventId;
        outcome.settlementResult.event->sequence = EventSequence(std::numeric_limits<std::uint64_t>::max());
        RTSFrameState frame;
        frame.damage.push_back({0, request.tick, DamageChannel::Weapon, request, outcome});
        frame.damage.push_back({3, request.tick, DamageChannel::Ability, request, outcome});
        frame.combat.push_back({1, request.tick, {CombatFireEventKind::WeaponFired, {}, targetId, {1.25f, -2.5f}}});
        frame.lifecycle.push_back(
            {2, request.tick, {LifecycleEventKind::StatusExpired, {}, targetId, "history\nstatus", 2.5}});
        frame.sequence             = 4;
        frame.preserveOnNextStep   = true;
        frame.nextGameplaySequence = std::numeric_limits<std::uint64_t>::max();
        frame.gameplay.push_back({frame.nextGameplaySequence - 1, request.tick, "history", targetId, "command",
                                  "correlation", Value::object({{"actual", "history\nvalue"}})});
        return RTSFrameArchive::capture(std::move(frame), epoch, Revision(4), request.tick).takeValue();
    }();
    auto                 holder        = std::make_unique<RTSFrameArchive>(std::move(archived));
    SnapshotHashProvider retireCapture = [&](std::string_view text) {
        holder.reset();
        return restoreHash(text);
    };
    auto envelope = holder->seal(retireCapture).takeValue();
    REQUIRE(!holder);
    // Frozen v1 wire ordinals; changing domain enum order must not silently reinterpret saved queues.
    REQUIRE_EQ(envelope.payload.find("damage")->at(0).at(2).asInt(), std::int64_t(2));
    REQUIRE_EQ(envelope.payload.find("lifecycle")->at(0).at(2).asInt(), std::int64_t(14));
    const auto wire = serializeSnapshotEnvelope(envelope).takeValue();
    auto       parsed =
        RTSFrameArchive::parse(parseSnapshotEnvelope(wire, restoreHash).takeValue(), epoch, restoreHash).takeValue();
    REQUIRE_EQ(serializeSnapshotEnvelope(parsed.seal(restoreHash).takeValue()).takeValue(), wire);
    REQUIRE_EQ(parsed.state()->get().damage[0].outcome.settlementResult.event->eventId, eventId);
    REQUIRE_EQ(parsed.state()->get().damage[0].outcome.settlementResult.event->sequence.value(),
               std::numeric_limits<std::uint64_t>::max());
    REQUIRE_EQ(parsed.state()->get().damage[0].request.actionExecution->value(),
               std::numeric_limits<std::uint64_t>::max());
    REQUIRE_EQ(parsed.state()->get().nextGameplaySequence, std::numeric_limits<std::uint64_t>::max());
    REQUIRE_EQ(parsed.state()->get().damage[0].outcome.health, 90.0);
    auto             table = std::make_shared<ecs::Table>();
    ecs::ScopedTable scope(*table);
    RTS              domain(table);
    domain.restoreFrameState(PreparedRTSFrameRestore::prepare(parsed.state()->get()).takeValue())
        .expect("restore historical queues without subjects");
    auto restored =
        RTSFrameArchive::capture(domain.captureFrameState().takeValue(), epoch, Revision(4), SimulationTick(12))
            .takeValue();
    REQUIRE_EQ(serializeSnapshotEnvelope(restored.seal(restoreHash).takeValue()).takeValue(), wire);
    action::ActionRuntime actions;
    ActionAdapter         adapter(actions);
    domain.step({SimulationTick(1), Duration(1)}, adapter).expect("preserved next step");
    REQUIRE_EQ(domain.frameDamageEvents().size(), std::size_t(2));
    domain.step({SimulationTick(2), Duration(1)}, adapter).expect("normal next-step clearing");
    REQUIRE(domain.frameDamageEvents().empty());
    auto reseal = [&](SnapshotEnvelope value) {
        return makeSnapshotEnvelope(value.type, value.schema, value.schemaVersion, value.instanceId, value.revision,
                                    value.tick, std::move(value.payload), restoreHash)
            .takeValue();
    };
    auto badState                = parsed.state()->get();
    badState.gameplay[0].payload = Value(std::numeric_limits<double>::quiet_NaN());
    REQUIRE(!RTSFrameArchive::capture(badState, epoch, Revision(4), SimulationTick(12)).ok());
    badState                          = parsed.state()->get();
    badState.damage[0].outcome.target = SubjectRef::fromPersistentId(epoch.child("foreign"));
    REQUIRE(!RTSFrameArchive::capture(badState, epoch, Revision(4), SimulationTick(12)).ok());
    auto bad = envelope;
    bad.payload.set("extra", true);
    REQUIRE(!RTSFrameArchive::parse(bad, epoch, restoreHash).ok());
    REQUIRE(!RTSFrameArchive::parse(reseal(bad), epoch, restoreHash).ok());
    bad               = envelope;
    bad.schemaVersion = SchemaVersion(2);
    REQUIRE(!RTSFrameArchive::parse(reseal(bad), epoch, restoreHash).ok());
    bad = envelope;
    bad.payload.set("sequence", "3");
    REQUIRE(!RTSFrameArchive::parse(reseal(bad), epoch, restoreHash).ok());
    bad = envelope;
    bad.payload.find("damage")->at(0).getIf<Value::Array>()->push_back(true);
    REQUIRE(!RTSFrameArchive::parse(reseal(bad), epoch, restoreHash).ok());
    REQUIRE(!RTSFrameArchive::parse(envelope, epoch.child("foreign"), restoreHash).ok());
    auto moved = std::move(parsed);
    REQUIRE(!parsed.state());
    REQUIRE(!parsed.seal(restoreHash).ok());
    REQUIRE(moved.state());
}
TEST_CASE("RTS frame restore owns historical queues and preserves exact next-step retention") {
    auto                    world = std::make_shared<ecs::Table>();
    ecs::ScopedTable        scope(*world);
    eve::rts::RTS           domain(world);
    eve::rts::RTSFrameState saved;
    saved.damage.resize(2);
    saved.damage[0].sequence           = 0;
    saved.damage[1].sequence           = 3;
    saved.damage[0].request.damageType = "historical-damage";
    saved.combat.resize(1);
    saved.combat[0].sequence = 1;
    saved.lifecycle.resize(1);
    saved.lifecycle[0].sequence     = 2;
    saved.lifecycle[0].event.detail = "historical-life";
    saved.sequence                  = 4;
    saved.preserveOnNextStep        = true;
    saved.gameplay.resize(1);
    saved.gameplay[0].sequence = 1;
    saved.gameplay[0].payload  = eve::Value("historical-payload");
    saved.nextGameplaySequence = 2;
    auto plan                  = eve::rts::PreparedRTSFrameRestore::prepare(saved).expect("owning queues");
    saved                      = {};
    auto moved                 = std::move(plan);
    REQUIRE(!domain.restoreFrameState(std::move(plan)).ok());
    {
        eve::rts::RTS borrowed;
        REQUIRE(!borrowed.restoreFrameState(std::move(moved)).ok());
        auto view = ecs::View<eve::rts::Unit>(*world);
        REQUIRE(!domain.restoreFrameState(std::move(moved)).ok());
        REQUIRE(domain.frameDamageEvents().empty());
    }
    domain.restoreFrameState(std::move(moved)).expect("joint queue publication");
    auto observed = domain.captureFrameState().expect("exact restored queues");
    REQUIRE_EQ(observed.damage.size(), std::size_t(2));
    REQUIRE_EQ(observed.damage[0].request.damageType, "historical-damage");
    REQUIRE_EQ(observed.combat[0].sequence, std::uint64_t(1));
    REQUIRE_EQ(observed.lifecycle[0].event.detail, "historical-life");
    REQUIRE_EQ(observed.sequence, std::uint64_t(4));
    REQUIRE_EQ(observed.gameplay[0].payload.toJson().expect("payload"), "\"historical-payload\"");
    REQUIRE_EQ(observed.nextGameplaySequence, std::uint64_t(2));
    REQUIRE(observed.preserveOnNextStep);
    REQUIRE(!domain.restoreFrameState(std::move(moved)).ok());
    eve::action::ActionRuntime actions;
    eve::rts::ActionAdapter    adapter(actions);
    domain.step({eve::SimulationTick(1), eve::Duration(1)}, adapter).expect("retained first step");
    observed = domain.captureFrameState().expect("retained queues");
    REQUIRE_EQ(observed.damage.size(), std::size_t(2));
    REQUIRE_EQ(observed.sequence, std::uint64_t(4));
    REQUIRE(!observed.preserveOnNextStep);
    domain.step({eve::SimulationTick(2), eve::Duration(1)}, adapter).expect("next normal step");
    observed = domain.captureFrameState().expect("normal reset");
    REQUIRE((observed.damage.empty() && observed.combat.empty() && observed.lifecycle.empty()));
    REQUIRE_EQ(observed.sequence, std::uint64_t(0));
    REQUIRE_EQ(observed.gameplay.size(), std::size_t(1));
    auto bad                 = observed;
    bad.gameplay[0].sequence = 0;
    REQUIRE(!eve::rts::PreparedRTSFrameRestore::prepare(bad).ok());
    bad                      = observed;
    bad.nextGameplaySequence = 1;
    REQUIRE(!eve::rts::PreparedRTSFrameRestore::prepare(bad).ok());
    bad                      = observed;
    bad.nextGameplaySequence = 0;
    REQUIRE(!eve::rts::PreparedRTSFrameRestore::prepare(bad).ok());
    bad = observed;
    bad.damage.resize(1);
    REQUIRE(!eve::rts::PreparedRTSFrameRestore::prepare(bad).ok());
    bad.sequence = 2;
    bad.combat.resize(1);
    REQUIRE(!eve::rts::PreparedRTSFrameRestore::prepare(bad).ok());
    bad.combat.clear();
    bad.damage.resize(2);
    bad.damage[0].sequence = 1;
    bad.damage[1].sequence = 0;
    REQUIRE(!eve::rts::PreparedRTSFrameRestore::prepare(bad).ok());
}
TEST_CASE("RTS combat archive owns canonical sets rejects malformed state and pins facts across digest callbacks") {
    using namespace eve;
    using namespace eve::rts;
    const auto           epoch         = *PersistentId::parse("01900000-0000-7000-8000-000000000d50");
    const auto           id            = epoch.child("retired-body").format();
    auto                 holder        = std::make_unique<RTSCombatArchive>([&] {
        RTSCombatSnapshot state;
        state.mirroredSubjects.insert(id);
        state.blockedSubjects.insert(id);
        return RTSCombatArchive::capture(std::move(state), epoch, Revision(4), SimulationTick(7)).takeValue();
    }());
    SnapshotHashProvider retireCapture = [&](std::string_view text) {
        holder.reset();
        return restoreHash(text);
    };
    auto envelope = holder->seal(retireCapture).takeValue();
    REQUIRE(!holder);
    const auto wire = serializeSnapshotEnvelope(envelope).takeValue();
    auto       restored =
        RTSCombatArchive::parse(parseSnapshotEnvelope(wire, restoreHash).takeValue(), epoch, restoreHash).takeValue();
    REQUIRE_EQ(serializeSnapshotEnvelope(restored.seal(restoreHash).takeValue()).takeValue(), wire);
    REQUIRE(restored.state()->get().mirroredSubjects.contains(id));
    REQUIRE(restored.state()->get().blockedSubjects.contains(id));
    auto reseal = [&](SnapshotEnvelope value) {
        return makeSnapshotEnvelope(value.type, value.schema, value.schemaVersion, value.instanceId, value.revision,
                                    value.tick, std::move(value.payload), restoreHash)
            .takeValue();
    };
    auto bad = envelope;
    bad.payload.set("extra", true);
    REQUIRE(!RTSCombatArchive::parse(bad, epoch, restoreHash).ok());
    REQUIRE(!RTSCombatArchive::parse(reseal(bad), epoch, restoreHash).ok());
    bad               = envelope;
    bad.schemaVersion = SchemaVersion(2);
    REQUIRE(!RTSCombatArchive::parse(reseal(bad), epoch, restoreHash).ok());
    bad = envelope;
    bad.payload.find("mirrored")->getIf<Value::Array>()->push_back(id);
    REQUIRE(!RTSCombatArchive::parse(reseal(bad), epoch, restoreHash).ok());
    bad = envelope;
    bad.payload.set("blocked", Value::Array{epoch.child("foreign").format()});
    REQUIRE(!RTSCombatArchive::parse(reseal(bad), epoch, restoreHash).ok());
    bad = envelope;
    bad.payload.set("mirrored", Value::Array{"not-a-subject"});
    bad.payload.set("blocked", Value::Array{});
    REQUIRE(!RTSCombatArchive::parse(reseal(bad), epoch, restoreHash).ok());
    REQUIRE(!RTSCombatArchive::parse(envelope, epoch.child("foreign"), restoreHash).ok());
    RTSCombatSnapshot invalid;
    invalid.blockedSubjects.insert(id);
    REQUIRE(!RTSCombatArchive::capture(invalid, epoch, Revision(4), SimulationTick(7)).ok());
    invalid.blockedSubjects.clear();
    invalid.mirroredSubjects.insert(PersistentId{}.format());
    REQUIRE(!RTSCombatArchive::capture(invalid, epoch, Revision(4), SimulationTick(7)).ok());
    auto moved = std::move(restored);
    REQUIRE(!restored.state());
    REQUIRE(!restored.seal(restoreHash).ok());
    REQUIRE(moved.state());
}
TEST_CASE("RTS combat restore refuses a replacement body with the same persistent identity") {
    Fixture fixture;
    fixture.commit();
    ecs::ScopedTable           scope(*fixture.table);
    eve::sensing::SensingWorld sensed;
    eve::combat::DamageRuntime damage;
    const auto                 subject = fixture.request.units[0].subject;
    sensed.upsert(subject.format(), 1, 2, "", "", "").expect("actual body mirror");
    auto                        frozen = sensed.capture().expect("saved mirror");
    eve::rts::RTSCombatSnapshot flags;
    flags.mirroredSubjects.insert(subject.format());
    fixture.domain->setCombatProviders(&sensed, &damage);
    auto plan =
        eve::rts::PreparedRTSCombatRestore::prepare(*fixture.domain, flags, frozen).expect("original body binding");
    auto*      body           = fixture.domain->findUnit(subject);
    auto       identity       = body->identity();
    const auto componentCount = body->getComponentManager().components.size();
    identity->subject         = {};
    REQUIRE(!fixture.domain->restoreCombat(std::move(plan)).ok());
    REQUIRE(!eve::rts::PreparedRTSCombatRestore::prepare(*fixture.domain, flags, frozen).ok());
    REQUIRE_EQ(body->getComponentManager().components.size(), componentCount);
    identity->subject = subject;
    const std::array subjects{subject};
    fixture.domain->prepareUnitRemoval(subjects).expect("native removal prepare").commit().expect("native removal");
    fixture.domain->newUnit(subject).expect("same stable id replacement");
    const auto before = sensed.snapshotJson();
    REQUIRE(!fixture.domain->restoreCombat(std::move(plan)).ok());
    REQUIRE_EQ(sensed.snapshotJson(), before);
    REQUIRE(fixture.domain->captureCombatState().expect("unchanged flags").mirroredSubjects.empty());
    fixture.domain->setCombatProviders(nullptr, nullptr);
}
TEST_CASE("RTS combat restore rejects source mismatch and physical target or provider reuse") {
    auto                                      world = std::make_shared<ecs::Table>();
    ecs::ScopedTable                          scope(*world);
    std::optional<eve::sensing::SensingWorld> provider;
    provider.emplace();
    eve::combat::DamageRuntime   damage;
    std::optional<eve::rts::RTS> domain;
    domain.emplace(world);
    auto                        captured = provider->capture().expect("empty facts");
    eve::rts::RTSCombatSnapshot combat;
    REQUIRE(!eve::rts::PreparedRTSCombatRestore::prepare(*domain, combat, captured).ok());
    domain->setCombatProviders(&*provider, &damage);
    {
        eve::rts::RTS borrowed;
        borrowed.setCombatProviders(&*provider, &damage);
        REQUIRE(!eve::rts::PreparedRTSCombatRestore::prepare(borrowed, combat, captured).ok());
        borrowed.setCombatProviders(nullptr, nullptr);
    }
    auto plan = eve::rts::PreparedRTSCombatRestore::prepare(*domain, combat, captured).expect("original provider");
    provider.reset();
    REQUIRE(!domain->restoreCombat(std::move(plan)).ok());
    provider.emplace();
    domain->setCombatProviders(&*provider, &damage);
    REQUIRE(!domain->restoreCombat(std::move(plan)).ok());
    auto fresh = eve::rts::PreparedRTSCombatRestore::prepare(*domain, combat, captured).expect("fresh provider");
    domain.reset();
    domain.emplace(world);
    domain->setCombatProviders(&*provider, &damage);
    REQUIRE(!domain->restoreCombat(std::move(fresh)).ok());
    auto bad = combat;
    bad.blockedSubjects.insert("missing");
    REQUIRE(!eve::rts::PreparedRTSCombatRestore::prepare(*domain, bad, captured).ok());
    bad.mirroredSubjects.insert("missing");
    REQUIRE(!eve::rts::PreparedRTSCombatRestore::prepare(*domain, bad, captured).ok());
    auto finalPlan = eve::rts::PreparedRTSCombatRestore::prepare(*domain, combat, captured).expect("fresh target");
    domain->restoreCombat(std::move(finalPlan)).expect("empty scope joint publish");
    domain->setCombatProviders(nullptr, nullptr);
}
TEST_CASE("RTS rejects retired sensing before advancing and explicitly rebinds same-address replacement") {
    auto                                      world = std::make_shared<ecs::Table>();
    ecs::ScopedTable                          scope(*world);
    std::optional<eve::sensing::SensingWorld> sensed;
    sensed.emplace();
    eve::combat::DamageRuntime damage;
    eve::rts::RTS              domain(world);
    eve::action::ActionRuntime actions;
    eve::rts::ActionAdapter    adapter(actions);
    domain.setCombatProviders(&*sensed, &damage);
    auto old = sensed->lifetimeWitness();
    sensed.reset();
    REQUIRE(!old.isAlive());
    REQUIRE(!domain.step({eve::SimulationTick(1), eve::Duration(10000000)}, adapter).ok());
    sensed.emplace();
    REQUIRE(sensed->upsert("replacement-only", 0, 0, "", "", "").ok());
    REQUIRE(!domain.step({eve::SimulationTick(1), eve::Duration(10000000)}, adapter).ok());
    domain.setCombatProviders(&*sensed, &damage);
    REQUIRE(sensed->subjects().contains("replacement-only"));
    domain.step({eve::SimulationTick(1), eve::Duration(10000000)}, adapter).expect("same tick remains admissible");
    sensed.reset();
    domain.setCombatProviders(nullptr, nullptr);
    REQUIRE(domain.captureCombatState().expect("safe detach after retirement").mirroredSubjects.empty());
}
TEST_CASE("RTS combat freeze preserves real blocked-notification latches and independent sensing facts") {
    auto                                 world = std::make_shared<ecs::Table>();
    ecs::ScopedTable                     scope(*world);
    eve::definitions::DefinitionRegistry registry;
    eve::sensing::SensingWorld           sensed;
    eve::combat::DamageRuntime           damage;
    eve::rts::RTS                        domain(world);
    domain
        .loadContent(
            registry,
            R"({"weapons":[{"id":"blocked","damageType":"normal","damage":10,"range":5,"cooldown":0.2,"projectileSpeed":0,"magazineSize":1000,"blockedByObstacles":true}],"units":[{"id":"fighter","weaponType":"blocked","health":40,"speed":1,"sight":10,"range":5}]})")
        .expect("canonical content");
    const auto epoch = *eve::PersistentId::parse("01900000-0000-7000-8000-000000000a35");
    auto       a     = domain.newFaction(eve::SubjectRef::fromPersistentId(epoch.child("a"))).expect("first faction");
    auto       b     = domain.newFaction(eve::SubjectRef::fromPersistentId(epoch.child("b"))).expect("second faction");
    const auto definition = *eve::LogicalId::fromParts("unit", "fighter");
    auto       source = domain.newFactionUnit(*a, eve::SubjectRef::fromPersistentId(epoch.child("source")), definition)
                      .expect("actual armed source");
    auto target = domain.newFactionUnit(*b, eve::SubjectRef::fromPersistentId(epoch.child("target")), definition)
                      .expect("actual target");
    target->motion()->x = 1;
    eve::rts::CommandSpec order;
    order.kind         = eve::rts::OrderKind::Attack;
    order.target       = {1, 0};
    order.targetEntity = ecs::handle_of(target);
    source->orders()->values.replace(order).expect("canonical attack queue");
    sensed.rebuildIndex(eve::sensing::SpatialIndexKind::XYZ, 2.5f).expect("actual index");
    domain.setCombatProviders(&sensed, &damage);
    int queries = 0;
    domain.setFireLineQuery([&](auto, auto, auto, auto, const auto&) {
        ++queries;
        return eve::Result<bool>::success(false);
    });
    eve::action::ActionRuntime actions;
    eve::rts::ActionAdapter    adapter(actions);
    domain.step({eve::SimulationTick(1), eve::Duration(10000000)}, adapter).expect("actual blocked weapon phase");
    REQUIRE(queries > 0);
    auto combat     = domain.captureCombatState().expect("actual latches");
    auto savedFrame = domain.captureFrameState().expect("actual committed blocked frame");
    auto frozen     = sensed.capture().expect("actual mirror facts");
    auto archive =
        eve::rts::RTSCombatArchive::capture(combat, epoch, eve::Revision(1), eve::SimulationTick(1)).takeValue();
    const auto combatText   = eve::serializeSnapshotEnvelope(archive.seal(restoreHash).takeValue()).takeValue();
    auto       parsedCombat = eve::rts::RTSCombatArchive::parse(
                            eve::parseSnapshotEnvelope(combatText, restoreHash).takeValue(), epoch, restoreHash)
                            .takeValue();
    combat                     = parsedCombat.state()->get();
    const auto sensingEnvelope = frozen.seal(epoch, eve::Revision(1), eve::SimulationTick(1), restoreHash).takeValue();
    const auto sensingText     = eve::serializeSnapshotEnvelope(sensingEnvelope).takeValue();
    frozen = eve::sensing::SensingCapture::parse(eve::parseSnapshotEnvelope(sensingText, restoreHash).takeValue(),
                                                 epoch, restoreHash)
                 .takeValue();
    REQUIRE(combat.blockedSubjects.contains(source->identity()->subject.format()));
    REQUIRE(combat.mirroredSubjects.contains(source->identity()->subject.format()));
    REQUIRE_EQ(frozen.subjects().size(), std::size_t(2));
    domain.step({eve::SimulationTick(2), eve::Duration(10000000)}, adapter).expect("same blockage next frame");
    for (const auto& event : domain.captureFrameState().expect("actual next-frame events").combat)
        REQUIRE(event.event.kind != eve::rts::CombatFireEventKind::FireBlocked);
    {
        auto view = ecs::View<eve::rts::Unit>(*world);
        REQUIRE(!domain.captureCombatState().ok());
    }
    domain.setCombatProviders(nullptr, nullptr);
    REQUIRE(domain.captureCombatState().expect("actual detach").mirroredSubjects.empty());
    REQUIRE(sensed.subjects().empty());
    REQUIRE(!combat.blockedSubjects.empty());
    domain.setCombatProviders(&sensed, &damage);
    REQUIRE(sensed.upsert("old-unrelated", 0, 0, "", "", "").ok());
    const auto oldFacts = sensed.snapshotJson();
    const auto witness  = sensed.lifetimeWitness();
    auto       plan     = eve::rts::PreparedRTSCombatRestore::prepare(domain, combat, frozen).expect("joint candidate");
    REQUIRE_EQ(sensed.snapshotJson(), oldFacts);
    {
        eve::rts::RTS foreign(world);
        REQUIRE(!foreign.restoreCombat(std::move(plan)).ok());
    }
    {
        auto view = ecs::View<eve::rts::Unit>(*world);
        REQUIRE(!domain.restoreCombat(std::move(plan)).ok());
        REQUIRE(!eve::rts::PreparedRTSCombatRestore::prepare(domain, combat, frozen).ok());
    }
    eve::sensing::SensingWorld alternate;
    domain.setCombatProviders(&alternate, &damage);
    REQUIRE(!domain.restoreCombat(std::move(plan)).ok());
    REQUIRE(alternate.subjects().empty());
    domain.setCombatProviders(&sensed, &damage);
    domain.restoreCombat(std::move(plan)).expect("joint native publish");
    REQUIRE(witness.isAlive());
    REQUIRE_EQ(sensed.subjects().size(), std::size_t(2));
    REQUIRE(!sensed.subjects().contains("old-unrelated"));
    REQUIRE(domain.captureCombatState().expect("restored blocked state").blockedSubjects == combat.blockedSubjects);
    REQUIRE(!domain.restoreCombat(std::move(plan)).ok());
    const auto queriesBeforeRestoreStep = queries;
    domain.step({eve::SimulationTick(3), eve::Duration(10000000)}, adapter).expect("restored latch continues");
    REQUIRE(queries > queriesBeforeRestoreStep);
    for (const auto& event : domain.captureFrameState().expect("restored next frame").combat)
        REQUIRE(event.event.kind != eve::rts::CombatFireEventKind::FireBlocked);
    domain.setCombatProviders(nullptr, nullptr);

    auto frameCandidate = eve::rts::PreparedRTSFrameRestore::prepare(savedFrame).expect("actual frame candidate");
    domain.restoreFrameState(std::move(frameCandidate)).expect("actual historical frame restore");
    auto returnedFrame = domain.captureFrameState().expect("actual restored frame");
    REQUIRE_EQ(returnedFrame.sequence, savedFrame.sequence);
    REQUIRE_EQ(returnedFrame.combat.size(), savedFrame.combat.size());
    REQUIRE(returnedFrame.combat[0].event.kind == eve::rts::CombatFireEventKind::FireBlocked);
    eve::sensing::SensingWorld restored;
    restored.restore(eve::sensing::PreparedSensingRestore::prepare(frozen).expect("mirror candidate"))
        .expect("independent mirror restore");
    REQUIRE_EQ(restored.subjects().size(), std::size_t(2));
    REQUIRE(restored.indexKind() == eve::sensing::SpatialIndexKind::XYZ);
}
TEST_CASE("RTS weapon freeze reads live reload request aim attributes and pool independently of source lifetime") {
    auto fixture = std::make_unique<Fixture>();
    fixture->commit();
    const auto                                        epoch = fixture->request.source.owner.child("weapon-save");
    std::shared_ptr<const eve::weapon::WeaponCapture> retained;
    std::weak_ptr<ecs::Table>                         world = fixture->table;
    {
        ecs::ScopedTable scope(*fixture->table);
        auto*            unit          = fixture->domain->findUnit(fixture->request.units[0].subject);
        auto*            target        = fixture->domain->findUnit(fixture->request.units[1].subject);
        auto*            gun           = dynamic_cast<eve::weapon::WeaponEntity*>(unit->weapon()->link.resolve());
        auto*            pool          = eve::weapon::AmmoPoolEntity::createPool();
        pool->identity()->id           = "actual-shared";
        pool->identity()->ammoType     = "rifle";
        pool->state()->count           = 12;
        pool->state()->max             = 20;
        gun->state()->ammoPool         = eve::weapon::AmmoPoolLink::bind(*gun, *pool).expect("actual shared link");
        gun->state()->resource.value   = 0;
        gun->state()->resource.reserve = 4;
        eve::weapon::WeaponSystem::startReload(*gun);
        REQUIRE(gun->state()->resource.reloading);
        eve::weapon::WeaponSystem::update(*gun, .05f);
        gun->state()->lastRequest.targetHandle = ecs::handle_of(target);
        gun->state()->lastRequest.hasTarget    = true;
        gun->state()->lastRequest.targetX      = 13;
        gun->aim()->yaw                        = 31;
        eve::weapon::WeaponAttributeAdapter::setBase(*gun, "mana", 3).expect("canonical selected base");
        eve::weapon::WeaponAttributeAdapter::addModifier(*gun, "saved-mana", "mana", "runtime",
                                                         eve::attributes::AttributeOperation::Add, 2)
            .expect("canonical selected modifier");
        const auto revision = gun->attributes()->values.revision();
        auto       capture  = fixture->factory->capture(epoch, {}).expect("actual native weapon participant");
        for (const auto& member : capture.units()) {
            REQUIRE(member.weapon != nullptr);
            if (member.subject == fixture->request.units[0].subject) retained = member.weapon;
        }
        REQUIRE(retained != nullptr);
        const auto& facts = retained->facts()->get();
        REQUIRE(retained->saveEpoch() == epoch);
        REQUIRE(facts.state.resource.reloading);
        REQUIRE_EQ(facts.state.resource.reloadProgress, gun->state()->resource.reloadProgress);
        REQUIRE_EQ(facts.state.resource.reserve, 4);
        REQUIRE(facts.target == target->identity()->subject);
        REQUIRE(facts.request.targetHandle.table == nullptr);
        REQUIRE_EQ(facts.request.targetX, 13.f);
        REQUIRE_EQ(facts.aim.yaw, 31.f);
        REQUIRE(facts.stages.has_value());
        REQUIRE(facts.attributes.has_value());
        REQUIRE(facts.attributes->revision == revision);
        REQUIRE_EQ(facts.attributes->modifiers.size(), std::size_t(1));
        REQUIRE_EQ(facts.pool->state.count, 12);
        REQUIRE_EQ(facts.pool->identity.id, std::string("actual-shared"));
        REQUIRE(gun->attributes()->values.revision() == revision);
        REQUIRE(ecs::try_get(gun->state()->lastRequest.targetHandle) == target);
        pool->state()->count           = 1;
        gun->state()->resource.reserve = 99;
        REQUIRE_EQ(facts.pool->state.count, 12);
        REQUIRE_EQ(facts.state.resource.reserve, 4);
        auto* otherGun              = dynamic_cast<eve::weapon::WeaponEntity*>(target->weapon()->link.resolve());
        auto* duplicate             = eve::weapon::AmmoPoolEntity::createPool();
        duplicate->identity()->id   = "actual-shared";
        otherGun->state()->ammoPool = eve::weapon::AmmoPoolLink::bind(*otherGun, *duplicate).expect("distinct pool");
        auto distinct = fixture->factory->capture(epoch, {}).expect("distinct same-name pools remain distinct");
        REQUIRE(distinct.units()[0].weapon->facts()->get().pool->archiveId !=
                distinct.units()[1].weapon->facts()->get().pool->archiveId);
        otherGun->state()->ammoPool.reset();
        duplicate->release();
        pool->release();
        REQUIRE(!fixture->factory->capture(epoch, {}).ok());
    }
    fixture.reset();
    REQUIRE(world.expired());
    REQUIRE_EQ(retained->facts()->get().pool->state.count, 12);
    REQUIRE_EQ(retained->facts()->get().attributes->modifiers[0].id, std::string("saved-mana"));
}
TEST_CASE("RTS weapon freeze rejects missing components and unmapped targets without initializing attributes") {
    Fixture fixture;
    fixture.commit();
    ecs::ScopedTable scope(*fixture.table);
    auto*            unit  = fixture.domain->findUnit(fixture.request.units[0].subject);
    auto*            gun   = dynamic_cast<eve::weapon::WeaponEntity*>(unit->weapon()->link.resolve());
    const auto       epoch = fixture.request.source.owner.child("weapon-save");
    auto* attributes       = gun->getComponentManager().getComponentBuffer<eve::weapon::WeaponEntity::Attributes>();
    if (attributes) attributes->reset_slot(gun->id);
    auto pools  = eve::weapon::WeaponPoolBindings::capture(*fixture.table, epoch).expect("actual pool graph");
    auto frozen = eve::weapon::WeaponCapture::capture(*gun, epoch, {}, pools).expect("missing optional attributes");
    REQUIRE(!frozen.facts()->get().attributes.has_value());
    REQUIRE((!attributes || !attributes->has(gun->id)));
    auto* target                           = fixture.domain->findUnit(fixture.request.units[1].subject);
    gun->state()->lastRequest.targetHandle = ecs::handle_of(target);
    REQUIRE(!eve::weapon::WeaponCapture::capture(*gun, epoch, {}, pools).ok());
    const std::array mapping{eve::weapon::WeaponCaptureTarget{ecs::handle_of(target), target->identity()->subject}};
    auto             mapped =
        eve::weapon::WeaponCapture::capture(*gun, epoch, mapping, pools).expect("explicit stable correspondence");
    REQUIRE(mapped.facts()->get().target == target->identity()->subject);
    {
        auto view = ecs::View<eve::weapon::WeaponEntity>(*fixture.table);
        REQUIRE(!eve::weapon::WeaponCapture::capture(*gun, epoch, mapping, pools).ok());
    }
    REQUIRE(!eve::weapon::WeaponCapture::capture(*gun, {}, mapping, pools).ok());
    const std::array ambiguous{mapping[0],
                               eve::weapon::WeaponCaptureTarget{ecs::handle_of(target), unit->identity()->subject}};
    REQUIRE(!eve::weapon::WeaponCapture::capture(*gun, epoch, ambiguous, pools).ok());
    {
        ecs::Table       unrelated;
        ecs::ScopedTable foreignScope(unrelated);
        REQUIRE(eve::weapon::WeaponCapture::capture(*gun, epoch, mapping, pools).ok());
    }
    (void)*gun->attributes();
    REQUIRE(!gun->attributes()->values.initialized());
    auto uninitialized =
        eve::weapon::WeaponCapture::capture(*gun, epoch, mapping, pools).expect("uninitialized remains uninitialized");
    REQUIRE(uninitialized.facts()->get().attributes.has_value());
    REQUIRE(!uninitialized.facts()->get().attributes->initialized);
    REQUIRE(!gun->attributes()->values.initialized());
    gun->attributes()->values.bindOwner(ecs::handle_of(target)).expect("wrong existing owner");
    REQUIRE(!eve::weapon::WeaponCapture::capture(*gun, epoch, mapping, pools).ok());
    gun->getComponentManager().getComponentBuffer<eve::weapon::WeaponEntity::Attributes>()->reset_slot(gun->id);
    auto moved = std::move(mapped);
    REQUIRE(!mapped.facts().has_value());
    REQUIRE(mapped.saveEpoch().isNil());
    REQUIRE(moved.facts().has_value());
    gun->getComponentManager().getComponentBuffer<eve::weapon::WeaponEntity::Aim>()->reset_slot(gun->id);
    REQUIRE(!fixture.factory->capture(epoch, {}).ok());
    REQUIRE(!gun->getComponentManager().getComponentBuffer<eve::weapon::WeaponEntity::Aim>()->has(gun->id));
}
TEST_CASE("RTS shared ammo retirement rejects resupply and never reports a cancelled reload as complete") {
    eve::sensing::SensingWorld sensing;
    eve::combat::DamageRuntime damage;
    eve::action::ActionRuntime actions;
    eve::rts::ActionAdapter    adapter(actions);
    Fixture                    fixture;
    fixture.commit();
    ecs::ScopedTable scope(*fixture.table);
    auto*            unit = fixture.domain->findUnit(fixture.request.units[0].subject);
    auto*            gun  = dynamic_cast<eve::weapon::WeaponEntity*>(unit->weapon()->link.resolve());
    REQUIRE(gun != nullptr);
    auto* pool             = eve::weapon::AmmoPoolEntity::createPool();
    pool->identity()->id   = "shared";
    pool->state()->max     = 20;
    pool->state()->count   = 3;
    gun->state()->ammoPool = eve::weapon::AmmoPoolLink::bind(*gun, *pool).expect("actual RTS weapon shared pool");
    REQUIRE_EQ(fixture.domain->addUnitReserveAmmo(*unit, 2).expect("actual pool resupply"), 5);
    {
        ecs::Table       unrelated;
        ecs::ScopedTable unrelatedScope(unrelated);
        REQUIRE_EQ(fixture.domain->addUnitReserveAmmo(*unit, 1).expect("actual owner independent of current scope"), 6);
    }
    gun->state()->resource.value   = 0;
    gun->state()->resource.reserve = 7;
    eve::weapon::WeaponSystem::startReload(*gun);
    REQUIRE(gun->state()->resource.reloading);
    gun->state()->resource.reloadProgress = 1.f;
    pool->release();
    auto* replacement           = eve::weapon::AmmoPoolEntity::createPool();
    replacement->identity()->id = "shared";
    replacement->state()->count = 11;
    REQUIRE(!fixture.domain->addUnitReserveAmmo(*unit, 1).ok());
    fixture.domain->setCombatProviders(&sensing, &damage);
    fixture.domain->step({eve::SimulationTick(1), eve::Duration(100000000)}, adapter).expect("real combat phase");
    REQUIRE(!gun->state()->resource.reloading);
    REQUIRE_EQ(gun->state()->resource.value, 0.f);
    REQUIRE_EQ(gun->state()->resource.reserve, 7);
    REQUIRE_EQ(replacement->state()->count, 11);
    const auto json = fixture.domain->inspectFrameEvents().toJson().expect("actual frame events");
    REQUIRE(json.find("reload_completed") == std::string::npos);
    fixture.domain->setCombatProviders(nullptr, nullptr);
}
TEST_CASE("RTS spawn freeze owns exact live and future revisions without retaining the domain or registry") {
    auto command    = std::make_shared<eve::rts::CommandSpec>();
    command->kind   = eve::rts::OrderKind::AttackMove;
    command->target = {12, 12};
    Fixture          fixture(command);
    const auto       epoch = fixture.request.source.owner.child("save");
    const std::array refs{fixture.request.units[0].definition, fixture.request.units[0].definition};
    auto             empty = fixture.factory->capture(epoch, refs).expect("before first publication");
    REQUIRE(empty.units().empty());
    REQUIRE(empty.receipts().empty());
    REQUIRE(empty.sourceOwner().isNil());
    REQUIRE_EQ(empty.futureUnits().size(), std::size_t(1));
    eve::rts_spawn::RTSSpawnFactory noRegistry(*fixture.domain, empty.faction(), {});
    REQUIRE(!noRegistry.capture(epoch, refs).ok());
    REQUIRE(!fixture.factory->capture({}, refs).ok());
    fixture.commit();
    auto frozen = fixture.factory->capture(epoch, refs).expect("exact provenance freeze");
    REQUIRE(frozen.saveEpoch() == epoch);
    REQUIRE(frozen.sourceOwner() == fixture.request.source.owner);
    REQUIRE(frozen.sourceIncarnation() == fixture.request.source.incarnation);
    REQUIRE_EQ(frozen.sourceEpoch(), fixture.request.source.epoch);
    REQUIRE_EQ(frozen.units().size(), std::size_t(2));
    REQUIRE_EQ(frozen.receipts().size(), std::size_t(1));
    REQUIRE(frozen.receipts()[0].operation == fixture.request.operation);
    REQUIRE(frozen.receipts()[0].ordinal == fixture.request.ordinal);
    REQUIRE_EQ(frozen.receipts()[0].units.size(), std::size_t(2));
    REQUIRE_EQ(frozen.receipts()[0].positions[0].x, fixture.positions[0].x);
    const auto original       = frozen.units()[0].recipe->source()->get().generation;
    const auto originalWeapon = frozen.units()[0].recipe->weaponSource()->get().generation;
    for (const auto& unit : frozen.units()) {
        REQUIRE(unit.presence == eve::rts_spawn::CapturedUnitPresence::Present);
        REQUIRE(unit.origin == fixture.request.source);
    }
    eve::rts::RTSContentLoader::load(*fixture.registry, R"({
        "weapons":[{"id":"club","damageType":"normal","damage":25,"range":1.2,"cooldown":0.2,"projectileSpeed":0,"magazineSize":1000}],
        "units":[{"id":"fighter","weaponType":"club","health":70,"speed":3,"radius":0.35}]})")
        .expect("content replacement");
    auto newer = fixture.factory->capture(epoch, refs).expect("new future, retained live revision");
    REQUIRE(newer.units()[0].recipe->source()->get().generation == original);
    REQUIRE(newer.futureUnits()[0].recipe->source()->get().generation != original);
    REQUIRE(newer.futureUnits()[0].recipe->weaponSource()->get().generation != originalWeapon);
    REQUIRE_EQ(fixture.factory->inspect(fixture.request.units[0].subject).expect("unchanged actual unit").health, 40.0);
    const std::array missing{eve::DefinitionRef::parse("unit:missing").expect("missing reference")};
    REQUIRE(!fixture.factory->capture(epoch, missing).ok());
    auto replay = fixture.factory->prepare(fixture.request, fixture.positions).expect("capture preserves receipt");
    REQUIRE(std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(replay)->isPublished());
    command->target.x = 99;
    REQUIRE_EQ(frozen.initialCommand()->target.x, 12.0f);
    std::weak_ptr<eve::definitions::DefinitionRegistry> registry = fixture.registry;
    std::weak_ptr<ecs::Table>                           world    = fixture.table;
    fixture.domain.reset();
    REQUIRE(!fixture.factory->capture(epoch, refs).ok());
    fixture.sourceWorld.reset();
    fixture.providers.reset();
    fixture.factory.reset();
    fixture.registry.reset();
    fixture.table.reset();
    REQUIRE(registry.expired());
    REQUIRE(world.expired());
    REQUIRE_EQ(frozen.units()[0].recipe->source()->get().id, "fighter");
    REQUIRE(frozen.units()[0].recipe->source()->get().generation == original);
    REQUIRE_EQ(newer.futureUnits()[0].recipe->weaponSource()->get().id, "club");
    auto moved = std::move(frozen);
    REQUIRE(frozen.saveEpoch().isNil());
    REQUIRE(frozen.units().empty());
    REQUIRE_EQ(moved.units().size(), std::size_t(2));
}
TEST_CASE("RTS spawn freeze preserves absent pending death facts lifecycle sequence and kept units") {
    Fixture fixture;
    fixture.commit();
    const auto                                 epoch   = fixture.request.source.owner.child("pending-save");
    const auto                                 subject = fixture.request.units[0].subject;
    std::vector<eve::rts::RTSFrameDamageEvent> events;
    {
        ecs::ScopedTable           scope(*fixture.table);
        eve::combat::DamageRuntime damage;
        eve::combat::DamageRequest request;
        request.target       = subject;
        request.healthDamage = 100;
        request.damageType   = "damage.physical";
        request.tick         = eve::SimulationTick(2);
        auto outcome =
            damage.apply(fixture.domain->findUnit(subject)->durability()->state, request).expect("actual death");
        events.push_back({1, request.tick, eve::rts::DamageChannel::Weapon, request, outcome});
        fixture.domain->cleanupDestroyed().expect("canonical casualty cleanup");
    }
    auto pending = fixture.factory->capture(epoch, {}).expect("freeze before lifecycle handoff");
    REQUIRE_EQ(pending.units().size(), std::size_t(2));
    auto absent = std::find_if(pending.units().begin(), pending.units().end(),
                               [&](const auto& row) { return row.subject == subject; });
    REQUIRE(absent != pending.units().end());
    REQUIRE(absent->presence == eve::rts_spawn::CapturedUnitPresence::Absent);
    REQUIRE(absent->origin == fixture.request.source);
    REQUIRE_EQ(pending.lifecycleSequence(), std::uint64_t(0));
    auto lifecycle = fixture.factory->collectLifecycle(events).expect("original actual damage batch");
    REQUIRE_EQ(lifecycle.size(), std::size_t(1));
    REQUIRE(lifecycle[0].reason == eve::spawn::RemovalReason::Death);
    auto delivered = fixture.factory->capture(epoch, {}).expect("freeze after lifecycle translation");
    REQUIRE_EQ(delivered.units().size(), std::size_t(1));
    REQUIRE_EQ(delivered.lifecycleSequence(), std::uint64_t(1));
    REQUIRE_EQ(delivered.receipts()[0].units.size(), std::size_t(2));
    REQUIRE_EQ(events.size(), std::size_t(1));
    eve::spawn::RemovalRequest removal;
    removal.source    = fixture.request.source;
    removal.operation = fixture.request.operation.child("keep");
    removal.policy    = eve::spawn::DetachPolicy::KeepUnits;
    for (const auto& unit : fixture.request.units) removal.subjects.push_back(unit.subject);
    fixture.factory->remove(removal).expect("explicit origin release");
    auto kept = fixture.factory->capture(epoch, {}).expect("kept unit remains captured");
    REQUIRE_EQ(kept.units().size(), std::size_t(1));
    REQUIRE(!kept.units()[0].origin);
    REQUIRE(kept.units()[0].presence == eve::rts_spawn::CapturedUnitPresence::Present);
    REQUIRE_EQ(kept.receipts()[0].units.size(), std::size_t(2));
    REQUIRE_EQ(fixture.crowd.getAgentCount(), 1);
}
TEST_CASE("RTS spawn freeze stabilizes command targets and rejects replacement views and expired origins") {
    Fixture    fixture;
    const auto epoch         = fixture.request.source.owner.child("identity-save");
    const auto targetSubject = eve::SubjectRef::fromPersistentId(epoch.child("target"));
    std::shared_ptr<eve::rts_spawn::RTSSpawnFactory> targeted;
    {
        ecs::ScopedTable scope(*fixture.table);
        auto*            target  = fixture.domain->newUnit(targetSubject).expect("actual target");
        auto             command = std::make_shared<eve::rts::CommandSpec>();
        command->kind            = eve::rts::OrderKind::Attack;
        command->targetEntity    = ecs::handle_of(target);
        const auto faction =
            eve::SubjectRef::fromPersistentId(*eve::PersistentId::parse("01900000-0000-7000-8000-000000000889"));
        targeted =
            std::make_shared<eve::rts_spawn::RTSSpawnFactory>(*fixture.domain, faction, fixture.registry, command);
    }
    auto command = targeted->capture(epoch, {}).expect("stable target capture");
    REQUIRE(command.initialCommandTarget() == targetSubject);
    REQUIRE(command.initialCommand()->targetEntity.table == nullptr);
    const std::array targets{targetSubject};
    fixture.domain->prepareUnitRemoval(targets).expect("prepare target removal").commit().expect("target removal");
    REQUIRE(!targeted->capture(epoch, {}).ok());
    REQUIRE(command.initialCommandTarget() == targetSubject);
    fixture.commit();
    {
        auto view = ecs::View<eve::rts::Unit>(*fixture.table);
        REQUIRE(!fixture.factory->capture(epoch, {}).ok());
    }
    const std::array removed{fixture.request.units[0].subject};
    fixture.domain->prepareUnitRemoval(removed).expect("prepare original removal").commit().expect("original removal");
    {
        ecs::ScopedTable scope(*fixture.table);
        fixture.domain->newUnit(removed[0]).expect("same identity new incarnation");
    }
    REQUIRE(!fixture.factory->capture(epoch, {}).ok());
    fixture.domain->prepareUnitRemoval(removed)
        .expect("prepare replacement removal")
        .commit()
        .expect("replacement removal");
    REQUIRE_EQ(fixture.factory->capture(epoch, {}).expect("old absence remains observable").units().size(),
               std::size_t(2));
    fixture.sourceWorld.reset();
    REQUIRE(!fixture.factory->capture(epoch, {}).ok());
}
TEST_CASE("RTS spawn freeze and stable identity queries never materialize missing components") {
    Fixture fixture;
    fixture.commit();
    const auto       epoch = fixture.request.source.owner.child("readonly-save");
    ecs::ScopedTable scope(*fixture.table);
    auto*            unit     = fixture.domain->findUnit(fixture.request.units[0].subject);
    const auto       identity = *unit->identity();
    auto             command  = std::make_shared<eve::rts::CommandSpec>();
    command->kind             = eve::rts::OrderKind::Attack;
    command->targetEntity     = ecs::handle_of(unit);
    const auto factionId =
        eve::SubjectRef::fromPersistentId(*eve::PersistentId::parse("01900000-0000-7000-8000-000000000889"));
    eve::rts_spawn::RTSSpawnFactory targeted(*fixture.domain, factionId, fixture.registry, command);
    auto* identities = unit->getComponentManager().getComponentBuffer<eve::rts::Unit::Identity>();
    identities->reset_slot(unit->id);
    REQUIRE(fixture.domain->findUnit(identity.subject) == nullptr);
    REQUIRE(!identities->has(unit->id));
    REQUIRE(!targeted.capture(epoch, {}).ok());
    REQUIRE(!identities->has(unit->id));
    REQUIRE(!fixture.factory->capture(epoch, {}).ok());
    REQUIRE(!identities->has(unit->id));
    *unit->identity()          = identity;
    auto*      faction         = fixture.domain->findFaction(factionId);
    const auto factionIdentity = *faction->identity();
    auto*      factions        = faction->getComponentManager().getComponentBuffer<eve::rts::Faction::Identity>();
    factions->reset_slot(faction->id);
    REQUIRE(fixture.domain->findFaction(factionId) == nullptr);
    REQUIRE(!fixture.factory->capture(epoch, {}).ok());
    REQUIRE(!factions->has(faction->id));
    *faction->identity() = factionIdentity;
    REQUIRE_EQ(fixture.factory->capture(epoch, {}).expect("repaired actual identity").units().size(), std::size_t(2));
    Fixture missingProvider;
    missingProvider.domain->setCrowdProvider(nullptr);
    REQUIRE(!missingProvider.factory->capture(epoch, {}).ok());
    missingProvider.domain->setCrowdProvider(&missingProvider.crowd);
    REQUIRE(missingProvider.factory->capture(epoch, {}).expect("restored actual provider").units().empty());
}
TEST_CASE("RTS spawn factory shares atomic creation and replay contract") {
    Fixture fixture;
    spawn_factory_contract::creationAndReplay(fixture);
    auto unit = fixture.factory->inspect(fixture.request.units[0].subject).expect("real RTS state");
    REQUIRE_EQ(unit.health, 40);
    REQUIRE_EQ(unit.position.x, 1);
    REQUIRE_EQ(fixture.crowd.getAgentCount(), 2);
}
TEST_CASE("RTS occupancy captures actual domain positions and radii with owning lifetime boundaries") {
    Fixture                           fixture;
    eve::rts_spawn::RTSSpawnOccupancy occupancy(*fixture.domain);
    REQUIRE(occupancy.capture().expect("known empty domain").empty());
    fixture.commit();
    const auto initial = occupancy.capture().expect("actual RTS bodies");
    REQUIRE_EQ(initial.size(), std::size_t(2));
    for (const auto& disk : initial) {
        REQUIRE(std::abs(disk.radius - 0.35) < 0.000001);
        auto actual = fixture.factory->inspect(disk.subject).expect("actual RTS identity");
        REQUIRE_EQ(disk.center.x, actual.position.x);
        REQUIRE_EQ(disk.center.y, actual.position.y);
    }
    {
        auto view = ecs::View<eve::rts::Unit>(*fixture.table);
        REQUIRE(!occupancy.capture().ok());
    }
    const auto subject = fixture.request.units[0].subject;
    {
        ecs::ScopedTable              scope(*fixture.table);
        eve::rts::UnitRelocationBatch movement;
        movement.units.push_back({subject, {8, 8}});
        REQUIRE_EQ(fixture.domain->relocateUnits(movement).expect("canonical unit and Crowd relocation"),
                   std::size_t(1));
    }
    const auto moved = occupancy.capture().expect("fresh actual position");
    auto row = std::find_if(moved.begin(), moved.end(), [&](const auto& disk) { return disk.subject == subject; });
    REQUIRE(row != moved.end());
    REQUIRE_EQ(row->center.x, 8.0);
    const auto old =
        std::find_if(initial.begin(), initial.end(), [&](const auto& disk) { return disk.subject == subject; });
    REQUIRE(old != initial.end());
    REQUIRE_EQ(old->center.x, 1.0);
    {
        ecs::ScopedTable scope(*fixture.table);
        auto*            unit   = fixture.domain->findUnit(subject);
        const auto       radius = unit->crowd()->radius;
        unit->crowd()->radius   = std::numeric_limits<float>::quiet_NaN();
        REQUIRE(!occupancy.capture().ok());
        unit->crowd()->radius = radius;
    }
    {
        ecs::ScopedTable scope(*fixture.table);
        auto*            passenger         = fixture.domain->findUnit(subject);
        auto*            transport         = fixture.domain->findUnit(fixture.request.units[1].subject);
        transport->containment()->capacity = 1;
        passenger->motion()->arrived       = true;
        eve::rts::CommandSpec board;
        board.kind         = eve::rts::OrderKind::BoardTransport;
        board.target       = {transport->motion()->x, transport->motion()->y};
        board.targetEntity = ecs::handle_of(transport);
        const std::array selection{subject};
        fixture.domain->commandUnits(selection, board).expect("canonical boarding order");
        eve::rts::ContainmentSystem::step().expect("canonical containment phase");
        REQUIRE(passenger->containment()->container.isBound());
        REQUIRE_EQ(occupancy.capture().expect("passenger has no separate world disk").size(), std::size_t(1));
        fixture.domain->unloadTransport(*transport, {10, 10}).expect("canonical unload");
    }
    REQUIRE_EQ(occupancy.capture().expect("unloaded physical bodies").size(), std::size_t(2));
    fixture.domain.reset();
    REQUIRE(!occupancy.capture().ok());
    REQUIRE_EQ(initial.size(), std::size_t(2));
    REQUIRE_EQ(moved.size(), std::size_t(2));
}
TEST_CASE("RTS spawn factory shares explicit removal contract") {
    Fixture fixture;
    spawn_factory_contract::removal(fixture, eve::spawn::DetachPolicy::RemoveUnits);
    REQUIRE_EQ(fixture.crowd.getAgentCount(), 0);
}
TEST_CASE("RTS spawn factory shares keep units contract") {
    Fixture fixture;
    spawn_factory_contract::removal(fixture, eve::spawn::DetachPolicy::KeepUnits);
    REQUIRE(!fixture.factory->inspect(fixture.request.units[0].subject).expect("released unit").origin);
    REQUIRE_EQ(fixture.crowd.getAgentCount(), 2);
}
TEST_CASE("RTS spawn factory shares source revocation contract") {
    Fixture fixture;
    spawn_factory_contract::sourceRevocation(fixture);
}
TEST_CASE("RTS spawn factory blocks whole overlapping batch and retries stable identity") {
    Fixture fixture;
    fixture.positions[1] = fixture.positions[0];
    auto prepared        = fixture.factory->prepare(fixture.request, fixture.positions).expect("prepare overlap");
    REQUIRE(std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(prepared)->commit().expect("blocked") ==
            eve::spawn::CommitOutcome::PlacementBlocked);
    REQUIRE_EQ(fixture.count(), 0);
    REQUIRE_EQ(fixture.crowd.getAgentCount(), 0);
    fixture.positions[1] = {4, 5};
    fixture.commit();
    REQUIRE_EQ(fixture.count(), 2);
}
TEST_CASE("RTS spawn factory preserves death over stale root and does not poll damage") {
    Fixture fixture;
    fixture.commit();
    std::vector<eve::rts::RTSFrameDamageEvent> events(1);
    events[0].outcome.target         = fixture.request.units[0].subject;
    events[0].outcome.previousHealth = 40;
    events[0].outcome.health         = 0;
    {
        ecs::ScopedTable scope(*fixture.table);
        fixture.domain->findUnit(events[0].outcome.target)->durability()->alive = false;
        fixture.domain->cleanupDestroyed().expect("canonical casualty cleanup");
    }
    auto facts = fixture.factory->collectLifecycle(events).expect("shared event batch");
    REQUIRE_EQ(facts.size(), 1u);
    REQUIRE(facts[0].reason == eve::spawn::RemovalReason::Death);
    REQUIRE_EQ(events.size(), 1u);
    REQUIRE(fixture.factory->collectLifecycle(events).expect("repeat").empty());
}
TEST_CASE("RTS spawn observes domain destruction and translates unload without dereferencing stale handles") {
    Fixture fixture;
    fixture.commit();
    fixture.domain.reset();
    auto facts = fixture.factory->collectLifecycle({}).expect("domain unload");
    REQUIRE_EQ(facts.size(), 2u);
    REQUIRE(facts[0].reason == eve::spawn::RemovalReason::Unloaded);
    REQUIRE_EQ(fixture.count(), 0);
    REQUIRE_EQ(fixture.crowd.getAgentCount(), 0);
    REQUIRE(fixture.factory->collectLifecycle({}).expect("unload repeat").empty());
    REQUIRE(fixture.sourceWorld->detach(fixture.request.source, eve::spawn::DetachPolicy::RemoveUnits).ok());
}
TEST_CASE("RTS spawn pending factory tokens do not extend domain or factory lifetime") {
    Fixture fixture;
    auto    candidate = fixture.factory->prepare(fixture.request, fixture.positions).expect("pending");
    fixture.providers.reset();
    fixture.factory.reset();
    REQUIRE(!std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(candidate)->commit().ok());
    REQUIRE_EQ(fixture.count(), 0);
}
TEST_CASE("RTS spawn world integrates real population budget and death replacement") {
    Fixture fixture;
    auto born = fixture.sourceWorld->advance({eve::SimulationTick(1), eve::Duration(0)}).expect("initial population");
    REQUIRE_EQ(born.units, 2u);
    REQUIRE_EQ(fixture.count(), 2u);
    {
        ecs::ScopedTable scope(*fixture.table);
        fixture.domain->findUnit(fixture.request.units[0].subject)->durability()->alive = false;
    }
    auto facts = fixture.factory->collectLifecycle({}).expect("actual death");
    REQUIRE_EQ(facts.size(), 1u);
    auto accepted =
        fixture.sourceWorld->advance({eve::SimulationTick(2), eve::Duration(0)}, facts).expect("death accepted");
    REQUIRE_EQ(accepted.units, 0u);
    {
        ecs::ScopedTable scope(*fixture.table);
        fixture.domain->cleanupDestroyed().expect("remove dead projection");
    }
    auto replacement =
        fixture.sourceWorld
            ->advance({eve::SimulationTick(3), eve::Duration::fromSeconds(1).expect("simulation second")})
            .expect("replacement");
    REQUIRE_EQ(replacement.units, 1u);
    REQUIRE_EQ(fixture.count(), 2u);
    fixture.sourceWorld->detach(fixture.request.source, eve::spawn::DetachPolicy::RemoveUnits).expect("source cleanup");
    REQUIRE_EQ(fixture.count(), 0u);
    REQUIRE_EQ(fixture.crowd.getAgentCount(), 0);
}
TEST_CASE("RTS spawn rejects forged member identities and identifies published receipt replays") {
    Fixture fixture;
    auto    forged          = fixture.request;
    forged.units[0].subject = fixture.request.units[1].subject;
    REQUIRE(!fixture.factory->prepare(forged, fixture.positions).ok());
    fixture.commit();
    auto replay = fixture.factory->prepare(fixture.request, fixture.positions).expect("replay");
    REQUIRE(std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(replay)->isPublished());
}

TEST_CASE("RTS spawn pins canonical recipes before registry replacement") {
    Fixture fixture;
    auto    candidate = fixture.factory->prepare(fixture.request, fixture.positions).expect("prepare old recipe");
    fixture.registry->replace("unit", "fighter", 1, R"({"health":90,"speed":9,"radius":0.35,"weaponType":"club"})")
        .expect("new recipe");
    std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(candidate)->commit().expect("old revision commit");
    REQUIRE_EQ(fixture.factory->inspect(fixture.request.units[0].subject).expect("pinned actor").health, 40);
}
TEST_CASE("RTS spawn bounds receipts by latest ordinal while published tokens remain historical confirmations") {
    Fixture fixture;
    auto    candidate = fixture.factory->prepare(fixture.request, fixture.positions).expect("first operation");
    auto&   first     = std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(candidate);
    first->commit().expect("first commit");
    auto next      = fixture.request;
    next.ordinal   = eve::spawn::OperationOrdinal(2);
    next.operation = next.lifetime.operationIdentity(next.ordinal).expect("next operation");
    for (std::size_t i = 0; i < next.units.size(); ++i)
        next.units[i].subject =
            next.lifetime.unitIdentity(next.ordinal, static_cast<std::uint32_t>(i)).expect("next member");
    std::vector<eve::spawn::Point> positions{{10, 2}, {14, 5}};
    auto                           newer = fixture.factory->prepare(next, positions).expect("second operation");
    std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(newer)->commit().expect("second commit");
    REQUIRE(!fixture.factory->prepare(fixture.request, fixture.positions).ok());
    REQUIRE(first->commit().expect("historical confirmation") == eve::spawn::CommitOutcome::Committed);
    REQUIRE_EQ(fixture.count(), 4u);
}

TEST_CASE("RTS spawn forwards an owning initial command through the real native transaction") {
    auto command    = std::make_shared<eve::rts::CommandSpec>();
    command->kind   = eve::rts::OrderKind::AttackMove;
    command->target = {20, 30};
    Fixture fixture(command);
    command->target = {90, 100};
    fixture.commit();
    ecs::ScopedTable scope(*fixture.table);
    for (const auto& member : fixture.request.units) {
        auto* unit = fixture.domain->findUnit(member.subject);
        REQUIRE(unit != nullptr);
        auto order = unit->orders()->values.current().expect("canonical initial order");
        REQUIRE(order.kind == eve::rts::OrderKind::AttackMove);
        REQUIRE_EQ(order.target.x, 20);
        REQUIRE_EQ(order.target.y, 30);
    }
}
TEST_CASE("RTS spawn rejects an absent Crowd provider and succeeds after explicit rebinding") {
    Fixture fixture;
    fixture.domain->setCrowdProvider(nullptr);
    REQUIRE(!fixture.factory->validateDomain().ok());
    REQUIRE(!fixture.factory->prepare(fixture.request, fixture.positions).ok());
    REQUIRE_EQ(fixture.count(), 0u);
    REQUIRE_EQ(fixture.crowd.getAgentCount(), 0);
    fixture.domain->setCrowdProvider(&fixture.crowd);
    REQUIRE(fixture.factory->validateDomain().ok());
    fixture.commit();
    REQUIRE_EQ(fixture.count(), 2u);
    REQUIRE_EQ(fixture.crowd.getAgentCount(), 2);
}

TEST_CASE("RTS spawn world retains identity and releases population capacity across a blocked Crowd commit") {
    Fixture fixture;
    REQUIRE(fixture.crowd.addNamedAgent("fixed-obstacle", 0, 2, 0, 1) >= 0);
    auto blocked = fixture.sourceWorld->advance({eve::SimulationTick(1), eve::Duration(0)}).expect("blocked frame");
    REQUIRE_EQ(blocked.units, 0u);
    REQUIRE_EQ(fixture.count(), 0u);
    auto state = fixture.sourceWorld->inspect(fixture.request.source).expect("blocked population");
    REQUIRE_EQ(state.alive, 0u);
    REQUIRE_EQ(state.total, 0u);
    REQUIRE_EQ(fixture.crowd.getAgentCount(), 1);
    REQUIRE(fixture.crowd.removeNamedAgent("fixed-obstacle"));
    auto retry =
        fixture.sourceWorld->advance({eve::SimulationTick(2), eve::Duration::fromSeconds(1).expect("retry delay")})
            .expect("capacity retry");
    REQUIRE_EQ(retry.units, 2u);
    REQUIRE_EQ(fixture.count(), 2u);
    REQUIRE_EQ(fixture.crowd.getAgentCount(), 2);
    {
        ecs::ScopedTable scope(*fixture.table);
        for (const auto& unit : fixture.request.units) REQUIRE(fixture.domain->findUnit(unit.subject) != nullptr);
    }
    auto capped = fixture.sourceWorld->advance({eve::SimulationTick(3), eve::Duration(0)}).expect("population cap");
    REQUIRE_EQ(capped.units, 0u);
    REQUIRE_EQ(fixture.count(), 2u);
}

}  // namespace test_rts_spawn
