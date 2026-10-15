#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <tuple>
#include "audio/editor/EditorAudioTarget.h"
#include "common/Module.h"
#include "editor/EditorAuthority.h"
#include "editor/EditorTransactionService.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// editor_audio_source_script.cpp
TEST_CASE("editor.audio.source_editor_script_composes_workspace_and_transactions") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        workspace <- editor.newWorkspace("test.audio", "Test Audio Editor");
        created <- eve.AudioEditorModule().create("test.asset.tone");
        audioEditor <- created.value;
        configured <- audioEditor.configureWorkspace(workspace);
        workspacePanelCount <- workspace.getPanelCount();
        panel0 <- workspace.getPanelId(0);
        cap0 <- workspace.getPanelCapability(0);
        ctx1 <- workspace.getPanelContext(1);
        viewport <- audioEditor.setViewportWidth(400.0);
        beforeVolume <- audioEditor.getFloat("play.volume");
        volumeResult <- audioEditor.setFloat("play.volume", 0.2);
        afterVolume <- audioEditor.getFloat("play.volume");
        revisionAfter <- audioEditor.getRevision();
        undoResult <- audioEditor.undo();
        restoredVolume <- audioEditor.getFloat("play.volume");
        playResult <- audioEditor.play();
        advanced <- audioEditor.update(0.25);
        playhead <- audioEditor.getPlayhead();
        seekResult <- audioEditor.seekSeconds(0.4);
        seeked <- audioEditor.getPlayhead();
        buckets <- audioEditor.getBucketCount();
    )"));

    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK(vm.find("configured").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("workspacePanelCount").toInt(), 4);
    CHECK_EQ(vm.find("panel0").toString(), std::string("audio.sources"));
    CHECK_EQ(vm.find("cap0").toString(), std::string("audio.source"));
    CHECK_EQ(vm.find("ctx1").toString(), std::string("preview"));
    CHECK(vm.find("viewport").toTable().get<bool>("ok"));
    CHECK(vm.find("volumeResult").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("afterVolume").toFloat() - 0.2F), 0.0001F);
    CHECK(vm.find("undoResult").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("restoredVolume").toFloat() - vm.find("beforeVolume").toFloat()), 0.0001F);
    CHECK(vm.find("playResult").toTable().get<bool>("ok"));
    CHECK(vm.find("advanced").toTable().get<bool>("ok"));
    CHECK(vm.find("playhead").toFloat() > 0.0F);
    CHECK(vm.find("seekResult").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("seeked").toFloat() - 0.4F), 0.02F);
    CHECK(vm.find("buckets").toInt() > 8);
    (void)vm.find("revisionAfter");
}

// editor_audio_target.cpp
using namespace eve::editor;

