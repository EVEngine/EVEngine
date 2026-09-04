#include "animation_editor/AnimationClipEditorScriptBindings.h"

#include "animation_editor/AnimationClipEditor.h"
#include "animation_editor/AnimationEditorModule.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "editor/EditorWorkspace.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace eve::animation_editor {
namespace {

constexpr const char* kBindingSource = "editor.animation.clip.squirrel";

Status statusFrom(const animation_editing::EditorResult<void>& result) { return result.status(); }

template <class T>
Status statusFrom(const animation_editing::EditorResult<T>& result) {
    return result.status();
}

ssq::Table project(HSQUIRRELVM vm, const animation_editing::EditorResult<void>& result) {
    return script::projectStatusResult(vm, statusFrom(result), result.ok(), false);
}

template <class T>
ssq::Table project(HSQUIRRELVM vm, const animation_editing::EditorResult<T>& result, Value value) {
    const bool hasValue = result.ok();
    return script::projectStatusResult(vm, statusFrom(result), hasValue, hasValue, value);
}

ssq::Table bindingFailure(HSQUIRRELVM vm, DiagnosticCode code, std::string message, std::string path = {}) {
    return script::projectStatusResult(
        vm, Status::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, kBindingSource)), false,
        false);
}

class ScriptAnimationClipEditor {
public:
    explicit ScriptAnimationClipEditor(std::string targetId) : editor_(std::move(targetId)) {}

    AnimationClipEditor&       editor() noexcept { return editor_; }
    const AnimationClipEditor& editor() const noexcept { return editor_; }

private:
    AnimationClipEditor editor_;
};

}  // namespace

