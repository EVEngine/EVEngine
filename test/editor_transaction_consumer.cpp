#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "editor/EditorAuthority.h"
#include "editor/EditorCommandService.h"
#include "editor/EditorSession.h"
#include "editor/EditorTransactionConsumer.h"
#include "editor/EditorTransactionService.h"
#include "map/level/editing/FieldTargets.h"
#include "map/level/editing/TileBuffer.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// editor_transaction_consumer.cpp
namespace {

using eve::Diagnostic;
using eve::DiagnosticCode;
using eve::Result;
using eve::StatusCode;
using eve::editor::DomainOperation;
using eve::editor::EditorCommitState;
using eve::editor::EditorStatus;
using eve::editor::EditorTransactionConsumer;
using eve::editor::IEditAuthority;
using eve::editor::IEditCommand;
using eve::editor::TransactionId;
using eve::editor::TransactionReceipt;
using eve::editor::TransactionSpec;
using eve::transaction::ITransactionParticipant;
using eve::transaction::TransactionContext;

class FailingParticipant final : public ITransactionParticipant {
public:
    std::string_view name() const noexcept override { return "test.second-participant"; }

    Result<void> prepare(const TransactionContext&) override {
        ++prepareCount;
        prepared = true;
        return Result<void>::success();
    }

    Result<void> commit(const TransactionContext&) override {
        ++commitCount;
        if (failCommit)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Failed, "injected second participant failure"));
        prepared = false;
        return Result<void>::success();
    }

    Result<void> rollback(const TransactionContext&) override {
        ++rollbackCount;
        prepared = false;
        return Result<void>::success();
    }

    Result<void> compensate(const TransactionContext&) override {
        ++compensateCount;
        return Result<void>::success();
    }

    int  prepareCount    = 0;
    int  commitCount     = 0;
    int  rollbackCount   = 0;
    int  compensateCount = 0;
    bool prepared        = false;
    bool failCommit      = true;
};

class ThrowingMergeCommand final : public IEditCommand {
public:
    ThrowingMergeCommand(std::string name, int* value, int after)
        : name_(std::move(name)), value_(value), after_(after) {}

    const std::string& name() const override { return name_; }
    bool               apply() override {
        if (!value_) return false;
        before_ = *value_;
        *value_ = after_;
        return true;
    }
    void revert() override {
        if (value_) *value_ = before_;
    }
    [[nodiscard]] std::unique_ptr<IEditCommand> clone() const override {
        return std::make_unique<ThrowingMergeCommand>(*this);
    }
    bool mergeWith(const IEditCommand&) override { throw std::runtime_error("injected merge failure"); }
    eve::editor::EditRegion dirtyRegion() const override { return {}; }

private:
    std::string name_;
    int*        value_  = nullptr;
    int         before_ = 0;
    int         after_  = 0;
};

class RejectingAuthority final : public IEditAuthority {
public:
    Result<eve::editor::AuthorityPlan> preflight(const TransactionSpec&, std::span<const DomainOperation>) override {
        ++preflightCount;
        return eve::editing::failed<eve::editor::AuthorityPlan>(
            EditorStatus::Conflict, eve::editor::RuleId("test.preflight.rejected"), "injected preflight rejection");
    }

    Result<TransactionReceipt> commit(const eve::editor::AuthorityPlan&) override {
        ++commitCount;
        return eve::editing::failed<TransactionReceipt>(EditorStatus::Failed,
                                                        eve::editor::RuleId("test.commit.unexpected"),
                                                        "commit must not be called after preflight rejection");
    }

    Result<TransactionReceipt> compensate(const TransactionReceipt&) override {
        ++compensateCount;
        return eve::editing::failed<TransactionReceipt>(EditorStatus::Failed,
                                                        eve::editor::RuleId("test.compensate.unexpected"),
                                                        "compensation must not be called after preflight rejection");
    }

    int preflightCount  = 0;
    int commitCount     = 0;
    int compensateCount = 0;
};

