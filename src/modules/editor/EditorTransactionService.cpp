#include "editor/EditorTransactionService.h"

#include <utility>

namespace eve::editor {
namespace {

TransactionState transactionState(eve::transaction::CoordinatorState state) noexcept {
    switch (state) {
        case eve::transaction::CoordinatorState::Committed: return TransactionState::Committed;
        case eve::transaction::CoordinatorState::Compensated: return TransactionState::RolledBack;
        case eve::transaction::CoordinatorState::CompensationFailed:
        case eve::transaction::CoordinatorState::RollbackFailed:
        case eve::transaction::CoordinatorState::PartiallyCommitted: return TransactionState::Failed;
        case eve::transaction::CoordinatorState::RolledBack: return TransactionState::RolledBack;
    }
    return TransactionState::Failed;
}

TransactionReceipt projectRecord(const EditorTransactionRecord& record) {
    TransactionReceipt result;
    result.id    = record.specification.id;
    result.state = transactionState(record.coordinator.state);
    if (record.authorityReceipt) {
        result.beforeRevision   = record.authorityReceipt->beforeRevision;
        result.afterRevision    = record.authorityReceipt->afterRevision;
        result.affectedObjects  = record.authorityReceipt->affectedObjects;
        result.diagnostics      = record.authorityReceipt->diagnostics;
        result.authorityReceipt = record.authorityReceipt->authorityReceipt;
    }
    return result;
}

}  // namespace

Result<TransactionId> LocalTransactionBackend::project(eve::Result<TransactionId>&& result) {
    // Result is eve::Result; no projection envelope exists at this boundary.
    // Named rvalue-ref parameters are lvalues; move explicitly (not a local NRVO candidate).
    return std::move(result);
}

Result<void> LocalTransactionBackend::project(eve::Result<void>&& result) {
    return std::move(result);
}

Result<TransactionReceipt> LocalTransactionBackend::project(eve::Result<EditorTransactionRecord>&& result) {
    if (!result.ok()) return Result<TransactionReceipt>::failure(result.status());
    EditorTransactionRecord record = std::move(result).takeValue();
    return eve::editing::applied<TransactionReceipt>(projectRecord(record));
}

Result<EditorDryRunReport> LocalTransactionBackend::project(eve::Result<EditorDryRunReport>&& result) {
    return std::move(result);
}

Result<void> LocalTransactionBackend::setAuthority(IEditAuthority* authority) {
    return project(consumer_.setAuthority(authority));
}

Result<TransactionId> LocalTransactionBackend::begin(TransactionSpec specification) {
    if (specification.id.empty() || specification.target.empty())
        return eve::editing::failed<TransactionId>(EditorStatus::Rejected,
                                                  RuleId("editor.transaction.invalid-specification"),
                                                  "Transaction id and target are required");
    return project(consumer_.begin(std::move(specification)));
}

Result<void> LocalTransactionBackend::append(DomainOperation operation) {
    return project(consumer_.append(std::move(operation)));
}

Result<EditorDryRunReport> LocalTransactionBackend::preview() { return project(consumer_.dryRun()); }

Result<TransactionReceipt> LocalTransactionBackend::commit() { return project(consumer_.commit()); }

Result<void> LocalTransactionBackend::discard() { return project(consumer_.discard()); }

Result<void> LocalTransactionBackend::rollback() { return discard(); }

Result<TransactionReceipt> LocalTransactionBackend::retry() { return project(consumer_.retry()); }

Result<TransactionReceipt> LocalTransactionBackend::undo() { return project(consumer_.undo()); }

Result<TransactionReceipt> LocalTransactionBackend::redo() { return project(consumer_.redo()); }

}  // namespace eve::editor
