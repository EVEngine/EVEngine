#pragma once

#include "animation/editor/AnimationClipEditor.h"
#include "editor/presentation/WorkspacePresentation.h"

namespace eve::animation_editor {
/**
 * @brief Call-local adapter for clip settings and the selected transform key.
 * @ownership Borrows editor for this adapter's lifetime; no UI or history ownership.
 * @thread Owner thread only; no callbacks.
 */
class AnimationClipPresentation final : public editor::IWorkspacePresentationSource {
public:
    /** @brief Borrow an editor for synchronous presentation calls. */
    explicit AnimationClipPresentation(AnimationClipEditor& source) : source_(source) {}
    editor::WorkspacePresentationState       state() const override;
    editor::PropertySchema                   schema() const override;
    editor::PropertyReadResult               read(const editor::PropertyPath& path) const override;
    [[nodiscard]] editor::EditorResult<void> write(const editor::PropertyPath& path,
                                                   const editor::EditorValue&  value) override;
    [[nodiscard]] editor::EditorResult<void> action(std::string_view id) override;

private:
    AnimationClipEditor& source_;
};
}  // namespace eve::animation_editor
