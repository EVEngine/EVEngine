#include "definitions_editor/EditorDefinitionTarget.h"
#include "definitions_editor/EditorDefinitionForm.h"
#include "schema/SchemaTypes.h"

#include "definitions/Definitions.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::definitions_editing;
using namespace eve::editing;

TEST_CASE("editor.definition.schema_form_exposes_field_metadata_and_reversible_edits") {
    DefinitionDocument document("weapon", "sword", 2);
    REQUIRE(document.setJson("{\"damage\":10,\"rarity\":\"common\"}").ok());
    eve::schema::SchemaDefinition schema;
    schema.id = "weapon"; schema.version = 2;
    eve::schema::FieldDefinition damage;
    damage.name = "damage"; damage.title = "Damage"; damage.type = eve::schema::ValueType::Number;
    damage.required = true; damage.minimum = 0.0; damage.maximum = 1000.0; damage.defaultJson = "1";
    eve::schema::FieldDefinition rarity;
    rarity.name = "rarity"; rarity.type = eve::schema::ValueType::String;
    rarity.enumValues = {"common", "rare", "legendary"}; rarity.defaultJson = "\"common\"";
    schema.fields = {damage, rarity};
    DefinitionSchemaFormTarget form(&document, &schema);
    SelectionSnapshot selected;
    selected.items.push_back({SelectionDomain::Asset, TargetId(document.targetId()), StableId("sword"), "weapon"});
    CHECK_EQ(form.schema(selected).properties.size(), 2U);
    CHECK(form.validate().empty());
    auto operation = form.makeSet(selected, PropertyPath("damage"), 25.0, PropertySetMode::Absolute);
    REQUIRE(operation.ok()); CHECK(document.applyDomainOperation(operation.value()).ok());
    const auto editedDamage = form.read(selected, PropertyPath("damage"));
    CHECK_EQ(*editedDamage.value.getIf<double>(), 25.0);
    DomainOperation undo = operation.value(); undo.payload = operation.value().inverse;
    CHECK(document.applyDomainOperation(undo).ok());
    const auto restoredDamage = form.read(selected, PropertyPath("damage"));
    CHECK_EQ(*restoredDamage.value.getIf<double>(), 10.0);
    CHECK_EQ(static_cast<int>(form.makeSet(selected, PropertyPath("rarity"), "invalid",
                                           PropertySetMode::Absolute).code()),
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
    EditorValue broken = snapshot;
    auto* object = broken.getIf<EditorValue::Object>();
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
    DefinitionRuntimePublisher publisher;
    REQUIRE(publisher.publish(document, &registry).ok());
    auto stored = registry.resolve("item", "potion");
    REQUIRE(stored.ok());
    CHECK_EQ(stored.value().get().json, R"({"heal":10})");

    REQUIRE(document.setJson(R"({"heal":20})").ok());
    CHECK_EQ(static_cast<int>(publisher.publish(document, &registry).code()),
             static_cast<int>(EditorStatus::Rejected));
    REQUIRE(publisher.publish(document, &registry, true).ok());
    stored = registry.resolve("item", "potion");
    REQUIRE(stored.ok());
    CHECK_EQ(stored.value().get().json, R"({"heal":20})");
}