std::unique_ptr<eve::editor::IntFieldEditCommand> makeTileCommand(eve::level_editing::TileBufferTarget& target,
                                                                  int                                   value) {
    auto command = std::make_unique<eve::editor::IntFieldEditCommand>("set-tile", &target);
    if (!command->record(0, 0, value)) return nullptr;
    return command;
}

TransactionSpec commandSpec(std::string id) {
    TransactionSpec specification;
    specification.id    = TransactionId(std::move(id));
    specification.label = "test command";
    return specification;
}

DomainOperation rejectedOperation() {
    DomainOperation operation;
    operation.type   = "test.operation";
    operation.target = eve::editor::TargetId("test-target");
    return operation;
}

}  // namespace

TEST_CASE("editor.transactionConsumer.commitUndoRedoAndDryRun") {
    eve::level_editing::TileBuffer       buffer(1, 1);
    eve::level_editing::TileBufferTarget target("tiles", &buffer);
    EditorTransactionConsumer            consumer;

    auto begun = consumer.begin(commandSpec("editor.command.1"));
    REQUIRE(begun.ok());
    auto command = makeTileCommand(target, 7);
    REQUIRE(static_cast<bool>(command));
    auto appended = consumer.append(std::move(command));
    REQUIRE(appended.ok());
    CHECK_EQ(buffer.getGid(0, 0), 0);

    auto dryRun = consumer.dryRun();
    REQUIRE(dryRun.ok());
    CHECK_EQ(dryRun.value().commandCount, size_t{1});
    CHECK_EQ(buffer.getGid(0, 0), 0);

    auto committed = consumer.commit();
    REQUIRE(committed.ok());
    CHECK_EQ(static_cast<int>(committed.value().coordinator.state),
             static_cast<int>(eve::transaction::CoordinatorState::Committed));
    CHECK_EQ(buffer.getGid(0, 0), 7);
    CHECK(consumer.canUndo());

    auto undone = consumer.undo();
    REQUIRE(undone.ok());
    CHECK_EQ(static_cast<int>(undone.value().coordinator.state),
             static_cast<int>(eve::transaction::CoordinatorState::Compensated));
    CHECK_EQ(buffer.getGid(0, 0), 0);
    CHECK(consumer.canRedo());

    auto redone = consumer.redo();
    REQUIRE(redone.ok());
    CHECK_EQ(static_cast<int>(redone.value().coordinator.state),
             static_cast<int>(eve::transaction::CoordinatorState::Committed));
    CHECK_EQ(buffer.getGid(0, 0), 7);
}

TEST_CASE("editor.transactionConsumer.authorityPreflightRejectsWithoutCommit") {
    RejectingAuthority        authority;
    EditorTransactionConsumer consumer(&authority);

    TransactionSpec specification;
    specification.id     = TransactionId("editor.authority.rejected");
    specification.target = eve::editor::TargetId("test-target");
    REQUIRE(consumer.begin(std::move(specification)).ok());
    REQUIRE(consumer.append(rejectedOperation()).ok());

    auto dryRun = consumer.dryRun();
    CHECK(!dryRun.ok());
    CHECK_EQ(static_cast<int>(dryRun.code()), static_cast<int>(StatusCode::Conflict));
    CHECK_EQ(authority.preflightCount, 1);
    CHECK_EQ(authority.commitCount, 0);

    auto committed = consumer.commit();
    CHECK(!committed.ok());
    CHECK_EQ(authority.preflightCount, 2);
    CHECK_EQ(authority.commitCount, 0);
    CHECK(!consumer.canUndo());
}

