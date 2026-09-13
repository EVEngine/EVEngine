#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>

TEST_CASE("editor.actionTimeline.scriptUsesCanonicalTransactionsAndWorkspace") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        workspace <- editor.newWorkspace("test.combat", "Test Combat Editor");
        timelineAsset <- {
            schema="eve.action.timeline", schemaVersion=1,
            actionId="test:light-attack", durationNs=1000000000,
            animationUri="asset://test/attack.glb#Attack",
            metadata={},
            tracks=[{
                id="test-track:gameplay", label="Gameplay", kind="gameplay",
                muted=false, locked=false, notifies=[],
                states=[{
                    id="test-state:hitbox", type="combat:hitbox-window",
                    startNs=200000000, endNs=500000000,
                    payload={hitbox="weapon.main"}
                }]
            }]
        };
        created <- eve.ActionEditorModule().create("test.asset.light-attack", timelineAsset);
        actionEditor <- created.value;
        previewHostAvailable <- actionEditor.hasPreviewHost();
        previewRefresh <- actionEditor.refreshPreview();
        configured <- actionEditor.configureWorkspace(workspace);
        workspacePanelCount <- workspace.getPanelCount();
        viewport <- actionEditor.setViewport(800.0, 36.0, 120.0);
        beforeStart <- actionEditor.getStateStart("test-state:hitbox");
        itemCenterX <- (actionEditor.getItemMinX(0) + actionEditor.getItemMaxX(0)) * 0.5;
        itemCenterY <- (actionEditor.getItemMinY(0) + actionEditor.getItemMaxY(0)) * 0.5;
        pointerDownResult <- actionEditor.pointerDown(itemCenterX, itemCenterY, false);
        pointerMoveResult <- actionEditor.pointerMove(itemCenterX + 80.0);
        pointerUpResult <- actionEditor.pointerUp(itemCenterX + 80.0);
        movedStart <- actionEditor.getStateStart("test-state:hitbox");
        revisionAfterDrag <- actionEditor.getRevision();
        undoResult <- actionEditor.undo();
        restoredStart <- actionEditor.getStateStart("test-state:hitbox");
        inspectorTiming <- actionEditor.setItemTiming("test-state:hitbox", 0.1, 0.6);
        inspectorStart <- actionEditor.getItemStart("test-state:hitbox");
        inspectorEnd <- actionEditor.getItemEnd("test-state:hitbox");
        invalidInspectorTiming <- actionEditor.setItemTiming("test-state:hitbox", 0.7, 0.2);
        detailsResult <- actionEditor.editItemDetails(
            "test-state:hitbox", "combat:hitbox-window", "{\"hitbox\":\"weapon.alt\"}");
        payloadAfterDetails <- actionEditor.getItemPayloadJson("test-state:hitbox");
        shapeMismatch <- actionEditor.editItemDetails(
            "test-state:hitbox", "combat:damage", "{\"damageType\":\"slash\",\"amount\":2}");
        invalidPayload <- actionEditor.editItemDetails(
            "test-state:hitbox", "combat:hitbox-window", "{}");
        disableResult <- actionEditor.setItemEnabled("test-state:hitbox", false);
        disabledValue <- actionEditor.getItemEnabled("test-state:hitbox");
        restoreEnabledResult <- actionEditor.undo();
        restoredEnabledValue <- actionEditor.getItemEnabled("test-state:hitbox");
        removeItemResult <- actionEditor.removeItem("test-state:hitbox");
        itemCountAfterRemove <- actionEditor.getItemCount();
        restoreRemovedResult <- actionEditor.undo();
        itemCountAfterRestore <- actionEditor.getItemCount();
        missResult <- actionEditor.pointerDown(700.0, 20.0, false);
        draggingAfterMiss <- actionEditor.isDragging();
        seekResult <- actionEditor.seekX(700.0);
        emptySpaceSeekTime <- actionEditor.getPreviewTime();
        actionEditor.seekSeconds(0.0);
        actionEditor.play();
        advanced <- actionEditor.update(1.0);
        previewEventCount <- actionEditor.getEventCount();
        snapshotResult <- actionEditor.snapshot();
        instantTypeCount <- actionEditor.getInsertableTypeCount(false);
        stateTypeCount <- actionEditor.getInsertableTypeCount(true);
        damageTypeIndex <- -1;
        invulnerabilityTypeIndex <- -1;
        for (local i = 0; i < instantTypeCount; ++i)
            if (actionEditor.getInsertableType(false, i) == "combat:damage") damageTypeIndex = i;
        for (local i = 0; i < stateTypeCount; ++i)
            if (actionEditor.getInsertableType(true, i) == "combat:invulnerability-window")
                invulnerabilityTypeIndex = i;
        damageTypeLabel <- actionEditor.getInsertableTypeLabel(false, damageTypeIndex);
        actionEditor.seekSeconds(0.7);
        insertedNotify <- actionEditor.addNotifyAtCursor(
            "test-track:gameplay", "combat:damage", "{\"damageType\":\"slash\",\"amount\":3}");
        insertedState <- actionEditor.addStateAtCursor(
            "test-track:gameplay", "combat:invulnerability-window", 0.1, "{}");
        invalidInsertedNotify <- actionEditor.addNotifyAtCursor(
            "test-track:gameplay", "combat:damage", "{}");
        itemCountAfterInsert <- actionEditor.getItemCount();
        initialTrackLocked <- actionEditor.getTrackLocked(0);
        lockTrack <- actionEditor.setTrackLocked("test-track:gameplay", true);
        lockedTrackValue <- actionEditor.getTrackLocked(0);
        unlockTrack <- actionEditor.setTrackLocked("test-track:gameplay", false);
        renameTrack <- actionEditor.renameTrack("test-track:gameplay", "Combat Events");
        renamedTrackLabel <- actionEditor.getTrackLabel(0);
        copyTrackResult <- actionEditor.copyTrack("test-track:gameplay");
        pasteTrackResult <- actionEditor.pasteTrack();
        trackCountAfterPaste <- actionEditor.getTrackCount();
        removePastedTrack <- actionEditor.removeTrack(pasteTrackResult.value);
        trackCountAfterTrackCleanup <- actionEditor.getTrackCount();
        addSectionResult <- actionEditor.addAnimationSection(
            "test-section:attack", "asset://test/attack.glb#Attack", 0.0, 0.4, 0.1);
        editSectionResult <- actionEditor.editAnimationSection(
            "test-section:attack", 0.05, 0.45, 0.08, "asset://test/attack.glb#AttackEdited");
        editSectionSourceResult <- actionEditor.editAnimationSectionSource(
            "test-section:attack", 0.1, 0.3, "linear");
        sectionUri <- actionEditor.getAnimationSectionUri(0);
        sectionStart <- actionEditor.getAnimationSectionStart(0);
        sectionBlend <- actionEditor.getAnimationSectionBlendIn(0);
        sectionSourceStart <- actionEditor.getAnimationSectionSourceStart(0);
        sectionCurve <- actionEditor.getAnimationSectionBlendCurve(0);
        removeSectionResult <- actionEditor.removeAnimationSection("test-section:attack");
        sectionCountAfterCleanup <- actionEditor.getAnimationSectionCount();
        invalidResult <- eve.ActionEditorModule().create("test.asset.invalid", {
            schema="eve.action.timeline", schemaVersion=1,
            actionId="test:invalid", durationNs=-1,
            animationUri="", metadata={}, tracks=[]
        });
    )"));

    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK(!vm.find("previewHostAvailable").toBool());
    CHECK(vm.find("previewRefresh").toTable().get<bool>("ok"));
    CHECK(vm.find("configured").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("workspacePanelCount").toInt(), 4);
    CHECK(vm.find("viewport").toTable().get<bool>("ok"));
    CHECK(vm.find("pointerDownResult").toTable().get<bool>("ok"));
    CHECK(vm.find("pointerMoveResult").toTable().get<bool>("ok"));
    CHECK(vm.find("pointerUpResult").toTable().get<bool>("ok"));
    CHECK(vm.find("movedStart").toFloat() > vm.find("beforeStart").toFloat());
    CHECK_EQ(vm.find("revisionAfterDrag").toInt(), 1);
    CHECK(vm.find("undoResult").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("restoredStart").toFloat() - vm.find("beforeStart").toFloat()), 0.0001F);
    CHECK(vm.find("inspectorTiming").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("inspectorStart").toFloat() - 0.1F), 0.0001F);
    CHECK_LT(std::abs(vm.find("inspectorEnd").toFloat() - 0.6F), 0.0001F);
    CHECK(!vm.find("invalidInspectorTiming").toTable().get<bool>("ok"));
    CHECK(vm.find("detailsResult").toTable().get<bool>("ok"));
    CHECK_NE(vm.find("payloadAfterDetails").toString().find("weapon.alt"), std::string::npos);
    CHECK(!vm.find("shapeMismatch").toTable().get<bool>("ok"));
    CHECK(!vm.find("invalidPayload").toTable().get<bool>("ok"));
    CHECK(vm.find("disableResult").toTable().get<bool>("ok"));
    CHECK(!vm.find("disabledValue").toBool());
    CHECK(vm.find("restoreEnabledResult").toTable().get<bool>("ok"));
    CHECK(vm.find("restoredEnabledValue").toBool());
    CHECK(vm.find("removeItemResult").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("itemCountAfterRemove").toInt(), 0);
    CHECK(vm.find("restoreRemovedResult").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("itemCountAfterRestore").toInt(), 1);
    CHECK(vm.find("missResult").toTable().get<bool>("ok"));
    CHECK(!vm.find("draggingAfterMiss").toBool());
    CHECK(vm.find("seekResult").toTable().get<bool>("ok"));
    CHECK(vm.find("emptySpaceSeekTime").toFloat() > 0.8F);
    CHECK(vm.find("advanced").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("previewEventCount").toInt(), 2);
    CHECK(vm.find("snapshotResult").toTable().get<bool>("ok"));
    CHECK(vm.find("instantTypeCount").toInt() > 0);
    CHECK(vm.find("stateTypeCount").toInt() > 0);
    CHECK(vm.find("damageTypeIndex").toInt() >= 0);
    CHECK(vm.find("invulnerabilityTypeIndex").toInt() >= 0);
    CHECK_NE(vm.find("damageTypeLabel").toString().find("Apply Damage"), std::string::npos);
    CHECK(vm.find("insertedNotify").toTable().get<bool>("ok"));
    CHECK(vm.find("insertedState").toTable().get<bool>("ok"));
    CHECK(!vm.find("invalidInsertedNotify").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("itemCountAfterInsert").toInt(), 3);
    CHECK(!vm.find("initialTrackLocked").toBool());
    CHECK(vm.find("lockTrack").toTable().get<bool>("ok"));
    CHECK(vm.find("lockedTrackValue").toBool());
    CHECK(vm.find("unlockTrack").toTable().get<bool>("ok"));
    CHECK(vm.find("renameTrack").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("renamedTrackLabel").toString(), std::string("Combat Events"));
    CHECK(vm.find("copyTrackResult").toTable().get<bool>("ok"));
    CHECK(vm.find("pasteTrackResult").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("trackCountAfterPaste").toInt(), 2);
    CHECK(vm.find("removePastedTrack").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("trackCountAfterTrackCleanup").toInt(), 1);
    CHECK(vm.find("addSectionResult").toTable().get<bool>("ok"));
    CHECK(vm.find("editSectionResult").toTable().get<bool>("ok"));
    CHECK(vm.find("editSectionSourceResult").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("sectionUri").toString(), std::string("asset://test/attack.glb#AttackEdited"));
    CHECK(std::fabs(vm.find("sectionStart").toFloat() - 0.05f) < 1e-5f);
    CHECK(std::fabs(vm.find("sectionBlend").toFloat() - 0.08f) < 1e-5f);
    CHECK(std::fabs(vm.find("sectionSourceStart").toFloat() - 0.1f) < 1e-5f);
    CHECK_EQ(vm.find("sectionCurve").toString(), std::string("linear"));
    CHECK(vm.find("removeSectionResult").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("sectionCountAfterCleanup").toInt(), 0);
    CHECK(!vm.find("invalidResult").toTable().get<bool>("ok"));
}

