#include "audio_editor/AudioSourceEditorScriptBindings.h"

#include "audio_editor/AudioEditorModule.h"
#include "audio_editor/AudioSourceEditor.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "editor/EditorWorkspace.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace eve::audio_editor {
namespace {

constexpr const char* kBindingSource = "editor.audio.source.squirrel";

Status statusFrom(const audio_editing::EditorResult<void>& result) { return result.status(); }

template <class T>
Status statusFrom(const audio_editing::EditorResult<T>& result) {
    return result.status();
}

ssq::Table project(HSQUIRRELVM vm, const audio_editing::EditorResult<void>& result) {
    return script::projectStatusResult(vm, statusFrom(result), result.ok(), false);
}

template <class T>
ssq::Table project(HSQUIRRELVM vm, const audio_editing::EditorResult<T>& result, Value value) {
    const bool hasValue = result.ok();
    return script::projectStatusResult(vm, statusFrom(result), hasValue, hasValue, value);
}

ssq::Table bindingFailure(HSQUIRRELVM vm, DiagnosticCode code, std::string message, std::string path = {}) {
    return script::projectStatusResult(
        vm, Status::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, kBindingSource)), false,
        false);
}

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
    const HSQUIRRELVM vm = table.getHandle();
    auto sourceEditor    = table.addClass<ScriptAudioSourceEditor>(
        "AudioSourceEditor",
        std::function<ScriptAudioSourceEditor*()>([]() -> ScriptAudioSourceEditor* { return nullptr; }), true);

    sourceEditor.addFunc("configureWorkspace", [vm](ScriptAudioSourceEditor* self, editor::EditorWorkspace* workspace) {
        if (!self || !workspace)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                  "audio source editor and workspace must not be null", "workspace");
        return project(vm, self->editor().configureWorkspace(*workspace));
    });
    sourceEditor.addFunc("setViewportWidth", [vm](ScriptAudioSourceEditor* self, float width) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "audio source editor must not be null");
        return project(vm, self->editor().setViewportWidth(width));
    });
    sourceEditor.addFunc("seekX", [vm](ScriptAudioSourceEditor* self, float x) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "audio source editor must not be null");
        return project(vm, self->editor().seekX(x));
    });
    sourceEditor.addFunc("seekSeconds", [vm](ScriptAudioSourceEditor* self, float seconds) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "audio source editor must not be null");
        return project(vm, self->editor().seekSeconds(static_cast<double>(seconds)));
    });
    sourceEditor.addFunc("setFloat", [vm](ScriptAudioSourceEditor* self, const std::string& path, float value) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "audio source editor must not be null");
        return project(vm, self->editor().setProperty(path, audio_editing::EditorValue(static_cast<double>(value))));
    });
    sourceEditor.addFunc("setBool", [vm](ScriptAudioSourceEditor* self, const std::string& path, bool value) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "audio source editor must not be null");
        return project(vm, self->editor().setProperty(path, audio_editing::EditorValue(value)));
    });
    sourceEditor.addFunc("undo", [vm](ScriptAudioSourceEditor* self) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "audio source editor must not be null");
        auto result = self->editor().undo();
        return project(vm, result,
                       Value(result.ok() ? static_cast<std::int64_t>(result.value().afterRevision) : 0));
    });
    sourceEditor.addFunc("redo", [vm](ScriptAudioSourceEditor* self) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "audio source editor must not be null");
        auto result = self->editor().redo();
        return project(vm, result,
                       Value(result.ok() ? static_cast<std::int64_t>(result.value().afterRevision) : 0));
    });
    sourceEditor.addFunc("play", [vm](ScriptAudioSourceEditor* self) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "audio source editor must not be null");
        return project(vm, self->editor().play());
    });
    sourceEditor.addFunc("pause", [vm](ScriptAudioSourceEditor* self) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "audio source editor must not be null");
        return project(vm, self->editor().pause());
    });
    sourceEditor.addFunc("stop", [vm](ScriptAudioSourceEditor* self) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "audio source editor must not be null");
        return project(vm, self->editor().stop());
    });
    sourceEditor.addFunc("update", [vm](ScriptAudioSourceEditor* self, float deltaSeconds) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "audio source editor must not be null");
        auto result = self->editor().update(static_cast<double>(deltaSeconds));
        return project(vm, result, Value(result.ok() ? result.value().position : 0.0));
    });
    sourceEditor.addFunc("attachLiveAudition", [vm](ScriptAudioSourceEditor* self) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "audio source editor must not be null");
        return project(vm, self->editor().attachLiveAudition());
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

    moduleClass.addFunc("create", [vm](AudioEditorModule*, const std::string& targetId) {
        if (targetId.empty())
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "audio source target id must not be empty",
                                  "targetId");
        auto object = script::makeOwnedSquirrelInstance<ScriptAudioSourceEditor>(
            vm, std::make_unique<ScriptAudioSourceEditor>(targetId));
        if (!object) return script::projectStatusResult(vm, object.status(), false, false);
        ssq::Object owned = std::move(object).takeValue();
        auto        result = script::projectStatusResult(vm, Status::success(StatusCode::Applied), true, false);
        result.set("value", owned);
        result.set("ownership", std::string("owned"));
        return result;
    });
}

}  // namespace eve::audio_editor
