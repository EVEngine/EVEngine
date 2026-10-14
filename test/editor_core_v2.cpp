#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include "editor/EditCommand.h"
#include "editor/EditorAssetDatabase.h"
#include "editor/EditorAuthority.h"
#include "editor/EditorCommandService.h"
#include "editor/EditorCurveDocument.h"
#include "editor/EditorDocumentService.h"
#include "editor/EditorEnvironmentBrowser.h"
#include "editor/EditorEventTimeline.h"
#include "editor/EditorHostProfile.h"
#include "editor/EditorIds.h"
#include "editor/EditorSession.h"
#include "editor/EditorTransactionService.h"
#include "editor/EditorValue.h"
#include "editor/EvaAssetDatabaseProjection.h"
#include "map/level/editing/FieldTargets.h"
#include "map/level/editing/TileBuffer.h"
#include "scene/editor/EditorSceneTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// editor_core_v2.cpp
using namespace eve::editor;
using namespace eve::level_editing;

static_assert(!std::is_convertible_v<CommandId, ToolId>);
static_assert(!std::is_convertible_v<TargetId, CommandId>);

TEST_CASE("editor.v2.strong_ids_and_values") {
    const CommandId command("scene.object.create");
    const CommandId same("scene.object.create");
    const CommandId other("scene.object.delete");
    CHECK(command == same);
    CHECK(command != other);
    CHECK_EQ(command.value(), std::string("scene.object.create"));

    EditorValue::Object root;
    root["name"]     = "Station";
    root["position"] = EditorValue::Array{1.0, 2.0, 3.0};
    root["visible"]  = true;
    const EditorValue value(std::move(root));
    CHECK_EQ(static_cast<int>(value.type()), static_cast<int>(EditorValue::Type::Object));
    CHECK(value.isWithinLimits(4, 16, 128));
    CHECK(!value.isWithinLimits(1, 16, 128));
    CHECK(!value.isWithinLimits(4, 2, 128));
    CHECK(!value.isWithinLimits(4, 16, 4));
}

TEST_CASE("editor.v2.idsUseCommonUuidAdapterWithLegacyParse") {
    const auto legacy = CommandId::parse("scene.object.create");
    REQUIRE(legacy.has_value());
    CHECK(!legacy->isCanonicalUuid());
    CHECK_EQ(legacy->value(), std::string("scene.object.create"));

    const auto projected = canonicalUuid(*legacy);
    const auto canonical = CommandId::fromUuid(projected);
    CHECK(canonical.isCanonicalUuid());
    CHECK_EQ(canonical.uuid(), projected);
    CHECK_EQ(canonical.canonicalFormat(), projected.format());

    const auto parsed = CommandId::parse("01020304-0506-0708-090a-0b0c0d0e0f10");
    REQUIRE(parsed.has_value());
    CHECK(parsed->isCanonicalUuid());
    CHECK_EQ(parsed->value(), std::string("01020304-0506-0708-090a-0b0c0d0e0f10"));
}

TEST_CASE("editor.v2.host_profile_is_an_execution_boundary") {
    const CommandId place("scene.asset.place");
    const CommandId importAsset("asset.import");
    HostProfile     runtime = HostProfile::runtimeBuilder();
    CHECK(!runtime.allowsCommand(place));
    runtime.allowCommand(place);
    CHECK(runtime.allowsCommand(place));
    CHECK(!runtime.allowsCommand(importAsset));
    CHECK(runtime.hasFeatures(HostFeature::RuntimeWorld));
    CHECK(!runtime.hasFeatures(HostFeature::SourceAssets));

    HostProfile developer = HostProfile::developer();
    CHECK(developer.allowsCommand(importAsset));
    CHECK(developer.hasFeatures(HostFeature::SourceAssets | HostFeature::BuildCook));
    developer.denyCommand(importAsset);
    CHECK(!developer.allowsCommand(importAsset));
}

