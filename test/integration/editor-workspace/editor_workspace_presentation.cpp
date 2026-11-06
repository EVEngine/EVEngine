// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "module": "editor",
  "modules": [
    "editor",
    "audio_editor",
    "animation_editor",
    "avatar_editor",
    "voxel_editor"
  ]
}
*/
// clang-format on
#include "editor_preview_test_support.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/editor/AnimationClipPresentation.h"
#include "audio/editor/AudioSourcePresentation.h"
#include "editor/presentation/WorkspacePresentationBindings.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>

namespace {
template <class Source>
void checkEditingContract(Source& source, const std::string& path, double first, double last,
                          zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    eve::editor::WorkspacePresentation controller;
    const auto                         before   = source.state();
    const auto                         original = source.read(eve::editor::PropertyPath(path)).value;
    REQUIRE(controller.stage(source, eve::editor::PropertyPath(path), first).ok());
    REQUIRE(controller.stage(source, eve::editor::PropertyPath(path), last).ok());
    REQUIRE_EQ(source.state().revision, before.revision);
    REQUIRE_EQ(source.state().canUndo, before.canUndo);
    REQUIRE(controller.commit(source).ok());
    REQUIRE(source.read(eve::editor::PropertyPath(path)).value == eve::editor::EditorValue(last));
    REQUIRE(controller.action(source, "undo").ok());
    REQUIRE(source.read(eve::editor::PropertyPath(path)).value == original);
    REQUIRE_EQ(source.state().canUndo, before.canUndo);
    REQUIRE(controller.action(source, "redo").ok());
    REQUIRE(source.read(eve::editor::PropertyPath(path)).value == eve::editor::EditorValue(last));
    const auto revision = source.state().revision;
    REQUIRE(controller.stage(source, eve::editor::PropertyPath(path), first).ok());
    controller.cancel();
    REQUIRE(controller.commit(source).ok());
    REQUIRE_EQ(source.state().revision, revision);
    REQUIRE(!controller.edit(source, eve::editor::PropertyPath("missing"), first).ok());
    REQUIRE(!controller.edit(source, eve::editor::PropertyPath(path), "bad type").ok());
    REQUIRE_EQ(source.state().revision, revision);
    REQUIRE(controller.edit(source, eve::editor::PropertyPath(path), last).ok());
    REQUIRE_EQ(source.state().revision, revision);
    REQUIRE(controller.stage(source, eve::editor::PropertyPath(path), first).ok());
    REQUIRE(source.write(eve::editor::PropertyPath(path), eve::editor::EditorValue(first)).ok());
    REQUIRE(!controller.commit(source).ok());
    REQUIRE(!controller.hasDraft());
}
}  // namespace

TEST_CASE("editor.presentation.audio_shared_editing_contract") {
    eve::audio_editor::AudioSourceEditor       editor("presentation.audio");
    eve::audio_editor::AudioSourcePresentation source(editor);
    checkEditingContract(source, "play.volume", 0.2, 0.4, _ZEROERR_TEST_CONTEXT);
#if defined(EVE_PRESENTATION_TEST_NO_AUDIO)
    REQUIRE(!editor.attachLiveAudition().ok());
#endif
    REQUIRE(source.action("play-pause").ok());
    REQUIRE(editor.update(0.1).ok());
    REQUIRE(editor.playhead() > 0.0);
}

TEST_CASE("editor.presentation.animation_shared_editing_contract") {
    eve::animation_editor::AnimationClipEditor       editor("presentation.animation");
    eve::animation_editor::AnimationClipPresentation source(editor);
    checkEditingContract(source, "mask", 0.2, 0.4, _ZEROERR_TEST_CONTEXT);
    eve::editor::WorkspacePresentation controller;
    REQUIRE(controller.stage(source, eve::editor::PropertyPath("mask"), 0.8).ok());
    REQUIRE(editor.selectBone("Spine").ok());
    REQUIRE(!controller.commit(source).ok());
    REQUIRE(!controller.edit(source, eve::editor::PropertyPath("key-time"), 0.4).ok());
}

