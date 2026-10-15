#include "definitions/Definitions.h"
#include "definitions/editing/DefinitionForm.h"
#include "definitions/editing/DefinitionTarget.h"
#include "editor/queue/editing/QueueInspector.h"
#include "orders/CommandQueue.h"
#include "production/Production.h"
#include "schema/SchemaTypes.h"
#include "social/SocialGraph.h"
#include "social/editing/SocialDocument.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// editor_definition_target.cpp
using namespace eve::definitions_editing;
using namespace eve::editing;

TEST_CASE("editor.definition.schema_form_exposes_field_metadata_and_reversible_edits") {
    DefinitionDocument document("weapon", "sword", 2);
    REQUIRE(document.setJson("{\"damage\":10,\"rarity\":\"common\"}").ok());
    eve::schema::SchemaDefinition schema;
    schema.id      = "weapon";
    schema.version = 2;
    eve::schema::FieldDefinition damage;
    damage.name        = "damage";
    damage.title       = "Damage";
    damage.type        = eve::schema::ValueType::Number;
    damage.required    = true;
    damage.minimum     = 0.0;
    damage.maximum     = 1000.0;
    damage.defaultJson = "1";
    eve::schema::FieldDefinition rarity;
    rarity.name        = "rarity";
    rarity.type        = eve::schema::ValueType::String;
    rarity.enumValues  = {"common", "rare", "legendary"};
    rarity.defaultJson = "\"common\"";
    schema.fields      = {damage, rarity};
    DefinitionSchemaFormTarget form(&document, &schema);
    SelectionSnapshot          selected;
    selected.items.push_back({SelectionDomain::Asset, TargetId(document.targetId()), StableId("sword"), "weapon"});
    CHECK_EQ(form.schema(selected).properties.size(), 2U);
    CHECK(form.validate().empty());
    auto operation = form.makeSet(selected, PropertyPath("damage"), 25.0, PropertySetMode::Absolute);
    REQUIRE(operation.ok());
    CHECK(document.applyDomainOperation(operation.value()).ok());
    const auto editedDamage = form.read(selected, PropertyPath("damage"));
    CHECK_EQ(*editedDamage.value.getIf<double>(), 25.0);
    DomainOperation undo = operation.value();
    undo.payload         = operation.value().inverse;
    CHECK(document.applyDomainOperation(undo).ok());
    const auto restoredDamage = form.read(selected, PropertyPath("damage"));
    CHECK_EQ(*restoredDamage.value.getIf<double>(), 10.0);
    CHECK_EQ(
        static_cast<int>(form.makeSet(selected, PropertyPath("rarity"), "invalid", PropertySetMode::Absolute).code()),
        static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.definition.validates_schema_and_cross_references") {
    DefinitionDocument document("weapon", "iron_sword", 2);
    REQUIRE(document.setJson(R"({"damage":12,"effect":"burn"})").ok());
    REQUIRE(document.setReferences({{"/effect", "effect", "burn", true}}).ok());
    const auto valid = document.validate(
        [&](const std::string& type, int version, const std::string&) {
            CHECK_EQ(type, "weapon");
            CHECK_EQ(version, 2);
            return std::vector<EditorDiagnostic>{};
        },
        [](const std::string& type, const std::string& id) { return type == "effect" && id == "burn"; });
    CHECK(valid.empty());

    const auto unresolved = document.validate({}, [](const std::string&, const std::string&) { return false; });
    CHECK_EQ(unresolved.size(), size_t{1});
    CHECK_EQ(diagnosticRule(unresolved.front()).value(), "editor.definition.reference-not-found");
}

TEST_CASE("editor.definition.snapshot_load_is_atomic") {
    DefinitionDocument source("item", "potion");
    REQUIRE(source.setJson(R"({"heal":10})").ok());
    REQUIRE(source.setReferences({{"/upgrade", "item", "greater_potion", false}}).ok());
    const EditorValue snapshot = source.snapshotValue();

    DefinitionDocument restored("placeholder", "placeholder");
    REQUIRE(restored.loadSnapshot(snapshot).ok());
    CHECK_EQ(restored.snapshotValue(), snapshot);
    const std::uint64_t before = restored.revision();
    EditorValue         broken = snapshot;
    auto*               object = broken.getIf<EditorValue::Object>();
    REQUIRE(object);
    (*object)["definitionVersion"] = int64_t{0};
    CHECK_EQ(static_cast<int>(restored.loadSnapshot(broken).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(restored.revision(), before);
    CHECK_EQ(restored.snapshotValue(), snapshot);
}

TEST_CASE("editor.definition.publishes_to_real_versioned_registry") {
    DefinitionDocument document("item", "potion");
    REQUIRE(document.setJson(R"({"heal":10})").ok());
    eve::definitions::DefinitionRegistry registry;
    DefinitionRuntimePublisher           publisher;
    REQUIRE(publisher.publish(document, &registry).ok());
    auto stored = registry.resolve("item", "potion");
    REQUIRE(stored.ok());
    CHECK_EQ(stored.value().get().json, R"({"heal":10})");

    REQUIRE(document.setJson(R"({"heal":20})").ok());
    CHECK_EQ(static_cast<int>(publisher.publish(document, &registry).code()), static_cast<int>(EditorStatus::Rejected));
    REQUIRE(publisher.publish(document, &registry, true).ok());
    stored = registry.resolve("item", "potion");
    REQUIRE(stored.ok());
    CHECK_EQ(stored.value().get().json, R"({"heal":20})");
}

// editor_queue_inspector.cpp
using namespace eve::queue_editing;

TEST_CASE("editor.queue.inspector_copies_orders_without_borrowing_runtime_storage") {
    eve::orders::CommandQueue queue;
    auto                      move = queue.append("move", 5, 10.0);
    auto                      wait = queue.append("wait", 1, 0.0);
    REQUIRE(move.value());
    REQUIRE(wait.value());
    RuntimeQueueInspector inspector;
    auto                  snapshot = inspector.capture(queue);
    CHECK_EQ(snapshot.domain, std::string("orders"));
    CHECK_EQ(snapshot.items.size(), 2U);
    CHECK(!snapshot.events.empty());
    auto filtered = inspector.capture(queue, "active", "move");
    CHECK_EQ(filtered.items.size(), 1U);
    queue.clear();
    CHECK_EQ(snapshot.items.size(), 2U);
}

TEST_CASE("editor.queue.inspector_filters_production_owner_and_state") {
    eve::production::WorkQueue queue;
    auto task = queue.enqueue("factory", "craft", "sword", eve::Value(eve::Value::Object{}), 3.0, 4);
    REQUIRE(task.value());
    RuntimeQueueInspector inspector;
    auto                  snapshot = inspector.capture(queue, "factory", "running", "craft");
    CHECK_EQ(snapshot.domain, std::string("production"));
    CHECK_EQ(snapshot.items.size(), 1U);
    CHECK_EQ(snapshot.items.front().product, std::string("sword"));
    CHECK_EQ(snapshot.items.front().duration, 3.0);
    CHECK(snapshot.diagnostics.empty());
}

// editor_social_document.cpp
using namespace eve::social_editing;
using namespace eve::editing;

TEST_CASE("editor.social.graph_edges_are_reversible_validated_and_publishable") {
    SocialDocumentTarget document("diplomacy");
    for (const SocialEntityRecord& entity : std::vector<SocialEntityRecord>{
             {StableId("unit"), "Unit", "actor"}, {StableId("player"), "Player", "faction"}}) {
        auto operation = document.makeSetEntity(entity);
        REQUIRE(operation.ok());
        CHECK(document.applyDomainOperation(operation.value()).ok());
    }
    SocialEdgeRecord owner{StableId("owns"), StableId("unit"), StableId("player"), "owner", {}, 1.0};
    auto             set = document.makeSetEdge(owner);
    REQUIRE(set.ok());
    CHECK(document.applyDomainOperation(set.value()).ok());
    CHECK_EQ(static_cast<int>(document.makeDeleteEntity(StableId("unit")).code()),
             static_cast<int>(EditorStatus::Conflict));
    eve::social::SocialGraph runtime;
    CHECK(SocialRuntimeApplier().apply(document, &runtime).ok());
    CHECK_EQ(runtime.ownerOf("unit"), std::string("player"));

    DomainOperation undo = set.value();
    undo.type            = set.value().inverseType;
    undo.payload         = set.value().inverse;
    CHECK(document.applyDomainOperation(undo).ok());
    CHECK(document.edges().empty());
    CHECK(document.makeDeleteEntity(StableId("unit")).ok());
}

TEST_CASE("editor.social.snapshot_rejects_dangling_or_duplicate_semantics_atomically") {
    SocialDocumentTarget source("source");
    CHECK(source.applyDomainOperation(source.makeSetEntity({StableId("a"), "A", "actor"}).value()).ok());
    CHECK(source.applyDomainOperation(source.makeSetEntity({StableId("b"), "B", "actor"}).value()).ok());
    CHECK(
        source
            .applyDomainOperation(
                source.makeSetEdge({StableId("likes"), StableId("a"), StableId("b"), "relation", "likes", 0.8}).value())
            .ok());
    SocialDocumentTarget loaded("loaded");
    CHECK(loaded.loadSnapshot(source.snapshotValue()).ok());
    CHECK_EQ(loaded.edges().size(), 1U);
    auto  invalid = source.snapshotValue();
    auto* root    = invalid.getIf<EditorValue::Object>();
    REQUIRE(root);
    auto* entities = root->at("entities").getIf<EditorValue::Array>();
    REQUIRE(entities);
    entities->clear();
    CHECK_EQ(static_cast<int>(loaded.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(loaded.entities().size(), 2U);
}
