#pragma once

#include "editing/EditableTarget.h"
#include "editing/EditingProperty.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace eve::editing {

/** @brief Domain-owned metadata for one planned editing command. */
struct EditingCommandDescriptor {
    CommandId   id;
    std::string ownerModule;
    std::string displayName;
    std::string category;
    bool        automationAllowed = true;
    /** @brief Target type names this command may bind (e.g. action-timeline). */
    std::vector<std::string> targetKinds;
    /**
     * @brief Payload shape shared by Inspector forms and MCP inputSchema.
     * @note Empty properties with typeId "emptyObject" means an explicit no-field payload.
     */
    PropertySchema payloadSchema;
    /** @brief Workspace panel capability this command is bound to (panel-before-automation). */
    std::string panelCapability;
    /** @brief Workspace panel context slot (list/preview/inspector/timeline/…). */
    std::string panelContext;
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
    static constexpr const char* capabilityName = "IEditingCommandRegistry";

    /** @brief Releases IEditingCommandRegistry resources. */
    virtual ~IEditingCommandRegistry() = default;
    /** @brief Register or replace one domain-owned planned command. */
    [[nodiscard]] virtual Result<void> registerPlannedCommand(
        EditingCommandDescriptor descriptor, EditingCommandPlanner planner) = 0;
    /**
     * @brief Remove every command registered by one domain module.
     * @param ownerModule Stable owner name used in command descriptors.
     * @return Applied with the number removed, or a structured failure.
     */
    [[nodiscard]] virtual Result<std::size_t> unregisterOwner(const std::string& ownerModule) = 0;
};

}  // namespace eve::editing
