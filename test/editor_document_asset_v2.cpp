#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/EditorAssetDatabase.h"
#include "editor/EditorAuthority.h"
#include "editor/EditorDocumentService.h"
#include "editor/EditorSceneTarget.h"
#include "editor/EditorTransactionService.h"

#include <string>

using namespace eve::editor;

TEST_CASE("editor.v2.asset_database_import_query_dependencies_and_page_generation") {
    MemoryAssetDatabase database;
    ImportCoordinator   imports(&database);
    ImportProduct       texture;
    texture.record.guid       = AssetGuid("asset-texture");
    texture.record.logicalUri = "content://Textures/Coaster.png";
    texture.record.typeId     = "texture";
    texture.record.sourceUri  = "source://Coaster.png";
    texture.record.importerId = "image.importer";
    texture.record.tags       = {"park", "metal"};
    CHECK(imports.publish(std::move(texture)).accepted());

    ImportProduct material;
    material.record.guid       = AssetGuid("asset-material");
    material.record.logicalUri = "content://Materials/Coaster.evmaterial";
    material.record.typeId     = "material";
    material.record.sourceUri  = "source://Coaster.material";
    material.record.importerId = "material.importer";
    material.dependencies.push_back(
        {material.record.guid, AssetGuid("asset-texture"), DependencyKind::Hard, PropertyPath("baseColor")});
    CHECK(imports.publish(std::move(material)).accepted());

    AssetQuery query;
    query.text     = "coaster";
    auto firstPage = database.query(query, 0, 1);
    CHECK(firstPage.accepted());
    CHECK_EQ(firstPage.value->values.size(), static_cast<std::size_t>(1));
    CHECK(firstPage.value->hasMore);
    const std::uint64_t generation = firstPage.value->generation;
    auto                secondPage = database.query(query, firstPage.value->nextOffset, 1, generation);
    CHECK(secondPage.accepted());
    CHECK_EQ(secondPage.value->values.size(), static_cast<std::size_t>(1));
    CHECK_EQ(database.dependencies(AssetGuid("asset-material")).size(), static_cast<std::size_t>(1));
    CHECK_EQ(database.dependencies(AssetGuid("asset-texture"), true).size(), static_cast<std::size_t>(1));

    AssetRecord extra;
    extra.guid       = AssetGuid("asset-extra");
    extra.logicalUri = "content://Other/Extra.asset";
    extra.typeId     = "generic";
    CHECK(database.publish(std::move(extra)).accepted());
    auto expired = database.query(query, firstPage.value->nextOffset, 1, generation);
    CHECK_EQ(static_cast<int>(expired.status), static_cast<int>(EditorStatus::Conflict));
}

TEST_CASE("editor.v2.document_save_ticket_preserves_newer_dirty_edits") {
    MemoryAtomicDocumentStore store;
    DocumentService           documents(&store);
    DocumentKey               key{DocumentKind::Scene, AssetGuid("scene-asset")};
    auto                      opened = documents.open(key, "Park", "content://World/Park.evscene");
    CHECK(opened.accepted());
    const DocumentId id = opened.value->id;

    EditorValue first(EditorValue::Object{{"version", EditorValue(1)}});
    auto        editOne = documents.edit(id, first);
    CHECK(editOne.accepted());
    auto ticket = documents.requestSave(id);
    CHECK(ticket.accepted());

    EditorValue second(EditorValue::Object{{"version", EditorValue(2)}});
    auto        editTwo = documents.edit(id, second, editOne.value->revision.edit);
    CHECK(editTwo.accepted());
    auto savedOldRevision = documents.executeSave(*ticket.value);
    CHECK(savedOldRevision.accepted());
    CHECK(savedOldRevision.value->dirty());
    CHECK_EQ(savedOldRevision.value->revision.saved, editOne.value->revision.edit);
    CHECK_EQ(documents.content(id).value, std::optional<EditorValue>(second));

    auto currentTicket = documents.requestSave(id);
    CHECK(documents.executeSave(*currentTicket.value).accepted());
    CHECK(!documents.snapshot(id).value->dirty());
    CHECK(documents.close(id).accepted());

    DocumentService reopenedService(&store);
    auto            reopened = reopenedService.open(key, "Park", "content://World/Park.evscene");
    CHECK(reopened.accepted());
    CHECK(reopenedService.content(reopened.value->id).value == std::optional<EditorValue>(second));
}