namespace {

SelectionSnapshot sourceSelection(const AudioSourceTarget& target) {
    SelectionSnapshot selection;
    selection.channel = "scene";
    SelectionItem item;
    item.domain = SelectionDomain::Scene;
    item.target = TargetId(target.targetId());
    item.item   = StableId(target.targetId().value());
    item.type   = "audio.source";
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

template <class Target>
Result<TransactionReceipt> commit(Target& target, LocalTransactionBackend& transactions,
                                  const DomainOperation& operation, const char* id) {
    TransactionSpec specification;
    specification.id           = TransactionId(id);
    specification.label        = "Edit audio";
    specification.target       = TargetId(target.targetId());
    specification.baseRevision = target.revision();
    auto begun                 = transactions.begin(std::move(specification));
    if (!begun.ok())
        return eve::editing::failed<TransactionReceipt>(begun.code(), RuleId("test.audio.begin"),
                                                        "Could not begin audio transaction");
    auto appended = transactions.append(operation);
    if (!appended.ok()) {
        [[maybe_unused]] const auto rolledBack = transactions.rollback();
        return eve::editing::failed<TransactionReceipt>(appended.code(), RuleId("test.audio.append"),
                                                        "Could not append audio operation");
    }
    return transactions.commit();
}

}  // namespace

TEST_CASE("editor.audio.source_schema_covers_playback_spatial_attenuation_and_routing") {
    AudioSourceTarget target("ambient-river");
    const auto        schema = target.schema(sourceSelection(target));
    CHECK_EQ(schema.typeId, std::string("audio.source"));
    CHECK(schema.find(PropertyPath("clip.asset")) != nullptr);
    CHECK(schema.find(PropertyPath("play.loop-start")) != nullptr);
    CHECK(schema.find(PropertyPath("spatial.position")) != nullptr);
    CHECK(schema.find(PropertyPath("spatial.maximum-distance")) != nullptr);
    CHECK(schema.find(PropertyPath("mixer.bus")) != nullptr);
    CHECK_EQ(target.validate().size(), static_cast<std::size_t>(1));
}

TEST_CASE("editor.audio.source_edits_are_reversible_and_cross_validated") {
    AudioSourceTarget       target("ambient-river");
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend transactions(&authority);
    const SelectionSnapshot selection = sourceSelection(target);
    for (const auto& [path, value, id] :
         {std::tuple{PropertyPath("clip.asset"), EditorValue("asset://audio/river.ogg"), "audio.clip"},
          {PropertyPath("play.loop"), EditorValue(true), "audio.loop"},
          {PropertyPath("play.loop-start"), EditorValue(10.0), "audio.loop-start"},
          {PropertyPath("play.loop-end"), EditorValue(5.0), "audio.loop-end"}}) {
        auto operation = target.makeSet(selection, path, value, PropertySetMode::Absolute);
        REQUIRE(operation.ok());
        REQUIRE(commit(target, transactions, operation.value(), id).ok());
    }
    CHECK_EQ(target.validate().size(), static_cast<std::size_t>(1));
    REQUIRE(transactions.undo().ok());
    CHECK(target.validate().empty());
    REQUIRE(transactions.redo().ok());
    CHECK_EQ(target.validate().size(), static_cast<std::size_t>(1));

    AudioSourceTarget restored("restored");
    REQUIRE(restored.loadSnapshot(target.snapshotValue()).ok());
    CHECK(restored.read(sourceSelection(restored), PropertyPath("clip.asset")).value ==
          EditorValue("asset://audio/river.ogg"));

    EditorValue unknownVersion = target.snapshotValue();
    auto*       unknownRoot    = unknownVersion.getIf<EditorValue::Object>();
    REQUIRE(unknownRoot != nullptr);
    unknownRoot->at("schemaVersion")               = int64_t{2};
    const Revision revisionBeforeRejectedMigration = restored.revision();
    CHECK_EQ(static_cast<int>(restored.loadSnapshot(unknownVersion).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(restored.revision(), revisionBeforeRejectedMigration);
    CHECK(restored.read(sourceSelection(restored), PropertyPath("clip.asset")).value ==
          EditorValue("asset://audio/river.ogg"));
}

TEST_CASE("editor.audio.mixer_bus_hierarchy_is_cycle_safe_and_reversible") {
    AudioMixerTarget        mixer("main-mixer");
    LocalWorldAuthority     authority(&mixer);
    LocalTransactionBackend transactions(&authority);
    auto                    music = mixer.makeCreate({ObjectId("music"), ObjectId("master"), "Music", 0.8});
    REQUIRE(music.ok());
    REQUIRE(commit(mixer, transactions, music.value(), "audio.bus.music").ok());
    auto combat = mixer.makeCreate({ObjectId("combat"), ObjectId("music"), "Combat", 1.0});
    REQUIRE(combat.ok());
    REQUIRE(commit(mixer, transactions, combat.value(), "audio.bus.combat").ok());
    CHECK_EQ(mixer.children(ObjectId("music")).size(), static_cast<std::size_t>(1));
    CHECK_EQ(static_cast<int>(mixer.makeDelete(ObjectId("music")).code()), static_cast<int>(EditorStatus::Rejected));

    AudioBusSnapshot changed = mixer.bus(ObjectId("music")).value();
    changed.parent           = ObjectId("combat");
    CHECK_EQ(static_cast<int>(mixer.makeReplace(changed).code()), static_cast<int>(EditorStatus::Rejected));
    changed        = mixer.bus(ObjectId("combat")).value();
    changed.volume = 0.5;
    changed.mute   = true;
    auto replace   = mixer.makeReplace(changed);
    REQUIRE(replace.ok());
    REQUIRE(commit(mixer, transactions, replace.value(), "audio.bus.settings").ok());
    CHECK_EQ(mixer.bus(ObjectId("combat")).value().volume, 0.5);
    CHECK(mixer.bus(ObjectId("combat")).value().mute);
    REQUIRE(transactions.undo().ok());
    CHECK_EQ(mixer.bus(ObjectId("combat")).value().volume, 1.0);
    CHECK(!mixer.bus(ObjectId("combat")).value().mute);
    CHECK_EQ(static_cast<int>(mixer.snapshotValue().type()), static_cast<int>(EditorValue::Type::Object));
    AudioMixerTarget restored("restored-mixer");
    REQUIRE(restored.loadSnapshot(mixer.snapshotValue()).ok());
    CHECK_EQ(restored.children(ObjectId("music")).size(), static_cast<std::size_t>(1));
}
