#include "editor/EditorAuthority.h"

#include <algorithm>
#include <exception>

namespace eve::editor {
namespace {

template <class T>
EditorResult<T> authorityError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

void appendAffected(TransactionReceipt& receipt, const DomainOperation& operation) {
    for (const ObjectRefValue& object : operation.affectedObjects) {
        if (std::find(receipt.affectedObjects.begin(), receipt.affectedObjects.end(), object) ==
            receipt.affectedObjects.end())
            receipt.affectedObjects.push_back(object);
    }
}

}  // namespace

EditorResult<AuthorityPlan> LocalWorldAuthority::preflight(const TransactionSpec&           transaction,
                                                           std::span<const DomainOperation> operations) {
    if (!target_)
        return authorityError<AuthorityPlan>(EditorStatus::Failed, "editor.authority.missing-target",
                                             "Local authority has no target");
    if (transaction.id.empty() || transaction.target.empty())
        return authorityError<AuthorityPlan>(EditorStatus::Rejected, "editor.authority.invalid-transaction",
                                             "Transaction id and target are required");
    if (transaction.target.value() != target_->targetId())
        return authorityError<AuthorityPlan>(EditorStatus::Rejected, "editor.authority.target-mismatch",
                                             "Transaction target does not match the authority target");
    if (transaction.baseRevision != target_->revision())
        return authorityError<AuthorityPlan>(EditorStatus::Conflict, "editor.authority.revision-conflict",
                                             "Target changed since the transaction was planned");
    if (committed_.contains(transaction.id))
        return authorityError<AuthorityPlan>(EditorStatus::Conflict, "editor.authority.duplicate-transaction",
                                             "Transaction id was already committed");
    for (const DomainOperation& operation : operations) {
        if (operation.type.empty())
            return authorityError<AuthorityPlan>(EditorStatus::Rejected, "editor.operation.missing-type",
                                                 "Domain operation type is required");
        if (operation.target != transaction.target)
            return authorityError<AuthorityPlan>(EditorStatus::Rejected, "editor.operation.target-mismatch",
                                                 "Domain operation target does not match the transaction");
    }

    AuthorityPlan plan;
    plan.transaction       = transaction;
    plan.validatedRevision = target_->revision();
    plan.operations.assign(operations.begin(), operations.end());
    return EditorResult<AuthorityPlan>::applied(std::move(plan));
}

EditorResult<TransactionReceipt> LocalWorldAuthority::commit(const AuthorityPlan& plan) {
    if (!target_)
        return authorityError<TransactionReceipt>(EditorStatus::Failed, "editor.authority.missing-target",
                                                  "Local authority has no target");
    if (plan.validatedRevision != target_->revision())
        return authorityError<TransactionReceipt>(EditorStatus::Conflict, "editor.authority.revision-conflict",
                                                  "Target changed after authority preflight");

    TransactionReceipt receipt;
    receipt.id             = plan.transaction.id;
    receipt.state          = TransactionState::PendingAuthority;
    receipt.beforeRevision = target_->revision();

    std::size_t appliedCount = 0;
    try {
        for (const DomainOperation& operation : plan.operations) {
            EditorResult<void> result = target_->applyDomainOperation(operation);
            if (!result.accepted()) {
                EditorResult<void> rollback = rollbackApplied(plan.operations, appliedCount);
                receipt.state         = rollback.accepted() ? TransactionState::Rejected : TransactionState::Failed;
                receipt.afterRevision = target_->revision();
                receipt.diagnostics   = std::move(result.diagnostics);
                receipt.diagnostics.insert(receipt.diagnostics.end(), rollback.diagnostics.begin(),
                                           rollback.diagnostics.end());
                EditorResult<TransactionReceipt> out;
                out.status      = rollback.accepted() ? result.status : EditorStatus::Failed;
                out.value       = receipt;
                out.diagnostics = receipt.diagnostics;
                return out;
            }
            ++appliedCount;
            appendAffected(receipt, operation);
        }
    } catch (const std::exception& exception) {
        EditorResult<void> rollback = rollbackApplied(plan.operations, appliedCount);
        return authorityError<TransactionReceipt>(EditorStatus::Failed, "editor.authority.target-exception",
                                                  exception.what());
    } catch (...) {
        EditorResult<void> rollback = rollbackApplied(plan.operations, appliedCount);
        (void)rollback;
        return authorityError<TransactionReceipt>(EditorStatus::Failed, "editor.authority.target-exception",
                                                  "Domain operation target threw an unknown exception");
    }

    receipt.state            = TransactionState::Committed;
    receipt.afterRevision    = target_->revision();
    receipt.authorityReceipt = "local:" + std::to_string(++receiptSequence_);
    committed_.emplace(receipt.id, CommittedEntry{receipt, plan.operations});
    return EditorResult<TransactionReceipt>::applied(std::move(receipt));
}

EditorResult<TransactionReceipt> LocalWorldAuthority::compensate(const TransactionReceipt& receipt) {
    if (!target_)
        return authorityError<TransactionReceipt>(EditorStatus::Failed, "editor.authority.missing-target",
                                                  "Local authority has no target");
    auto entry = committed_.find(receipt.id);
    if (entry == committed_.end())
        return authorityError<TransactionReceipt>(EditorStatus::NotFound, "editor.authority.receipt-not-found",
                                                  "Committed transaction is not available for compensation");
    for (const DomainOperation& operation : entry->second.operations) {
        if (!operation.hasInverse)
            return authorityError<TransactionReceipt>(EditorStatus::Unsupported,
                                                      "editor.authority.operation-not-reversible",
                                                      "Transaction contains an operation without an inverse");
    }

    TransactionReceipt compensation;
    compensation.id             = TransactionId(receipt.id.value() + ".undo." + std::to_string(++receiptSequence_));
    compensation.state          = TransactionState::PendingAuthority;
    compensation.beforeRevision = target_->revision();
    for (auto operation = entry->second.operations.rbegin(); operation != entry->second.operations.rend();
         ++operation) {
        EditorResult<void> result = target_->applyDomainOperation(inverseOf(*operation));
        if (!result.accepted()) {
            compensation.state         = TransactionState::Failed;
            compensation.afterRevision = target_->revision();
            compensation.diagnostics   = std::move(result.diagnostics);
            EditorResult<TransactionReceipt> out;
            out.status      = EditorStatus::Failed;
            out.value       = compensation;
            out.diagnostics = compensation.diagnostics;
            return out;
        }
        appendAffected(compensation, *operation);
    }
    compensation.state            = TransactionState::Committed;
    compensation.afterRevision    = target_->revision();
    compensation.authorityReceipt = "local:" + std::to_string(receiptSequence_);
    return EditorResult<TransactionReceipt>::applied(std::move(compensation));
}

DomainOperation LocalWorldAuthority::inverseOf(const DomainOperation& operation) {
    DomainOperation inverse = operation;
    if (!operation.inverseType.empty()) inverse.type = operation.inverseType;
    inverse.inverseType = operation.type;
    inverse.payload     = operation.inverse;
    inverse.inverse     = operation.payload;
    return inverse;
}

EditorResult<void> LocalWorldAuthority::rollbackApplied(std::span<const DomainOperation> operations,
                                                        std::size_t                      count) {
    while (count > 0) {
        const DomainOperation& operation = operations[--count];
        if (!operation.hasInverse)
            return EditorResult<void>::error(EditorStatus::Failed, RuleId("editor.authority.rollback-impossible"),
                                             "Applied operation has no inverse");
        EditorResult<void> result = target_->applyDomainOperation(inverseOf(operation));
        if (!result.accepted()) return result;
    }
    return EditorResult<void>::applied();
}

EditorResult<AuthorityPlan> ReadOnlyAuthority::preflight(const TransactionSpec&           transaction,
                                                         std::span<const DomainOperation> operations) {
    (void)transaction;
    (void)operations;
    return authorityError<AuthorityPlan>(EditorStatus::Rejected, "editor.authority.read-only",
                                         "The current editor authority is read-only");
}

EditorResult<TransactionReceipt> ReadOnlyAuthority::commit(const AuthorityPlan& plan) {
    (void)plan;
    return authorityError<TransactionReceipt>(EditorStatus::Rejected, "editor.authority.read-only",
                                              "The current editor authority is read-only");
}

EditorResult<TransactionReceipt> ReadOnlyAuthority::compensate(const TransactionReceipt& receipt) {
    (void)receipt;
    return authorityError<TransactionReceipt>(EditorStatus::Rejected, "editor.authority.read-only",
                                              "The current editor authority is read-only");
}

}  // namespace eve::editor
