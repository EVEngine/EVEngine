#include "editor/EditorUndoCoordinator.h"

#include "editor/EditorHost.h"
#include "editor/EditorSession.h"
#include "editor/EditorTransactions.h"

namespace eve::editor {

EditorUndoCoordinator::EditorUndoCoordinator(EditorHost& host) : host_(&host) {}

Result<void> EditorUndoCoordinator::focusOwner(std::string_view ownerModule) {
    if (!host_)
        return eve::editing::failed<void>(EditorStatus::Failed, RuleId("editor.undo.no-host"),
                                          "UndoCoordinator has no host");
    if (ownerModule.empty()) {
        focusedOwner_.clear();
        return eve::editing::applied<void>();
    }
    if (!host_->isMounted(ownerModule))
        return eve::editing::failed<void>(EditorStatus::NotFound, RuleId("editor.undo.owner-not-mounted"),
                                          "Cannot focus undo for an unmounted package: " + std::string(ownerModule));
    focusedOwner_ = std::string(ownerModule);
    return eve::editing::applied<void>();
}

editing::EditorUndoPolicy EditorUndoCoordinator::focusedPolicy() const {
    if (!host_ || focusedOwner_.empty()) return editing::EditorUndoPolicy::Share;
    return host_->mountedUndoPolicy(focusedOwner_);
}

bool EditorUndoCoordinator::canUndo(EditorSession* session) const {
    if (focusedPolicy() == editing::EditorUndoPolicy::Private) return false;
    return session && session->transactions().canUndo();
}

bool EditorUndoCoordinator::canRedo(EditorSession* session) const {
    if (focusedPolicy() == editing::EditorUndoPolicy::Private) return false;
    return session && session->transactions().canRedo();
}

}  // namespace eve::editor