TEST_CASE("editor.v2.command_registry_filters_and_executes") {
    EditorCommandService service;
    CommandDescriptor    descriptor;
    descriptor.id               = CommandId("scene.rename");
    descriptor.ownerModule      = "scene";
    descriptor.displayName      = "Rename";
    descriptor.requiredFeatures = HostFeature::RuntimeWorld;

    int  calls      = 0;
    auto registered = service.registerCommand(descriptor, [&](const CommandContext&, const EditorValue& payload) {
        ++calls;
        return eve::editing::applied<EditorValue>(payload);
    });
    CHECK(registered.ok());
    CHECK_EQ(service.revision(), static_cast<uint64_t>(1));

    auto duplicate = service.registerCommand(descriptor, [](const CommandContext&, const EditorValue&) {
        return eve::editing::applied<EditorValue>(EditorValue{});
    });
    CHECK_EQ(static_cast<int>(duplicate.code()), static_cast<int>(EditorStatus::Rejected));

    HostProfile    runtime = HostProfile::runtimeBuilder();
    CommandContext context;
    context.profile = &runtime;
    auto denied     = service.execute(descriptor.id, context, EditorValue("New Name"));
    CHECK_EQ(static_cast<int>(denied.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(calls, 0);

    runtime.allowCommand(descriptor.id);
    auto executed = service.execute(descriptor.id, context, EditorValue("New Name"));
    CHECK(executed.ok());
    CHECK_EQ(calls, 1);
    CHECK(executed.ok());
    CHECK_EQ(*executed.value().getIf<std::string>(), std::string("New Name"));
    CHECK_EQ(service.commands(runtime).size(), static_cast<size_t>(1));

    CHECK_EQ(service.unregisterOwner("scene"), static_cast<size_t>(1));
    CHECK(service.find(descriptor.id) == nullptr);
}

TEST_CASE("editor.v2.command_registry_gates_automation_and_exceptions") {
    EditorCommandService service;
    CommandDescriptor    descriptor;
    descriptor.id                = CommandId("scene.dangerous");
    descriptor.ownerModule       = "scene";
    descriptor.automationAllowed = false;
    CHECK(service
              .registerCommand(descriptor,
                               [](const CommandContext&, const EditorValue&) {
                                   throw std::runtime_error("boom");
                                   return eve::editing::applied<EditorValue>(EditorValue{});
                               })
              .ok());

    HostProfile    developer = HostProfile::developer();
    CommandContext context;
    context.profile = &developer;
    context.source  = CommandSource::Automation;
    auto automation = service.execute(descriptor.id, context, {});
    CHECK_EQ(static_cast<int>(automation.code()), static_cast<int>(EditorStatus::Rejected));

    context.source = CommandSource::Api;
    auto failure   = service.execute(descriptor.id, context, {});
    CHECK_EQ(static_cast<int>(failure.code()), static_cast<int>(EditorStatus::Failed));
    CHECK_EQ(failure.diagnostics().size(), static_cast<size_t>(1));
    CHECK(failure.diagnostics().front().message().find("boom") != std::string::npos);
}

TEST_CASE("editor.v2.session_wraps_command_in_legacy_transaction") {
    TileBuffer           buffer(2, 2);
    TileBufferTarget     target("tiles", &buffer);
    EditorCommandService service;

    CommandDescriptor descriptor;
    descriptor.id               = CommandId("map.paint-one");
    descriptor.ownerModule      = "map";
    descriptor.displayName      = "Paint One Tile";
    descriptor.requiredFeatures = HostFeature::RuntimeWorld;
    CHECK(service
              .registerCommand(
                  descriptor,
                  [](const CommandContext& context, const EditorValue& payload) {
                      auto* after = payload.getIf<int64_t>();
                      if (!context.session || !after)
                          return eve::editing::failed<EditorValue>(EditorStatus::Rejected, RuleId("map.paint.invalid"),
                                                                   "Paint command requires an integer value");
                      auto command = std::make_unique<IntFieldEditCommand>("Paint One Tile", context.session->target());
                      if (!command->record(0, 0, static_cast<int>(*after)) ||
                          !context.session->execute(std::move(command)))
                          return eve::editing::failed<EditorValue>(EditorStatus::Failed, RuleId("map.paint.failed"),
                                                                   "Tile edit could not be applied");
                      return eve::editing::applied<EditorValue>(payload);
                  })
              .ok());

    EditorSession session;
    HostProfile   runtime = HostProfile::runtimeBuilder();
    runtime.allowCommand(descriptor.id);
    session.setHostProfile(std::move(runtime));
    session.setCommandService(&service);
    session.bindTarget(&target);

    auto result = session.executeCommand(descriptor.id, EditorValue(7));
    CHECK(result.ok());
    CHECK_EQ(buffer.getGid(0, 0), 7);
    CHECK(session.transactions().canUndo());
    CHECK(session.transactions().undo());
    CHECK_EQ(buffer.getGid(0, 0), 0);

    session.setHostProfile(HostProfile::runtimeBuilder());
    auto denied = session.executeCommand(descriptor.id, EditorValue(9));
    CHECK_EQ(static_cast<int>(denied.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(buffer.getGid(0, 0), 0);
}

// editor_curve_document.cpp
using namespace eve::editor;

TEST_CASE("editor.curve.timeline_is_reversible_sampled_and_serializable") {
    EditorCurveDocument curve("particle-size-color");
    auto                start = curve.makeSetKey({StableId("start"), 0.0, 0.0, 0.0, 0.0, "linear"});
    auto                end   = curve.makeSetKey({StableId("end"), 1.0, 2.0, 0.0, 0.0, "linear"});
    REQUIRE(start.ok());
    REQUIRE(end.ok());
    CHECK(curve.applyDomainOperation(start.value()).ok());
    CHECK(curve.applyDomainOperation(end.value()).ok());
    auto black = curve.makeSetStop({StableId("black"), 0.0, {0.0, 0.0, 0.0, 1.0}});
    auto white = curve.makeSetStop({StableId("white"), 1.0, {1.0, 1.0, 1.0, 1.0}});
    REQUIRE(black.ok());
    REQUIRE(white.ok());
    CHECK(curve.applyDomainOperation(black.value()).ok());
    CHECK(curve.applyDomainOperation(white.value()).ok());
    CHECK_EQ(curve.sampleCurve(0.5), 1.0);
    CHECK_EQ(curve.sampleGradient(0.5)[0], 0.5);
    const auto preview = curve.preview(16);
    CHECK_EQ(static_cast<int>(preview.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(preview.curveSamples.size(), 16U);
    auto remove = curve.makeDeleteKey(StableId("end"));
    REQUIRE(remove.ok());
    CHECK(curve.applyDomainOperation(remove.value()).ok());
    DomainOperation undo = remove.value();
    undo.type            = remove.value().inverseType;
    undo.payload         = remove.value().inverse;
    CHECK(curve.applyDomainOperation(undo).ok());
    CHECK_EQ(curve.keys().size(), 2U);
    EditorCurveDocument restored("particle-size-color");
    CHECK(restored.loadSnapshot(curve.snapshotValue()).ok());
    CHECK_EQ(restored.snapshotValue(), curve.snapshotValue());
    CHECK_EQ(static_cast<int>(curve.preview(5000, 4096).status), static_cast<int>(EditorStatus::Rejected));
}

// editor_document_asset_v2.cpp
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
    CHECK(imports.publish(std::move(texture)).ok());

    ImportProduct material;
    material.record.guid       = AssetGuid("asset-material");
    material.record.logicalUri = "content://Materials/Coaster.evmaterial";
    material.record.typeId     = "material";
    material.record.sourceUri  = "source://Coaster.material";
    material.record.importerId = "material.importer";
    material.dependencies.push_back(
        {material.record.guid, AssetGuid("asset-texture"), DependencyKind::Hard, PropertyPath("baseColor")});
    CHECK(imports.publish(std::move(material)).ok());

    AssetQuery query;
    query.text     = "coaster";
    auto firstPage = database.query(query, 0, 1);
    CHECK(firstPage.ok());
    CHECK_EQ(firstPage.value().values.size(), static_cast<std::size_t>(1));
    CHECK(firstPage.value().hasMore);
    const std::uint64_t generation = firstPage.value().generation;
    auto                secondPage = database.query(query, firstPage.value().nextOffset, 1, generation);
    CHECK(secondPage.ok());
    CHECK_EQ(secondPage.value().values.size(), static_cast<std::size_t>(1));
    CHECK_EQ(database.dependencies(AssetGuid("asset-material")).size(), static_cast<std::size_t>(1));
    CHECK_EQ(database.dependencies(AssetGuid("asset-texture"), true).size(), static_cast<std::size_t>(1));

    AssetRecord extra;
    extra.guid       = AssetGuid("asset-extra");
    extra.logicalUri = "content://Other/Extra.asset";
    extra.typeId     = "generic";
    CHECK(database.publish(std::move(extra)).ok());
    auto expired = database.query(query, firstPage.value().nextOffset, 1, generation);
    CHECK_EQ(static_cast<int>(expired.code()), static_cast<int>(EditorStatus::Conflict));
}

TEST_CASE("editor.v2.document_save_ticket_preserves_newer_dirty_edits") {
    MemoryAtomicDocumentStore store;
    DocumentService           documents(&store);
    DocumentKey               key{DocumentKind::Scene, AssetGuid("scene-asset")};
    auto                      opened = documents.open(key, "Park", "content://World/Park.evscene");
    CHECK(opened.ok());
    const DocumentId id = opened.value().id;

    EditorValue first(EditorValue::Object{{"version", EditorValue(1)}});
    auto        editOne = documents.edit(id, first);
    CHECK(editOne.ok());
    auto ticket = documents.requestSave(id);
    CHECK(ticket.ok());

    EditorValue second(EditorValue::Object{{"version", EditorValue(2)}});
    auto        editTwo = documents.edit(id, second, editOne.value().revision.edit);
    CHECK(editTwo.ok());
    auto savedOldRevision = documents.executeSave(ticket.value());
    CHECK(savedOldRevision.ok());
    CHECK(savedOldRevision.value().dirty());
    CHECK_EQ(savedOldRevision.value().revision.saved, editOne.value().revision.edit);
    CHECK_EQ(documents.content(id).value(), second);

    auto currentTicket = documents.requestSave(id);
    CHECK(documents.executeSave(currentTicket.value()).ok());
    CHECK(!documents.snapshot(id).value().dirty());
    CHECK(documents.close(id).ok());

    DocumentService reopenedService(&store);
    auto            reopened = reopenedService.open(key, "Park", "content://World/Park.evscene");
    CHECK(reopened.ok());
    CHECK(reopenedService.content(reopened.value().id).value() == second);
}

TEST_CASE("editor.v2.document_save_detects_external_revision_conflict") {
    MemoryAtomicDocumentStore store;
    DocumentService           documents(&store);
    DocumentKey               key{DocumentKind::Material, AssetGuid("material-asset")};
    auto                      opened = documents.open(key, "Material", "content://Materials/Test.evmaterial");
    const DocumentId          id     = opened.value().id;
    CHECK(documents.edit(id, EditorValue("local-v1")).ok());
    auto firstTicket = documents.requestSave(id);
    CHECK(documents.executeSave(firstTicket.value()).ok());

    CHECK(documents.edit(id, EditorValue("local-v2")).ok());
    auto       staleTicket = documents.requestSave(id);
    const auto persisted   = store.read("content://Materials/Test.evmaterial");
    CHECK(store
              .compareAndSwap("content://Materials/Test.evmaterial", 1, persisted.value().contentHash,
                              EditorValue("external-v2"))
              .ok());
    auto conflict = documents.executeSave(staleTicket.value());
    CHECK_EQ(static_cast<int>(conflict.code()), static_cast<int>(EditorStatus::Conflict));
    CHECK_EQ(static_cast<int>(conflict.value().state), static_cast<int>(DocumentState::Conflict));
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
    CHECK(imports.publish(std::move(product)).ok());
    AssetRecord placeable = database.find(AssetGuid("asset-coaster-blueprint")).value();

    SceneDocumentTarget      scene("park-scene");
    LocalWorldAuthority      authority(&scene);
    LocalTransactionBackend  transactions(&authority);
    CreateSceneObjectRequest request;
    request.id        = ObjectId("placed-coaster");
    request.name      = placeable.guid.value();
    request.transform = {10.0, 0.0, 20.0};
    auto operation    = ScenePlacementToolLogic().plan(scene, request);
    CHECK(operation.ok());
    TransactionSpec specification;
    specification.id           = TransactionId("place-imported-asset");
    specification.label        = "Place imported asset";
    specification.target       = TargetId(scene.targetId());
    specification.baseRevision = scene.revision();
    CHECK(transactions.begin(std::move(specification)).ok());
    CHECK(transactions.append(operation.value()).ok());
    CHECK(transactions.commit().ok());

    MemoryAtomicDocumentStore store;
    DocumentService           documents(&store);
    DocumentKey               key{DocumentKind::Scene, AssetGuid("park-scene-asset")};
    auto                      opened = documents.open(key, "Park Scene", "content://World/Park.evscene");
    CHECK(documents.edit(opened.value().id, scene.snapshotValue()).ok());
    auto ticket = documents.requestSave(opened.value().id);
    CHECK(documents.executeSave(ticket.value()).ok());
    const EditorValue savedScene = documents.content(opened.value().id).value();
    CHECK(documents.close(opened.value().id).ok());

    DocumentService reopened(&store);
    auto            reopenedDocument = reopened.open(key, "Park Scene", "content://World/Park.evscene");
    CHECK(reopenedDocument.ok());
    CHECK(reopened.content(reopenedDocument.value().id).value() == savedScene);
}

TEST_CASE("editor.v2.asset_database_indexes_and_pages_100k_records") {
    MemoryAssetDatabase database;
    constexpr int       count = 100000;
    for (int index = 0; index < count; ++index) {
        AssetRecord record;
        record.guid       = AssetGuid("asset-" + std::to_string(index));
        record.logicalUri = "content://Bulk/asset-" + std::to_string(index);
        record.typeId     = index % 2 == 0 ? "bulk.even" : "bulk.odd";
        REQUIRE(database.publish(std::move(record)).ok());
    }
    CHECK_EQ(database.generation(), static_cast<std::uint64_t>(count));
    AssetQuery query;
    query.typeIds = {"bulk.odd"};
    auto page     = database.query(query, 1000, 128, database.generation());
    REQUIRE(page.ok());
    CHECK_EQ(page.value().values.size(), static_cast<std::size_t>(128));
    CHECK(page.value().hasMore);
    CHECK(database.findByUri("content://Bulk/asset-99999").ok());
}

// editor_environment_browser.cpp
using namespace eve::editor;

namespace {
AssetRecord environment(const char* id, const char* uri, const char* layout, int width, int height, int faces = 1) {
    AssetRecord record;
    record.guid       = AssetGuid(id);
    record.logicalUri = uri;
    record.typeId     = "environment-map";
    record.sourceUri  = std::string("source/") + id;
    record.sourceHash = "hash";
    record.importerId = "environment-test";
    record.artifacts  = {std::string("artifact/") + id};
    record.metadata   = EditorValue::Object{
          {"layout", layout}, {"width", int64_t{width}}, {"height", int64_t{height}}, {"faceCount", int64_t{faces}}};
    return record;
}
}  // namespace

TEST_CASE("editor.environment.browser_pages_and_rejects_stale_generation") {
    MemoryAssetDatabase database;
    CHECK(database.publish(environment("sky-a", "env/sky-a", "equirectangular", 2048, 1024)).ok());
    const auto              generation = database.generation();
    EnvironmentAssetBrowser browser(&database);
    auto                    page = browser.query("sky", 0, 20, generation);
    REQUIRE(page.ok());
    CHECK_EQ(page.value().values.size(), 1U);
    CHECK_EQ(page.value().values.front().layout, std::string("equirectangular"));
    CHECK(database.publish(environment("sky-b", "env/sky-b", "cubemap", 512, 512, 6)).ok());
    CHECK_EQ(static_cast<int>(browser.query({}, 0, 20, generation).code()), static_cast<int>(EditorStatus::Conflict));
}

TEST_CASE("editor.environment.browser_surfaces_invalid_import_metadata") {
    MemoryAssetDatabase database;
    CHECK(database.publish(environment("broken", "env/broken", "cubemap", 512, 512, 5)).ok());
    EnvironmentAssetBrowser browser(&database);
    auto                    selected = browser.select(AssetGuid("broken"));
    CHECK_EQ(static_cast<int>(selected.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK(!selected.diagnostics().empty());
}

// editor_eva_asset_projection.cpp
using namespace eve::editor;

TEST_CASE("editor.asset.evaProjectionPublishesOneGenerationAndRejectsAtomically") {
    MemoryAssetDatabase     database;
    eve::asset::EvaManifest manifest;
    manifest.packageId      = *eve::PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    manifest.packageName    = "terrain-kit";
    manifest.packageVersion = "1.0.0";
    auto terrain            = eve::AssetRef::parse("asset://018f6f22-2490-7ad2-bf58-4f1dbca31041").takeValue();
    auto material           = eve::AssetRef::parse("asset://018f6f22-2490-7ad2-bf58-4f1dbca31042").takeValue();
    manifest.assets         = {
        {terrain, "eve.terrain", eve::SchemaVersion(1), "assets/terrain.json", "sha256:" + std::string(64, '1'), {}},
        {material,
                 "eve.terrain-material",
                 eve::SchemaVersion(1),
                 "assets/material.json",
                 "sha256:" + std::string(64, '2'),
                 {"ground"}},
    };
    manifest.dependencies.push_back({terrain,
                                     material,
                                     eve::asset::EvaDependencyKind::RuntimeRequired,
                                     "material",
                                     {},
                                     "eve.terrain-material",
                                     {}});

    auto published = publishEvaAssetProjection(database, manifest, "project://Assets/terrain.eva", "unity/1");
    REQUIRE(published.ok());
    CHECK_EQ(published.value().size(), std::size_t(2));
    CHECK_EQ(database.generation(), std::uint64_t(1));
    CHECK_EQ(database.dependencies(AssetGuid(terrain.id().format())).size(), std::size_t(1));

    auto conflicting            = manifest;
    conflicting.assets[1].asset = terrain;
    auto rejected = publishEvaAssetProjection(database, conflicting, "project://Assets/bad.eva", "unity/1");
    CHECK(!rejected.ok());
    CHECK_EQ(database.generation(), std::uint64_t(1));
    CHECK_EQ(database.find(AssetGuid(material.id().format())).value().sourceUri,
             std::string("project://Assets/terrain.eva#assets/material.json"));
}

// editor_event_timeline.cpp
using namespace eve::editor;

TEST_CASE("editor.timeline_filters_correlations_and_rejects_stale_pages") {
    EditorEventTimeline timeline;
    CHECK(timeline
              .append({0, 10, "network", "connected", "peer-1", "session-a", DiagnosticSeverity::Info,
                       EditorValue::Object{}})
              .ok());
    CHECK(timeline
              .append({0, 12, "network", "timeout", "peer-2", "session-b", DiagnosticSeverity::Warning,
                       EditorValue::Object{{"retry", int64_t{2}}}})
              .ok());
    EditorTimelineQuery filter;
    filter.domains         = {"network"};
    filter.correlation     = "session-b";
    filter.minimumSeverity = DiagnosticSeverity::Warning;
    auto page              = timeline.query(filter, 0, 10);
    REQUIRE(page.ok());
    CHECK_EQ(page.value().values.size(), 1U);
    const auto generation = page.value().generation;
    CHECK(timeline.append({0, 13, "filesystem", "modified", "asset", {}, DiagnosticSeverity::Info, {}}).ok());
    CHECK_EQ(static_cast<int>(timeline.query(filter, 0, 10, generation).code()),
             static_cast<int>(EditorStatus::Conflict));
}

TEST_CASE("editor.timeline_capacity_drops_oldest_and_payload_is_bounded") {
    EditorEventTimeline timeline;
    CHECK(timeline.setCapacity(2).ok());
    CHECK(timeline.append({0, 1, "a", "one", {}, {}, DiagnosticSeverity::Info, {}}).ok());
    CHECK(timeline.append({0, 2, "a", "two", {}, {}, DiagnosticSeverity::Info, {}}).ok());
    CHECK(timeline.append({0, 3, "a", "three", {}, {}, DiagnosticSeverity::Info, {}}).ok());
    auto page = timeline.query({}, 0, 10);
    REQUIRE(page.ok());
    CHECK_EQ(page.value().values.size(), 2U);
    CHECK_EQ(page.value().droppedEvents, 1U);
    CHECK_EQ(page.value().values.front().type, std::string("two"));
    CHECK_EQ(
        static_cast<int>(
            timeline.append({0, 4, "a", "large", {}, {}, DiagnosticSeverity::Info, EditorValue::Array{1.0, 2.0}}, 2)
                .code()),
        static_cast<int>(EditorStatus::Rejected));
}