TEST_CASE("editor.transactionConsumer.secondParticipantFailureCompensatesEditor") {
    eve::level_editing::TileBuffer       buffer(1, 1);
    eve::level_editing::TileBufferTarget target("tiles", &buffer);
    EditorTransactionConsumer            consumer;
    REQUIRE(consumer.begin(commandSpec("editor.composed.failure")).ok());
    auto command = makeTileCommand(target, 9);
    REQUIRE(static_cast<bool>(command));
    REQUIRE(consumer.append(std::move(command)).ok());

    FailingParticipant                      second;
    std::array<ITransactionParticipant*, 1> additional{&second};
    auto result = consumer.commit(std::span<ITransactionParticipant*>(additional.data(), additional.size()));
    CHECK(!result.ok());
    CHECK_EQ(buffer.getGid(0, 0), 0);
    CHECK_EQ(second.prepareCount, 1);
    CHECK_EQ(second.commitCount, 1);
    CHECK_EQ(second.rollbackCount, 1);
    CHECK_EQ(second.compensateCount, 0);
    CHECK(!consumer.canUndo());
    CHECK(consumer.active());
    CHECK_EQ(static_cast<int>(consumer.state()), static_cast<int>(EditorCommitState::FailedRetryable));
    CHECK(!consumer.diagnostics().empty());

    second.failCommit = false;
    auto retried      = consumer.retry(std::span<ITransactionParticipant*>(additional.data(), additional.size()));
    REQUIRE(retried.ok());
    CHECK_EQ(buffer.getGid(0, 0), 9);
    CHECK_EQ(second.commitCount, 2);
    CHECK_EQ(static_cast<int>(consumer.state()), static_cast<int>(EditorCommitState::Committed));

    auto duplicateCommit = consumer.commit(std::span<ITransactionParticipant*>(additional.data(), additional.size()));
    CHECK(!duplicateCommit.ok());
    CHECK_EQ(second.commitCount, 2);
}

TEST_CASE("editor.transactionConsumer.previewMergeFailureDoesNotMutateTarget") {
    int                       value = 0;
    EditorTransactionConsumer consumer;
    REQUIRE(consumer.begin(commandSpec("editor.preview.merge-failure")).ok());

    auto first = std::make_unique<ThrowingMergeCommand>("preview", &value, 1);
    REQUIRE(consumer.appendPreview(std::move(first)).ok());
    CHECK_EQ(value, 1);

    auto second = std::make_unique<ThrowingMergeCommand>("preview", &value, 2);
    auto failed = consumer.appendPreview(std::move(second));
    CHECK(!failed.ok());
    CHECK_EQ(value, 1);

    auto discarded = consumer.discard();
    REQUIRE(discarded.ok());
    CHECK_EQ(value, 0);
    CHECK(!consumer.active());
    CHECK_EQ(static_cast<int>(consumer.state()), static_cast<int>(EditorCommitState::Discarded));
}

TEST_CASE("editor.transactionConsumer.explicitDiscardClearsPendingWork") {
    eve::level_editing::TileBuffer       buffer(1, 1);
    eve::level_editing::TileBufferTarget target("tiles", &buffer);
    EditorTransactionConsumer            consumer;
    REQUIRE(consumer.begin(commandSpec("editor.explicit-discard")).ok());
    auto command = makeTileCommand(target, 4);
    REQUIRE(static_cast<bool>(command));
    REQUIRE(consumer.appendPreview(std::move(command)).ok());
    CHECK_EQ(buffer.getGid(0, 0), 4);

    auto discarded = consumer.discard();
    REQUIRE(discarded.ok());
    CHECK_EQ(buffer.getGid(0, 0), 0);
    CHECK(!consumer.active());
    auto retry = consumer.retry();
    CHECK(!retry.ok());
}

// editor_transaction_v2.cpp
using namespace eve::editor;

namespace {

class IntegerOperationTarget final : public IDomainOperationTarget, public IDomainOperationTargetStaging {
public:
    explicit IntegerOperationTarget(std::string id, int value = 0) : id_(std::move(id)), value_(value) {}

    TargetId      targetId() const override { return TargetId(id_); }
    std::uint64_t revision() const override { return revision_; }
    EditRegion    dirtyRegion() const override { return dirty_; }
    void          clearDirtyRegion() override { dirty_.clear(); }

