#include "editing/EditingAuthority.h"

#include <algorithm>
#include <exception>

namespace eve::editing {
namespace {

template <class T>
Result<T> authorityError(Status status, const char* rule, std::string message) {
    return Result<T>::error(status, RuleId(rule), std::move(message));
}

void appendAffected(TransactionReceipt& receipt, const DomainOperation& operation) {
    for (const ObjectRefValue& object : operation.affectedObjects) {
        if (std::find(receipt.affectedObjects.begin(), receipt.affectedObjects.end(), object) ==
            receipt.affectedObjects.end())
            receipt.affectedObjects.push_back(object);
    }
}

}  // namespace

Result<AuthorityPlan> LocalWorldAuthority::preflight(const TransactionSpec&           transaction,
                                                           std::span<const DomainOperation> operations) {
    if (!target_)
        return authorityError<AuthorityPlan>(Status::Failed, "editor.authority.missing-target",
                                             "Local authority has no target");
    if (transaction.id.empty() || transaction.target.empty())
        return authorityError<AuthorityPlan>(Status::Rejected, "editor.authority.invalid-transaction",
                                             "Transaction id and target are required");
    if (transaction.target.value() != target_->targetId())
        return authorityError<AuthorityPlan>(Status::Rejected, "editor.authority.target-mismatch",
                                             "Transaction target does not match the authority target");
    if (transaction.baseRevision != target_->revision())
        return authorityError<AuthorityPlan>(Status::Conflict, "editor.authority.revision-conflict",
                                             "Target changed since the transaction was planned");
    if (committed_.contains(transaction.id))
        return authorityError<AuthorityPlan>(Status::Conflict, "editor.authority.duplicate-transaction",
                                             "Transaction id was already committed");
    for (const DomainOperation& operation : operations) {
        if (operation.type.empty())
            return authorityError<AuthorityPlan>(Status::Rejected, "editor.operation.missing-type",
                                                 "Domain operation type is required");
        if (operation.target != transaction.target)
            return authorityError<AuthorityPlan>(Status::Rejected, "editor.operation.target-mismatch",
                                                 "Domain operation target does not match the transaction");
    }

    AuthorityPlan plan;
    plan.transaction       = transaction;
    plan.validatedRevision = target_->revision();
    plan.operations.assign(operations.begin(), operations.end());
    return Result<AuthorityPlan>::applied(std::move(plan));
}

Result<TransactionReceipt> LocalWorldAuthority::commit(const AuthorityPlan& plan) {
    if (!target_)
        return authorityError<TransactionReceipt>(Status::Failed, "editor.authority.missing-target",
                                                  "Local authority has no target");
    if (plan.validatedRevision != target_->revision())
        return authorityError<TransactionReceipt>(Status::Conflict, "editor.authority.revision-conflict",
                                                  "Target changed after authority preflight");

    TransactionReceipt receipt;
    receipt.id             = plan.transaction.id;
    receipt.state          = TransactionState::PendingAuthority;
    receipt.beforeRevision = target_->revision();

    std::size_t appliedCount = 0;
    try {
        for (const DomainOperation& operation : plan.operations) {
            Result<void> result = target_->applyDomainOperation(operation);
            if (!result.isAccepted()) {
                Result<void> rollback = rollbackApplied(plan.operations, appliedCount);
                receipt.state         = rollback.isAccepted() ? TransactionState::Rejected : TransactionState::Failed;
                receipt.afterRevision = target_->revision();
                receipt.diagnostics   = std::move(result.diagnostics);
                receipt.diagnostics.insert(receipt.diagnostics.end(), rollback.diagnostics.begin(),
                                           rollback.diagnostics.end());
                Result<TransactionReceipt> out;
                out.status      = rollback.isAccepted() ? result.status : Status::Failed;
                out.value       = receipt;
                out.diagnostics = receipt.diagnostics;
                return out;
            }
            ++appliedCount;
            appendAffected(receipt, operation);
        }
    } catch (const std::exception& exception) {
        Result<void> rollback = rollbackApplied(plan.operations, appliedCount);
        return authorityError<TransactionReceipt>(Status::Failed, "editor.authority.target-exception",
                                                  exception.what());
    } catch (...) {
        Result<void> rollback = rollbackApplied(plan.operations, appliedCount);
        (void)rollback;
        return authorityError<TransactionReceipt>(Status::Failed, "editor.authority.target-exception",
                                                  "Domain operation target threw an unknown exception");
    }

    receipt.state            = TransactionState::Committed;
    receipt.afterRevision    = target_->revision();
    receipt.authorityReceipt = "local:" + std::to_string(++receiptSequence_);
    committed_.emplace(receipt.id, CommittedEntry{receipt, plan.operations});
    commitOrder_.push_back(receipt.id);
    return Result<TransactionReceipt>::applied(std::move(receipt));
}

Result<TransactionReceipt> LocalWorldAuthority::compensate(const TransactionReceipt& receipt) {
    if (!target_)
        return authorityError<TransactionReceipt>(Status::Failed, "editor.authority.missing-target",
                                                  "Local authority has no target");
    auto entry = committed_.find(receipt.id);
    if (entry == committed_.end())
        return authorityError<TransactionReceipt>(Status::NotFound, "editor.authority.receipt-not-found",
                                                  "Committed transaction is not available for compensation");
    if (commitOrder_.empty() || commitOrder_.back() != receipt.id)
        return authorityError<TransactionReceipt>(Status::Conflict, "editor.authority.compensation-order",
                                                  "Only the latest committed transaction can be compensated");
    for (const DomainOperation& operation : entry->second.operations) {
        if (!operation.hasInverse)
            return authorityError<TransactionReceipt>(Status::Unsupported,
                                                      "editor.authority.operation-not-reversible",
                                                      "Transaction contains an operation without an inverse");
    }

    if (entry->second.receipt.afterRevision != target_->revision())
        return authorityError<TransactionReceipt>(Status::Conflict, "editor.authority.revision-conflict",
                                                  "Target changed after the committed transaction");

    auto* staging = dynamic_cast<IDomainOperationTargetStaging*>(target_);
    if (!staging)
        return authorityError<TransactionReceipt>(Status::Unsupported, "editor.authority.staging-unavailable",
                                                  "Target cannot stage a complete compensation candidate");

    TransactionReceipt compensation;
    compensation.id             = TransactionId(receipt.id.value() + ".undo." + std::to_string(++receiptSequence_));
    compensation.state          = TransactionState::PendingAuthority;
    compensation.beforeRevision = target_->revision();

    const auto failed = [&](Status status, std::string rule, std::string message) {
        compensation.state         = TransactionState::Failed;
        compensation.afterRevision = target_->revision();
        if (compensation.diagnostics.empty())
            compensation.diagnostics.push_back(
                {RuleId(std::move(rule)), DiagnosticSeverity::Error, std::move(message)});
        Result<TransactionReceipt> out;
        out.status      = status;
        out.value       = compensation;
        out.diagnostics = compensation.diagnostics;
        return out;
    };

    std::unique_ptr<IDomainOperationTarget> candidate;
    try {
        candidate = staging->cloneDomainState();
    } catch (const std::exception& exception) {
        return failed(Status::Failed, "editor.authority.candidate-exception",
                      std::string("Could not clone compensation candidate: ") + exception.what());
    } catch (...) {
        return failed(Status::Failed, "editor.authority.candidate-exception",
                      "Could not clone compensation candidate");
    }
    if (!candidate)
        return failed(Status::Unsupported, "editor.authority.staging-unavailable",
                      "Target did not provide a compensation candidate");
    if (candidate->targetId() != target_->targetId())
        return failed(Status::Conflict, "editor.authority.candidate-mismatch",
                      "Compensation candidate belongs to another target");

    for (auto operation = entry->second.operations.rbegin(); operation != entry->second.operations.rend();
         ++operation) {
        Result<void> result;
        try {
            result = candidate->applyDomainOperation(inverseOf(*operation));
        } catch (const std::exception& exception) {
            compensation.diagnostics.push_back({RuleId("editor.authority.candidate-exception"),
                                                DiagnosticSeverity::Error,
                                                std::string("Compensation candidate threw: ") + exception.what()});
            return failed(Status::Failed, "editor.authority.candidate-exception", "Compensation candidate threw");
        } catch (...) {
            compensation.diagnostics.push_back({RuleId("editor.authority.candidate-exception"),
                                                DiagnosticSeverity::Error,
                                                "Compensation candidate threw an unknown exception"});
            return failed(Status::Failed, "editor.authority.candidate-exception",
                          "Compensation candidate threw an unknown exception");
        }
        if (!result.isAccepted()) {
            compensation.diagnostics   = std::move(result.diagnostics);
            return failed(result.status, "editor.authority.compensation-candidate-rejected",
                          "Compensation candidate rejected an inverse operation");
        }
        appendAffected(compensation, *operation);
    }

    Result<void> published;
    try {
        published = staging->commitDomainState(std::move(candidate));
    } catch (const std::exception& exception) {
        return failed(Status::Failed, "editor.authority.candidate-publish-exception",
                      std::string("Could not publish compensation candidate: ") + exception.what());
    } catch (...) {
        return failed(Status::Failed, "editor.authority.candidate-publish-exception",
                      "Could not publish compensation candidate");
    }
    if (!published.isAccepted()) {
        compensation.diagnostics = std::move(published.diagnostics);
        return failed(published.status, "editor.authority.candidate-publish-rejected",
                      "Target rejected the compensation candidate");
    }

    compensation.state            = TransactionState::Committed;
    compensation.afterRevision    = target_->revision();
    compensation.authorityReceipt = "local:" + std::to_string(receiptSequence_);
    committed_.erase(entry);
    commitOrder_.pop_back();
    if (!commitOrder_.empty()) committed_.at(commitOrder_.back()).receipt.afterRevision = compensation.afterRevision;
    return Result<TransactionReceipt>::applied(std::move(compensation));
}

DomainOperation LocalWorldAuthority::inverseOf(const DomainOperation& operation) {
    DomainOperation inverse = operation;
    if (!operation.inverseType.empty()) inverse.type = operation.inverseType;
    inverse.inverseType = operation.type;
    inverse.payload     = operation.inverse;
    inverse.inverse     = operation.payload;
    return inverse;
}

Result<void> LocalWorldAuthority::rollbackApplied(std::span<const DomainOperation> operations,
                                                        std::size_t                      count) {
    while (count > 0) {
        const DomainOperation& operation = operations[--count];
        if (!operation.hasInverse)
            return Result<void>::error(Status::Failed, RuleId("editor.authority.rollback-impossible"),
                                             "Applied operation has no inverse");
        Result<void> result = target_->applyDomainOperation(inverseOf(operation));
        if (!result.isAccepted()) return result;
    }
    return Result<void>::applied();
}

Result<AuthorityPlan> ReadOnlyAuthority::preflight(const TransactionSpec&           transaction,
                                                         std::span<const DomainOperation> operations) {
    (void)transaction;
    (void)operations;
    return authorityError<AuthorityPlan>(Status::Rejected, "editor.authority.read-only",
                                         "The current editor authority is read-only");
}

Result<TransactionReceipt> ReadOnlyAuthority::commit(const AuthorityPlan& plan) {
    (void)plan;
    return authorityError<TransactionReceipt>(Status::Rejected, "editor.authority.read-only",
                                              "The current editor authority is read-only");
}

Result<TransactionReceipt> ReadOnlyAuthority::compensate(const TransactionReceipt& receipt) {
    (void)receipt;
    return authorityError<TransactionReceipt>(Status::Rejected, "editor.authority.read-only",
                                              "The current editor authority is read-only");
}

}  // namespace eve::editing
