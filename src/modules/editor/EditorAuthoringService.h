#pragma once

#include "editor/EditorCommandService.h"
#include "editor/EditorTargetV2.h"

#include <memory>

namespace eve::editor {

class EditorSession;

/**
 * @brief Registers the first built-in authoring commands and their editable targets.
 *
 * Targets are borrowed and must be unregistered before destruction. Each target
 * receives an independent transaction history, so UI and automation use the
 * same domain operations without sharing mutable preview state.
 * @thread Owner-thread only.
 */
class EditorAuthoringService {
public:
    /** @brief Register built-in Scene Transform and Material Property commands. */
    explicit EditorAuthoringService(EditorCommandService& commands);
    ~EditorAuthoringService();

    EditorAuthoringService(const EditorAuthoringService&)            = delete;
    EditorAuthoringService& operator=(const EditorAuthoringService&) = delete;

    /**
     * @brief Make one borrowed V2 target available to UI and automation commands.
     * @param target Target implementing IDomainOperationTarget; caller retains ownership.
     * @return Applied, NoOp for the same registration, or a structured rejection.
     */
    [[nodiscard]] EditorResult<void> registerTarget(IEditableTargetV2& target);

    /**
     * @brief Remove one target and its local undo history.
     * @return Applied when removed, or NoOp when the target was not registered.
     */
    [[nodiscard]] EditorResult<void> unregisterTarget(const TargetId& target);

    /** @brief Bind a registered borrowed target to a session. */
    [[nodiscard]] EditorResult<void> bind(EditorSession& session, const TargetId& target);

    /** @brief Return descriptor and deterministic authoring snapshot for a target. */
    [[nodiscard]] EditorResult<EditorValue> inspect(const TargetId& target) const;

    /** @brief Undo the latest domain transaction for a target. */
    [[nodiscard]] EditorResult<TransactionReceipt> undo(const TargetId& target);
    /** @brief Redo the latest compensated domain transaction for a target. */
    [[nodiscard]] EditorResult<TransactionReceipt> redo(const TargetId& target);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::editor
