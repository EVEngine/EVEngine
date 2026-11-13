// eve-test-policy:
// {"schema":"evengine.test-policy","version":1,"module":"spawn_editing","default":"module-composition"}
#include <functional>
#include <stdexcept>
#include "editing/EditingAuthority.h"
#include "map/spawn/editing/SpawnDocument.h"
#include "map/spawn/editing/SpawnTemplateDocument.h"
#include "spawn/SpawnMap.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_spawn_editing {
namespace {
struct Content final : eve::spawn::ITemplateResolver, eve::spawn::IUnitDefinitionResolver {
    std::function<void()> callback;
    eve::Result<void>     validate(const eve::DefinitionRef& unit) const override {
        if (unit.format() != "rpg.unit:wolf")
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "Missing wolf"));
        return eve::Result<void>::success();
    }
    eve::Result<eve::spawn::CompiledTemplate> resolve(const eve::DefinitionRef& reference) const override {
        if (callback) callback();
        if (reference.format() != "spawn.template:camp")
            return eve::Result<eve::spawn::CompiledTemplate>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "Missing template"));
        auto value = eve::Value::fromJson(R"({"schema":"eve.spawn-template","version":1,"id":"camp",
          "content":{"kind":"fixed","members":[{"id":"wolf","unit":{"type":"rpg.unit","id":"wolf"},"count":1}]},
          "schedule":{"kind":"once"},"limits":{"alive":1,"total":{"kind":"unlimited"},"budget_group":"hostiles"},
          "placement":{"kind":"random_in_region","constraints":[{"kind":"walkable"}],"attempts_per_unit":2},
          "batch_policy":"atomic","failure":{"placement_blocked":{"kind":"retry","delay_seconds":1}}})")
                         .expect("template");
        return eve::spawn::compileTemplate(value, *this);
    }
};
eve::Value source() {
    return eve::Value::fromJson(R"({"schema":"eve.map-spawns","version":1,"map":"forest",
      "budgets":[{"id":"hostiles","alive_limit":10}],"instances":[{"id":"north",
      "template":{"type":"spawn.template","id":"camp"},"region":{"kind":"circle","space":"world2d","center":[0,0],"radius":3},
      "activation":{"kind":"map_loaded"},"inactive_policy":"pause","unload_policy":"persist"}]})")
        .expect("sidecar");
}
eve::Value changed(double radius) {
    auto value = source();
    value.find("instances")->at(0).find("region")->set("radius", radius);
    return value;
}
std::unique_ptr<eve::spawn_editing::SpawnDocumentTarget> document(const std::shared_ptr<Content>& content) {
    return eve::spawn_editing::SpawnDocumentTarget::create(eve::editing::TargetId("forest-spawns"), source(), content,
                                                           content)
        .expect("document");
}
eve::editing::TransactionSpec transaction(std::string id, const eve::spawn_editing::SpawnDocumentTarget& target) {
    eve::editing::TransactionSpec spec;
    spec.id           = eve::editing::TransactionId(std::move(id));
    spec.target       = target.targetId();
    spec.baseRevision = target.revision();
    return spec;
}
}  // namespace
TEST_CASE("Spawn authoring document validates canonical assets and retains real content ownership") {
    auto content = std::make_shared<Content>();
    auto doc     = document(content);
    REQUIRE(doc->queryCapability(eve::spawn_editing::ISpawnDocumentEditTarget::editingCapabilityId()) != nullptr);
    REQUIRE(doc->queryCapability(eve::editing::CapabilityId("unknown")) == nullptr);
    auto snapshot = doc->source();
    snapshot.set("runtime_alive", 1);
    REQUIRE(!doc->makeReplace(snapshot).ok());
    REQUIRE_EQ(doc->revision(), std::uint64_t(1));
    REQUIRE(doc->dirtyRegion().empty());
    auto pinned     = doc->compiled();
    auto capability = doc->capability<eve::spawn_editing::ISpawnDocumentEditTarget>();
    REQUIRE(capability.has_value());
    auto op      = capability->get().makeReplace(changed(8)).expect("plan through actual editing capability");
    auto witness = std::weak_ptr<Content>(content);
    content.reset();
    REQUIRE(!witness.expired());
    doc->applyDomainOperation(op).expect("retained actual resolvers");
    REQUIRE_EQ(std::get<eve::spawn::CircleRegion>(doc->compiled().instances()[0].region).radius, 8.0);
    REQUIRE_EQ(std::get<eve::spawn::CircleRegion>(pinned.instances()[0].region).radius, 3.0);
    REQUIRE(!doc->applyDomainOperation(op).ok());
    const auto revision = doc->revision();
    doc->clearDirtyRegion();
    auto noOp = doc->makeReplace(doc->source()).expect("same source");
    doc->applyDomainOperation(noOp).expect("no change");
    REQUIRE_EQ(doc->revision(), revision);
    REQUIRE(doc->dirtyRegion().empty());
    doc.reset();
    REQUIRE(witness.expired());
    REQUIRE(!eve::spawn_editing::SpawnDocumentTarget::create({}, source(), {}, {}).ok());
}
TEST_CASE("Spawn authoring operations use real staged authority undo and redo without runtime state") {
    auto                                       content = std::make_shared<Content>();
    auto                                       doc     = document(content);
    eve::editing::LocalWorldAuthority          authority(doc.get());
    auto                                       operation = doc->makeReplace(changed(7)).expect("operation");
    std::vector<eve::editing::DomainOperation> operations{operation};
    auto plan    = authority.preflight(transaction("edit-one", *doc), operations).expect("preflight");
    auto receipt = authority.commit(plan).expect("commit through actual authority");
    REQUIRE_EQ(doc->revision(), std::uint64_t(2));
    REQUIRE_EQ(std::get<eve::spawn::CircleRegion>(doc->compiled().instances()[0].region).radius, 7.0);
    authority.compensate(receipt).expect("actual inverse transaction");
    REQUIRE_EQ(doc->source().toJson().expect("undo source"), source().toJson().expect("original source"));
    auto redo = authority.preflight(transaction("redo-one", *doc), operations).expect("redo plan at new revision");
    authority.commit(redo).expect("redo source edit");
    REQUIRE_EQ(doc->revision(), std::uint64_t(4));
    REQUIRE(!doc->source().find("runtime_alive"));
    auto stale = doc->cloneDomainState();
    doc->applyDomainOperation(doc->makeReplace(changed(9)).expect("another edit")).expect("mutate original");
    REQUIRE(!doc->commitDomainState(std::move(stale)).ok());
    auto foreign     = document(content);
    auto wrongFamily = foreign->cloneDomainState();
    REQUIRE(!doc->commitDomainState(std::move(wrongFamily)).ok());
}
TEST_CASE("Spawn authoring rejects forged operations and complete batches retain the old source") {
    auto content = std::make_shared<Content>();
    auto doc     = document(content);
    auto first   = doc->makeReplace(changed(5)).expect("first valid edit");
    auto staging = doc->cloneDomainState();
    staging->applyDomainOperation(first).expect("independent stage");
    auto* typed = dynamic_cast<eve::spawn_editing::SpawnDocumentTarget*>(staging.get());
    REQUIRE(typed != nullptr);
    auto       second  = typed->makeReplace(changed(9)).expect("second edit");
    const auto invalid = changed(-1).toJson().expect("invalid radius source");
    second.payload.getIf<eve::editing::Value::Object>()->at("after")  = eve::editing::Value(invalid);
    second.inverse.getIf<eve::editing::Value::Object>()->at("before") = eve::editing::Value(invalid);
    eve::editing::LocalWorldAuthority authority(doc.get());
    auto                              plan =
        authority.preflight(transaction("bad-batch", *doc), std::vector<eve::editing::DomainOperation>{first, second})
            .expect("preflight public operations");
    REQUIRE(!authority.commit(plan).ok());
    REQUIRE_EQ(doc->revision(), std::uint64_t(1));
    REQUIRE(doc->dirtyRegion().empty());
    REQUIRE_EQ(doc->source().toJson().expect("unchanged"), source().toJson().expect("original"));
    auto malformed                                                      = first;
    malformed.inverse.getIf<eve::editing::Value::Object>()->at("after") = eve::editing::Value("{}");
    REQUIRE(!doc->applyDomainOperation(malformed).ok());
    malformed        = first;
    malformed.target = eve::editing::TargetId("other");
    REQUIRE(!doc->applyDomainOperation(malformed).ok());
    malformed      = first;
    malformed.type = "spawn.document.replace.v2";
    REQUIRE(!doc->applyDomainOperation(malformed).ok());
    malformed = first;
    malformed.payload.getIf<eve::editing::Value::Object>()->emplace("unknown", true);
    REQUIRE(!doc->applyDomainOperation(malformed).ok());
}
TEST_CASE("Spawn authoring copies edits before resolver callbacks and rejects mutation reentry") {
    auto content      = std::make_shared<Content>();
    auto doc          = document(content);
    auto operation    = doc->makeReplace(changed(6)).expect("valid prior operation");
    auto input        = changed(7);
    bool blocked      = false;
    content->callback = [&] {
        input   = changed(99);
        blocked = !doc->applyDomainOperation(operation).ok() && !doc->makeReplace(source()).ok();
    };
    auto copied = doc->makeReplace(input).expect("copy input before callbacks");
    REQUIRE(blocked);
    content->callback = {};
    doc->applyDomainOperation(copied).expect("planned original radius");
    REQUIRE_EQ(std::get<eve::spawn::CircleRegion>(doc->compiled().instances()[0].region).radius, 7.0);
    auto       retry    = doc->makeReplace(changed(8)).expect("next valid edit");
    const auto revision = doc->revision();
    content->callback   = [] { throw std::runtime_error("resolver unavailable"); };
    auto failed         = doc->applyDomainOperation(retry);
    REQUIRE(!failed.ok());
    REQUIRE(failed.status().primaryDiagnostic()->code() == eve::DiagnosticCode::CallbackFailure);
    REQUIRE_EQ(doc->revision(), revision);
    content->callback = {};
    doc->applyDomainOperation(retry).expect("same edit retries after provider repair");
}
TEST_CASE("Spawn template authoring edits weighted content and waves through real authority") {
    auto units = std::make_shared<Content>();
    auto original =
        units->resolve(eve::DefinitionRef::parse("spawn.template:camp").expect("reference")).expect("template");
    auto source = eve::Value::fromJson(original.canonicalJson()).expect("source");
    auto doc =
        eve::spawn_editing::SpawnTemplateDocumentTarget::create(eve::editing::TargetId("template"), source, units)
            .expect("template document");
    REQUIRE(doc->queryCapability(eve::spawn_editing::ISpawnTemplateDocumentEditTarget::editingCapabilityId()) !=
            nullptr);
    REQUIRE(doc->queryCapability(eve::spawn_editing::ISpawnDocumentEditTarget::editingCapabilityId()) == nullptr);
    auto weighted = source;
    weighted.getIf<eve::Value::Object>()->erase("failure");
    weighted.set("content", eve::Value::fromJson(R"({"kind":"weighted","selection":"per_unit","entries":[
        {"unit":{"type":"rpg.unit","id":"wolf"},"weight":3}]})")
                                .expect("weighted"));
    weighted.set(
        "schedule",
        eve::Value::fromJson(
            R"({"kind":"interval","period_seconds":2,"initial_delay_seconds":0,"count_per_batch":1,"on_missed":"skip"})")
            .expect("interval"));
    auto                              operation = doc->makeReplace(weighted).expect("weighted edit");
    eve::editing::LocalWorldAuthority authority(doc.get());
    eve::editing::TransactionSpec     spec;
    spec.id           = eve::editing::TransactionId("weighted");
    spec.target       = doc->targetId();
    spec.baseRevision = doc->revision();
    auto plan    = authority.preflight(spec, std::vector<eve::editing::DomainOperation>{operation}).expect("preflight");
    auto receipt = authority.commit(plan).expect("weighted commit");
    REQUIRE_EQ(std::get<eve::spawn::WeightedContent>(doc->compiled().definition().content).entries[0].weight, 3u);
    auto pinned = doc->compiled();
    authority.compensate(receipt).expect("undo");
    REQUIRE_EQ(doc->compiled().canonicalJson(), original.canonicalJson());
    REQUIRE_EQ(std::get<eve::spawn::WeightedContent>(pinned.definition().content).entries[0].weight, 3u);
    auto waves = source;
    waves.getIf<eve::Value::Object>()->erase("content");
    waves.set("schedule", eve::Value::fromJson(R"({"kind":"waves","waves":[
        {"id":"first","delay_seconds":0,"members":[{"id":"wolf","unit":{"type":"rpg.unit","id":"wolf"},"count":1}],"complete_when":"all_members_dead"},
        {"id":"second","delay_seconds":2,"members":[{"id":"wolf","unit":{"type":"rpg.unit","id":"wolf"},"count":1}],"complete_when":"all_members_dead"}]})")
                              .expect("waves"));
    doc->applyDomainOperation(doc->makeReplace(waves).expect("wave edit")).expect("wave publication");
    REQUIRE_EQ(std::get<eve::spawn::WaveSchedule>(doc->compiled().definition().schedule).waves.size(), std::size_t(2));
    auto invalid = waves;
    invalid.find("schedule")->find("waves")->at(0).find("members")->at(0).set("count", 0);
    REQUIRE(!doc->makeReplace(invalid).ok());
    REQUIRE_EQ(doc->revision(), std::uint64_t(4));
    auto map         = document(units);
    auto wrongKind   = map->makeReplace(changed(5)).expect("map operation");
    wrongKind.target = doc->targetId();
    REQUIRE(!doc->applyDomainOperation(wrongKind).ok());
    auto stage = doc->cloneDomainState();
    doc->applyDomainOperation(doc->makeReplace(source).expect("reset")).expect("reset edit");
    REQUIRE(!doc->commitDomainState(std::move(stage)).ok());
}
TEST_CASE("Spawn template authoring owns validators and isolates callback mutation and failure") {
    struct Units final : eve::spawn::IUnitDefinitionResolver {
        std::function<void()> callback;
        eve::Result<void>     validate(const eve::DefinitionRef&) const override {
            if (callback) callback();
            return eve::Result<void>::success();
        }
    };
    auto units = std::make_shared<Units>();
    auto original =
        Content().resolve(eve::DefinitionRef::parse("spawn.template:camp").expect("reference")).expect("original");
    auto source = eve::Value::fromJson(original.canonicalJson()).expect("source");
    auto doc =
        eve::spawn_editing::SpawnTemplateDocumentTarget::create(eve::editing::TargetId("template"), source, units)
            .expect("document");
    auto next = source;
    next.find("content")->find("members")->at(0).set("count", 2);
    next.find("limits")->set("alive", 2);
    auto noop       = doc->makeReplace(source).expect("noop");
    units->callback = [&] {
        next.find("content")->find("members")->at(0).set("count", 99);
        REQUIRE(!doc->applyDomainOperation(noop).ok());
        REQUIRE(!doc->makeReplace(source).ok());
    };
    auto operation  = doc->makeReplace(next).expect("copied before callback");
    units->callback = [] { throw std::runtime_error("validator unavailable"); };
    auto failed     = doc->applyDomainOperation(operation);
    REQUIRE(!failed.ok());
    REQUIRE_EQ(doc->revision(), std::uint64_t(1));
    REQUIRE(doc->dirtyRegion().empty());
    units->callback              = {};
    std::weak_ptr<Units> witness = units;
    units.reset();
    REQUIRE(!witness.expired());
    doc->applyDomainOperation(operation).expect("retry same operation");
    REQUIRE_EQ(std::get<eve::spawn::FixedContent>(doc->compiled().definition().content).members[0].count, 2u);
    auto foreign = eve::spawn_editing::SpawnTemplateDocumentTarget::create(doc->targetId(), source, witness.lock())
                       .expect("foreign same id");
    REQUIRE(!doc->commitDomainState(foreign->cloneDomainState()).ok());
    foreign.reset();
    doc.reset();
    REQUIRE(witness.expired());
}
TEST_CASE("Spawn structured property forms plan canonical map and template edits without direct mutation") {
    using namespace eve::editing;
    auto              content = std::make_shared<Content>();
    auto              map     = document(content);
    SelectionSnapshot selected;
    selected.items.push_back(
        {SelectionDomain::Asset, map->targetId(), StableId(map->targetId().value()), "spawn-document"});
    auto* provider = static_cast<IPropertyProvider*>(map->queryCapability(IPropertyProvider::editingCapabilityId()));
    REQUIRE(provider != nullptr);
    REQUIRE(provider->currentRevision(selected).ok());
    auto schema = provider->schema(selected);
    REQUIRE(schema.find(PropertyPath("instances"))->type == PropertyType::Array);
    REQUIRE(hasPropertyFlag(schema.find(PropertyPath("version"))->flags, PropertyFlag::ReadOnly));
    auto instances = provider->read(selected, PropertyPath("instances"));
    REQUIRE(instances.state == PropertyReadState::Value);
    auto edited = eve::editing::toPresentationValue(instances.value);
    edited.at(0).find("region")->set("radius", 12);
    auto operation =
        provider->makeSet(selected, PropertyPath("instances"), toEditingValue(edited), PropertySetMode::Absolute)
            .expect("form plan");
    REQUIRE_EQ(map->revision(), std::uint64_t(1));
    LocalWorldAuthority authority(map.get());
    auto plan = authority.preflight(transaction("form-edit", *map), std::vector<DomainOperation>{operation})
                    .expect("form preflight");
    auto receipt = authority.commit(plan).expect("form commit");
    REQUIRE_EQ(std::get<eve::spawn::CircleRegion>(map->compiled().instances()[0].region).radius, 12.0);
    authority.compensate(receipt).expect("form undo");
    REQUIRE_EQ(std::get<eve::spawn::CircleRegion>(map->compiled().instances()[0].region).radius, 3.0);
    edited.at(0).find("region")->set("radius", -1);
    REQUIRE(!provider->makeSet(selected, PropertyPath("instances"), toEditingValue(edited), PropertySetMode::Absolute)
                 .ok());
    REQUIRE(!provider->makeSet(selected, PropertyPath("version"), int64_t(2), PropertySetMode::Absolute).ok());
    REQUIRE(!provider->makeSet(selected, PropertyPath("missing"), int64_t(2), PropertySetMode::Absolute).ok());
    REQUIRE(!provider->makeSet(selected, PropertyPath("map"), "forest", PropertySetMode::Relative).ok());
    REQUIRE(!provider->makeReset(selected, PropertyPath("map")).ok());
    auto wrong          = selected;
    wrong.items[0].item = StableId("wrong");
    REQUIRE(!provider->currentRevision(wrong).ok());
    REQUIRE(provider->schema(wrong).properties.empty());
    REQUIRE(provider->read(wrong, PropertyPath("map")).state == PropertyReadState::Missing);
    REQUIRE(!provider->makeSet(wrong, PropertyPath("map"), "forest", PropertySetMode::Absolute).ok());
    auto initial =
        content->resolve(eve::DefinitionRef::parse("spawn.template:camp").expect("reference")).expect("template");
    auto tpl = eve::spawn_editing::SpawnTemplateDocumentTarget::create(
                   TargetId("camp"), eve::Value::fromJson(initial.canonicalJson()).expect("template source"), content)
                   .expect("template document");
    selected.items = {
        {SelectionDomain::Asset, tpl->targetId(), StableId(tpl->targetId().value()), "spawn-template-document"}};
    auto root = tpl->read(selected, PropertyPath("$source"));
    REQUIRE(root.state == PropertyReadState::Value);
    auto candidate = toPresentationValue(root.value);
    candidate.getIf<eve::Value::Object>()->erase("content");
    candidate.set("schedule", eve::Value::fromJson(R"({"kind":"waves","waves":[
        {"id":"first","delay_seconds":0,"members":[{"id":"wolf","unit":{"type":"rpg.unit","id":"wolf"},"count":1}],"complete_when":"all_members_dead"}]})")
                                  .expect("wave"));
    auto transition =
        tpl->makeSet(selected, PropertyPath("$source"), toEditingValue(candidate), PropertySetMode::Absolute)
            .expect("atomic branch switch");
    tpl->applyDomainOperation(transition).expect("apply branch switch");
    REQUIRE_EQ(std::get<eve::spawn::WaveSchedule>(tpl->compiled().definition().schedule).waves.size(), std::size_t(1));
}
}  // namespace test_spawn_editing
