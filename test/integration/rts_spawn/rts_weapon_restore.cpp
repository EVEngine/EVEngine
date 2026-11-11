// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"role":"tests","default":"module-composition","module":"rts","task":"rts_spawn","modules":["rts","weapon","attributes","spawn","rts_spawn","crowd","definitions"]}
*/
// clang-format on
#include <algorithm>
#include "RTSWeaponRestoreFixture.h"
#include "rts/RTSWeaponRestore.h"
#include "weapon/WeaponGraphRestore.h"
#include "zeroerr/unittest.h"
namespace test_rts_spawn {
namespace {
eve::weapon::WeaponEntity& gun(RestoredRTSSpawn& target, eve::SubjectRef subject) {
    return *dynamic_cast<eve::weapon::WeaponEntity*>(target.domain->findUnit(subject)->weapon()->link.resolve());
}
template <class T>
std::size_t rows(ecs::Table& table) {
    std::size_t count = 0;
    auto        view  = ecs::View<T>(table);
    for (auto it = view.begin(); it != view.end(); ++it) ++count;
    return count;
}
void roundTripWeapons(SavedRTSWeaponScope& saved) {
    const auto epoch    = saved.weapons->saveEpoch();
    auto       envelope = saved.weapons
                        ->seal(eve::Revision(19), eve::SimulationTick(1),
                               [&](std::string_view bytes) {
                                   saved.weapons.reset();
                                   return restoreHash(bytes);
                               })
                        .expect("seal graph while digest retires capture");
    REQUIRE(!saved.weapons);
    const auto text   = eve::serializeSnapshotEnvelope(envelope).expect("graph text");
    auto       input  = eve::parseSnapshotEnvelope(text, restoreHash).expect("graph wire");
    auto       parsed = eve::rts::RTSWeaponCapture::parse(input, epoch, [&](std::string_view bytes) {
                      input.payload = eve::Value{};
                      return restoreHash(bytes);
                  }).expect("owning graph admission independent of caller input");
    REQUIRE_EQ(eve::serializeSnapshotEnvelope(
                   parsed.seal(eve::Revision(19), eve::SimulationTick(1), restoreHash).expect("repeat graph envelope"))
                   .expect("repeat graph wire"),
               text);
    const eve::weapon::WeaponCapture* first    = nullptr;
    const eve::weapon::WeaponCapture* building = nullptr;
    for (const auto& member : parsed.members()) {
        if (member.body == saved.first) first = member.weapon.get();
        if (member.body == saved.building) building = member.weapon.get();
        if (member.body == saved.unarmed) REQUIRE(!member.weapon);
    }
    REQUIRE(first != nullptr);
    REQUIRE(first == building);
    saved.weapons = std::make_shared<const eve::rts::RTSWeaponCapture>(std::move(parsed));
}
}  // namespace
TEST_CASE("RTS native body weapon restore joins roots full attributes source ownership and shared reload state") {
    auto saved = freezeWeaponScope();
    REQUIRE(saved.sourceWorld.expired());
    roundTripWeapons(saved);
    RestoredRTSSpawn target(saved.session);
    ecs::ScopedTable scope(*target.table);
    auto             attributes =
        eve::rts::PreparedRTSUnitAttributeRestore::prepare(*target.domain, *saved.attributes).expect("full attributes");
    target.domain->restoreUnitAttributes(std::move(attributes)).expect("native Unit publication");
    auto candidate =
        eve::rts::PreparedRTSWeaponRestore::prepare(*target.domain, *saved.weapons).expect("body graph preparation");
    auto competing =
        eve::rts::PreparedRTSWeaponRestore::prepare(*target.domain, *saved.weapons).expect("second CPU candidate");
    std::size_t events = 0;
    struct Reset {
        ~Reset() { eve::weapon::WeaponSystem::setEventSink({}); }
    } reset;
    eve::weapon::WeaponSystem::setEventSink([&](const auto&) { ++events; });
    auto pools = target.domain->restoreWeapons(std::move(candidate)).expect("native graph materialization");
    REQUIRE_EQ(events, std::size_t(0));
    REQUIRE(!target.domain->restoreWeapons(std::move(candidate)).ok());
    REQUIRE(!target.domain->restoreWeapons(std::move(competing)).ok());
    REQUIRE(!eve::rts::PreparedRTSWeaponRestore::prepare(*target.domain, *saved.weapons).ok());
    REQUIRE_EQ(rows<eve::weapon::WeaponEntity>(*target.table), std::size_t(2));
    REQUIRE_EQ(rows<eve::weapon::AmmoPoolEntity>(*target.table), std::size_t(1));
    auto& first  = gun(target, saved.first);
    auto& second = gun(target, saved.second);
    REQUIRE(target.domain->findBuilding(saved.building)->weapon()->link.handle().id == ecs::handle_of(&first).id);
    REQUIRE(target.domain->findBuilding(saved.building)->weapon()->link.resolve() == &first);
    REQUIRE(!target.domain->findUnit(saved.unarmed)->weapon()->link.isBound());
    REQUIRE(first.state()->lastRequest.targetHandle.table == target.table.get());
    REQUIRE(first.state()->lastRequest.targetHandle.id ==
            ecs::handle_of(target.domain->findBuilding(saved.building)).id);
    REQUIRE_EQ(first.state()->cooldown, .75f);
    REQUIRE_EQ(target.domain->findUnit(saved.first)->attributes()->values.getFinal("mana").expect("full custom value"),
               17.0);
    REQUIRE_EQ(first.attributes()->values.getFinal("mana").expect("weapon native value"), 3.0);
    REQUIRE(first.state()->ammoPool.resolve() == second.state()->ammoPool.resolve());
    REQUIRE_EQ(pools.pools().size(), std::size_t(1));
    REQUIRE(pools.pools()[0].pool.resolve() == first.state()->ammoPool.resolve());
    target.factory->restoreSources(*target.core, {}, {}).expect("actual source binding after graph restoration");
    REQUIRE(target.factory->inspect(saved.first).expect("native factory body").origin);
    eve::weapon::WeaponSystem::update(second, .5f);
    REQUIRE_EQ(second.state()->resource.value, 9.f);
    REQUIRE_EQ(first.state()->ammoPool.resolve()->state()->count, 0);
    eve::weapon::WeaponSystem::update(first, .75f);
    REQUIRE_EQ(first.state()->resource.value, 0.f);
    REQUIRE_EQ(first.state()->ammoPool.resolve()->state()->count, 0);
}
TEST_CASE(
    "RTS native body weapon candidates reject claimed missing foreign and substituted targets before publication") {
    auto             saved = freezeWeaponScope();
    RestoredRTSSpawn target(saved.session), other(saved.session);
    ecs::ScopedTable scope(*target.table);
    auto candidate = eve::rts::PreparedRTSWeaponRestore::prepare(*target.domain, *saved.weapons).expect("candidate");
    auto moved     = std::move(candidate);
    REQUIRE(!target.domain->restoreWeapons(std::move(candidate)).ok());
    REQUIRE(!other.domain->restoreWeapons(std::move(moved)).ok());
    {
        ecs::View<eve::rts::Unit> view(*target.table);
        REQUIRE(!target.domain->restoreWeapons(std::move(moved)).ok());
        REQUIRE(!eve::rts::PreparedRTSWeaponRestore::prepare(*target.domain, *saved.weapons).ok());
    }
    auto* first  = target.domain->findUnit(saved.first);
    auto* buffer = first->getComponentManager().getComponentBuffer<eve::rts::Unit::Weapon>();
    buffer->reset_slot(first->id);
    REQUIRE(!target.domain->restoreWeapons(std::move(moved)).ok());
    REQUIRE(!buffer->has(first->id));
    REQUIRE_EQ(rows<eve::weapon::WeaponEntity>(*target.table), std::size_t(0));
    REQUIRE_EQ(rows<eve::weapon::AmmoPoolEntity>(*target.table), std::size_t(0));
    (void)*first->weapon();
    saved.weapons.reset();
    auto pools = target.domain->restoreWeapons(std::move(moved))
                     .expect("same candidate after explicit repair and source capture destruction");
    REQUIRE_EQ(pools.pools().size(), std::size_t(1));
    auto          original = freezeWeaponScope();
    auto          table    = std::make_shared<ecs::Table>();
    eve::rts::RTS substituted(table);
    {
        ecs::ScopedTable changed(*table);
        for (const auto& member : original.weapons->members()) {
            if (member.kind == eve::rts::RTSWeaponBodyKind::Building || member.body == original.first)
                substituted.newBuilding(member.body).expect("substituted body kind");
            else
                substituted.newUnit(member.body).expect("other body");
        }
    }
    REQUIRE(!eve::rts::PreparedRTSWeaponRestore::prepare(substituted, *original.weapons).ok());
    REQUIRE_EQ(rows<eve::weapon::WeaponEntity>(*table), std::size_t(0));
}
TEST_CASE("RTS restored pool owner preserves same-name identity and supports both destruction orders") {
    auto saved = freezeWeaponScope(true);
    REQUIRE(saved.sourceWorld.expired());
    roundTripWeapons(saved);
    RestoredRTSSpawn target(saved.session);
    auto             candidate =
        eve::rts::PreparedRTSWeaponRestore::prepare(*target.domain, *saved.weapons).expect("distinct pools");
    auto pools = target.domain->restoreWeapons(std::move(candidate)).expect("graph");
    REQUIRE_EQ(pools.pools().size(), std::size_t(2));
    auto first = pools.pools()[0].pool, second = pools.pools()[1].pool;
    REQUIRE(first.resolve() != second.resolve());
    REQUIRE(first.resolve()->identity()->id == second.resolve()->identity()->id);
    REQUIRE(first.resolve()->state()->count + second.resolve()->state()->count == 22);
    {
        ecs::View<eve::weapon::AmmoPoolEntity> view(*target.table);
        REQUIRE(!pools.close().ok());
        REQUIRE(first.resolve());
    }
    pools.close().expect("explicit pool owner close first");
    REQUIRE(!first.resolve());
    REQUIRE(!second.resolve());
    {
        ecs::ScopedTable scope(*target.table);
        REQUIRE(gun(target, saved.first).state()->ammoPool.isStale());
    }
    RestoredRTSSpawn other(saved.session);
    auto otherPlan  = eve::rts::PreparedRTSWeaponRestore::prepare(*other.domain, *saved.weapons).expect("other graph");
    auto otherPools = other.domain->restoreWeapons(std::move(otherPlan)).expect("other owner");
    auto live       = otherPools.pools()[0].pool;
    other.domain.reset();
    REQUIRE(live.resolve());
    REQUIRE_EQ(rows<eve::weapon::WeaponEntity>(*other.table), std::size_t(0));
    otherPools.close().expect("RTS destroyed first");
    REQUIRE(!live.resolve());
    REQUIRE_EQ(rows<eve::weapon::AmmoPoolEntity>(*other.table), std::size_t(0));
    RestoredRTSSpawn changed(saved.session);
    auto             changedPlan =
        eve::rts::PreparedRTSWeaponRestore::prepare(*changed.domain, *saved.weapons).expect("generation scope");
    auto changedPools = changed.domain->restoreWeapons(std::move(changedPlan)).expect("owned pool generations");
    ecs::ScopedTable scope(*changed.table);
    changedPools.pools()[0].pool.resolve()->release();
    auto* substitute            = eve::weapon::AmmoPoolEntity::createPool();
    substitute->state()->count  = 51;
    const auto substituteHandle = ecs::handle_of(substitute);
    changedPools.close().expect("retire only owned pool generations");
    REQUIRE(ecs::try_get(substituteHandle) == substitute);
    REQUIRE_EQ(substitute->state()->count, 51);
    substitute->release();
}
TEST_CASE("RTS pending body weapon targets never claim replacement generations") {
    auto             saved = freezeWeaponScope();
    RestoredRTSSpawn target(saved.session);
    auto             candidate =
        eve::rts::PreparedRTSWeaponRestore::prepare(*target.domain, *saved.weapons).expect("original incarnation");
    ecs::ScopedTable scope(*target.table);
    auto*            original = target.domain->findUnit(saved.first);
    const auto       retired  = ecs::handle_of(original);
    original->release();
    auto* replacement = eve::rts::Unit::createUnit(saved.first);
    replacement->attributes()->values.bindOwner(ecs::handle_of(replacement)).expect("replacement owner");
    replacement->attributes()->values.setBase("mana", 51).expect("replacement state");
    REQUIRE(!ecs::try_get(retired));
    REQUIRE(!target.domain->restoreWeapons(std::move(candidate)).ok());
    REQUIRE(!replacement->weapon()->link.isBound());
    REQUIRE_EQ(replacement->attributes()->values.getFinal("mana").expect("replacement preserved"), 51.0);
    REQUIRE_EQ(rows<eve::weapon::WeaponEntity>(*target.table), std::size_t(0));
    REQUIRE_EQ(rows<eve::weapon::AmmoPoolEntity>(*target.table), std::size_t(0));
    replacement->release();
}
TEST_CASE("RTS native weapon candidate and restored pool owner do not retain worlds and admit empty scopes") {
    auto                                              saved = freezeWeaponScope();
    std::optional<eve::rts::PreparedRTSWeaponRestore> abandoned;
    std::weak_ptr<ecs::Table>                         weak;
    {
        RestoredRTSSpawn target(saved.session);
        weak = target.table;
        abandoned.emplace(
            eve::rts::PreparedRTSWeaponRestore::prepare(*target.domain, *saved.weapons).expect("weak candidate"));
    }
    REQUIRE(weak.expired());
    std::optional<eve::rts::RestoredRTSWeaponPools> owner;
    eve::weapon::AmmoPoolLink                       link;
    {
        RestoredRTSSpawn target(saved.session);
        REQUIRE(!target.domain->restoreWeapons(std::move(*abandoned)).ok());
        weak = target.table;
        auto candidate =
            eve::rts::PreparedRTSWeaponRestore::prepare(*target.domain, *saved.weapons).expect("own graph");
        owner.emplace(target.domain->restoreWeapons(std::move(candidate)).expect("weak-world pool owner"));
        link = owner->pools()[0].pool;
    }
    REQUIRE(weak.expired());
    REQUIRE(!link.resolve());
    owner->close().expect("dead world retirement");
    auto          table = std::make_shared<ecs::Table>();
    eve::rts::RTS empty(table);
    auto          captured = empty.captureWeapons(saved.weapons->saveEpoch()).expect("empty native scope");
    auto          emptyEnvelope =
        captured.seal(eve::Revision(1), eve::SimulationTick(0), restoreHash).expect("empty graph archive");
    captured = eve::rts::RTSWeaponCapture::parse(std::move(emptyEnvelope), captured.saveEpoch(), restoreHash)
                   .expect("empty graph admission");
    auto candidate = eve::rts::PreparedRTSWeaponRestore::prepare(empty, captured).expect("empty candidate");
    auto pools     = empty.restoreWeapons(std::move(candidate)).expect("empty restoration");
    REQUIRE(pools.pools().empty());
    REQUIRE(pools.saveEpoch() == captured.saveEpoch());
}
TEST_CASE("RTS body weapon archive rejects broken aliases pool conflicts and malformed boundaries before publication") {
    auto       saved = freezeWeaponScope();
    const auto epoch = saved.weapons->saveEpoch();
    const auto base = saved.weapons->seal(eve::Revision(31), eve::SimulationTick(1), restoreHash).expect("valid graph");
    const auto& root = *base.payload.getIf<eve::Value::Object>();
    REQUIRE_EQ(root.at("weapons").getIf<eve::Value::Array>()->size(), std::size_t(2));
    REQUIRE_EQ(root.at("members").getIf<eve::Value::Array>()->size(), std::size_t(4));
    auto admit = [&](eve::Value payload) {
        auto envelope = eve::makeSnapshotEnvelope(base.type, base.schema, base.schemaVersion, base.instanceId,
                                                  base.revision, base.tick, std::move(payload), restoreHash)
                            .expect("reseal test graph");
        return eve::rts::RTSWeaponCapture::parse(std::move(envelope), epoch, restoreHash);
    };
    auto candidate                                      = base.payload;
    (*candidate.getIf<eve::Value::Object>())["unknown"] = 1;
    REQUIRE(!admit(candidate).ok());
    candidate             = base.payload;
    auto& duplicateBodies = *candidate.getIf<eve::Value::Object>()->at("members").getIf<eve::Value::Array>();
    duplicateBodies.push_back(duplicateBodies.front());
    REQUIRE(!admit(candidate).ok());
    candidate     = base.payload;
    auto& badKind = *candidate.getIf<eve::Value::Object>()->at("members").getIf<eve::Value::Array>();
    (*badKind.front().getIf<eve::Value::Array>())[1] = 2;
    REQUIRE(!admit(candidate).ok());
    candidate     = base.payload;
    auto& missing = *candidate.getIf<eve::Value::Object>()->at("members").getIf<eve::Value::Array>();
    (*missing.front().getIf<eve::Value::Array>())[2] = epoch.child("missing-weapon").format();
    REQUIRE(!admit(candidate).ok());
    candidate   = base.payload;
    auto& nodes = *candidate.getIf<eve::Value::Object>()->at("weapons").getIf<eve::Value::Array>();
    nodes.push_back(nodes.front());
    REQUIRE(!admit(candidate).ok());
    candidate      = base.payload;
    auto& reversed = *candidate.getIf<eve::Value::Object>()->at("weapons").getIf<eve::Value::Array>();
    std::swap(reversed[0], reversed[1]);
    REQUIRE(!admit(candidate).ok());
    candidate    = base.payload;
    auto& unused = *candidate.getIf<eve::Value::Object>()->at("members").getIf<eve::Value::Array>();
    for (auto& row : unused) (*row.getIf<eve::Value::Array>())[2] = eve::Value{};
    REQUIRE(!admit(candidate).ok());
    candidate          = base.payload;
    auto& inconsistent = *candidate.getIf<eve::Value::Object>()->at("weapons").getIf<eve::Value::Array>();
    (*inconsistent[1].getIf<eve::Value::Object>()->at("pool").getIf<eve::Value::Object>())["count"] = 10;
    REQUIRE(!admit(candidate).ok());
    candidate          = base.payload;
    auto& foreignEpoch = *candidate.getIf<eve::Value::Object>()->at("weapons").getIf<eve::Value::Array>();
    (*foreignEpoch.front().getIf<eve::Value::Object>())["epoch"] = epoch.child("other").format();
    REQUIRE(!admit(candidate).ok());
    for (int mode = 0; mode < 4; ++mode) {
        auto envelope = base;
        if (mode == 0) envelope.schemaVersion = eve::SchemaVersion(2);
        if (mode == 1) envelope.revision = eve::Revision::zero();
        if (mode == 2) envelope.type = "other";
        if (mode == 3) envelope.schema = *eve::LogicalId::fromParts("rts", "other");
        auto sealed =
            eve::makeSnapshotEnvelope(envelope.type, envelope.schema, envelope.schemaVersion, envelope.instanceId,
                                      envelope.revision, envelope.tick, envelope.payload, restoreHash)
                .expect("metadata reseal");
        REQUIRE(!eve::rts::RTSWeaponCapture::parse(std::move(sealed), epoch, restoreHash).ok());
    }
    REQUIRE(!eve::rts::RTSWeaponCapture::parse(base, epoch.child("wrong"), restoreHash).ok());
    RestoredRTSSpawn target(saved.session);
    const auto       before = target.domain->canonicalStateJson().expect("target baseline");
    candidate               = base.payload;
    auto& subset            = *candidate.getIf<eve::Value::Object>()->at("members").getIf<eve::Value::Array>();
    subset.erase(std::find_if(subset.begin(), subset.end(), [&](const auto& row) {
        return (*row.template getIf<eve::Value::Array>())[0].template getIf<std::string>() &&
               *(*row.template getIf<eve::Value::Array>())[0].template getIf<std::string>() == saved.unarmed.format();
    }));
    auto parsed = admit(candidate).expect("valid partial data is not topology permission");
    REQUIRE(!eve::rts::PreparedRTSWeaponRestore::prepare(*target.domain, parsed).ok());
    auto moved = std::move(parsed);
    REQUIRE(!parsed.seal(eve::Revision(1), eve::SimulationTick(1), restoreHash).ok());
    REQUIRE_EQ(target.domain->canonicalStateJson().expect("target unchanged"), before);
    REQUIRE_EQ(rows<eve::weapon::WeaponEntity>(*target.table), std::size_t(0));
    REQUIRE_EQ(rows<eve::weapon::AmmoPoolEntity>(*target.table), std::size_t(0));
}
}  // namespace test_rts_spawn
