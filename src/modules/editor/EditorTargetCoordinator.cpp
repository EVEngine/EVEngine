#include "editor/EditorTargetCoordinator.h"

#include "editor/EditorAuthority.h"
#include "editor/EditorSession.h"
#include "editor/EditorTransactionService.h"

#include <map>
#include <set>
#include <utility>
#include <vector>

namespace eve::editor {
namespace {

template <class T>
EditorResult<T> coordinatorError(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, RuleId(rule), std::move(message));
}

EditorValue descriptorValue(const eve::editing::TargetDescriptor& descriptor) {
    EditorValue::Array capabilities;
    for (const CapabilityId& capability : descriptor.capabilities)
        capabilities.emplace_back(capability.value());
    return EditorValue::Object{{"id", descriptor.id.value()},
                               {"type", descriptor.type},
                               {"revision", static_cast<std::int64_t>(descriptor.revision)},
                               {"readOnly", descriptor.readOnly},
                               {"capabilities", std::move(capabilities)}};
}

}  // namespace

struct EditorTargetCoordinator::Impl {
    struct TargetEntry {
        explicit TargetEntry(IEditableTarget& value, IDomainOperationTarget& operations, std::uint64_t valueGeneration)
            : target(&value), generation(valueGeneration), authority(&operations), transactions(&authority) {}

        IEditableTarget*        target = nullptr;
        std::shared_ptr<void>    lifetime = std::make_shared<int>(0);
        std::uint64_t            generation = 0;
        LocalWorldAuthority     authority;
        LocalTransactionBackend transactions;
    };

    explicit Impl(EditorCommandService& value) : commands(&value) {}

    TargetEntry* entry(const TargetId& id) {
        const auto found = targets.find(id);
        return found == targets.end() ? nullptr : found->second.get();
    }
    const TargetEntry* entry(const TargetId& id) const {
        const auto found = targets.find(id);
        return found == targets.end() ? nullptr : found->second.get();
    }

    EditorResult<TransactionReceipt> execute(const CommandPlan& plan) {
        TargetEntry* selected = entry(plan.target);
        if (!selected || !selected->target)
            return coordinatorError<TransactionReceipt>(EditorStatus::NotFound, "editor.target.not-found",
                                                         "Editing target is not registered: " + plan.target.value());
        if (selected->target->revision() != plan.baseRevision)
            return coordinatorError<TransactionReceipt>(EditorStatus::Conflict, "editor.target.revision-conflict",
                                                         "Editing target changed after planning");
        if (selected->generation != plan.targetGeneration)
            return coordinatorError<TransactionReceipt>(EditorStatus::Conflict, "editor.target.generation-conflict",
                                                         "Editing target lifetime changed after planning");
        TransactionSpec specification;
        specification.id           = TransactionId(plan.id.value() + ".transaction");
        specification.label        = plan.command.value();
        specification.origin       = ActionOrigin::Automation;
        specification.target       = plan.target;
        specification.baseRevision = plan.baseRevision;
        auto begun = selected->transactions.begin(std::move(specification));
        if (!begun.ok())
            return coordinatorError<TransactionReceipt>(begun.code(), "editor.target.begin-failed",
                                                         "Could not begin the editing transaction");
        for (const DomainOperation& operation : plan.operations) {
            auto appended = selected->transactions.append(operation);
            if (appended.ok()) continue;
            auto discarded = selected->transactions.discard();
            if (!discarded.ok())
                return coordinatorError<TransactionReceipt>(EditorStatus::Failed, "editor.target.discard-failed",
                                                             "Could not discard a rejected transaction");
            return coordinatorError<TransactionReceipt>(appended.code(), "editor.target.append-failed",
                                                         "Could not stage the editing operation");
        }
        return selected->transactions.commit();
    }

    EditorCommandService* commands = nullptr;
    std::map<TargetId, std::unique_ptr<TargetEntry>> targets;
    struct SessionBinding {
        TargetId       target;
        std::uint64_t generation = 0;
    };
    std::map<EditorSession*, SessionBinding> sessions;
    std::vector<std::pair<CommandId, std::string>> registeredCommands;
    std::uint64_t nextTargetGeneration = 1;
};

EditorTargetCoordinator::EditorTargetCoordinator(EditorCommandService& commands)
    : impl_(std::make_unique<Impl>(commands)) {}
EditorTargetCoordinator::~EditorTargetCoordinator() {
    while (!impl_->sessions.empty()) {
        EditorSession* session = impl_->sessions.begin()->first;
        if (session) session->coordinatorDestroyed(*this);
        impl_->sessions.erase(impl_->sessions.begin());
    }
    for (const auto& [id, owner] : impl_->registeredCommands)
        (void)impl_->commands->unregisterCommand(id, owner);
}

