#include "audio/editor/AudioSourceEditorScriptBindings.h"

#include "audio/editor/AudioEditorModule.h"
#include "audio/editor/AudioSourceEditor.h"
#include "editor/EditorWorkspace.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <string>
#include "editor/EditorScriptProjection.h"

namespace eve::audio_editor {
namespace {

constexpr const char* kBindingSource = "editor.audio.source.squirrel";

class ScriptAudioSourceEditor {
public:
    explicit ScriptAudioSourceEditor(std::string targetId) : editor_(std::move(targetId)) {}

    AudioSourceEditor&       editor() noexcept { return editor_; }
    const AudioSourceEditor& editor() const noexcept { return editor_; }

private:
    AudioSourceEditor editor_;
};

const char* transportLabel(audio_editing::AudioTransportState state) {
    switch (state) {
        case audio_editing::AudioTransportState::Playing: return "playing";
        case audio_editing::AudioTransportState::Paused: return "paused";
        case audio_editing::AudioTransportState::Stopped: return "stopped";
    }
    return "stopped";
}

}  // namespace

void exposeAudioSourceEditorScriptBindings(ssq::Table& table, ssq::Class& moduleClass) {
    const editor::ScriptBind bind{table.getHandle(), kBindingSource};
    auto sourceEditor = editor::addScriptClass<ScriptAudioSourceEditor>(table, "AudioSourceEditor");

    sourceEditor.addFunc("configureWorkspace",
                         [bind](ScriptAudioSourceEditor* self, editor::EditorWorkspace* workspace) {
                             return bind.checked(self && workspace,
                                                 "audio source editor and workspace must not be null",
                                                 [&] { return self->editor().configureWorkspace(*workspace); },
                                                 "workspace");
                         });
    sourceEditor.addFunc("setViewportWidth", [bind](ScriptAudioSourceEditor* self, float width) {
        return bind.checked(self, "audio source editor must not be null",
                            [&] { return self->editor().setViewportWidth(width); });
    });
    sourceEditor.addFunc("seekX", [bind](ScriptAudioSourceEditor* self, float x) {
        return bind.checked(self, "audio source editor must not be null", [&] { return self->editor().seekX(x); });
    });
    sourceEditor.addFunc("seekSeconds", [bind](ScriptAudioSourceEditor* self, float seconds) {
        return bind.checked(self, "audio source editor must not be null",
                            [&] { return self->editor().seekSeconds(static_cast<double>(seconds)); });
    });
    sourceEditor.addFunc("setFloat", [bind](ScriptAudioSourceEditor* self, const std::string& path, float value) {
        return bind.checked(self, "audio source editor must not be null", [&] {
            return self->editor().setProperty(path, audio_editing::EditorValue(static_cast<double>(value)));
        });
    });
    sourceEditor.addFunc("setBool", [bind](ScriptAudioSourceEditor* self, const std::string& path, bool value) {
        return bind.checked(self, "audio source editor must not be null",
                            [&] { return self->editor().setProperty(path, audio_editing::EditorValue(value)); });
    });
    sourceEditor.addFunc("undo", [bind](ScriptAudioSourceEditor* self) {
        return bind.history(self, "audio source editor must not be null", [&] { return self->editor().undo(); });
    });
    sourceEditor.addFunc("redo", [bind](ScriptAudioSourceEditor* self) {
        return bind.history(self, "audio source editor must not be null", [&] { return self->editor().redo(); });
    });
    sourceEditor.addFunc("play", [bind](ScriptAudioSourceEditor* self) {
        return bind.checked(self, "audio source editor must not be null", [&] { return self->editor().play(); });
    });
    sourceEditor.addFunc("pause", [bind](ScriptAudioSourceEditor* self) {
        return bind.checked(self, "audio source editor must not be null", [&] { return self->editor().pause(); });
    });
    sourceEditor.addFunc("stop", [bind](ScriptAudioSourceEditor* self) {
        return bind.checked(self, "audio source editor must not be null", [&] { return self->editor().stop(); });
    });
    sourceEditor.addFunc("update", [bind](ScriptAudioSourceEditor* self, float deltaSeconds) {
        if (!self) return bind.fail(DiagnosticCode::InvalidArgument, "audio source editor must not be null");
        auto result = self->editor().update(static_cast<double>(deltaSeconds));
        return editor::project(bind.vm(), result, Value(result.ok() ? result.value().position : 0.0));
    });
    sourceEditor.addFunc("attachLiveAudition", [bind](ScriptAudioSourceEditor* self) {
        return bind.checked(self, "audio source editor must not be null",
                            [&] { return self->editor().attachLiveAudition(); });
    });
    sourceEditor.addFunc("canUndo", [](ScriptAudioSourceEditor* self) { return self && self->editor().canUndo(); });
    sourceEditor.addFunc("canRedo", [](ScriptAudioSourceEditor* self) { return self && self->editor().canRedo(); });
    sourceEditor.addFunc("isPlaying", [](ScriptAudioSourceEditor* self) { return self && self->editor().isPlaying(); });
    sourceEditor.addFunc("getRevision", [](ScriptAudioSourceEditor* self) {
        return self ? static_cast<int>(self->editor().revision()) : 0;
    });
    sourceEditor.addFunc("getDuration", [](ScriptAudioSourceEditor* self) {
        return self ? static_cast<float>(self->editor().duration()) : 0.0f;
    });
    sourceEditor.addFunc("getPlayhead", [](ScriptAudioSourceEditor* self) {
        return self ? static_cast<float>(self->editor().playhead()) : 0.0f;
    });
    sourceEditor.addFunc("getLayoutWidth", [](ScriptAudioSourceEditor* self) {
        return self ? self->editor().layoutWidth() : 0.0f;
    });
    sourceEditor.addFunc("getPlayheadX", [](ScriptAudioSourceEditor* self) {
        return self ? self->editor().playheadX() : 0.0f;
    });
    sourceEditor.addFunc("getLoopStartX", [](ScriptAudioSourceEditor* self) {
        return self ? self->editor().loopStartX() : 0.0f;
    });
    sourceEditor.addFunc("getLoopEndX", [](ScriptAudioSourceEditor* self) {
        return self ? self->editor().loopEndX() : 0.0f;
    });
    sourceEditor.addFunc("getBucketCount", [](ScriptAudioSourceEditor* self) {
        return self ? self->editor().bucketCount() : 0;
    });
    sourceEditor.addFunc("getBucketMin", [](ScriptAudioSourceEditor* self, int index) {
        if (!self) return 0.0f;
        const auto* bucket = self->editor().bucket(index);
        return bucket ? bucket->minimum : 0.0f;
    });
    sourceEditor.addFunc("getBucketMax", [](ScriptAudioSourceEditor* self, int index) {
        if (!self) return 0.0f;
        const auto* bucket = self->editor().bucket(index);
        return bucket ? bucket->maximum : 0.0f;
    });
    sourceEditor.addFunc("getTransportState", [](ScriptAudioSourceEditor* self) {
        return std::string(self ? transportLabel(self->editor().transport().state) : "stopped");
    });
    sourceEditor.addFunc("getFloat", [](ScriptAudioSourceEditor* self, const std::string& path) {
        if (!self) return 0.0f;
        auto value = self->editor().read(path);
        if (const auto* real = value.value.getIf<double>()) return static_cast<float>(*real);
        if (const auto* integer = value.value.getIf<std::int64_t>()) return static_cast<float>(*integer);
        return 0.0f;
    });
    sourceEditor.addFunc("getBool", [](ScriptAudioSourceEditor* self, const std::string& path) {
        if (!self) return false;
        auto        value = self->editor().read(path);
        const auto* flag  = value.value.getIf<bool>();
        return flag ? *flag : false;
    });
    sourceEditor.addFunc("getString", [](ScriptAudioSourceEditor* self, const std::string& path) {
        if (!self) return std::string{};
        auto        value = self->editor().read(path);
        const auto* text  = value.value.getIf<std::string>();
        return text ? *text : std::string{};
    });

    moduleClass.addFunc("create", [bind](AudioEditorModule*, const std::string& targetId) {
        return bind.ownedCreate<ScriptAudioSourceEditor>("audio source target id must not be empty", targetId);
    });
}

}  // namespace eve::audio_editor