    Result<void> applyDomainOperation(const DomainOperation& operation) override {
        if (operation.type == "test.fail.v1")
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("test.operation.failure"),
                                              "Synthetic operation failure");
        if (operation.type != "test.integer.set.v1")
            return eve::editing::failed<void>(EditorStatus::Unsupported, RuleId("test.operation.unsupported"),
                                              "Unsupported operation type");
        const auto* value = operation.payload.getIf<std::int64_t>();
        if (!value)
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("test.operation.payload"),
                                              "Integer payload required");
        value_ = static_cast<int>(*value);
        ++revision_;
        dirty_.include(0, 0);
        return eve::editing::applied<void>();
    }

    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override {
        return std::make_unique<IntegerOperationTarget>(*this);
    }

    Result<void> commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override {
        if (failCandidateCommit)
            return eve::editing::failed<void>(EditorStatus::Failed, RuleId("test.candidate.publish"),
                                              "Injected candidate publish failure");
        auto* staged = dynamic_cast<IntegerOperationTarget*>(candidate.get());
        if (!staged || staged->id_ != id_)
            return eve::editing::failed<void>(EditorStatus::Conflict, RuleId("test.candidate.identity"),
                                              "Candidate belongs to another target");
        value_    = staged->value_;
        revision_ = staged->revision_;
        dirty_    = staged->dirty_;
        return eve::editing::applied<void>();
    }

    int  value() const { return value_; }
    bool failCandidateCommit = false;

private:
    std::string        id_;
    int                value_    = 0;
    unsigned long long revision_ = 0;
    EditRegion         dirty_;
};

DomainOperation setInteger(const IntegerOperationTarget& target, int before, int after, std::string inverseType = {}) {
    DomainOperation operation;
    operation.type        = "test.integer.set.v1";
    operation.inverseType = std::move(inverseType);
    operation.target      = TargetId(target.targetId());
    operation.payload     = EditorValue(after);
    operation.inverse     = EditorValue(before);
    operation.hasInverse  = true;
    operation.affectedProperties.push_back("value");
    return operation;
}

TransactionSpec transaction(const IntegerOperationTarget& target, const char* id) {
    TransactionSpec specification;
    specification.id           = TransactionId(id);
    specification.label        = "Set integer";
    specification.target       = TargetId(target.targetId());
    specification.baseRevision = target.revision();
    return specification;
}

}  // namespace

TEST_CASE("editor.v2.authority_preflight_commit_and_compensate") {
    IntegerOperationTarget target("integer", 1);
    LocalWorldAuthority    authority(&target);
    DomainOperation        operation     = setInteger(target, 1, 5);
    TransactionSpec        specification = transaction(target, "transaction.one");

    auto plan = authority.preflight(specification, std::span<const DomainOperation>(&operation, 1));
    CHECK(plan.ok());
    CHECK_EQ(target.value(), 1);
    auto committed = authority.commit(plan.value());
    CHECK(committed.ok());
    CHECK_EQ(target.value(), 5);
    CHECK_EQ(committed.value().beforeRevision, static_cast<Revision>(0));
    CHECK_EQ(committed.value().afterRevision, static_cast<Revision>(1));

    target.failCandidateCommit                  = true;
    const auto valueBeforeFailedCompensation    = target.value();
    const auto revisionBeforeFailedCompensation = target.revision();
    auto       failedCompensation               = authority.compensate(committed.value());
    CHECK(!failedCompensation.ok());
    CHECK_EQ(target.value(), valueBeforeFailedCompensation);
    CHECK_EQ(target.revision(), revisionBeforeFailedCompensation);

    target.failCandidateCommit = false;
    auto compensated           = authority.compensate(committed.value());
    CHECK(compensated.ok());
    CHECK_EQ(target.value(), 1);
    CHECK_EQ(compensated.value().afterRevision, static_cast<Revision>(2));

    auto conflict = authority.preflight(specification, std::span<const DomainOperation>(&operation, 1));
    CHECK_EQ(static_cast<int>(conflict.code()), static_cast<int>(EditorStatus::Conflict));
}

