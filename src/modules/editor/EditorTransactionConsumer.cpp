#include "editor/EditorTransactionConsumer.h"

#include <charconv>
#include <exception>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace eve::editor {
namespace {

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

eve::StatusCode commonStatus(EditorStatus status) noexcept {
    switch (status) {
    case EditorStatus::Rejected: return eve::StatusCode::Rejected;
    case EditorStatus::Conflict: return eve::StatusCode::Conflict;
    case EditorStatus::NotFound: return eve::StatusCode::NotFound;
    case EditorStatus::Unsupported: return eve::StatusCode::Unsupported;
    case EditorStatus::Cancelled: return eve::StatusCode::Cancelled;
    case EditorStatus::Applied:
    case EditorStatus::Pending:
    case EditorStatus::NoOp:
    case EditorStatus::Failed: return eve::StatusCode::Failed;
    }
    return eve::StatusCode::Failed;
}

eve::DiagnosticCode commonDiagnostic(EditorStatus status) noexcept {
    switch (status) {
    case EditorStatus::Rejected: return eve::DiagnosticCode::InvalidArgument;
    case EditorStatus::Conflict: return eve::DiagnosticCode::Conflict;
    case EditorStatus::NotFound: return eve::DiagnosticCode::NotFound;
    case EditorStatus::Unsupported: return eve::DiagnosticCode::Unsupported;
    case EditorStatus::Cancelled: return eve::DiagnosticCode::Cancelled;
    case EditorStatus::Applied:
    case EditorStatus::Pending:
    case EditorStatus::NoOp:
    case EditorStatus::Failed: return eve::DiagnosticCode::Failed;
    }
    return eve::DiagnosticCode::Failed;
}

template <class Output, class Input>
eve::Result<Output> convertEditorFailure(const EditorResult<Input>& source, std::string_view context) {
    std::vector<eve::Diagnostic> diagnostics;
    const eve::DiagnosticCode fallbackCode = commonDiagnostic(source.status);
    for (const auto& diagnostic : source.diagnostics) {
        diagnostics.push_back(eve::Diagnostic::error(
            fallbackCode, std::string(context) + ": " + diagnostic.message, diagnostic.rule.value()));
    }
    if (diagnostics.empty())
        diagnostics.push_back(eve::Diagnostic::error(fallbackCode, std::string(context)));
    return eve::Result<Output>::failure(eve::Status(commonStatus(source.status), std::move(diagnostics)));
}

bool hasCoordinatorCleanupFailure(const eve::Status& status) {
    for (const auto& diagnostic : status.diagnostics()) {
        if (diagnostic.path().starts_with("transaction.rollback") ||
            diagnostic.path().starts_with("transaction.compensation"))
            return true;
    }
    return false;
}

std::optional<std::size_t> coordinatorFailedCommitIndex(const eve::Status& status) noexcept {
    constexpr std::string_view prefix = "transaction.commit[";
    for (const auto& diagnostic : status.diagnostics()) {
        const std::string& path = diagnostic.path();
        if (!path.starts_with(prefix) || path.back() != ']') continue;
        std::uint64_t index = 0;
        const char*    first = path.data() + prefix.size();
        const char*    last  = path.data() + path.size() - 1;
        const auto [end, error] = std::from_chars(first, last, index);
        if (error == std::errc{} && end == last &&
            index <= static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
            return static_cast<std::size_t>(index);
    }
    return std::nullopt;
}

bool hasPreviouslyCommittedAdditionalParticipant(const eve::Status& status,
                                                 std::size_t editorParticipantCount) noexcept {
    const auto failedIndex = coordinatorFailedCommitIndex(status);
    // Editor-owned participants are rebuilt for every attempt. A borrowed
    // additional participant that precedes the failing commit is different:
    // its effect may already have escaped, and retaining only its name is not
    // enough to prove that a retry is idempotent. Require explicit discard or
    // reconciliation instead of invoking that participant twice.
    return failedIndex.has_value() && *failedIndex > editorParticipantCount;
}

struct CommandBatch {
    std::vector<std::unique_ptr<IEditCommand>> commands;
    bool                                  previewApplied = false;
};

class CommandParticipant final : public transaction::ITransactionParticipant {
public:
    enum class InitialPhase { Idle, Committed };

    explicit CommandParticipant(CommandBatch& batch, InitialPhase initial = InitialPhase::Idle)
        : batch_(batch), phase_(initial == InitialPhase::Committed ? Phase::Committed : Phase::Idle) {}

    std::string_view name() const noexcept override { return "editor.commands"; }

    [[nodiscard]] eve::Result<void> prepare(const transaction::TransactionContext&) override {
        if (phase_ != Phase::Idle)
            return failure<void>(eve::DiagnosticCode::PreconditionViolation,
                                 "editor command participant is not idle", "editor.commands.prepare");
        if (batch_.commands.empty())
            return failure<void>(eve::DiagnosticCode::InvalidArgument,
                                 "editor transaction must contain at least one command", "editor.commands");
        for (const auto& command : batch_.commands) {
            if (!command)
                return failure<void>(eve::DiagnosticCode::InvalidArgument,
                                     "editor command must not be null", "editor.commands");
        }
        phase_ = Phase::Prepared;
        return eve::Result<void>::success();
    }

    [[nodiscard]] eve::Result<void> commit(const transaction::TransactionContext&) override {
        if (phase_ != Phase::Prepared)
            return failure<void>(eve::DiagnosticCode::PreconditionViolation,
                                 "editor command participant is not prepared", "editor.commands.commit");
        if (!batch_.previewApplied) {
            std::size_t applied = 0;
            try {
                for (const auto& command : batch_.commands) {
                    if (!command->apply()) {
                        const bool reverted = revertPrefix(applied);
                        if (!reverted) batch_.previewApplied = true;
                        return failure<void>(
                            reverted ? eve::DiagnosticCode::InvalidArgument : eve::DiagnosticCode::Failed,
                            reverted ? "editor command rejected during commit"
                                     : "editor command commit failed and compensation was incomplete",
                            "editor.commands.commit");
                    }
                    ++applied;
                }
            } catch (const std::exception& exception) {
                const bool reverted = revertPrefix(applied);
                if (!reverted) batch_.previewApplied = true;
                return failure<void>(eve::DiagnosticCode::Failed,
                                     std::string("editor command threw during commit: ") + exception.what(),
                                     "editor.commands.commit");
            } catch (...) {
                const bool reverted = revertPrefix(applied);
                if (!reverted) batch_.previewApplied = true;
                return failure<void>(eve::DiagnosticCode::Failed,
                                     "editor command threw during commit", "editor.commands.commit");
            }
            batch_.previewApplied = true;
        }
        phase_ = Phase::Committed;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> rollback(const transaction::TransactionContext&) override {
        if (phase_ != Phase::Prepared)
            return failure<void>(eve::DiagnosticCode::PreconditionViolation,
                                 "editor command participant is not prepared", "editor.commands.rollback");
        if (batch_.previewApplied && !revertAll())
            return failure<void>(eve::DiagnosticCode::Failed,
                                 "editor command preview rollback was incomplete", "editor.commands.rollback");
        batch_.previewApplied = false;
        phase_               = Phase::RolledBack;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> compensate(const transaction::TransactionContext&) override {
        if (phase_ != Phase::Committed)
            return failure<void>(eve::DiagnosticCode::PreconditionViolation,
                                 "editor command participant is not committed", "editor.commands.compensate");
        if (!batch_.previewApplied || !revertAll())
            return failure<void>(eve::DiagnosticCode::Failed,
                                 "editor command compensation was incomplete", "editor.commands.compensate");
        batch_.previewApplied = false;
        phase_               = Phase::Compensated;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

private:
    enum class Phase { Idle, Prepared, Committed, RolledBack, Compensated };

    bool revertPrefix(std::size_t count) noexcept {
        try {
            while (count > 0) batch_.commands[--count]->revert();
            return true;
        } catch (...) {
            return false;
        }
    }

    bool revertAll() noexcept { return revertPrefix(batch_.commands.size()); }

    CommandBatch& batch_;
    Phase         phase_ = Phase::Idle;
};

class AuthorityParticipant final : public transaction::ITransactionParticipant {
public:
    AuthorityParticipant(IEditAuthority& authority, TransactionSpec specification,
                         std::vector<DomainOperation> operations)
        : authority_(&authority), specification_(std::move(specification)), operations_(std::move(operations)) {}

    AuthorityParticipant(IEditAuthority& authority, TransactionSpec specification,
                         std::vector<DomainOperation> operations, TransactionReceipt receipt)
        : authority_(&authority), specification_(std::move(specification)), operations_(std::move(operations)),
          receipt_(std::move(receipt)), phase_(Phase::Committed) {}

    std::string_view name() const noexcept override { return "editor.authority"; }

    [[nodiscard]] eve::Result<void> prepare(const transaction::TransactionContext&) override {
        if (phase_ != Phase::Idle)
            return failure<void>(eve::DiagnosticCode::PreconditionViolation,
                                 "editor authority participant is not idle", "editor.authority.prepare");
        if (!authority_)
            return failure<void>(eve::DiagnosticCode::Failed,
                                 "editor authority participant has no authority", "editor.authority");
        auto plan = authority_->preflight(specification_, operations_);
        if (!plan.accepted() || !plan.value)
            return convertEditorFailure<void>(plan, "editor authority preflight");
        plan_  = std::move(*plan.value);
        phase_ = Phase::Prepared;
        return eve::Result<void>::success();
    }

    [[nodiscard]] eve::Result<void> commit(const transaction::TransactionContext&) override {
        if (phase_ != Phase::Prepared || !plan_)
            return failure<void>(eve::DiagnosticCode::PreconditionViolation,
                                 "editor authority participant is not prepared", "editor.authority.commit");
        auto result = authority_->commit(*plan_);
        if (!result.accepted() || !result.value)
            return convertEditorFailure<void>(result, "editor authority commit");
        receipt_ = std::move(*result.value);
        phase_   = Phase::Committed;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> rollback(const transaction::TransactionContext&) override {
        if (phase_ != Phase::Prepared)
            return failure<void>(eve::DiagnosticCode::PreconditionViolation,
                                 "editor authority participant is not prepared", "editor.authority.rollback");
        plan_.reset();
        phase_ = Phase::RolledBack;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> compensate(const transaction::TransactionContext&) override {
        if (phase_ != Phase::Committed || !receipt_)
            return failure<void>(eve::DiagnosticCode::PreconditionViolation,
                                 "editor authority participant has no committed receipt",
                                 "editor.authority.compensate");
        auto result = authority_->compensate(*receipt_);
        if (!result.accepted() || !result.value)
            return convertEditorFailure<void>(result, "editor authority compensation");
        // History keeps the original commit receipt.  This participant's
        // latest effect is the compensation receipt, whose afterRevision is
        // also the base revision required by a later redo.
        receipt_ = std::move(*result.value);
        phase_ = Phase::Compensated;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] const std::optional<TransactionReceipt>& receipt() const noexcept { return receipt_; }

private:
    enum class Phase { Idle, Prepared, Committed, RolledBack, Compensated };

    IEditAuthority*                    authority_ = nullptr;
    TransactionSpec                    specification_;
    std::vector<DomainOperation>       operations_;
    std::optional<AuthorityPlan>       plan_;
    std::optional<TransactionReceipt>  receipt_;
    Phase                              phase_ = Phase::Idle;
};

}  // namespace

struct EditorTransactionConsumer::Impl {
    struct Pending {
        TransactionSpec              specification;
        std::unique_ptr<CommandBatch> commands;
        std::vector<DomainOperation> operations;
        bool                         commandModeSet = false;
        bool                         previewMode    = false;
        std::vector<std::string>     additionalNames;
        std::uint64_t                attemptSequence = 0;
    };

    struct HistoryEntry {
        TransactionSpec              specification;
        std::unique_ptr<CommandBatch> commands;
        std::vector<DomainOperation> operations;
        std::optional<TransactionReceipt> authorityReceipt;
    };

    explicit Impl(IEditAuthority* authorityValue) : authority(authorityValue) {}

    IEditAuthority*          authority = nullptr;
    std::optional<Pending>   pending;
    std::vector<HistoryEntry> undo;
    std::vector<HistoryEntry> redo;
    transaction::Coordinator coordinator;
    std::uint64_t             nextLegacyId = 1;
    std::uint64_t             redoSequence = 0;
    EditorCommitState          state        = EditorCommitState::Discarded;
    std::vector<eve::Diagnostic> diagnostics;
    bool                        retryAllowed = false;
    bool                        retryBlockedByCommittedParticipant = false;
};

EditorTransactionConsumer::EditorTransactionConsumer(IEditAuthority* authority)
    : impl_(std::make_unique<Impl>(authority)) {}

EditorTransactionConsumer::~EditorTransactionConsumer() = default;

eve::Result<void> EditorTransactionConsumer::setAuthority(IEditAuthority* authority) {
    if (impl_->pending)
        return failure<void>(eve::DiagnosticCode::PreconditionViolation,
                             "editor authority cannot change during an active transaction",
                             "editor.authority");
    impl_->authority = authority;
    return eve::Result<void>::success();
}

eve::Result<TransactionId> EditorTransactionConsumer::begin(TransactionSpec specification) {
    if (impl_->pending)
        return failure<TransactionId>(eve::DiagnosticCode::PreconditionViolation,
                                     "an editor transaction is already active", "editor.transaction");
    if (specification.id.empty())
        return failure<TransactionId>(eve::DiagnosticCode::InvalidArgument,
                                     "editor transaction id is required", "editor.transaction.id");
    const TransactionId id = specification.id;
    Impl::Pending pending;
    pending.specification = std::move(specification);
    impl_->pending        = std::move(pending);
    impl_->state           = EditorCommitState::Pending;
    impl_->diagnostics.clear();
    impl_->retryAllowed = false;
    impl_->retryBlockedByCommittedParticipant = false;
    return eve::Result<TransactionId>::success(id);
}

eve::Result<TransactionId> EditorTransactionConsumer::beginLegacy(std::string label) {
    if (impl_->nextLegacyId == std::numeric_limits<std::uint64_t>::max())
        return failure<TransactionId>(eve::DiagnosticCode::Failed,
                                     "legacy editor transaction id allocator is exhausted",
                                     "editor.transaction.id");
    TransactionSpec specification;
    specification.id    = TransactionId("editor.legacy." + std::to_string(impl_->nextLegacyId++));
    specification.label = std::move(label);
    return begin(std::move(specification));
}

eve::Result<void> EditorTransactionConsumer::append(std::unique_ptr<IEditCommand> command) {
    if (!impl_->pending)
        return failure<void>(eve::DiagnosticCode::PreconditionViolation,
                             "no editor transaction is active", "editor.transaction");
    if (!command)
        return failure<void>(eve::DiagnosticCode::InvalidArgument, "editor command must not be null",
                             "editor.command");
    if (!impl_->pending->operations.empty())
        return failure<void>(eve::DiagnosticCode::Conflict,
                             "commands and authority operations cannot share one editor transaction",
                             "editor.transaction");
    if (impl_->pending->commandModeSet && impl_->pending->previewMode)
        return failure<void>(eve::DiagnosticCode::Conflict,
                             "a strict command cannot follow a preview command", "editor.transaction");
    impl_->pending->commandModeSet = true;
    impl_->pending->previewMode    = false;
    if (!impl_->pending->commands) impl_->pending->commands = std::make_unique<CommandBatch>();
    if (!impl_->pending->commands->commands.empty()) {
        try {
            if (impl_->pending->commands->commands.back()->mergeWith(*command))
                return eve::Result<void>::success();
        } catch (const std::exception& exception) {
            return failure<void>(eve::DiagnosticCode::Failed,
                                 std::string("editor command merge threw: ") + exception.what(),
                                 "editor.command.merge");
        } catch (...) {
            return failure<void>(eve::DiagnosticCode::Failed, "editor command merge threw",
                                 "editor.command.merge");
        }
    }
    impl_->pending->commands->commands.push_back(std::move(command));
    return eve::Result<void>::success();
}

eve::Result<void> EditorTransactionConsumer::appendPreview(std::unique_ptr<IEditCommand> command) {
    if (!impl_->pending)
        return failure<void>(eve::DiagnosticCode::PreconditionViolation,
                             "no editor transaction is active", "editor.transaction");
    if (!command)
        return failure<void>(eve::DiagnosticCode::InvalidArgument, "editor command must not be null",
                             "editor.command");
    if (!impl_->pending->operations.empty())
        return failure<void>(eve::DiagnosticCode::Conflict,
                             "commands and authority operations cannot share one editor transaction",
                             "editor.transaction");
    if (impl_->pending->commandModeSet && !impl_->pending->previewMode)
        return failure<void>(eve::DiagnosticCode::Conflict,
                             "a preview command cannot follow a strict command", "editor.transaction");
    if (!impl_->pending->commands) impl_->pending->commands = std::make_unique<CommandBatch>();

    // A command is applied only after merge evaluation has completed on an
    // owning candidate copy. The candidate carries the aggregate history but
    // never publishes to the real target; the incoming command is the only
    // object that applies the next preview delta.
    std::unique_ptr<IEditCommand> mergedCandidate;
    if (!impl_->pending->commands->commands.empty()) {
        try {
            mergedCandidate = impl_->pending->commands->commands.back()->clone();
        } catch (const std::exception& exception) {
            return failure<void>(eve::DiagnosticCode::Failed,
                                 std::string("editor command staging threw: ") + exception.what(),
                                 "editor.command.stage");
        } catch (...) {
            return failure<void>(eve::DiagnosticCode::Failed, "editor command staging threw",
                                 "editor.command.stage");
        }
        if (mergedCandidate) {
            try {
                if (mergedCandidate->mergeWith(*command)) {
                    // The merge has changed only mergedCandidate. Applying the
                    // incoming delta keeps the live target at the same state
                    // that the command recorded as its `before` value.
                    bool applied = false;
                    try {
                        applied = command->apply();
                    } catch (const std::exception& exception) {
                        try {
                            command->revert();
                        } catch (...) {
                            return failure<void>(eve::DiagnosticCode::Failed,
                                                 "editor command preview failed and could not be reverted",
                                                 "editor.command.preview");
                        }
                        return failure<void>(eve::DiagnosticCode::Failed,
                                             std::string("editor command preview threw: ") + exception.what(),
                                             "editor.command.preview");
                    } catch (...) {
                        try {
                            command->revert();
                        } catch (...) {
                            return failure<void>(eve::DiagnosticCode::Failed,
                                                 "editor command preview failed and could not be reverted",
                                                 "editor.command.preview");
                        }
                        return failure<void>(eve::DiagnosticCode::Failed, "editor command preview threw",
                                             "editor.command.preview");
                    }
                    if (!applied) {
                        try {
                            command->revert();
                        } catch (...) {
                            return failure<void>(eve::DiagnosticCode::Failed,
                                                 "editor command preview was rejected and could not be reverted",
                                                 "editor.command.preview");
                        }
                        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                                             "editor command preview was rejected", "editor.command.preview");
                    }
                    impl_->pending->commands->commands.back() = std::move(mergedCandidate);
                    impl_->pending->commands->previewApplied = true;
                    impl_->pending->commandModeSet = true;
                    impl_->pending->previewMode    = true;
                    return eve::Result<void>::success();
                }
            } catch (const std::exception& exception) {
                return failure<void>(eve::DiagnosticCode::Failed,
                                     std::string("editor command merge threw: ") + exception.what(),
                                     "editor.command.merge");
            } catch (...) {
                return failure<void>(eve::DiagnosticCode::Failed, "editor command merge threw",
                                     "editor.command.merge");
            }
        }
    }

    try {
        // Reserve before applying so vector growth cannot be the first
        // observable failure after the target has been changed.
        impl_->pending->commands->commands.reserve(
            impl_->pending->commands->commands.size() + 1);
    } catch (const std::exception& exception) {
        return failure<void>(eve::DiagnosticCode::Failed,
                             std::string("editor command staging allocation threw: ") + exception.what(),
                             "editor.command.stage");
    } catch (...) {
        return failure<void>(eve::DiagnosticCode::Failed, "editor command staging allocation threw",
                             "editor.command.stage");
    }

    bool applied = false;
    try {
        applied = command->apply();
    } catch (const std::exception& exception) {
        try {
            command->revert();
        } catch (...) {
            return failure<void>(eve::DiagnosticCode::Failed,
                                 "editor command preview failed and could not be reverted",
                                 "editor.command.preview");
        }
        return failure<void>(eve::DiagnosticCode::Failed,
                             std::string("editor command preview threw: ") + exception.what(),
                             "editor.command.preview");
    } catch (...) {
        try {
            command->revert();
        } catch (...) {
            return failure<void>(eve::DiagnosticCode::Failed,
                                 "editor command preview failed and could not be reverted",
                                 "editor.command.preview");
        }
        return failure<void>(eve::DiagnosticCode::Failed, "editor command preview threw",
                             "editor.command.preview");
    }
    if (!applied) {
        try {
            command->revert();
        } catch (...) {
            return failure<void>(eve::DiagnosticCode::Failed,
                                 "editor command preview was rejected and could not be reverted",
                                 "editor.command.preview");
        }
        return failure<void>(eve::DiagnosticCode::InvalidArgument, "editor command preview was rejected",
                             "editor.command.preview");
    }
    impl_->pending->commands->commands.push_back(std::move(command));
    impl_->pending->commands->previewApplied = true;
    impl_->pending->commandModeSet = true;
    impl_->pending->previewMode    = true;
    return eve::Result<void>::success();
}

eve::Result<void> EditorTransactionConsumer::append(DomainOperation operation) {
    if (!impl_->pending)
        return failure<void>(eve::DiagnosticCode::PreconditionViolation,
                             "no editor transaction is active", "editor.transaction");
    if (operation.type.empty() || operation.target.empty())
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "authority operation type and target are required", "editor.operation");
    if (impl_->pending->commands)
        return failure<void>(eve::DiagnosticCode::Conflict,
                             "commands and authority operations cannot share one editor transaction",
                             "editor.transaction");
    if (impl_->pending->specification.target.empty())
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "authority transaction target is required", "editor.transaction.target");
    if (operation.target != impl_->pending->specification.target)
        return failure<void>(eve::DiagnosticCode::Conflict,
                             "authority operation target does not match the transaction target",
                             "editor.operation.target");
    impl_->pending->operations.push_back(std::move(operation));
    return eve::Result<void>::success();
}

eve::Result<EditorDryRunReport> EditorTransactionConsumer::dryRun() const {
    if (!impl_->pending)
        return failure<EditorDryRunReport>(eve::DiagnosticCode::PreconditionViolation,
                                           "no editor transaction is active", "editor.transaction");
    const auto& pending = *impl_->pending;
    const std::size_t commandCount = pending.commands ? pending.commands->commands.size() : 0;
    if (commandCount == 0 && pending.operations.empty())
        return failure<EditorDryRunReport>(eve::DiagnosticCode::InvalidArgument,
                                           "editor transaction has no work", "editor.transaction");
    EditorDryRunReport report;
    report.specification = pending.specification;
    report.commandCount  = commandCount;
    report.operationCount = pending.operations.size();
    if (!pending.operations.empty()) {
        if (!impl_->authority)
            return failure<EditorDryRunReport>(eve::DiagnosticCode::Unsupported,
                                               "authority preflight requires an injected authority",
                                               "editor.authority");
        auto plan = impl_->authority->preflight(pending.specification, pending.operations);
        if (!plan.accepted() || !plan.value)
            return convertEditorFailure<EditorDryRunReport>(plan, "editor authority preflight");
        report.authorityPlan = std::move(*plan.value);
    }
    return eve::Result<EditorDryRunReport>::success(std::move(report));
}

eve::Result<EditorTransactionRecord> EditorTransactionConsumer::commit() {
    return commitAttempt(std::span<transaction::ITransactionParticipant*>{}, false);
}

eve::Result<EditorTransactionRecord> EditorTransactionConsumer::commit(
    std::span<transaction::ITransactionParticipant*> additional) {
    return commitAttempt(additional, false);
}

eve::Result<EditorTransactionRecord> EditorTransactionConsumer::retry(
    std::span<transaction::ITransactionParticipant*> additional) {
    if (!impl_->pending)
        return failure<EditorTransactionRecord>(eve::DiagnosticCode::PreconditionViolation,
                                                "no failed editor transaction is pending",
                                                "editor.transaction.retry");
    if (impl_->state != EditorCommitState::FailedRetryable)
        return failure<EditorTransactionRecord>(eve::DiagnosticCode::PreconditionViolation,
                                                "editor transaction is not awaiting retry",
                                                "editor.transaction.retry");
    if (!impl_->retryAllowed)
        return failure<EditorTransactionRecord>(
            eve::DiagnosticCode::Conflict,
            impl_->retryBlockedByCommittedParticipant
                ? "editor transaction cannot be retried after an additional participant committed; reconcile or discard it"
                : "editor transaction cannot be retried after incomplete cleanup; discard it",
            "editor.transaction.retry");
    return commitAttempt(additional, true);
}

eve::Result<EditorTransactionRecord> EditorTransactionConsumer::commitAttempt(
    std::span<transaction::ITransactionParticipant*> additional, bool retryAttempt) {
    if (!impl_->pending)
        return failure<EditorTransactionRecord>(eve::DiagnosticCode::PreconditionViolation,
                                                "no editor transaction is active", "editor.transaction");

    auto retainFailure = [&](const eve::Status& status, bool retryAllowed) {
        impl_->state        = EditorCommitState::FailedRetryable;
        impl_->retryAllowed = retryAllowed;
        impl_->retryBlockedByCommittedParticipant = false;
        impl_->diagnostics  = status.diagnostics();
        return eve::Result<EditorTransactionRecord>::failure(status);
    };
    auto reject = [&](eve::DiagnosticCode code, std::string message, std::string path) {
        auto result = failure<EditorTransactionRecord>(code, std::move(message), std::move(path));
        const bool preserveRetryBlock = impl_->state == EditorCommitState::FailedRetryable;
        impl_->state        = EditorCommitState::FailedRetryable;
        if (!preserveRetryBlock) {
            impl_->retryAllowed = true;
            impl_->retryBlockedByCommittedParticipant = false;
        }
        impl_->diagnostics  = result.diagnostics();
        return result;
    };

    if (!retryAttempt && impl_->state == EditorCommitState::FailedRetryable)
        return reject(eve::DiagnosticCode::Conflict,
                      "editor commit failed; call retry() or discard() before another commit attempt",
                      "editor.transaction.commit");
    if (retryAttempt && impl_->state != EditorCommitState::FailedRetryable)
        return reject(eve::DiagnosticCode::PreconditionViolation,
                      "editor transaction is not awaiting retry", "editor.transaction.retry");

    Impl::Pending& pending = *impl_->pending;
    const std::size_t commandCount = pending.commands ? pending.commands->commands.size() : 0;
    if (commandCount == 0 && pending.operations.empty())
        return reject(eve::DiagnosticCode::InvalidArgument, "editor transaction has no work", "editor.transaction");
    if (!pending.operations.empty() && !impl_->authority)
        return reject(eve::DiagnosticCode::Unsupported,
                      "authority commit requires an injected authority", "editor.authority");

    for (std::size_t i = 0; i < additional.size(); ++i) {
        if (!additional[i])
            return reject(eve::DiagnosticCode::InvalidArgument, "additional participant must not be null",
                          "editor.transaction.participants[" + std::to_string(i) + "]");
        for (std::size_t previous = 0; previous < i; ++previous) {
            if (additional[previous] == additional[i])
                return reject(eve::DiagnosticCode::Conflict,
                              "an additional participant may occur only once in an editor transaction",
                              "editor.transaction.participants[" + std::to_string(i) + "]");
        }
    }

    if (!retryAttempt) {
        pending.additionalNames.clear();
        pending.additionalNames.reserve(additional.size());
        for (auto* participant : additional)
            pending.additionalNames.emplace_back(participant->name());
    } else {
        if (additional.size() != pending.additionalNames.size())
            return reject(eve::DiagnosticCode::Conflict,
                          "retry must provide the same additional participant set", "editor.transaction.retry");
        for (std::size_t i = 0; i < additional.size(); ++i) {
            if (additional[i]->name() != pending.additionalNames[i])
                return reject(eve::DiagnosticCode::Conflict,
                              "retry participant order or identity does not match the failed attempt",
                              "editor.transaction.retry");
        }
    }

    std::unique_ptr<AuthorityParticipant> authorityParticipant;
    std::unique_ptr<CommandParticipant>   commandParticipant;
    std::vector<transaction::ITransactionParticipant*> participants;
    if (!pending.operations.empty()) {
        authorityParticipant = std::make_unique<AuthorityParticipant>(
            *impl_->authority, pending.specification, pending.operations);
        participants.push_back(authorityParticipant.get());
    }
    if (pending.commands) {
        commandParticipant = std::make_unique<CommandParticipant>(*pending.commands);
        participants.push_back(commandParticipant.get());
    }
    participants.insert(participants.end(), additional.begin(), additional.end());
    if (participants.empty())
        return reject(eve::DiagnosticCode::InvalidArgument, "editor transaction has no participant",
                      "editor.transaction");

    if (retryAttempt) {
        if (pending.attemptSequence == std::numeric_limits<std::uint64_t>::max())
            return reject(eve::DiagnosticCode::Failed, "editor transaction retry sequence is exhausted",
                          "editor.transaction.retry");
        ++pending.attemptSequence;
    }
    std::string attemptId = pending.specification.id.value();
    if (retryAttempt)
        attemptId += ".retry." + std::to_string(pending.attemptSequence);
    transaction::TransactionContext context(std::move(attemptId));
    auto coordinated = impl_->coordinator.execute(context, participants);
    if (!coordinated.ok()) {
        const eve::Status& status = coordinated.status();
        const bool committedAdditional = hasPreviouslyCommittedAdditionalParticipant(
            status, (pending.operations.empty() ? 0u : 1u) + (pending.commands ? 1u : 0u));
        auto result = retainFailure(status, !hasCoordinatorCleanupFailure(status) && !committedAdditional);
        impl_->retryBlockedByCommittedParticipant = committedAdditional;
        return result;
    }
    transaction::TransactionReceipt coordinatorReceipt = std::move(coordinated).takeValue();

    Impl::HistoryEntry history;
    history.specification  = pending.specification;
    history.commands       = std::move(pending.commands);
    history.operations     = std::move(pending.operations);
    if (authorityParticipant) history.authorityReceipt = authorityParticipant->receipt();
    impl_->undo.push_back(std::move(history));
    impl_->redo.clear();
    impl_->pending.reset();
    impl_->state        = EditorCommitState::Committed;
    impl_->retryAllowed = false;
    impl_->retryBlockedByCommittedParticipant = false;
    impl_->diagnostics.clear();

    EditorTransactionRecord record;
    record.coordinator    = std::move(coordinatorReceipt);
    record.specification  = impl_->undo.back().specification;
    record.authorityReceipt = impl_->undo.back().authorityReceipt;
    record.commandCount   = commandCount;
    record.operationCount = impl_->undo.back().operations.size();
    return eve::Result<EditorTransactionRecord>::success(std::move(record));
}

eve::Result<void> EditorTransactionConsumer::discard() {
    if (!impl_->pending)
        return failure<void>(eve::DiagnosticCode::PreconditionViolation,
                             "no editor transaction is active", "editor.transaction");
    Impl::Pending& pending = *impl_->pending;
    if (pending.commands && pending.commands->previewApplied) {
        try {
            for (auto it = pending.commands->commands.rbegin(); it != pending.commands->commands.rend(); ++it)
                (*it)->revert();
        } catch (const std::exception& exception) {
            auto result = failure<void>(eve::DiagnosticCode::Failed,
                                        std::string("editor preview rollback threw: ") + exception.what(),
                                        "editor.transaction.discard");
            impl_->state        = EditorCommitState::FailedRetryable;
            impl_->retryAllowed = false;
            impl_->retryBlockedByCommittedParticipant = false;
            impl_->diagnostics  = result.diagnostics();
            return result;
        } catch (...) {
            auto result = failure<void>(eve::DiagnosticCode::Failed, "editor preview rollback threw",
                                        "editor.transaction.discard");
            impl_->state        = EditorCommitState::FailedRetryable;
            impl_->retryAllowed = false;
            impl_->retryBlockedByCommittedParticipant = false;
            impl_->diagnostics  = result.diagnostics();
            return result;
        }
        pending.commands->previewApplied = false;
    }
    impl_->pending.reset();
    impl_->state        = EditorCommitState::Discarded;
    impl_->retryAllowed = false;
    impl_->retryBlockedByCommittedParticipant = false;
    impl_->diagnostics.clear();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> EditorTransactionConsumer::rollback() {
    return discard();
}

eve::Result<EditorTransactionRecord> EditorTransactionConsumer::undo() {
    if (impl_->pending)
        return failure<EditorTransactionRecord>(eve::DiagnosticCode::PreconditionViolation,
                                                "cannot undo while an editor transaction is active",
                                                "editor.transaction.undo");
    if (impl_->undo.empty())
        return failure<EditorTransactionRecord>(eve::DiagnosticCode::NotFound,
                                                "there is no editor transaction to undo", "editor.history.undo");
    Impl::HistoryEntry history = std::move(impl_->undo.back());
    impl_->undo.pop_back();

    std::unique_ptr<AuthorityParticipant> authorityParticipant;
    std::unique_ptr<CommandParticipant>   commandParticipant;
    std::vector<transaction::ITransactionParticipant*> participants;
    if (history.authorityReceipt && impl_->authority) {
        authorityParticipant = std::make_unique<AuthorityParticipant>(
            *impl_->authority, history.specification, history.operations, *history.authorityReceipt);
        participants.push_back(authorityParticipant.get());
    }
    if (history.commands) {
        commandParticipant = std::make_unique<CommandParticipant>(
            *history.commands, CommandParticipant::InitialPhase::Committed);
        participants.push_back(commandParticipant.get());
    }
    if (participants.empty()) {
        impl_->undo.push_back(std::move(history));
        return failure<EditorTransactionRecord>(eve::DiagnosticCode::Unsupported,
                                                "editor history entry has no compensatable participant",
                                                "editor.history.undo");
    }
    const std::string compensationId = history.specification.id.value() + ".undo." +
                                       std::to_string(++impl_->redoSequence);
    transaction::TransactionContext context(compensationId);
    auto compensated = impl_->coordinator.compensate(context, participants);
    if (!compensated.ok()) {
        impl_->undo.push_back(std::move(history));
        return eve::Result<EditorTransactionRecord>::failure(compensated.status());
    }
    transaction::TransactionReceipt receipt = std::move(compensated).takeValue();
    if (authorityParticipant && authorityParticipant->receipt())
        history.specification.baseRevision = authorityParticipant->receipt()->afterRevision;
    impl_->redo.push_back(std::move(history));
    const auto& moved = impl_->redo.back();
    EditorTransactionRecord record;
    record.coordinator      = std::move(receipt);
    record.specification    = moved.specification;
    record.authorityReceipt = moved.authorityReceipt;
    record.commandCount     = moved.commands ? moved.commands->commands.size() : 0;
    record.operationCount   = moved.operations.size();
    return eve::Result<EditorTransactionRecord>::success(std::move(record));
}

eve::Result<EditorTransactionRecord> EditorTransactionConsumer::redo() {
    if (impl_->pending)
        return failure<EditorTransactionRecord>(eve::DiagnosticCode::PreconditionViolation,
                                                "cannot redo while an editor transaction is active",
                                                "editor.transaction.redo");
    if (impl_->redo.empty())
        return failure<EditorTransactionRecord>(eve::DiagnosticCode::NotFound,
                                                "there is no editor transaction to redo", "editor.history.redo");
    Impl::HistoryEntry history = std::move(impl_->redo.back());
    impl_->redo.pop_back();
    if (history.operations.empty() && !history.commands) {
        impl_->redo.push_back(std::move(history));
        return failure<EditorTransactionRecord>(eve::DiagnosticCode::Unsupported,
                                                "editor history entry has no replayable participant",
                                                "editor.history.redo");
    }
    if (!history.operations.empty() && !impl_->authority) {
        impl_->redo.push_back(std::move(history));
        return failure<EditorTransactionRecord>(eve::DiagnosticCode::Unsupported,
                                                "authority redo requires an injected authority",
                                                "editor.authority");
    }

    TransactionSpec specification = history.specification;
    specification.id = TransactionId(specification.id.value() + ".redo." +
                                     std::to_string(++impl_->redoSequence));
    std::unique_ptr<AuthorityParticipant> authorityParticipant;
    std::unique_ptr<CommandParticipant>   commandParticipant;
    std::vector<transaction::ITransactionParticipant*> participants;
    if (!history.operations.empty()) {
        authorityParticipant = std::make_unique<AuthorityParticipant>(
            *impl_->authority, specification, history.operations);
        participants.push_back(authorityParticipant.get());
    }
    if (history.commands) {
        commandParticipant = std::make_unique<CommandParticipant>(*history.commands);
        participants.push_back(commandParticipant.get());
    }
    transaction::TransactionContext context(specification.id.value());
    auto replayed = impl_->coordinator.execute(context, participants);
    if (!replayed.ok()) {
        impl_->redo.push_back(std::move(history));
        return eve::Result<EditorTransactionRecord>::failure(replayed.status());
    }
    transaction::TransactionReceipt receipt = std::move(replayed).takeValue();
    history.specification = specification;
    if (authorityParticipant) history.authorityReceipt = authorityParticipant->receipt();
    impl_->undo.push_back(std::move(history));
    const auto& moved = impl_->undo.back();
    EditorTransactionRecord record;
    record.coordinator      = std::move(receipt);
    record.specification    = moved.specification;
    record.authorityReceipt = moved.authorityReceipt;
    record.commandCount     = moved.commands ? moved.commands->commands.size() : 0;
    record.operationCount   = moved.operations.size();
    return eve::Result<EditorTransactionRecord>::success(std::move(record));
}

bool EditorTransactionConsumer::active() const noexcept { return impl_->pending.has_value(); }
bool EditorTransactionConsumer::canUndo() const noexcept { return !impl_->undo.empty(); }
bool EditorTransactionConsumer::canRedo() const noexcept { return !impl_->redo.empty(); }
std::size_t EditorTransactionConsumer::undoCount() const noexcept { return impl_->undo.size(); }
std::size_t EditorTransactionConsumer::redoCount() const noexcept { return impl_->redo.size(); }
EditorCommitState EditorTransactionConsumer::state() const noexcept { return impl_->state; }
const std::vector<eve::Diagnostic>& EditorTransactionConsumer::diagnostics() const noexcept {
    return impl_->diagnostics;
}

void EditorTransactionConsumer::clear() {
    if (impl_->pending) {
        auto result = rollback();
        result.ignore("EditorTransactionConsumer::clear discards active work");
    }
    impl_->undo.clear();
    impl_->redo.clear();
}

}  // namespace eve::editor
