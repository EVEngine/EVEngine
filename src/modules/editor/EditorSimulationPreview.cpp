#include "editor/EditorSimulationPreview.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <utility>
#include "common/Exception.h"
#include "editor/EditorToolbar.h"
#include "editor/EditorTransactionService.h"
#include "editor/EditorTransactions.h"

// EditorSimulationPreview.cpp
namespace eve::editor {

Result<void> SimulationPreviewController::setFixedDelta(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0.0 || seconds > 1.0)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.simulation.invalid-fixed-delta"),
                                          "Simulation preview delta must be finite and within (0, 1] seconds");
    fixedDelta_ = seconds;
    return eve::editing::applied<void>();
}

Result<SimulationPreviewFrame> SimulationPreviewController::advance(IEditorSimulationBackend& backend,
                                                                    std::uint64_t             tick) const {
    auto stepped = backend.step(tick, fixedDelta_);
    if (!stepped.ok()) return Result<SimulationPreviewFrame>::failure(stepped.status());
    auto captured = backend.capture();
    if (!captured.ok()) return Result<SimulationPreviewFrame>::failure(captured.status());
    std::set<std::string> ids;
    for (const auto& object : captured.value()) {
        if (object.object.empty() || !ids.insert(object.object).second)
            return eve::editing::failed<SimulationPreviewFrame>(
                EditorStatus::Conflict, RuleId("editor.simulation.unstable-object-id"),
                "Simulation preview samples require unique stable object ids");
        const double values[]{object.positionX, object.positionY, object.positionZ, object.rotationX,
                              object.rotationY, object.rotationZ, object.rotationW};
        if (std::any_of(std::begin(values), std::end(values), [](double value) { return !std::isfinite(value); }))
            return eve::editing::failed<SimulationPreviewFrame>(EditorStatus::Failed,
                                                                RuleId("editor.simulation.nonfinite-sample"),
                                                                "Simulation preview produced a non-finite transform");
    }
    std::sort(captured.value().begin(), captured.value().end(),
              [](const auto& a, const auto& b) { return a.object < b.object; });
    return eve::editing::applied<SimulationPreviewFrame>(
        {tick, static_cast<double>(tick) * fixedDelta_, std::move(captured.value())});
}

Result<SimulationPreviewFrame> SimulationPreviewController::singleStep(const IEditorSimulationBackend& source) {
    if (!previewBackend_) previewBackend_ = source.cloneForPreview();
    if (!previewBackend_)
        return eve::editing::failed<SimulationPreviewFrame>(EditorStatus::Unsupported,
                                                            RuleId("editor.simulation.clone-unsupported"),
                                                            "Simulation backend cannot create an isolated preview");
    auto result = advance(*previewBackend_, tick_ + 1);
    if (result.ok()) ++tick_;
    return result;
}

void SimulationPreviewController::rewind() {
    tick_ = 0;
    previewBackend_.reset();
}

Result<SimulationPreviewFrame> SimulationPreviewController::update(const IEditorSimulationBackend& source) {
    if (paused_) {
        return Result<SimulationPreviewFrame>::success(
            SimulationPreviewFrame{tick_, static_cast<double>(tick_) * fixedDelta_, {}},
            eve::Status::success(EditorStatus::NoOp));
    }
    return singleStep(source);
}

SimulationBakeResult SimulationPreviewController::bake(const IEditorSimulationBackend& source,
                                                       std::uint64_t sourceRevision, int steps, int maxSteps) const {
    SimulationBakeResult result;
    result.sourceRevision = sourceRevision;
    result.fixedDelta     = fixedDelta_;
    if (steps < 0 || maxSteps <= 0 || steps > maxSteps) {
        result.status = EditorStatus::Rejected;
        result.diagnostics.push_back(eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::InvalidArgument, RuleId("editor.simulation.bake-budget"), DiagnosticSeverity::Error,
            "Simulation bake step count exceeds the configured budget"));
        return result;
    }
    auto backend = source.cloneForPreview();
    if (!backend) {
        result.status = EditorStatus::Unsupported;
        result.diagnostics.push_back(eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::Unsupported, RuleId("editor.simulation.clone-unsupported"), DiagnosticSeverity::Error,
            "Simulation backend cannot create an isolated preview"));
        return result;
    }
    auto initial = backend->capture();
    if (!initial.ok()) {
        result.status      = initial.code();
        result.diagnostics = initial.diagnostics();
        return result;
    }
    std::sort(initial.value().begin(), initial.value().end(),
              [](const auto& a, const auto& b) { return a.object < b.object; });
    result.frames.push_back({0, 0.0, std::move(initial.value())});
    for (int index = 1; index <= steps; ++index) {
        auto frame = advance(*backend, static_cast<std::uint64_t>(index));
        if (!frame.ok()) {
            result.status      = frame.code();
            result.diagnostics = frame.diagnostics();
            return result;
        }
        result.frames.push_back(std::move(frame.value()));
    }
    result.status = EditorStatus::Applied;
    return result;
}

}  // namespace eve::editor

