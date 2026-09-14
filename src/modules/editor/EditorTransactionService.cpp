#include "editor/EditorTransactionService.h"

#include "editor/EditorResultProjection.h"

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

EditorResult<TransactionId> LocalTransactionBackend::project(eve::Result<TransactionId>&& result) {
    return projectCommonResult(std::move(result));
}

EditorResult<void> LocalTransactionBackend::project(eve::Result<void>&& result) {
    return projectCommonResult(std::move(result));
}

EditorResult<TransactionReceipt> LocalTransactionBackend::project(eve::Result<EditorTransactionRecord>&& result) {
    if (!result.ok()) return projectCommonFailure<TransactionReceipt>(result.status());
    EditorTransactionRecord record = std::move(result).takeValue();
    return eve::editing::applied<TransactionReceipt>(projectRecord(record));
}

EditorResult<EditorDryRunReport> LocalTransactionBackend::project(eve::Result<EditorDryRunReport>&& result) {
    return projectCommonResult(std::move(result));
}

EditorResult<void> LocalTransactionBackend::setAuthority(IEditAuthority* authority) {
    return project(consumer_.setAuthority(authority));
}

EditorResult<TransactionId> LocalTransactionBackend::begin(TransactionSpec specification) {
    if (specification.id.empty() || specification.target.empty())
        return eve::editing::failed<TransactionId>(EditorStatus::Rejected,
                                                  RuleId("editor.transaction.invalid-specification"),
                                                  "Transaction id and target are required");
    return project(consumer_.begin(std::move(specification)));
}

EditorResult<void> LocalTransactionBackend::append(DomainOperation operation) {
    return project(consumer_.append(std::move(operation)));
}

EditorResult<EditorDryRunReport> LocalTransactionBackend::preview() { return project(consumer_.dryRun()); }

EditorResult<TransactionReceipt> LocalTransactionBackend::commit() { return project(consumer_.commit()); }

EditorResult<void> LocalTransactionBackend::discard() { return project(consumer_.discard()); }

EditorResult<void> LocalTransactionBackend::rollback() { return discard(); }

EditorResult<TransactionReceipt> LocalTransactionBackend::retry() { return project(consumer_.retry()); }

EditorResult<TransactionReceipt> LocalTransactionBackend::undo() { return project(consumer_.undo()); }

EditorResult<TransactionReceipt> LocalTransactionBackend::redo() { return project(consumer_.redo()); }

}  // namespace eve::editor
