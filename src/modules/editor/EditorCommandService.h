#pragma once

#include "editor/EditorCommandTypes.h"
#include "editor/EditorHostProfile.h"
#include "editor/EditorProtocol.h"
#include "editor/EditorResult.h"
#include "editor/EditorValue.h"

#include <functional>
#include <string>
#include <vector>

namespace eve::editor {

class EditorSession;

/** @brief Immutable execution context captured for a synchronous command. */
struct CommandContext {
    EditorSession*     session = nullptr;
    const HostProfile* profile = nullptr;
    TargetId           target;
    uint64_t           targetRevision = 0;
    CommandSource      source         = CommandSource::Api;
};

/** @brief Presentation and execution metadata for a registered command. */
struct CommandDescriptor {
    CommandId   id;
    std::string ownerModule;
    std::string displayName;
    std::string category;
    HostFeature requiredFeatures   = HostFeature::None;
    bool        createsTransaction = true;
    bool        automationAllowed  = true;
};

using EditorCommandHandler = std::function<EditorResult<EditorValue>(const CommandContext&, const EditorValue&)>;
using EditorCommandPlanner = std::function<EditorResult<CommandPlan>(const CommandRequest&)>;
using EditorCommandPlanExecutor =
    std::function<EditorResult<TransactionReceipt>(const CommandRequest&, const CommandPlan&)>;

/**
 * @brief Registry and execution gate for UI-, script- and automation-neutral commands.
 *
 * Registration is intended for module startup on the main thread. Execution
 * is synchronous in this first slice; asynchronous work should return a stable
 * task handle as its EditorValue.
 */
class EditorCommandService {
public:
    /**
     * @brief Register one command.
     * @param descriptor Stable metadata. id and ownerModule must be non-empty.
     * @param handler Function invoked after host/profile checks.
     * @param replace True to replace an existing command owned by the same module.
     * @return Applied on success, Rejected for invalid or duplicate registration.
     */
    EditorResult<EditorValue> registerCommand(CommandDescriptor descriptor, EditorCommandHandler handler,
                                              bool replace = false);
    /**
     * @brief Register a side-effect-free planner and explicit plan executor.
     * @param descriptor Stable command metadata.
     * @param planner Function that validates and produces a plan.
     * @param executor Function that commits an accepted plan.
     * @param replace True to replace the same owner's registration.
     * @return Applied on success, otherwise a structured registration error.
     */
    EditorResult<EditorValue> registerPlannedCommand(CommandDescriptor descriptor, EditorCommandPlanner planner,
                                                     EditorCommandPlanExecutor executor, bool replace = false);
    /** @brief Remove one command, optionally requiring a matching owner. */
    bool unregisterCommand(const CommandId& id, const std::string& ownerModule = {});
    /** @brief Remove every command registered by a module. */
    size_t unregisterOwner(const std::string& ownerModule);
    /** @brief Remove all registrations. Primarily useful for isolated tests. */
    void clear();

    /** @brief Return command metadata or nullptr when it is not registered. */
    const CommandDescriptor* find(const CommandId& id) const;
    /** @brief Return descriptors visible to a host profile. */
    std::vector<CommandDescriptor> commands(const HostProfile& profile) const;
    /** @brief Monotonic registry generation changed by registration mutations. */
    uint64_t revision() const { return revision_; }

    /**
     * @brief Execute a command after payload and profile checks.
     * @param id Registered command identity.
     * @param context Captured session, target, source and host profile.
     * @param payload Structured input value.
     * @return Handler result or a structured rejection/failure.
     */
    EditorResult<EditorValue> execute(const CommandId& id, const CommandContext& context,
                                      const EditorValue& payload) const;
    /** @brief Produce a side-effect-free plan after applying host execution policy. */
    EditorResult<CommandPlan> plan(const CommandRequest& request, const HostProfile& profile) const;
    /** @brief Execute a previously produced plan after revision and identity checks. */
    EditorResult<TransactionReceipt> executePlan(const CommandRequest& request, const CommandPlan& plan,
                                                 const HostProfile& profile) const;

private:
    struct Registration {
        CommandDescriptor         descriptor;
        EditorCommandHandler      handler;
        EditorCommandPlanner      planner;
        EditorCommandPlanExecutor planExecutor;
    };

    static EditorResult<EditorValue> error(EditorStatus status, const char* rule, std::string message);
    std::vector<Registration>        commands_;
    uint64_t                         revision_     = 0;
    mutable uint64_t                 planSequence_ = 0;
};

}  // namespace eve::editor
