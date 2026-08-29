#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/EditorAuthority.h"
#include "editor/EditorTransactionConsumer.h"
#include "editor/FieldTargets.h"
#include "editor/TileBuffer.h"

#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using eve::Diagnostic;
using eve::DiagnosticCode;
using eve::Result;
using eve::StatusCode;
using eve::editor::DomainOperation;
using eve::editor::EditorCommitState;
using eve::editor::EditorResult;
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
    EditorResult<eve::editor::AuthorityPlan> preflight(const TransactionSpec&,
                                                       std::span<const DomainOperation>) override {
        ++preflightCount;
        return EditorResult<eve::editor::AuthorityPlan>::error(
            EditorStatus::Conflict, eve::editor::RuleId("test.preflight.rejected"), "injected preflight rejection");
    }

    EditorResult<TransactionReceipt> commit(const eve::editor::AuthorityPlan&) override {
        ++commitCount;
        return EditorResult<TransactionReceipt>::error(EditorStatus::Failed,
                                                       eve::editor::RuleId("test.commit.unexpected"),
                                                       "commit must not be called after preflight rejection");
    }

    EditorResult<TransactionReceipt> compensate(const TransactionReceipt&) override {
        ++compensateCount;
        return EditorResult<TransactionReceipt>::error(EditorStatus::Failed,
                                                       eve::editor::RuleId("test.compensate.unexpected"),
                                                       "compensation must not be called after preflight rejection");
    }

    int preflightCount  = 0;
    int commitCount     = 0;
    int compensateCount = 0;
};

std::unique_ptr<eve::editor::IntFieldEditCommand> makeTileCommand(eve::editor::TileBufferTarget& target, int value) {
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
    eve::editor::TileBuffer       buffer(1, 1);
    eve::editor::TileBufferTarget target("tiles", &buffer);
    EditorTransactionConsumer     consumer;

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
    eve::editor::TileBuffer       buffer(1, 1);
    eve::editor::TileBufferTarget target("tiles", &buffer);
    EditorTransactionConsumer     consumer;
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
    eve::editor::TileBuffer       buffer(1, 1);
    eve::editor::TileBufferTarget target("tiles", &buffer);
    EditorTransactionConsumer     consumer;
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
