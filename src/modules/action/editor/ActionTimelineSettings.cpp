#include "action/editor/ActionTimelineEditor.h"

namespace eve::editor {

EditorResult<void> ActionTimelineEditor::setMontageSettings(action::ActionMontageSettings settings) {
    action::ActionTimeline candidate = target_.timeline();
    if (candidate.montage == settings) return eve::editing::noOp();
    candidate.montage = std::move(settings);
    return commit(std::move(candidate), "Edit montage settings", "action.timeline.montage.settings");
}

}  // namespace eve::editor