TEST_CASE("editor.v2.authority_compensation_validates_full_inverse_on_candidate") {
    IntegerOperationTarget target("integer", 0);
    LocalWorldAuthority    authority(&target);
    const DomainOperation  operations[] = {
        setInteger(target, 0, 9, "test.fail.v1"),
        setInteger(target, 9, 5),
    };

    auto plan = authority.preflight(transaction(target, "transaction.candidate-failure"), operations);
    CHECK(plan.ok());
    auto committed = authority.commit(plan.value());
    CHECK(committed.ok());
    CHECK_EQ(target.value(), 5);
    const auto revisionAfterCommit = target.revision();

    auto compensation = authority.compensate(committed.value());
    CHECK(!compensation.ok());
    CHECK_EQ(static_cast<int>(compensation.code()), static_cast<int>(EditorStatus::Failed));
    CHECK_EQ(target.value(), 5);
    CHECK_EQ(target.revision(), revisionAfterCommit);
}

TEST_CASE("editor.v2.authority_rolls_back_partial_commit") {
    IntegerOperationTarget target("integer");
    LocalWorldAuthority    authority(&target);
    DomainOperation        first = setInteger(target, 0, 9);
    DomainOperation        failure;
    failure.type                       = "test.fail.v1";
    failure.target                     = TargetId(target.targetId());
    const DomainOperation operations[] = {first, failure};

    auto plan = authority.preflight(transaction(target, "transaction.rollback"), operations);
    CHECK(plan.ok());
    auto result = authority.commit(plan.value());
    CHECK(!result.ok());
    CHECK_EQ(target.value(), 0);
    CHECK(result.ok());
    CHECK_EQ(static_cast<int>(result.value().state), static_cast<int>(TransactionState::Rejected));
}

TEST_CASE("editor.v2.local_transaction_backend_undo_and_redo") {
    IntegerOperationTarget  target("integer");
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend backend(&authority);

    CHECK(backend.begin(transaction(target, "transaction.history")).ok());
    CHECK(backend.append(setInteger(target, 0, 7)).ok());
    auto committed = backend.commit();
    CHECK(committed.ok());
    CHECK_EQ(target.value(), 7);
    CHECK(backend.canUndo());

    auto undone = backend.undo();
    CHECK(undone.ok());
    CHECK_EQ(target.value(), 0);
    CHECK(backend.canRedo());

    auto redone = backend.redo();
    CHECK(redone.ok());
    CHECK_EQ(target.value(), 7);
}

