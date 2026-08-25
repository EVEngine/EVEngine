#include "editor/EditorTransactionService.h"

namespace eve::editor {

EditorResult<void> LocalTransactionBackend::setAuthority(IEditAuthority* authority) {
    if (pending_)
        return EditorResult<void>::error(EditorStatus::Rejected, RuleId("editor.transaction.active"),
                                         "Cannot change authority during an active transaction");
    authority_ = authority;
    return EditorResult<void>::applied();
}

EditorResult<TransactionId> LocalTransactionBackend::begin(TransactionSpec specification) {
    if (pending_)
        return EditorResult<TransactionId>::error(EditorStatus::Rejected, RuleId("editor.transaction.active"),
                                                  "A transaction is already active");
    if (specification.id.empty() || specification.target.empty())
        return EditorResult<TransactionId>::error(EditorStatus::Rejected,
                                                  RuleId("editor.transaction.invalid-specification"),
                                                  "Transaction id and target are required");
    TransactionId id = specification.id;
    pending_         = Pending{std::move(specification), {}};
    return EditorResult<TransactionId>::applied(std::move(id));
}

EditorResult<void> LocalTransactionBackend::append(DomainOperation operation) {
    if (!pending_)
        return EditorResult<void>::error(EditorStatus::Rejected, RuleId("editor.transaction.not-active"),
                                         "No transaction is active");
    if (operation.type.empty() || operation.target != pending_->specification.target)
        return EditorResult<void>::error(EditorStatus::Rejected, RuleId("editor.operation.invalid"),
                                         "Operation type and matching target are required");
    pending_->operations.push_back(std::move(operation));
    return EditorResult<void>::applied();
}

EditorResult<TransactionReceipt> LocalTransactionBackend::commit() {
    if (!pending_) return error(EditorStatus::Rejected, "editor.transaction.not-active", "No transaction is active");
    if (!authority_)
        return error(EditorStatus::Failed, "editor.transaction.missing-authority",
                     "Transaction backend has no authority");

    Pending pending = std::move(*pending_);
    pending_.reset();
    EditorResult<AuthorityPlan> plan = authority_->preflight(pending.specification, pending.operations);
    if (!plan.accepted() || !plan.value) {
        EditorResult<TransactionReceipt> out;
        out.status      = plan.status;
        out.diagnostics = std::move(plan.diagnostics);
        return out;
    }
    EditorResult<TransactionReceipt> result = authority_->commit(*plan.value);
    if (result.accepted() && result.value) {
        undo_.push_back({pending.specification, pending.operations, *result.value});
        redo_.clear();
    }
    return result;
}

EditorResult<void> LocalTransactionBackend::rollback() {
    if (!pending_)
        return EditorResult<void>::error(EditorStatus::Rejected, RuleId("editor.transaction.not-active"),
                                         "No transaction is active");
    pending_.reset();
    return EditorResult<void>::applied();
}

EditorResult<TransactionReceipt> LocalTransactionBackend::undo() {
    if (!authority_)
        return error(EditorStatus::Failed, "editor.transaction.missing-authority",
                     "Transaction backend has no authority");
    if (undo_.empty())
        return error(EditorStatus::NotFound, "editor.history.undo-empty", "There is no transaction to undo");
    HistoryEntry entry = std::move(undo_.back());
    undo_.pop_back();
    EditorResult<TransactionReceipt> result = authority_->compensate(entry.receipt);
    if (result.accepted() && result.value) {
        entry.specification.baseRevision = result.value->afterRevision;
        redo_.push_back(std::move(entry));
    } else {
        undo_.push_back(std::move(entry));
    }
    return result;
}

EditorResult<TransactionReceipt> LocalTransactionBackend::redo() {
    if (!authority_)
        return error(EditorStatus::Failed, "editor.transaction.missing-authority",
                     "Transaction backend has no authority");
    if (redo_.empty())
        return error(EditorStatus::NotFound, "editor.history.redo-empty", "There is no transaction to redo");
    HistoryEntry entry = std::move(redo_.back());
    redo_.pop_back();
    TransactionSpec specification = entry.specification;
    specification.id = TransactionId(entry.specification.id.value() + ".redo." + std::to_string(++redoSequence_));
    specification.baseRevision       = entry.specification.baseRevision;
    EditorResult<AuthorityPlan> plan = authority_->preflight(specification, entry.operations);
    if (!plan.accepted() || !plan.value) {
        redo_.push_back(std::move(entry));
        EditorResult<TransactionReceipt> out;
        out.status      = plan.status;
        out.diagnostics = std::move(plan.diagnostics);
        return out;
    }
    EditorResult<TransactionReceipt> result = authority_->commit(*plan.value);
    if (result.accepted() && result.value) {
        entry.specification = specification;
        entry.receipt       = *result.value;
        undo_.push_back(std::move(entry));
    } else {
        redo_.push_back(std::move(entry));
    }
    return result;
}

EditorResult<TransactionReceipt> LocalTransactionBackend::error(EditorStatus status, const char* rule,
                                                                std::string message) {
    return EditorResult<TransactionReceipt>::error(status, RuleId(rule), std::move(message));
}

}  // namespace eve::editor
