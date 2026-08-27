#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "statepatch/StatePatch.h"
#include "transaction/Transaction.h"
#include "StatePatchTestSupport.h"

#include <array>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using eve::Diagnostic;
using eve::DiagnosticCode;
using eve::Result;
using eve::StatusCode;
using eve::transaction::ITransactionParticipant;
using eve::transaction::TransactionContext;

Result<void> participantFailure(std::string_view name, std::string_view phase) {
    return Result<void>::failure(Diagnostic::error(
        DiagnosticCode::Failed, std::string(name) + " failed during " + std::string(phase),
        "test." + std::string(phase)));
}

class RecordingParticipant final : public ITransactionParticipant {
public:
    RecordingParticipant(std::string name, std::vector<std::string>& events) : name_(std::move(name)), events_(events) {}

    std::string_view name() const noexcept override { return name_; }

    Result<void> prepare(const TransactionContext& context) override {
        record("prepare", context);
        if (failPrepare) return participantFailure(name_, "prepare");
        prepared = true;
        return Result<void>::success();
    }

    Result<void> commit(const TransactionContext& context) override {
        record("commit", context);
        if (!prepared) return participantFailure(name_, "commit_without_prepare");
        if (failCommit) return participantFailure(name_, "commit");
        prepared  = false;
        committed = true;
        return Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    Result<void> rollback(const TransactionContext& context) override {
        record("rollback", context);
        if (!prepared) return participantFailure(name_, "rollback_without_prepare");
        if (failRollback) return participantFailure(name_, "rollback");
        prepared = false;
        return Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    Result<void> compensate(const TransactionContext& context) override {
        record("compensate", context);
        if (!committed) return participantFailure(name_, "compensate_without_commit");
        if (failCompensate) return participantFailure(name_, "compensate");
        committed = false;
        return Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    bool failPrepare    = false;
    bool failCommit     = false;
    bool failRollback   = false;
    bool failCompensate = false;
    bool prepared       = false;
    bool committed      = false;
    std::string seenTransaction;
    std::string seenCorrelation;
    std::string seenCausation;

private:
    void record(std::string_view phase, const TransactionContext& context) {
        events_.push_back(name_ + "." + std::string(phase));
        seenTransaction = context.transactionId();
        seenCorrelation = context.correlationId();
        seenCausation   = context.causationId();
    }

    std::string              name_;
    std::vector<std::string>& events_;
};

void checkEvents(const std::vector<std::string>& actual, std::initializer_list<std::string_view> expected) {
    CHECK_EQ(actual.size(), expected.size());
    size_t index = 0;
    for (const auto value : expected) {
        const std::string actualValue = actual[index];
        ++index;
        CHECK_EQ(actualValue, std::string(value));
    }
}

template <size_t N>
auto participantSpan(std::array<ITransactionParticipant*, N>& participants) {
    return std::span<ITransactionParticipant*>(participants.data(), participants.size());
}

}  // namespace

TEST_CASE("transaction.participants.prepareAndCommitPreserveOrderAndContext") {
    std::vector<std::string> events;
    RecordingParticipant     first("first", events);
    RecordingParticipant     second("second", events);
    std::array<ITransactionParticipant*, 2> participants{&first, &second};
    const TransactionContext context("tx-1", "chain-7", "command-3");

    eve::transaction::Coordinator coordinator;
    auto result = coordinator.execute(context, participantSpan(participants));
    REQUIRE(result.ok());
    const auto& receipt = result.value();
    CHECK_EQ(receipt.transactionId, std::string("tx-1"));
    CHECK_EQ(receipt.correlationId, std::string("chain-7"));
    CHECK_EQ(receipt.causationId, std::string("command-3"));
    CHECK_EQ(receipt.participantCount, size_t{2});
    CHECK_EQ(receipt.preparedCount, size_t{2});
    CHECK_EQ(receipt.committedCount, size_t{2});
    CHECK(first.seenTransaction == "tx-1");
    CHECK(first.seenCorrelation == "chain-7");
    CHECK(first.seenCausation == "command-3");
    CHECK(second.seenTransaction == "tx-1");
    checkEvents(events, {"first.prepare", "second.prepare", "first.commit", "second.commit"});
}

TEST_CASE("transaction.participants.prepareFailureRollsBackPreparedInReverse") {
    std::vector<std::string> events;
    RecordingParticipant     first("first", events);
    RecordingParticipant     second("second", events);
    RecordingParticipant     third("third", events);
    third.failPrepare = true;
    std::array<ITransactionParticipant*, 3> participants{&first, &second, &third};

    eve::transaction::Coordinator coordinator;
    auto result = coordinator.execute(TransactionContext("tx-prepare-fail"), participantSpan(participants));
    CHECK(!result);
    CHECK_EQ(static_cast<int>(result.code()), static_cast<int>(StatusCode::Failed));
    CHECK(!first.prepared);
    CHECK(!second.prepared);
    CHECK(!third.prepared);
    checkEvents(events, {"first.prepare", "second.prepare", "third.prepare", "second.rollback", "first.rollback"});
}

TEST_CASE("transaction.participants.commitFailureRollsBackAndCompensatesSeparately") {
    std::vector<std::string> events;
    RecordingParticipant     first("first", events);
    RecordingParticipant     second("second", events);
    RecordingParticipant     third("third", events);
    second.failCommit = true;
    std::array<ITransactionParticipant*, 3> participants{&first, &second, &third};

    eve::transaction::Coordinator coordinator;
    auto result = coordinator.execute(TransactionContext("tx-commit-fail"), participantSpan(participants));
    CHECK(!result);
    CHECK_EQ(static_cast<int>(result.code()), static_cast<int>(StatusCode::Failed));
    CHECK(!first.committed);
    CHECK(!second.committed);
    CHECK(!third.committed);
    checkEvents(events, {"first.prepare", "second.prepare", "third.prepare", "first.commit", "second.commit",
                         "third.rollback", "second.rollback", "first.compensate"});
}

TEST_CASE("transaction.participants.rejectNullAndDuplicateWithoutCallingParticipants") {
    std::vector<std::string> events;
    RecordingParticipant     participant("only", events);
    eve::transaction::Coordinator coordinator;

    std::array<ITransactionParticipant*, 1> nullParticipant{nullptr};
    auto nullResult = coordinator.execute(TransactionContext("tx-null"), participantSpan(nullParticipant));
    CHECK(!nullResult);
    CHECK_EQ(static_cast<int>(nullResult.code()), static_cast<int>(StatusCode::Rejected));

    std::array<ITransactionParticipant*, 2> duplicate{&participant, &participant};
    auto duplicateResult = coordinator.execute(TransactionContext("tx-duplicate"), participantSpan(duplicate));
    CHECK(!duplicateResult);
    CHECK_EQ(static_cast<int>(duplicateResult.code()), static_cast<int>(StatusCode::Rejected));
    CHECK(events.empty());
}

TEST_CASE("transaction.participants.compensationFailureIsNotSilentlyRolledBack") {
    std::vector<std::string> events;
    RecordingParticipant     participant("external", events);
    participant.failCompensate = true;
    participant.failCommit     = false;
    RecordingParticipant failing("failing", events);
    failing.failCommit = true;
    std::array<ITransactionParticipant*, 2> participants{&participant, &failing};

    eve::transaction::Coordinator coordinator;
    auto result = coordinator.execute(TransactionContext("tx-compensation-fail"), participantSpan(participants));
    CHECK(!result);
    CHECK_EQ(static_cast<int>(result.code()), static_cast<int>(StatusCode::Failed));
    checkEvents(events, {"external.prepare", "failing.prepare", "external.commit", "failing.commit",
                         "failing.rollback", "external.compensate"});
    CHECK(participant.committed);
    CHECK(result.error() != nullptr);
}

TEST_CASE("transaction.statepatchParticipantCommitsCandidateAtomically") {
    eve::statepatch::Store store;
    auto seed = eve::test_support::openStatePatchBatch(store);
    REQUIRE(seed.view.isBound());
    CHECK(seed.view->set("actor", "value", "1"));
    REQUIRE(store.commit(seed.view.get()));

    auto batch = eve::test_support::openStatePatchBatch(store);
    REQUIRE(batch.view.isBound());
    CHECK(batch.view->setExpected("actor", "value", "2", "1"));
    eve::statepatch::StoreTransactionParticipant participant(store, *batch.view);
    std::array<ITransactionParticipant*, 1> participants{&participant};

    eve::transaction::Coordinator coordinator;
    auto result = coordinator.execute(TransactionContext("tx-statepatch", "chain", "cause"),
                                      participantSpan(participants));
    REQUIRE(result.ok());
    CHECK_EQ(store.get("actor", "value"), std::string("2"));
    CHECK_EQ(store.revision(), uint64_t{2});
    CHECK_EQ(store.eventCount(), 2);
    CHECK_EQ(result.value().correlationId, std::string("chain"));
}

TEST_CASE("transaction.statepatchParticipantPrepareFailureKeepsOriginalState") {
    eve::statepatch::Store store;
    auto seed = eve::test_support::openStatePatchBatch(store);
    REQUIRE(seed.view.isBound());
    seed.view->set("actor", "value", "1");
    REQUIRE(store.commit(seed.view.get()));
    const std::string before = store.snapshotJson();

    auto batch = eve::test_support::openStatePatchBatch(store);
    REQUIRE(batch.view.isBound());
    CHECK(!batch.view->set("actor", "value", "{"));
    eve::statepatch::StoreTransactionParticipant participant(store, *batch.view);
    std::array<ITransactionParticipant*, 1> participants{&participant};
    eve::transaction::Coordinator coordinator;
    auto result = coordinator.execute(TransactionContext("tx-statepatch-invalid"), participantSpan(participants));

    CHECK(!result);
    CHECK_EQ(store.snapshotJson(), before);
    CHECK_EQ(store.get("actor", "value"), std::string("1"));
    CHECK_EQ(store.revision(), uint64_t{1});
}

TEST_CASE("transaction.statepatchParticipantRejectsExternalRevisionChangeAfterPrepare") {
    eve::statepatch::Store store;
    auto seed = eve::test_support::openStatePatchBatch(store);
    REQUIRE(seed.view.isBound());
    seed.view->set("actor", "value", "1");
    REQUIRE(store.commit(seed.view.get()));
    auto batch = eve::test_support::openStatePatchBatch(store);
    REQUIRE(batch.view.isBound());
    batch.view->setExpected("actor", "value", "2", "1");
    eve::statepatch::StoreTransactionParticipant participant(store, *batch.view);
    const TransactionContext context("tx-statepatch-stale");
    auto prepared = participant.prepare(context);
    REQUIRE(prepared.ok());

    auto external = eve::test_support::openStatePatchBatch(store);
    REQUIRE(external.view.isBound());
    external.view->set("actor", "other", "9");
    REQUIRE(store.commit(external.view.get()));

    auto committed = participant.commit(context);
    CHECK(!committed);
    CHECK_EQ(static_cast<int>(committed.code()), static_cast<int>(StatusCode::Conflict));
    CHECK_EQ(store.get("actor", "value"), std::string("1"));
    CHECK_EQ(store.get("actor", "other"), std::string("9"));
    auto rolledBack = participant.rollback(context);
    REQUIRE(rolledBack.ok());
}

TEST_CASE("transaction.statepatchParticipantCompensateRestoresBeforeState") {
    eve::statepatch::Store store;
    auto seed = eve::test_support::openStatePatchBatch(store);
    REQUIRE(seed.view.isBound());
    seed.view->set("actor", "value", "1");
    REQUIRE(store.commit(seed.view.get()));
    const std::string before = store.snapshotJson();

    auto batch = eve::test_support::openStatePatchBatch(store);
    REQUIRE(batch.view.isBound());
    batch.view->setExpected("actor", "value", "2", "1");
    eve::statepatch::StoreTransactionParticipant participant(store, *batch.view);
    const TransactionContext context("tx-statepatch-compensate");
    auto prepared = participant.prepare(context);
    REQUIRE(prepared.ok());
    auto committed = participant.commit(context);
    REQUIRE(committed.ok());
    CHECK_EQ(store.get("actor", "value"), std::string("2"));

    auto compensated = participant.compensate(context);
    REQUIRE(compensated.ok());
    CHECK_EQ(store.snapshotJson(), before);
    CHECK_EQ(store.get("actor", "value"), std::string("1"));
    CHECK_EQ(store.revision(), uint64_t{1});
}
