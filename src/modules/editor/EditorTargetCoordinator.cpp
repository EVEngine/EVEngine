#include "editor/EditorTargetCoordinator.h"

#include "editor/EditorAuthority.h"
#include "editor/EditorSession.h"
#include "editor/EditorTransactionService.h"

#include <map>
#include <utility>

namespace eve::editor {
namespace {

template <class T>
EditorResult<T> coordinatorError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
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
        explicit TargetEntry(IEditableTarget& value, IDomainOperationTarget& operations)
            : target(&value), authority(&operations), transactions(&authority) {}

        IEditableTarget*        target = nullptr;
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
        TransactionSpec specification;
        specification.id           = TransactionId(plan.id.value() + ".transaction");
        specification.label        = plan.command.value();
        specification.origin       = ActionOrigin::Automation;
        specification.target       = plan.target;
        specification.baseRevision = plan.baseRevision;
        auto begun = selected->transactions.begin(std::move(specification));
        if (!begun.accepted())
            return coordinatorError<TransactionReceipt>(begun.status, "editor.target.begin-failed",
                                                         "Could not begin the editing transaction");
        for (const DomainOperation& operation : plan.operations) {
            auto appended = selected->transactions.append(operation);
            if (appended.accepted()) continue;
            auto discarded = selected->transactions.discard();
            if (!discarded.accepted())
                return coordinatorError<TransactionReceipt>(EditorStatus::Failed, "editor.target.discard-failed",
                                                             "Could not discard a rejected transaction");
            return coordinatorError<TransactionReceipt>(appended.status, "editor.target.append-failed",
                                                         "Could not stage the editing operation");
        }
        return selected->transactions.commit();
    }

    EditorCommandService* commands = nullptr;
    std::map<TargetId, std::unique_ptr<TargetEntry>> targets;
};

EditorTargetCoordinator::EditorTargetCoordinator(EditorCommandService& commands)
    : impl_(std::make_unique<Impl>(commands)) {}
EditorTargetCoordinator::~EditorTargetCoordinator() = default;

eve::editing::Result<void> EditorTargetCoordinator::registerPlannedCommand(
    eve::editing::EditingCommandDescriptor descriptor, eve::editing::EditingCommandPlanner planner) {
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
            return planner(*selected->target, request);
        },
        [state = impl_.get()](const CommandRequest&, const CommandPlan& plan) { return state->execute(plan); });
    EditorResult<void> result;
    result.status      = registered.status;
    result.diagnostics = std::move(registered.diagnostics);
    return result;
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
            EditorResult<void> result;
            result.status = EditorStatus::NoOp;
            return result;
        }
        return coordinatorError<void>(EditorStatus::Conflict, "editor.target.duplicate-id",
                                      "Another editing target already uses id: " + id.value());
    }
    impl_->targets.emplace(id, std::make_unique<Impl::TargetEntry>(target, *operations));
    return EditorResult<void>::applied();
}

EditorResult<void> EditorTargetCoordinator::unregisterTarget(const TargetId& target) {
    if (impl_->targets.erase(target) != 0) return EditorResult<void>::applied();
    EditorResult<void> result;
    result.status = EditorStatus::NoOp;
    return result;
}

EditorResult<void> EditorTargetCoordinator::bind(EditorSession& session, const TargetId& target) {
    Impl::TargetEntry* selected = impl_->entry(target);
    if (!selected || !selected->target)
        return coordinatorError<void>(EditorStatus::NotFound, "editor.target.not-found",
                                      "Editing target is not registered: " + target.value());
    session.clearRetainedPlans();
    session.bindTarget(selected->target);
    return EditorResult<void>::applied();
}

EditorResult<EditorValue> EditorTargetCoordinator::inspect(const TargetId& target) const {
    const Impl::TargetEntry* selected = impl_->entry(target);
    if (!selected || !selected->target)
        return coordinatorError<EditorValue>(EditorStatus::NotFound, "editor.target.not-found",
                                             "Editing target is not registered: " + target.value());
    EditorValue::Object result;
    result["descriptor"] = descriptorValue(selected->target->describe());
    auto* snapshot = static_cast<eve::editing::IEditingSnapshotProvider*>(
        selected->target->queryCapability(eve::editing::IEditingSnapshotProvider::editingCapabilityId()));
    result["snapshot"] = snapshot ? snapshot->snapshotValue() : EditorValue{};
    return EditorResult<EditorValue>::applied(EditorValue(std::move(result)));
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