// EditorToolbar.cpp
namespace eve::editor {

void EditorToolbar::clear() {
    tools_.clear();
    active_.clear();
}

int EditorToolbar::findIndex(const std::string& id) const {
    for (int i = 0; i < static_cast<int>(tools_.size()); ++i) {
        if (tools_[i].id == id) return i;
    }
    return -1;
}

void EditorToolbar::addTool(const std::string& id, const std::string& label) {
    if (id.empty()) throw Exception("EditorToolbar::addTool: empty id");
    if (findIndex(id) >= 0) throw Exception("EditorToolbar::addTool: duplicate id");
    tools_.push_back(Tool{id, label, "", ""});
    if (active_.empty()) active_ = id;
}

void EditorToolbar::setIcon(const std::string& id, const std::string& icon) {
    int i = findIndex(id);
    if (i < 0) throw Exception("EditorToolbar::setIcon: unknown id");
    tools_[i].icon = icon;
}

void EditorToolbar::setShortcut(const std::string& id, const std::string& key) {
    int i = findIndex(id);
    if (i < 0) throw Exception("EditorToolbar::setShortcut: unknown id");
    tools_[i].shortcut = key;
}

bool EditorToolbar::setActive(const std::string& id) {
    if (findIndex(id) < 0) return false;
    active_ = id;
    return true;
}

bool EditorToolbar::matchShortcut(const std::string& key) {
    for (const auto& t : tools_) {
        if (!t.shortcut.empty() && t.shortcut == key) {
            active_ = t.id;
            return true;
        }
    }
    return false;
}

std::string EditorToolbar::getToolId(int index) const {
    if (index < 0 || index >= static_cast<int>(tools_.size())) throw Exception("EditorToolbar::getToolId: bad index");
    return tools_[index].id;
}

std::string EditorToolbar::getToolLabel(int index) const {
    if (index < 0 || index >= static_cast<int>(tools_.size()))
        throw Exception("EditorToolbar::getToolLabel: bad index");
    return tools_[index].label;
}

std::string EditorToolbar::getToolIcon(int index) const {
    if (index < 0 || index >= static_cast<int>(tools_.size())) throw Exception("EditorToolbar::getToolIcon: bad index");
    return tools_[index].icon;
}

std::string EditorToolbar::getToolShortcut(int index) const {
    if (index < 0 || index >= static_cast<int>(tools_.size()))
        throw Exception("EditorToolbar::getToolShortcut: bad index");
    return tools_[index].shortcut;
}

}  // namespace eve::editor

// EditorTransactionService.cpp
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

Result<void> LocalTransactionBackend::project(eve::Result<void>&& result) { return std::move(result); }

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

// EditorTransactions.cpp
namespace eve::editor {

eve::Result<TransactionId> EditorTransactions::beginTransaction(TransactionSpec specification) {
    return consumer_.begin(std::move(specification));
}

eve::Result<TransactionId> EditorTransactions::beginTransaction(std::string label) {
    return consumer_.beginLegacy(std::move(label));
}

eve::Result<void> EditorTransactions::append(std::unique_ptr<IEditCommand> command) {
    return consumer_.appendPreview(std::move(command));
}

eve::Result<EditorTransactionRecord> EditorTransactions::commitTransaction() { return consumer_.commit(); }

eve::Result<void> EditorTransactions::rollbackTransaction() { return consumer_.rollback(); }

eve::Result<EditorTransactionRecord> EditorTransactions::undoTransaction() { return consumer_.undo(); }

eve::Result<EditorTransactionRecord> EditorTransactions::redoTransaction() { return consumer_.redo(); }

bool EditorTransactions::begin(const std::string& name) {
    auto result = beginTransaction(name);
    return result.ok();
}
bool EditorTransactions::execute(std::unique_ptr<IEditCommand> command) {
    auto result = append(std::move(command));
    return result.ok();
}
bool EditorTransactions::commit() {
    auto result = commitTransaction();
    return result.ok();
}
bool EditorTransactions::rollback() {
    auto result = rollbackTransaction();
    return result.ok();
}
bool EditorTransactions::undo() {
    auto result = undoTransaction();
    return result.ok();
}
bool EditorTransactions::redo() {
    auto result = redoTransaction();
    return result.ok();
}
void EditorTransactions::clear() { consumer_.clear(); }

}  // namespace eve::editor
