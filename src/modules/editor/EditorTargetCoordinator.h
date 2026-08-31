#pragma once

#include "editing/EditingCommandRegistry.h"
#include "editor/EditorCommandService.h"
#include "editor/EditorTarget.h"

#include <memory>

namespace eve::editor {

class EditorSession;

/**
 * @brief Coordinates editable targets, per-target histories and domain commands.
 *
 * The coordinator knows no concrete domain target. Domain satellites register
 * planners through IEditingCommandRegistry; targets remain borrowed.
 * @thread Owner-thread only.
 */
class EditorTargetCoordinator final : public eve::editing::IEditingCommandRegistry {
public:
    explicit EditorTargetCoordinator(EditorCommandService& commands);
    ~EditorTargetCoordinator();

    EditorTargetCoordinator(const EditorTargetCoordinator&)            = delete;
    EditorTargetCoordinator& operator=(const EditorTargetCoordinator&) = delete;

    [[nodiscard]] eve::editing::Result<void> registerPlannedCommand(
        eve::editing::EditingCommandDescriptor descriptor,
        eve::editing::EditingCommandPlanner planner) override;
    [[nodiscard]] EditorResult<void> registerTarget(IEditableTarget& target);
    [[nodiscard]] EditorResult<void> unregisterTarget(const TargetId& target);
    [[nodiscard]] EditorResult<void> bind(EditorSession& session, const TargetId& target);
    [[nodiscard]] EditorResult<EditorValue> inspect(const TargetId& target) const;
    [[nodiscard]] EditorResult<TransactionReceipt> undo(const TargetId& target);
    [[nodiscard]] EditorResult<TransactionReceipt> redo(const TargetId& target);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::editor
