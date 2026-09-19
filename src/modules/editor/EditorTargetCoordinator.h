#pragma once

#include "editing/EditingCommandRegistry.h"
#include "editor/EditorCommandService.h"
#include "editor/EditorTarget.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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
    [[nodiscard]] eve::editing::Result<std::size_t> unregisterOwner(
        const std::string& ownerModule) override;
    [[nodiscard]] EditorResult<void> registerTarget(IEditableTarget& target);
    [[nodiscard]] EditorResult<void> unregisterTarget(const TargetId& target);
    [[nodiscard]] EditorResult<void> bind(EditorSession& session, const TargetId& target);
    [[nodiscard]] EditorResult<EditorValue> inspect(const TargetId& target) const;
    [[nodiscard]] EditorResult<TransactionReceipt> undo(const TargetId& target);
    [[nodiscard]] EditorResult<TransactionReceipt> redo(const TargetId& target);

    /** @brief Discovery metadata for one registered editable target. */
    struct TargetSummary {
        std::string   id;
        std::string   type;  // IEditableTarget::describe().type, when the target reports one
        std::uint64_t revision   = 0;
        std::uint64_t generation = 0;
    };

    /**
     * @brief Enumerate every registered target, ordered by id.
     *
     * Discovery exists because a project can register targets the automation
     * host never created (a script binds its own tile layer, height map or voxel
     * world), and `inspect`/`execute` require the id up front.
     * @return One summary per registered target; never null entries.
     * @thread Owner-thread only.
     */
    [[nodiscard]] std::vector<TargetSummary> targets() const;

private:
    friend class EditorSession;
    void detach(EditorSession& session) noexcept;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::editor