TEST_CASE("editor.actionTimeline.scriptPersistsDocumentAndReportsDirtyState") {
    const auto root = std::filesystem::temp_directory_path() /
                      ("eve_action_script_document_" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);

    {
        ssq::VM vm(2048, ssq::Libs::ALL);
        eve::ModuleManager::expose(vm);
        const std::string source = std::string(R"(
            timelineAsset <- {
                schema="eve.action.timeline", schemaVersion=1,
                actionId="test:persistent", durationNs=1000000000,
                animationUri="asset://test/attack.glb#Attack", metadata={},
                tracks=[{
                    id="test-track:gameplay", label="Gameplay", kind="gameplay",
                    muted=false, locked=false, notifies=[], states=[]
                }]
            };
            editor <- eve.Editor();
            module <- eve.ActionEditorModule();
            opened <- module.openDocument(")") + root.generic_string() + R"(", "test.asset.persistent", "Persistent Action",
                "content://Actions/Persistent.action", timelineAsset);
            actionEditor <- opened.value;
            backed <- actionEditor.isDocumentBacked();
            initiallyDirty <- actionEditor.isDirty();
            renameResult <- actionEditor.renameTrack("test-track:gameplay", "Gameplay Saved");
            dirtyAfterEdit <- actionEditor.isDirty();
            saveResult <- actionEditor.saveDocument();
            dirtyAfterSave <- actionEditor.isDirty();
            reopened <- module.openDocument(")" + root.generic_string() + R"(", "test.asset.reopened", "Reopened Action",
                "content://Actions/Persistent.action", timelineAsset);
            reopenedLabel <- reopened.value.getTrackLabel(0);
        )";
        vm.run(vm.compileSource(source.c_str()));

        CHECK(vm.find("opened").toTable().get<bool>("ok"));
        CHECK(vm.find("backed").toBool());
        CHECK(!vm.find("initiallyDirty").toBool());
        CHECK(vm.find("renameResult").toTable().get<bool>("ok"));
        CHECK(vm.find("dirtyAfterEdit").toBool());
        CHECK(vm.find("saveResult").toTable().get<bool>("ok"));
        CHECK(!vm.find("dirtyAfterSave").toBool());
        CHECK(vm.find("reopened").toTable().get<bool>("ok"));
        CHECK_EQ(vm.find("reopenedLabel").toString(), std::string("Gameplay Saved"));

        const std::string catalogSource = std::string(R"(
            catalogCreated <- module.createAssetCatalog(")") + root.generic_string() + R"(");
            catalog <- catalogCreated.value;
            catalogRegister <- catalog.registerDocument(
                "test.asset.persistent", "content://Actions/Persistent.action");
            catalogRefresh <- catalog.refresh("persistent");
            catalogCount <- catalog.getAssetCount();
            catalogGuid <- catalog.getAssetGuid(0);
            catalogUri <- catalog.getAssetUri(0);
            catalogTitle <- catalog.getAssetTitle(0);
            catalogGeneration <- catalog.getGeneration();
            catalogEmptySearch <- catalog.refresh("missing-query");
            catalogEmptyCount <- catalog.getAssetCount();
        )";
        vm.run(vm.compileSource(catalogSource.c_str()));
        CHECK(vm.find("catalogCreated").toTable().get<bool>("ok"));
        CHECK(vm.find("catalogRegister").toTable().get<bool>("ok"));
        CHECK(vm.find("catalogRefresh").toTable().get<bool>("ok"));
        CHECK_EQ(vm.find("catalogCount").toInt(), 1);
        CHECK_EQ(vm.find("catalogGuid").toString(), std::string("test.asset.persistent"));
        CHECK_EQ(vm.find("catalogUri").toString(), std::string("content://Actions/Persistent.action"));
        CHECK_EQ(vm.find("catalogTitle").toString(), std::string("Persistent"));
        CHECK(vm.find("catalogGeneration").toInt() > 0);
        CHECK(vm.find("catalogEmptySearch").toTable().get<bool>("ok"));
        CHECK_EQ(vm.find("catalogEmptyCount").toInt(), 0);
    }

    CHECK(std::filesystem::is_regular_file(root / "Content" / "Actions" / "Persistent.action"));
    std::filesystem::remove_all(root, cleanupError);
}