namespace {
template <class Domain>
class PresentationScriptFixture {
public:
    PresentationScriptFixture() : editor_("presentation.script") {}
    Domain&                             editor() noexcept { return editor_; }
    eve::editor::WorkspacePresentation& presentation() noexcept { return presentation_; }

private:
    Domain                             editor_;
    eve::editor::WorkspacePresentation presentation_;
};

template <class Domain, class Source>
void checkScriptJourney(const std::string& path, zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    using Fixture = PresentationScriptFixture<Domain>;
    ssq::VM     vm(4096, ssq::Libs::ALL);
    ssq::Table& root = vm;
    auto        cls  = root.addClass<Fixture>("TestDomain", ssq::Class::Ctor<Fixture()>());
    eve::editor::exposeWorkspacePresentation<Fixture, Source>(cls, vm.getHandle());
    cls.addFunc("cancelPreviewForTest", [handle = vm.getHandle()](Fixture* self) {
        return eve::script::projectResult(handle, self->editor().cancelPreview());
    });
    const auto scriptPath = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() /
                            "src/scripts/editor_workspace_presenter.nut";
    std::ifstream input(scriptPath);
    REQUIRE(input.good());
    const std::string framework((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    vm.run(vm.compileSource(framework.c_str()));
    vm.set("fieldPath", path);
    vm.run(vm.compileSource(R"(
        class TestUI {
            values = null; enabled = null; clicks = null; changes = null; selected = "";
            keyboardCapture = false; mountCount = 0; valueUpdates = 0;
            constructor() { values = {}; enabled = {}; clicks = []; changes = []; }
            function beginRow(...) {} function end() {} function beginBuild() {} function beginWindow(...) {}
            function text(...) {} function textWrapped(...) {} function button(...) {}
            function checkbox(label, value, id) { values[id] <- value; }
            function slider(label, value, low, high, id) { values[id] <- value; }
            function inputText(label, value, id) { values[id] <- value; }
            function combo(label, options, index, id) { values[id] <- index; }
            function setItemEnabled(...) {} function setItemAccessibility(...) {}
            function mountBuildAs(id) { mountCount++; return true; }
            function select(id) { selected = id; }
            function setHostPos(...) {} function setHostSize(...) {} function setHostMovable(...) {}
            function setHostResizable(...) {} function setHostOverlay(...) {} function setHostVisible(...) {}
            function setText(id, value) { values[id] <- value; } function setEnabled(id, value) { enabled[id] <- value; }
            function setValue(id, value) { values[id] <- value; valueUpdates++; }
            function setValueText(id, value) { values[id] <- value; }
            function setChecked(id, value) { values[id] <- value; }
            function getValue(id) { return values[id]; } function getValueText(id) { return values[id]; }
            function getChecked(id) { return values[id]; }
            function consumeClick() { return clicks.len() ? clicks.remove(0) : ""; }
            function consumeChange() { return changes.len() ? changes.remove(0) : ""; }
            function wantCaptureKeyboard() { return keyboardCapture; }
        }
        class TestWorkspace {
            function getPanelCount() { return 2; }
            function getPanelId(i) { return i == 0 ? "test.inspector" : "test.transport"; }
            function getPanelContext(i) { return i == 0 ? "inspector" : "transport"; }
            function getPanelTitle(i) { return "Test"; }
            function getPanelCapability(i) { return "test"; }
            function getPanelRegion(i) { return "right"; }
            function getRegionX(...) { return 0.0; } function getRegionY(...) { return 0.0; }
            function getRegionW(...) { return 300.0; } function getRegionH(...) { return 700.0; }
        }
        class TestKeyboard {
            down = null;
            constructor() { down = {}; }
            function isDown(key) { return key in down && down[key]; }
        }
        domain <- TestDomain(); ui <- TestUI(); keys <- TestKeyboard();
        view <- EditorWorkspacePresenter(ui, TestWorkspace(), domain);
        view.mount({ ["test.transport"] = function() { view.buildHistory(); } });
        start <- domain.presentationSnapshot().value;
        rowCount <- start.rows.len();
        for (local i = 0; i < 5; i++) view.update(keys, false);
        updatesAtRest <- ui.valueUpdates;
        view.update(keys, false);
        noRestRefresh <- updatesAtRest == ui.valueUpdates;
        ui.values[fieldPath] = 0.3;
        ui.changes.append("test.inspector/" + fieldPath); view.update(keys, true);
        ui.values[fieldPath] = 0.4;
        ui.changes.append("test.inspector/" + fieldPath); view.update(keys, true);
        draftRevision <- domain.presentationSnapshot().value.revision;
        view.update(keys, false);
        committed <- domain.presentationSnapshot().value;
        undoEnabled <- ui.enabled["undo"];
        ui.clicks.append("test.transport/undo"); view.update(keys, false);
        undone <- domain.presentationSnapshot().value;
        undoDisabled <- !ui.enabled["undo"];
        keys.down["ctrl"] <- true; keys.down["y"] <- true;
        view.update(keys, false); view.update(keys, false);
        redone <- domain.presentationSnapshot().value;
        keys.down.clear(); view.update(keys, false);
        ui.keyboardCapture = true;
        keys.down["ctrl"] <- true; keys.down["z"] <- true;
        view.update(keys, false);
        textInputPreserved <- domain.presentationSnapshot().value.revision == redone.revision;
        keys.down.clear(); ui.keyboardCapture = false; view.update(keys, false);
        ui.values[fieldPath] = 0.7;
        ui.changes.append("test.inspector/" + fieldPath); view.update(keys, true);
        keys.down["escape"] <- true; view.update(keys, true); keys.down.clear(); view.update(keys, false);
        cancelled <- domain.presentationSnapshot().value.revision == redone.revision;
        ui.values[fieldPath] = 0.6;
        ui.changes.append("test.inspector/" + fieldPath); view.update(keys, true);
        ui.changes.append("test.inspector/" + fieldPath);
        view.update(keys, false, false);
        focusCancelled <- domain.presentationSnapshot().value.revision == redone.revision;
        view.update(keys, false);
        ui.values[fieldPath] = -100.0;
        ui.changes.append("test.inspector/" + fieldPath); view.update(keys, false);
        rejected <- domain.presentationSnapshot().value.revision == redone.revision && view.status.len() > 0;
        cancelledPreview <- domain.cancelPreviewForTest();
        view.update(keys, false, false);
        unfocusedPreviewUpdated <- cancelledPreview.ok && ui.values["preview-diagnostic"] == "Preview cancelled";
        ui.values[fieldPath] = 0.9;
        ui.changes.append("test.inspector/" + fieldPath); view.update(keys, true);
        view.close(); view.close();
        domain.presentationCommit();
        closedSafely <- domain.presentationSnapshot().value.revision == redone.revision;
        mountedOnce <- ui.mountCount == 2;
        previewClosed <- domain.presentationState().value.preview == "closed";
    )"));
    REQUIRE(vm.find("noRestRefresh").toBool());
    REQUIRE(vm.find("rowCount").toInt() >= 4);
    REQUIRE_EQ(vm.find("draftRevision").toInt(), vm.find("start").toTable().get<int>("revision"));
    REQUIRE(vm.find("committed").toTable().get<int>("revision") > vm.find("draftRevision").toInt());
    REQUIRE(vm.find("undoEnabled").toBool());
    REQUIRE(vm.find("undoDisabled").toBool());
    REQUIRE(vm.find("textInputPreserved").toBool());
    REQUIRE(vm.find("cancelled").toBool());
    REQUIRE(vm.find("focusCancelled").toBool());
    REQUIRE(vm.find("rejected").toBool());
    REQUIRE(vm.find("closedSafely").toBool());
    REQUIRE(vm.find("mountedOnce").toBool());
    REQUIRE(vm.find("previewClosed").toBool());
    REQUIRE(vm.find("unfocusedPreviewUpdated").toBool());
}
}  // namespace

TEST_CASE("editor.presentation.audio_script_ui_journey") {
    checkScriptJourney<eve::audio_editor::AudioSourceEditor, eve::audio_editor::AudioSourcePresentation>(
        "play.volume", _ZEROERR_TEST_CONTEXT);
}

TEST_CASE("editor.presentation.animation_script_ui_journey") {
    checkScriptJourney<eve::animation_editor::AnimationClipEditor, eve::animation_editor::AnimationClipPresentation>(
        "mask", _ZEROERR_TEST_CONTEXT);
}

TEST_CASE("editor.presentation.audio_loop_rejection_preserves_document_history_and_transport") {
    eve::audio_editor::AudioSourceEditor       source("presentation.audio.reject");
    eve::audio_editor::AudioSourcePresentation adapter(source);
    eve::editor::WorkspacePresentation         controller;
    const auto                                 revision  = source.revision();
    const auto                                 before    = source.read("play.loop-start").value;
    const auto                                 transport = source.transport();
    const auto changed = controller.edit(adapter, eve::editor::PropertyPath("play.loop-start"), 1.7);
    REQUIRE(!changed.ok());
    REQUIRE_EQ(source.revision(), revision);
    REQUIRE(source.read("play.loop-start").value == before);
    REQUIRE(!source.canUndo());
    REQUIRE_EQ(source.transport().loopStart, transport.loopStart);
    REQUIRE_EQ(source.transport().loopEnd, transport.loopEnd);
}

TEST_CASE("editor.presentation.audio_failed_resize_preserves_preview_and_layout") {
    eve::audio_editor::AudioSourceEditor source("presentation.audio.resize");
    REQUIRE(source.setViewportWidth(256.0f).ok());
    REQUIRE(preview_test::finish(source).ok());
    const auto width    = source.layoutWidth();
    const auto count    = source.bucketCount();
    const auto first    = *source.bucket(0);
    const auto revision = source.revision();
    for (float rejected : {20000.0f, std::numeric_limits<float>::max(), std::numeric_limits<float>::quiet_NaN()}) {
        REQUIRE(!source.setViewportWidth(rejected).ok());
        REQUIRE_EQ(source.layoutWidth(), width);
        REQUIRE_EQ(source.bucketCount(), count);
        REQUIRE(source.bucket(0) != nullptr);
        REQUIRE_EQ(source.bucket(0)->minimum, first.minimum);
        REQUIRE_EQ(source.bucket(0)->maximum, first.maximum);
        REQUIRE_EQ(source.revision(), revision);
        REQUIRE(!source.canUndo());
    }
    REQUIRE(source.setViewportWidth(128.0f).ok());
    REQUIRE(preview_test::finish(source).ok());
    REQUIRE_EQ(source.layoutWidth(), 128.0f);
    REQUIRE_EQ(source.bucketCount(), 128);
}

TEST_CASE("editor.presentation.animation_target_switch_rejects_pending_draft") {
    eve::animation_editor::AnimationClipEditor       first("presentation.animation.first");
    eve::animation_editor::AnimationClipEditor       second("presentation.animation.second");
    eve::animation_editor::AnimationClipPresentation firstSource(first);
    eve::animation_editor::AnimationClipPresentation secondSource(second);
    eve::editor::WorkspacePresentation               controller;
    const auto before   = secondSource.read(eve::editor::PropertyPath("mask")).value;
    const auto revision = second.revision();
    REQUIRE(first.revision() == second.revision());
    REQUIRE(controller.stage(firstSource, eve::editor::PropertyPath("mask"), 0.4).ok());
    REQUIRE(!controller.commit(secondSource).ok());
    REQUIRE(!controller.hasDraft());
    REQUIRE(secondSource.read(eve::editor::PropertyPath("mask")).value == before);
    REQUIRE_EQ(second.revision(), revision);
    REQUIRE(!second.canUndo());
    REQUIRE(!first.canUndo());
}
