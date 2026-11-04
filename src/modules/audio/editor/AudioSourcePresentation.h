#pragma once
#include "common/Export.h"

#include "audio/editor/AudioSourceEditor.h"
#include "editor/presentation/WorkspacePresentation.h"

namespace eve::audio_editor {
/**
 * @brief Call-local presentation adapter over the authoritative audio editor.
 * @ownership Borrows editor until this adapter is destroyed; editor must outlive it.
 * @thread Owner thread. No callbacks or retained UI state.
 */
class EVENGINE_API_EDITORS AudioSourcePresentation final : public editor::IWorkspacePresentationSource {
public:
    /** @brief Borrow an editor for synchronous presentation calls. */
    explicit AudioSourcePresentation(AudioSourceEditor& source) : source_(source) {}
    editor::WorkspacePresentationState state() const override;
    editor::PropertySchema             schema() const override;
    editor::PropertyReadResult         read(const editor::PropertyPath& path) const override;
    [[nodiscard]] editor::Result<void> write(const editor::PropertyPath& path,
                                             const editor::EditorValue&  value) override;
    [[nodiscard]] editor::Result<void> action(std::string_view id) override;

private:
    AudioSourceEditor& source_;
};
}  // namespace eve::audio_editor
