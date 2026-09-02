#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/EditorAuthority.h"
#include "material_editing/MaterialTarget.h"
#include "editor/EditorTransactionService.h"

#include <tuple>

using namespace eve::material_editing;
using namespace eve::editing;
using eve::editor::LocalTransactionBackend;

namespace {

SelectionSnapshot materialSelection(const MaterialDocumentTarget& target) {
    SelectionSnapshot selection;
    selection.channel = "asset";
    SelectionItem item;
    item.domain = SelectionDomain::Asset;
    item.target = TargetId(target.targetId());
    item.item = StableId(target.targetId().value());
    item.type = "graphics.material";
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

EditorResult<TransactionReceipt> commitMaterial(MaterialDocumentTarget& target,
                                                LocalTransactionBackend& transactions,
                                                const DomainOperation& operation,
                                                const char* transactionId) {
    TransactionSpec specification;
    specification.id = TransactionId(transactionId);
    specification.label = "Edit material";
    specification.target = TargetId(target.targetId());
    specification.baseRevision = target.revision();
    auto begun = transactions.begin(std::move(specification));
    if (!begun.ok())
        return eve::editing::failed<TransactionReceipt>(begun.code(), RuleId("test.material.begin"),
                                                       "Could not begin material transaction");
    auto appended = transactions.append(operation);
    if (!appended.ok()) {
        [[maybe_unused]] const auto rolledBack = transactions.rollback();
        return eve::editing::failed<TransactionReceipt>(appended.code(), RuleId("test.material.append"),
                                                       "Could not append material operation");
    }
    return transactions.commit();
}

}  // namespace

TEST_CASE("editor.material.schema_matches_core_material_authoring_surface") {
    MaterialDocumentTarget target("metal-panel");
    const SelectionSnapshot selection = materialSelection(target);
    const PropertySchema schema = target.schema(selection);
    CHECK_EQ(schema.typeId, std::string("graphics.material"));
    CHECK(schema.find(PropertyPath("shading.metallic")) != nullptr);
    CHECK(schema.find(PropertyPath("shading.roughness")) != nullptr);
    CHECK(schema.find(PropertyPath("textures.normal")) != nullptr);
    CHECK(schema.find(PropertyPath("surface.blend")) != nullptr);
    CHECK(schema.find(PropertyPath("shadow.receive")) != nullptr);
    CHECK(target.queryCapability(CapabilityId("eve.editor.target.material-properties")) != nullptr);
}

TEST_CASE("editor.material.properties_are_validated_and_reversible") {
    MaterialDocumentTarget target("metal-panel");
    LocalWorldAuthority authority(&target);
    LocalTransactionBackend transactions(&authority);
    const SelectionSnapshot selection = materialSelection(target);

    auto invalid = target.makeSet(selection, PropertyPath("shading.metallic"), 2.0,
                                  PropertySetMode::Absolute);
    CHECK_EQ(static_cast<int>(invalid.code()), static_cast<int>(EditorStatus::Rejected));

    auto change = target.makeSet(selection, PropertyPath("shading.metallic"), 0.8,
                                 PropertySetMode::Absolute);
    REQUIRE(change.ok());
    REQUIRE(commitMaterial(target, transactions, change.value(), "material.metallic").ok());
    CHECK(target.read(selection, PropertyPath("shading.metallic")).value == EditorValue(0.8));
    REQUIRE(transactions.undo().ok());
    CHECK(target.read(selection, PropertyPath("shading.metallic")).value == EditorValue(0.0));
    REQUIRE(transactions.redo().ok());
    CHECK(target.read(selection, PropertyPath("shading.metallic")).value == EditorValue(0.8));
}

TEST_CASE("editor.material.snapshot_roundtrip_and_cross_field_diagnostics") {
    MaterialDocumentTarget source("source");
    LocalWorldAuthority authority(&source);
    LocalTransactionBackend transactions(&authority);
    const SelectionSnapshot selection = materialSelection(source);

    for (const auto& [path, value, id] : {
             std::tuple{PropertyPath("shading.model"), EditorValue("custom"), "material.model"},
             {PropertyPath("surface.mode"), EditorValue("masked"), "material.surface"},
             {PropertyPath("textures.height"), EditorValue("asset://height.png"), "material.height"}}) {
        auto operation = source.makeSet(selection, path, value, PropertySetMode::Absolute);
        REQUIRE(operation.ok());
        REQUIRE(commitMaterial(source, transactions, operation.value(), id).ok());
    }
    CHECK_EQ(source.validate().size(), static_cast<std::size_t>(3));

    MaterialDocumentTarget restored("restored");
    REQUIRE(restored.loadSnapshot(source.snapshotValue()).ok());
    const SelectionSnapshot restoredSelection = materialSelection(restored);
    CHECK(restored.read(restoredSelection, PropertyPath("shading.model")).value == EditorValue("custom"));
    CHECK(restored.read(restoredSelection, PropertyPath("textures.height")).value ==
          EditorValue("asset://height.png"));

    EditorValue::Object invalidRoot;
    invalidRoot["schemaVersion"] = 99;
    invalidRoot["properties"] = EditorValue::Object{};
    CHECK_EQ(static_cast<int>(restored.loadSnapshot(EditorValue(std::move(invalidRoot))).code()),
             static_cast<int>(EditorStatus::Rejected));
}