TEST_CASE("editor.v2.document_save_detects_external_revision_conflict") {
    MemoryAtomicDocumentStore store;
    DocumentService           documents(&store);
    DocumentKey               key{DocumentKind::Material, AssetGuid("material-asset")};
    auto                      opened = documents.open(key, "Material", "content://Materials/Test.evmaterial");
    const DocumentId          id     = opened.value->id;
    CHECK(documents.edit(id, EditorValue("local-v1")).accepted());
    auto firstTicket = documents.requestSave(id);
    CHECK(documents.executeSave(*firstTicket.value).accepted());

    CHECK(documents.edit(id, EditorValue("local-v2")).accepted());
    auto       staleTicket = documents.requestSave(id);
    const auto persisted   = store.read("content://Materials/Test.evmaterial");
    CHECK(store
              .compareAndSwap("content://Materials/Test.evmaterial", 1, persisted.value->contentHash,
                              EditorValue("external-v2"))
              .accepted());
    auto conflict = documents.executeSave(*staleTicket.value);
    CHECK_EQ(static_cast<int>(conflict.status), static_cast<int>(EditorStatus::Conflict));
    CHECK_EQ(static_cast<int>(conflict.value->state), static_cast<int>(DocumentState::Conflict));
}

TEST_CASE("editor.v2.import_place_save_and_reopen_headless_loop") {
    MemoryAssetDatabase database;
    ImportCoordinator   imports(&database);
    ImportProduct       product;
    product.record.guid       = AssetGuid("asset-coaster-blueprint");
    product.record.logicalUri = "content://Park/Coaster.blueprint";
    product.record.typeId     = "park.placeable";
    product.record.sourceUri  = "source://Coaster.blueprint";
    product.record.importerId = "park.blueprint.importer";
    CHECK(imports.publish(std::move(product)).accepted());
    AssetRecord placeable = *database.find(AssetGuid("asset-coaster-blueprint")).value;

    SceneDocumentTarget      scene("park-scene");
    LocalWorldAuthority      authority(&scene);
    LocalTransactionBackend  transactions(&authority);
    CreateSceneObjectRequest request;
    request.id        = ObjectId("placed-coaster");
    request.name      = placeable.guid.value();
    request.transform = {10.0, 0.0, 20.0};
    auto operation    = ScenePlacementToolLogic().plan(scene, request);
    CHECK(operation.accepted());
    TransactionSpec specification;
    specification.id           = TransactionId("place-imported-asset");
    specification.label        = "Place imported asset";
    specification.target       = TargetId(scene.targetId());
    specification.baseRevision = scene.revision();
    CHECK(transactions.begin(std::move(specification)).accepted());
    CHECK(transactions.append(*operation.value).accepted());
    CHECK(transactions.commit().accepted());

    MemoryAtomicDocumentStore store;
    DocumentService           documents(&store);
    DocumentKey               key{DocumentKind::Scene, AssetGuid("park-scene-asset")};
    auto                      opened = documents.open(key, "Park Scene", "content://World/Park.evscene");
    CHECK(documents.edit(opened.value->id, scene.snapshotValue()).accepted());
    auto ticket = documents.requestSave(opened.value->id);
    CHECK(documents.executeSave(*ticket.value).accepted());
    const EditorValue savedScene = *documents.content(opened.value->id).value;
    CHECK(documents.close(opened.value->id).accepted());

    DocumentService reopened(&store);
    auto            reopenedDocument = reopened.open(key, "Park Scene", "content://World/Park.evscene");
    CHECK(reopenedDocument.accepted());
    CHECK(reopened.content(reopenedDocument.value->id).value == std::optional<EditorValue>(savedScene));
}

TEST_CASE("editor.v2.asset_database_indexes_and_pages_100k_records") {
    MemoryAssetDatabase database;
    constexpr int       count = 100000;
    for (int index = 0; index < count; ++index) {
        AssetRecord record;
        record.guid       = AssetGuid("asset-" + std::to_string(index));
        record.logicalUri = "content://Bulk/asset-" + std::to_string(index);
        record.typeId     = index % 2 == 0 ? "bulk.even" : "bulk.odd";
        REQUIRE(database.publish(std::move(record)).accepted());
    }
    CHECK_EQ(database.generation(), static_cast<std::uint64_t>(count));
    AssetQuery query;
    query.typeIds = {"bulk.odd"};
    auto page     = database.query(query, 1000, 128, database.generation());
    REQUIRE(page.accepted());
    CHECK_EQ(page.value->values.size(), static_cast<std::size_t>(128));
    CHECK(page.value->hasMore);
    CHECK(database.findByUri("content://Bulk/asset-99999").accepted());
}