void exposeAnimationClipEditorScriptBindings(ssq::Table& table, ssq::Class& moduleClass) {
    const HSQUIRRELVM vm = table.getHandle();
    auto clipEditor      = table.addClass<ScriptAnimationClipEditor>(
        "AnimationClipEditor",
        std::function<ScriptAnimationClipEditor*()>([]() -> ScriptAnimationClipEditor* { return nullptr; }), true);

    clipEditor.addFunc("configureWorkspace",
                       [vm](ScriptAnimationClipEditor* self, editor::EditorWorkspace* workspace) {
                           if (!self || !workspace)
                               return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                                     "animation clip editor and workspace must not be null",
                                                     "workspace");
                           return project(vm, self->editor().configureWorkspace(*workspace));
                       });
    clipEditor.addFunc("setViewport",
                       [vm](ScriptAnimationClipEditor* self, float width, float rowHeight, float labelWidth) {
                           if (!self)
                               return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                                     "animation clip editor must not be null");
                           return project(vm, self->editor().setViewport(width, rowHeight, labelWidth));
                       });
    clipEditor.addFunc("seekX", [vm](ScriptAnimationClipEditor* self, float x) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "animation clip editor must not be null");
        return project(vm, self->editor().seekX(x));
    });
    clipEditor.addFunc("seekSeconds", [vm](ScriptAnimationClipEditor* self, float seconds) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "animation clip editor must not be null");
        return project(vm, self->editor().seekSeconds(static_cast<double>(seconds)));
    });
    clipEditor.addFunc("pointerDown", [vm](ScriptAnimationClipEditor* self, float x, float y) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "animation clip editor must not be null");
        return project(vm, self->editor().pointerDown(x, y));
    });
    clipEditor.addFunc("selectBone", [vm](ScriptAnimationClipEditor* self, const std::string& bone) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "animation clip editor must not be null");
        return project(vm, self->editor().selectBone(bone));
    });
    clipEditor.addFunc("setMaskWeight", [vm](ScriptAnimationClipEditor* self, float weight) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "animation clip editor must not be null");
        return project(vm, self->editor().setMaskWeight(static_cast<double>(weight)));
    });
    clipEditor.addFunc("setDuration", [vm](ScriptAnimationClipEditor* self, float duration) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "animation clip editor must not be null");
        return project(vm, self->editor().setDuration(static_cast<double>(duration)));
    });
    clipEditor.addFunc("setSampleRate", [vm](ScriptAnimationClipEditor* self, float sampleRate) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "animation clip editor must not be null");
        return project(vm, self->editor().setSampleRate(static_cast<double>(sampleRate)));
    });
    clipEditor.addFunc("setLoop", [vm](ScriptAnimationClipEditor* self, bool loop) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "animation clip editor must not be null");
        return project(vm, self->editor().setLoop(loop));
    });
    clipEditor.addFunc("moveSelectedKey", [vm](ScriptAnimationClipEditor* self, float time) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "animation clip editor must not be null");
        return project(vm, self->editor().moveSelectedKey(static_cast<double>(time)));
    });
    clipEditor.addFunc("undo", [vm](ScriptAnimationClipEditor* self) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "animation clip editor must not be null");
        auto result = self->editor().undo();
        return project(vm, result,
                       Value(result.ok() ? static_cast<std::int64_t>(result.value().afterRevision) : 0));
    });
    clipEditor.addFunc("redo", [vm](ScriptAnimationClipEditor* self) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "animation clip editor must not be null");
        auto result = self->editor().redo();
        return project(vm, result,
                       Value(result.ok() ? static_cast<std::int64_t>(result.value().afterRevision) : 0));
    });
    clipEditor.addFunc("play", [](ScriptAnimationClipEditor* self) {
        if (self) self->editor().play();
    });
    clipEditor.addFunc("pause", [](ScriptAnimationClipEditor* self) {
        if (self) self->editor().pause();
    });
    clipEditor.addFunc("stop", [vm](ScriptAnimationClipEditor* self) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "animation clip editor must not be null");
        self->editor().stop();
        return project(vm, self->editor().update(0.0));
    });
    clipEditor.addFunc("update", [vm](ScriptAnimationClipEditor* self, float deltaSeconds) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "animation clip editor must not be null");
        auto result = self->editor().update(static_cast<double>(deltaSeconds));
        return project(vm, result, Value(self->editor().playhead()));
    });
    clipEditor.addFunc("canUndo", [](ScriptAnimationClipEditor* self) { return self && self->editor().canUndo(); });
    clipEditor.addFunc("canRedo", [](ScriptAnimationClipEditor* self) { return self && self->editor().canRedo(); });
    clipEditor.addFunc("isPlaying", [](ScriptAnimationClipEditor* self) { return self && self->editor().isPlaying(); });
    clipEditor.addFunc("getRevision", [](ScriptAnimationClipEditor* self) {
        return self ? static_cast<int>(self->editor().revision()) : 0;
    });
    clipEditor.addFunc("getDuration", [](ScriptAnimationClipEditor* self) {
        return self ? static_cast<float>(self->editor().duration()) : 0.0f;
    });
    clipEditor.addFunc("getSampleRate", [](ScriptAnimationClipEditor* self) {
        return self ? static_cast<float>(self->editor().sampleRate()) : 0.0f;
    });
    clipEditor.addFunc("getLoop", [](ScriptAnimationClipEditor* self) { return self && self->editor().isLooping(); });
    clipEditor.addFunc("getPlayhead", [](ScriptAnimationClipEditor* self) {
        return self ? static_cast<float>(self->editor().playhead()) : 0.0f;
    });
    clipEditor.addFunc("getLayoutWidth", [](ScriptAnimationClipEditor* self) {
        return self ? self->editor().layoutWidth() : 0.0f;
    });
    clipEditor.addFunc("getLayoutHeight", [](ScriptAnimationClipEditor* self) {
        return self ? self->editor().layoutHeight() : 0.0f;
    });
    clipEditor.addFunc("getPlayheadX", [](ScriptAnimationClipEditor* self) {
        return self ? self->editor().playheadX() : 0.0f;
    });
    clipEditor.addFunc("getSelectedBone", [](ScriptAnimationClipEditor* self) {
        return self ? self->editor().selectedBone() : std::string{};
    });
    clipEditor.addFunc("getSelectedMaskWeight", [](ScriptAnimationClipEditor* self) {
        return self ? static_cast<float>(self->editor().selectedMaskWeight()) : 1.0f;
    });
    clipEditor.addFunc("getTrackCount", [](ScriptAnimationClipEditor* self) {
        return self ? self->editor().trackCount() : 0;
    });
    clipEditor.addFunc("getTrackBone", [](ScriptAnimationClipEditor* self, int index) {
        return self ? self->editor().trackBone(index) : std::string{};
    });
    clipEditor.addFunc("getTrackId", [](ScriptAnimationClipEditor* self, int index) {
        return self ? self->editor().trackId(index) : std::string{};
    });
    clipEditor.addFunc("getTrackSelected", [](ScriptAnimationClipEditor* self, int index) {
        return self && self->editor().isTrackSelected(index);
    });
    clipEditor.addFunc("getKeyCount", [](ScriptAnimationClipEditor* self) {
        return self ? self->editor().keyCount() : 0;
    });
    clipEditor.addFunc("getKeyX", [](ScriptAnimationClipEditor* self, int index) {
        return self ? self->editor().keyX(index) : 0.0f;
    });
    clipEditor.addFunc("getKeyY", [](ScriptAnimationClipEditor* self, int index) {
        return self ? self->editor().keyY(index) : 0.0f;
    });
    clipEditor.addFunc("getKeySelected", [](ScriptAnimationClipEditor* self, int index) {
        return self && self->editor().isKeySelected(index);
    });
    clipEditor.addFunc("getEventCount", [](ScriptAnimationClipEditor* self) {
        return self ? self->editor().eventCount() : 0;
    });
    clipEditor.addFunc("getEventX", [](ScriptAnimationClipEditor* self, int index) {
        return self ? self->editor().eventX(index) : 0.0f;
    });
    clipEditor.addFunc("getEventName", [](ScriptAnimationClipEditor* self, int index) {
        return self ? self->editor().eventName(index) : std::string{};
    });
    clipEditor.addFunc("getPrimitiveCount", [](ScriptAnimationClipEditor* self) {
        return self ? self->editor().primitiveCount() : 0;
    });
    clipEditor.addFunc("getPrimitiveKind", [](ScriptAnimationClipEditor* self, int index) {
        return self ? self->editor().primitiveKind(index) : std::string{};
    });
    clipEditor.addFunc("getPrimitiveX", [](ScriptAnimationClipEditor* self, int index) {
        return self ? self->editor().primitiveX(index) : 0.0f;
    });
    clipEditor.addFunc("getPrimitiveY", [](ScriptAnimationClipEditor* self, int index) {
        return self ? self->editor().primitiveY(index) : 0.0f;
    });
    clipEditor.addFunc("getPrimitiveDirX", [](ScriptAnimationClipEditor* self, int index) {
        return self ? self->editor().primitiveDirX(index) : 0.0f;
    });
    clipEditor.addFunc("getPrimitiveDirY", [](ScriptAnimationClipEditor* self, int index) {
        return self ? self->editor().primitiveDirY(index) : 0.0f;
    });
    clipEditor.addFunc("getPrimitiveLength", [](ScriptAnimationClipEditor* self, int index) {
        return self ? self->editor().primitiveLength(index) : 0.0f;
    });
    clipEditor.addFunc("getPrimitiveRadius", [](ScriptAnimationClipEditor* self, int index) {
        return self ? self->editor().primitiveRadius(index) : 0.0f;
    });
    clipEditor.addFunc("getPrimitiveR", [](ScriptAnimationClipEditor* self, int index) {
        return self ? self->editor().primitiveR(index) : 0.0f;
    });
    clipEditor.addFunc("getPrimitiveG", [](ScriptAnimationClipEditor* self, int index) {
        return self ? self->editor().primitiveG(index) : 0.0f;
    });
    clipEditor.addFunc("getPrimitiveB", [](ScriptAnimationClipEditor* self, int index) {
        return self ? self->editor().primitiveB(index) : 0.0f;
    });

    moduleClass.addFunc("create", [vm](AnimationEditorModule*, const std::string& targetId) {
        if (targetId.empty())
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "animation clip target id must not be empty",
                                  "targetId");
        auto object = script::makeOwnedSquirrelInstance<ScriptAnimationClipEditor>(
            vm, std::make_unique<ScriptAnimationClipEditor>(targetId));
        if (!object) return script::projectStatusResult(vm, object.status(), false, false);
        ssq::Object owned = std::move(object).takeValue();
        auto        result = script::projectStatusResult(vm, Status::success(StatusCode::Applied), true, false);
        result.set("value", owned);
        result.set("ownership", std::string("owned"));
        return result;
    });
}

}  // namespace eve::animation_editor
