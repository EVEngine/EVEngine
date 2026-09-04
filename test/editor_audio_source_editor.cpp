#include "audio_editor/AudioSourceEditor.h"
#include "editor/EditorWorkspace.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>

using eve::audio_editor::AudioSourceEditor;
using eve::editor::EditorWorkspace;

TEST_CASE("editor.audio.source_editor_installs_workspace_and_reverses_volume") {
    AudioSourceEditor editor("preview.tone");
    EditorWorkspace   workspace("audio.preview", "Audio Source Editor");
    REQUIRE(editor.configureWorkspace(workspace).ok());
    CHECK_EQ(workspace.getPanelCount(), 4);
    CHECK_EQ(workspace.getPanelId(0), std::string("audio.sources"));
    CHECK_EQ(workspace.getPanelCapability(0), std::string("audio.source"));
    CHECK_EQ(workspace.getPanelContext(0), std::string("list"));
    CHECK_EQ(workspace.getPanelContext(1), std::string("preview"));
    CHECK_EQ(workspace.getPanelContext(2), std::string("inspector"));
    CHECK_EQ(workspace.getPanelContext(3), std::string("transport"));

    REQUIRE(editor.setViewportWidth(320.0f).ok());
    CHECK(editor.bucketCount() > 8);
    CHECK(editor.duration() > 1.9);

    const double before = editor.read("play.volume").value.getIf<double>() ?
                              *editor.read("play.volume").value.getIf<double>() :
                              0.0;
    REQUIRE(editor.setProperty("play.volume", eve::audio_editing::EditorValue(0.25)).ok());
    CHECK_EQ(*editor.read("play.volume").value.getIf<double>(), 0.25);
    REQUIRE(editor.undo().ok());
    CHECK_EQ(*editor.read("play.volume").value.getIf<double>(), before);

    REQUIRE(editor.play().ok());
    REQUIRE(editor.update(0.2).ok());
    CHECK(editor.playhead() > 0.0);
    REQUIRE(editor.seekSeconds(0.75).ok());
    CHECK(std::abs(editor.playhead() - 0.75) < 0.02);
    REQUIRE(editor.seekX(editor.layoutWidth() * 0.5f).ok());
    CHECK(std::abs(editor.playhead() - editor.duration() * 0.5) < 0.05);
}

TEST_CASE("editor.audio.source_editor_property_edits_do_not_stale_audition") {
    AudioSourceEditor editor("preview.tone");
    REQUIRE(editor.play().ok());
    const auto pcm = editor.pcmRevision();
    REQUIRE(editor.setProperty("play.volume", eve::audio_editing::EditorValue(0.4)).ok());
    CHECK_EQ(editor.pcmRevision(), pcm);
    auto advanced = editor.update(0.05);
    REQUIRE(advanced.ok());
    CHECK_EQ(static_cast<int>(advanced.value().state),
             static_cast<int>(eve::audio_editing::AudioTransportState::Playing));
}