eve::editing::Result<void> EditorTargetCoordinator::registerPlannedCommand(
    eve::editing::EditingCommandDescriptor descriptor, eve::editing::EditingCommandPlanner planner) {
    const CommandId commandId = descriptor.id;
    const std::string ownerModule = descriptor.ownerModule;
    CommandDescriptor hostDescriptor;
    hostDescriptor.id                = descriptor.id;
    hostDescriptor.ownerModule       = std::move(descriptor.ownerModule);
    hostDescriptor.displayName       = std::move(descriptor.displayName);
    hostDescriptor.category          = std::move(descriptor.category);
    hostDescriptor.automationAllowed = descriptor.automationAllowed;
    auto registered = impl_->commands->registerPlannedCommand(
        std::move(hostDescriptor),
        [state = impl_.get(), planner = std::move(planner)](const CommandRequest& request) {
            Impl::TargetEntry* selected = state->entry(request.context.target);
            if (!selected || !selected->target)
                return coordinatorError<CommandPlan>(EditorStatus::NotFound, "editor.target.not-found",
                                                     "Editing target is not registered");
            EditorResult<CommandPlan> result = planner(*selected->target, request);
            if (result.ok()) result.value().targetGeneration = selected->generation;
            return result;
        },
        [state = impl_.get()](const CommandRequest&, const CommandPlan& plan) { return state->execute(plan); });
    if (!registered.ok()) return EditorResult<void>::failure(registered.status());
    impl_->registeredCommands.emplace_back(commandId, ownerModule);
    return eve::editing::applied<void>(registered.diagnostics());
}

eve::editing::Result<std::size_t> EditorTargetCoordinator::unregisterOwner(
    const std::string& ownerModule) {
    const std::size_t removed = impl_->commands->unregisterOwner(ownerModule);
    std::erase_if(impl_->registeredCommands,
                  [&](const auto& registration) { return registration.second == ownerModule; });
    return eve::editing::applied<std::size_t>(removed);
}

EditorResult<void> EditorTargetCoordinator::registerTarget(IEditableTarget& target) {
    auto* operations = dynamic_cast<IDomainOperationTarget*>(&target);
    if (!operations)
        return coordinatorError<void>(EditorStatus::Unsupported, "editor.target.operations-unsupported",
                                      "Editing target does not support domain operations");
    const TargetId id(target.targetId());
    if (id.empty())
        return coordinatorError<void>(EditorStatus::Rejected, "editor.target.empty-id",
                                      "Editing target id must not be empty");
    if (const auto found = impl_->targets.find(id); found != impl_->targets.end()) {
        if (found->second->target == &target) {
            return eve::editing::noOp();
        }
        return coordinatorError<void>(EditorStatus::Conflict, "editor.target.duplicate-id",
                                      "Another editing target already uses id: " + id.value());
    }
    impl_->targets.emplace(id,
                           std::make_unique<Impl::TargetEntry>(target, *operations, impl_->nextTargetGeneration++));
    return eve::editing::applied<void>();
}

EditorResult<void> EditorTargetCoordinator::unregisterTarget(const TargetId& target) {
    Impl::TargetEntry* selected = impl_->entry(target);
    if (selected) {
        std::vector<EditorSession*> sessions;
        for (const auto& [session, binding] : impl_->sessions)
            if (binding.target == target && binding.generation == selected->generation) sessions.push_back(session);
        for (EditorSession* session : sessions)
            if (session) session->invalidateTrackedTarget(*this, target, selected->generation);
        impl_->targets.erase(target);
        return eve::editing::applied<void>();
    }
    return eve::editing::noOp();
}

EditorResult<void> EditorTargetCoordinator::bind(EditorSession& session, const TargetId& target) {
    Impl::TargetEntry* selected = impl_->entry(target);
    if (!selected || !selected->target)
        return coordinatorError<void>(EditorStatus::NotFound, "editor.target.not-found",
                                      "Editing target is not registered: " + target.value());
    session.bindTrackedTarget(*selected->target, selected->lifetime, *this, selected->generation);
    impl_->sessions[&session] = {target, selected->generation};
    return eve::editing::applied<void>();
}

void EditorTargetCoordinator::detach(EditorSession& session) noexcept { impl_->sessions.erase(&session); }

EditorResult<EditorValue> EditorTargetCoordinator::inspect(const TargetId& target) const {
    const Impl::TargetEntry* selected = impl_->entry(target);
    if (!selected || !selected->target)
        return coordinatorError<EditorValue>(EditorStatus::NotFound, "editor.target.not-found",
                                             "Editing target is not registered: " + target.value());
    EditorValue::Object result;
    result["descriptor"] = descriptorValue(selected->target->describe());
    auto snapshot = selected->target->capability<eve::editing::IEditingSnapshotProvider>();
    result["snapshot"] = snapshot ? snapshot->get().snapshotValue() : EditorValue{};
    return eve::editing::applied<EditorValue>(EditorValue(std::move(result)));
}

EditorResult<TransactionReceipt> EditorTargetCoordinator::undo(const TargetId& target) {
    Impl::TargetEntry* selected = impl_->entry(target);
    if (!selected)
        return coordinatorError<TransactionReceipt>(EditorStatus::NotFound, "editor.target.not-found",
                                                     "Editing target is not registered: " + target.value());
    return selected->transactions.undo();
}

EditorResult<TransactionReceipt> EditorTargetCoordinator::redo(const TargetId& target) {
    Impl::TargetEntry* selected = impl_->entry(target);
    if (!selected)
        return coordinatorError<TransactionReceipt>(EditorStatus::NotFound, "editor.target.not-found",
                                                     "Editing target is not registered: " + target.value());
    return selected->transactions.redo();
}

}  // namespace eve::editor
