#pragma once

#include "editing/EditableTarget.h"

#include <functional>
#include <string>

namespace eve::editing {

/** @brief Domain-owned metadata for one planned editing command. */
struct EditingCommandDescriptor {
    CommandId   id;
    std::string ownerModule;
    std::string displayName;
    std::string category;
    bool        automationAllowed = true;
};

/** @brief Side-effect-free planner invoked with the resolved target. */
using EditingCommandPlanner =
    std::function<Result<CommandPlan>(IEditableTarget&, const CommandRequest&)>;

/**
 * @brief Lower-layer registration boundary implemented by an editor or runtime editing host.
 *
 * Domain satellites depend on this interface rather than on EditorSession or
 * EditorCommandService. The host owns execution, transaction history and policy.
 */
class IEditingCommandRegistry {
public:
    virtual ~IEditingCommandRegistry() = default;
    /** @brief Register or replace one domain-owned planned command. */
    [[nodiscard]] virtual Result<void> registerPlannedCommand(
        EditingCommandDescriptor descriptor, EditingCommandPlanner planner) = 0;
};

}  // namespace eve::editing
