#include "animation/editor/AnimationClipEditorScriptBindings.h"

#include "animation/editor/AnimationClipEditor.h"
#include "animation/editor/AnimationEditorModule.h"
#if defined(EVE_ANIMATION_EDITOR_RUNTIME)
#include "animation/AnimClip.h"
#include "animation/AnimSkeleton.h"
#endif
#include "editor/EditorWorkspace.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>
#include "editor/EditorScriptProjection.h"

namespace eve::animation_editor {
namespace {

constexpr const char* kBindingSource = "editor.animation.clip.squirrel";

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
    const editor::ScriptBind bind{table.getHandle(), kBindingSource};
    auto clipEditor = editor::addScriptClass<ScriptAnimationClipEditor>(table, "AnimationClipEditor");
    clipEditor.addFunc("configureWorkspace", [bind](ScriptAnimationClipEditor* self, editor::EditorWorkspace* workspace) {
        return bind.checked(self && workspace, "animation clip editor and workspace must not be null",
                            [&] { return self->editor().configureWorkspace(*workspace); }, "workspace");
    });
#if defined(EVE_ANIMATION_EDITOR_RUNTIME)
    clipEditor.addFunc("loadRuntimeClip", [bind](ScriptAnimationClipEditor* self, animation::AnimSkeleton* skeleton,
                                                  animation::AnimClip* clip) {
        return bind.checked(self && skeleton && clip,
                            "animation clip editor, skeleton and clip must not be null",
                            [&] { return self->editor().loadRuntimeClip(*skeleton, *clip); });
    });
    clipEditor.addFunc("writeRuntimeClip", [bind](ScriptAnimationClipEditor* self, animation::AnimClip* clip,
                                                   animation::AnimSkeleton* skeleton) {
        return bind.checked(self && skeleton && clip,
                            "animation clip editor, clip and skeleton must not be null",
                            [&] { return self->editor().writeRuntimeClip(*clip, *skeleton); });
    });
#endif
    clipEditor.addFunc("setViewport", [bind](ScriptAnimationClipEditor* self, float width, float rowHeight, float labelWidth) {
        return bind.checked(self, "animation clip editor must not be null",
                            [&] { return self->editor().setViewport(width, rowHeight, labelWidth); });
    });
    clipEditor.addFunc("seekX", [bind](ScriptAnimationClipEditor* self, float x) {
        return bind.checked(self, "animation clip editor must not be null",
                            [&] { return self->editor().seekX(x); });
    });
    clipEditor.addFunc("seekSeconds", [bind](ScriptAnimationClipEditor* self, float seconds) {
        return bind.checked(self, "animation clip editor must not be null",
                            [&] { return self->editor().seekSeconds(static_cast<double>(seconds)); });
    });
    clipEditor.addFunc("pointerDown", [bind](ScriptAnimationClipEditor* self, float x, float y) {
        return bind.checked(self, "animation clip editor must not be null",
                            [&] { return self->editor().pointerDown(x, y); });
    });
    clipEditor.addFunc("selectBone", [bind](ScriptAnimationClipEditor* self, const std::string& bone) {
        return bind.checked(self, "animation clip editor must not be null",
                            [&] { return self->editor().selectBone(bone); });
    });
    clipEditor.addFunc("setMaskWeight", [bind](ScriptAnimationClipEditor* self, float weight) {
        return bind.checked(self, "animation clip editor must not be null",
                            [&] { return self->editor().setMaskWeight(static_cast<double>(weight)); });
    });
    clipEditor.addFunc("setDuration", [bind](ScriptAnimationClipEditor* self, float duration) {
        return bind.checked(self, "animation clip editor must not be null",
                            [&] { return self->editor().setDuration(static_cast<double>(duration)); });
    });
    clipEditor.addFunc("setSampleRate", [bind](ScriptAnimationClipEditor* self, float sampleRate) {
        return bind.checked(self, "animation clip editor must not be null",
                            [&] { return self->editor().setSampleRate(static_cast<double>(sampleRate)); });
    });
    clipEditor.addFunc("setLoop", [bind](ScriptAnimationClipEditor* self, bool loop) {
        return bind.checked(self, "animation clip editor must not be null",
                            [&] { return self->editor().setLoop(loop); });
    });
    clipEditor.addFunc("moveSelectedKey", [bind](ScriptAnimationClipEditor* self, float time) {
        return bind.checked(self, "animation clip editor must not be null",
                            [&] { return self->editor().moveSelectedKey(static_cast<double>(time)); });
    });
    clipEditor.addFunc("keySelectedBone", [bind](ScriptAnimationClipEditor* self) {
        return bind.checked(self, "animation clip editor must not be null",
                            [&] { return self->editor().keySelectedBone(); });
    });
    clipEditor.addFunc("deleteSelectedKey", [bind](ScriptAnimationClipEditor* self) {
        return bind.checked(self, "animation clip editor must not be null",
                            [&] { return self->editor().deleteSelectedKey(); });
    });
    clipEditor.addFunc("setSelectedPosition", [bind](ScriptAnimationClipEditor* self, float x, float y, float z) {
        return bind.checked(self, "animation clip editor must not be null",
                            [&] { return self->editor().setSelectedPosition(x, y, z); });
    });
    clipEditor.addFunc("setSelectedRotation", [bind](ScriptAnimationClipEditor* self, float x, float y, float z) {
        return bind.checked(self, "animation clip editor must not be null",
                            [&] { return self->editor().setSelectedRotation(x, y, z); });
    });
    clipEditor.addFunc("setSelectedScale", [bind](ScriptAnimationClipEditor* self, float x, float y, float z) {
        return bind.checked(self, "animation clip editor must not be null",
                            [&] { return self->editor().setSelectedScale(x, y, z); });
    });
    clipEditor.addFunc("undo", [bind](ScriptAnimationClipEditor* self) {
        return bind.history(self, "animation clip editor must not be null", [&] { return self->editor().undo(); });
    });
    clipEditor.addFunc("redo", [bind](ScriptAnimationClipEditor* self) {
        return bind.history(self, "animation clip editor must not be null", [&] { return self->editor().redo(); });
    });
    clipEditor.addFunc("play", [](ScriptAnimationClipEditor* self) {
        if (self) self->editor().play();
    });
    clipEditor.addFunc("pause", [](ScriptAnimationClipEditor* self) {
        if (self) self->editor().pause();
    });
    clipEditor.addFunc("stop", [bind](ScriptAnimationClipEditor* self) {
        return bind.checked(self, "animation clip editor must not be null", [&] {
            self->editor().stop();
            return self->editor().update(0.0);
        });
    });
    clipEditor.addFunc("update", [bind](ScriptAnimationClipEditor* self, float deltaSeconds) {
        if (!self) return bind.fail(DiagnosticCode::InvalidArgument, "animation clip editor must not be null");
        auto result = self->editor().update(static_cast<double>(deltaSeconds));
        return editor::project(bind.vm(), result, Value(self->editor().playhead()));
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
    clipEditor.addFunc("hasSelectedKey", [](ScriptAnimationClipEditor* self) { return self && self->editor().hasSelectedKey(); });
    clipEditor.addFunc("getSelectedKeyTime", [](ScriptAnimationClipEditor* self) { return self ? static_cast<float>(self->editor().selectedKeyTime()) : 0.0f; });
    clipEditor.addFunc("getSelectedPositionX", [](ScriptAnimationClipEditor* self) { return self ? static_cast<float>(self->editor().selectedPositionX()) : 0.0f; });
    clipEditor.addFunc("getSelectedPositionY", [](ScriptAnimationClipEditor* self) { return self ? static_cast<float>(self->editor().selectedPositionY()) : 0.0f; });
    clipEditor.addFunc("getSelectedPositionZ", [](ScriptAnimationClipEditor* self) { return self ? static_cast<float>(self->editor().selectedPositionZ()) : 0.0f; });
    clipEditor.addFunc("getSelectedRotationX", [](ScriptAnimationClipEditor* self) { return self ? static_cast<float>(self->editor().selectedRotationX()) : 0.0f; });
    clipEditor.addFunc("getSelectedRotationY", [](ScriptAnimationClipEditor* self) { return self ? static_cast<float>(self->editor().selectedRotationY()) : 0.0f; });
    clipEditor.addFunc("getSelectedRotationZ", [](ScriptAnimationClipEditor* self) { return self ? static_cast<float>(self->editor().selectedRotationZ()) : 0.0f; });
    clipEditor.addFunc("getSelectedScaleX", [](ScriptAnimationClipEditor* self) { return self ? static_cast<float>(self->editor().selectedScaleX()) : 1.0f; });
    clipEditor.addFunc("getSelectedScaleY", [](ScriptAnimationClipEditor* self) { return self ? static_cast<float>(self->editor().selectedScaleY()) : 1.0f; });
    clipEditor.addFunc("getSelectedScaleZ", [](ScriptAnimationClipEditor* self) { return self ? static_cast<float>(self->editor().selectedScaleZ()) : 1.0f; });
    clipEditor.addFunc("getBoneCount", [](ScriptAnimationClipEditor* self) { return self ? self->editor().boneCount() : 0; });
    clipEditor.addFunc("getBoneName", [](ScriptAnimationClipEditor* self, int index) { return self ? self->editor().boneName(index) : std::string{}; });
    clipEditor.addFunc("getBoneParent", [](ScriptAnimationClipEditor* self, int index) { return self ? self->editor().boneParent(index) : std::string{}; });
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

        moduleClass.addFunc("create", [bind](AnimationEditorModule*, const std::string& targetId) {
        return bind.ownedCreate<ScriptAnimationClipEditor>("animation clip target id must not be empty", targetId);
    });
}

}  // namespace eve::animation_editor
