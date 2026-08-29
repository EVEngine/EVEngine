#include "editor/EditorTransactionService.h"

#include <utility>

namespace eve::editor {
namespace {

EditorStatus editorStatus(eve::StatusCode status) noexcept {
    switch (status) {
        case eve::StatusCode::Rejected: return EditorStatus::Rejected;
        case eve::StatusCode::Conflict: return EditorStatus::Conflict;
        case eve::StatusCode::NotFound: return EditorStatus::NotFound;
        case eve::StatusCode::Unsupported: return EditorStatus::Unsupported;
        case eve::StatusCode::Cancelled: return EditorStatus::Cancelled;
        case eve::StatusCode::Ok:
        case eve::StatusCode::Applied:
        case eve::StatusCode::NoOp:
        case eve::StatusCode::Pending:
        case eve::StatusCode::Failed: return EditorStatus::Failed;
    }
    return EditorStatus::Failed;
}

RuleId diagnosticRule(const eve::Diagnostic& diagnostic) {
    if (!diagnostic.path().empty()) return RuleId(diagnostic.path());
    std::string rule = "editor.transaction.";
    rule.append(eve::diagnosticCodeName(diagnostic.code()));
    return RuleId(std::move(rule));
}

template <class Output>
EditorResult<Output> projectFailure(const eve::Status& status) {
    EditorResult<Output> result;
    result.status = editorStatus(status.code());
    for (const auto& diagnostic : status.diagnostics())
        result.diagnostics.push_back({diagnosticRule(diagnostic), DiagnosticSeverity::Error, diagnostic.message()});
    return result;
}

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
    if (!result.ok()) return projectFailure<TransactionId>(result.status());
    return EditorResult<TransactionId>::applied(std::move(result).takeValue());
}

EditorResult<void> LocalTransactionBackend::project(eve::Result<void>&& result) {
    if (!result.ok()) return projectFailure<void>(result.status());
    return EditorResult<void>::applied();
}

EditorResult<TransactionReceipt> LocalTransactionBackend::project(eve::Result<EditorTransactionRecord>&& result) {
    if (!result.ok()) return projectFailure<TransactionReceipt>(result.status());
    EditorTransactionRecord record = std::move(result).takeValue();
    return EditorResult<TransactionReceipt>::applied(projectRecord(record));
}

EditorResult<EditorDryRunReport> LocalTransactionBackend::project(eve::Result<EditorDryRunReport>&& result) {
    if (!result.ok()) return projectFailure<EditorDryRunReport>(result.status());
    return EditorResult<EditorDryRunReport>::applied(std::move(result).takeValue());
}

EditorResult<void> LocalTransactionBackend::setAuthority(IEditAuthority* authority) {
    return project(consumer_.setAuthority(authority));
}

EditorResult<TransactionId> LocalTransactionBackend::begin(TransactionSpec specification) {
    if (specification.id.empty() || specification.target.empty())
        return EditorResult<TransactionId>::error(EditorStatus::Rejected,
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