TEST_CASE("editor.v2.planned_command_is_dry_run_and_revision_safe") {
    IntegerOperationTarget  target("integer", 2);
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend backend(&authority);
    EditorCommandService    commands;

    CommandDescriptor descriptor;
    descriptor.id          = CommandId("test.integer.set");
    descriptor.ownerModule = "test";
    CHECK(commands
              .registerPlannedCommand(
                  descriptor,
                  [&](const CommandRequest& request) {
                      const auto* after = request.payload.getIf<std::int64_t>();
                      if (!after)
                          return eve::editing::failed<CommandPlan>(
                              EditorStatus::Rejected, RuleId("test.command.payload"), "Integer payload required");
                      CommandPlan plan;
                      plan.operations.push_back(setInteger(target, target.value(), static_cast<int>(*after)));
                      return eve::editing::applied<CommandPlan>(std::move(plan));
                  },
                  [&](const CommandRequest&, const CommandPlan& plan) {
                      TransactionSpec specification;
                      specification.id           = TransactionId(plan.id.value());
                      specification.label        = "Planned integer change";
                      specification.target       = plan.target;
                      specification.baseRevision = plan.baseRevision;
                      auto begun                 = backend.begin(std::move(specification));
                      if (!begun.ok())
                          return eve::editing::failed<TransactionReceipt>(begun.code(), RuleId("test.command.begin"),
                                                                          "Could not begin transaction");
                      for (const DomainOperation& operation : plan.operations) {
                          auto appended = backend.append(operation);
                          if (!appended.ok()) {
                              auto rolledBack = backend.rollback();
                              if (!rolledBack.ok())
                                  return eve::editing::failed<TransactionReceipt>(rolledBack.code(),
                                                                                  RuleId("test.command.rollback"),
                                                                                  "Could not roll back transaction");
                              return eve::editing::failed<TransactionReceipt>(
                                  appended.code(), RuleId("test.command.append"), "Could not append operation");
                          }
                      }
                      return backend.commit();
                  })
              .ok());

    EditorSession session;
    session.setSessionId(SessionId("test-session"));
    session.setCommandService(&commands);
    session.bindTarget(&target);

    auto plan = session.planCommand(descriptor.id, EditorValue(11), CommandSource::Script, target.revision());
    CHECK(plan.ok());
    CHECK_EQ(target.value(), 2);
    auto executed = session.executePlan(plan.value(), EditorValue(11), CommandSource::Script);
    CHECK(executed.ok());
    CHECK_EQ(target.value(), 11);
    CHECK_EQ(static_cast<int>(executed.value().state), static_cast<int>(TransactionState::Committed));

    auto stalePlan = session.planCommand(descriptor.id, EditorValue(15));
    CHECK(stalePlan.ok());
    DomainOperation external = setInteger(target, 11, 12);
    CHECK(target.applyDomainOperation(external).ok());
    auto stale = session.executePlan(stalePlan.value(), EditorValue(15));
    CHECK_EQ(static_cast<int>(stale.code()), static_cast<int>(EditorStatus::Conflict));
    CHECK_EQ(target.value(), 12);
}

TEST_CASE("editor.v2.plans_bind_payload_target_lifetime_and_command_registration") {
    IntegerOperationTarget target("planned.identity", 1);
    IntegerOperationTarget replacement("planned.identity", 1);
    EditorCommandService   commands;

    auto registerCommand = [&](bool replace) {
        CommandDescriptor descriptor;
        descriptor.id          = CommandId("test.plan.identity");
        descriptor.ownerModule = "test";
        return commands.registerPlannedCommand(
            descriptor, [](const CommandRequest&) { return eve::editing::applied<CommandPlan>(CommandPlan{}); },
            [](const CommandRequest&, const CommandPlan&) {
                TransactionReceipt receipt;
                receipt.state = TransactionState::Committed;
                return eve::editing::applied<TransactionReceipt>(std::move(receipt));
            },
            replace);
    };
    REQUIRE(registerCommand(false).ok());

    EditorSession session;
    session.setCommandService(&commands);
    session.bindTarget(&target);
    auto plan = session.planCommand(CommandId("test.plan.identity"), EditorValue(3));
    REQUIRE(plan.ok());
    REQUIRE(plan.ok());

    auto changedPayload = session.executePlan(plan.value(), EditorValue(4));
    CHECK_EQ(static_cast<int>(changedPayload.code()), static_cast<int>(EditorStatus::Rejected));

    session.bindTarget(&replacement);
    auto replacedTarget = session.executePlan(plan.value(), EditorValue(3));
    CHECK_EQ(static_cast<int>(replacedTarget.code()), static_cast<int>(EditorStatus::Conflict));

    auto replacementPlan = session.planCommand(CommandId("test.plan.identity"), EditorValue(5));
    REQUIRE(replacementPlan.ok());
    REQUIRE(registerCommand(true).ok());
    auto replacedCommand = session.executePlan(replacementPlan.value(), EditorValue(5));
    CHECK_EQ(static_cast<int>(replacedCommand.code()), static_cast<int>(EditorStatus::Conflict));
}

TEST_CASE("editor.v2.read_only_authority_rejects_mutation") {
    ReadOnlyAuthority authority;
    TransactionSpec   specification;
    specification.id     = TransactionId("read-only");
    specification.target = TargetId("target");
    auto result          = authority.preflight(specification, {});
    CHECK_EQ(static_cast<int>(result.code()), static_cast<int>(EditorStatus::Rejected));
}
