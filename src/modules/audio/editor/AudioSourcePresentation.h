#pragma once

#include "audio/editor/AudioSourceEditor.h"
#include "editor/presentation/WorkspacePresentation.h"

namespace eve::audio_editor {
/**
 * @brief Call-local presentation adapter over the authoritative audio editor.
 * @ownership Borrows editor until this adapter is destroyed; editor must outlive it.
 * @thread Owner thread. No callbacks or retained UI state.
 */
class AudioSourcePresentation final : public editor::IWorkspacePresentationSource {
public:
    /** @brief Borrow an editor for synchronous presentation calls. */
    explicit AudioSourcePresentation(AudioSourceEditor& source) : source_(source) {}
    editor::WorkspacePresentationState       state() const override;
    editor::PropertySchema                   schema() const override;
    editor::PropertyReadResult               read(const editor::PropertyPath& path) const override;
    [[nodiscard]] editor::EditorResult<void> write(const editor::PropertyPath& path,
                                                   const editor::EditorValue&  value) override;
    [[nodiscard]] editor::EditorResult<void> action(std::string_view id) override;

private:
    AudioSourceEditor& source_;
};
}  // namespace eve::audio_editor
