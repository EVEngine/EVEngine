// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "transaction.cpp"
  ]
}
*/
// clang-format on

#include "StatePatchTestSupport.h"
#include "statepatch/StatePatch.h"
#include "transaction/Transaction.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace eve::transaction;

namespace {

eve::TransactionId transactionId(const char* text) {
    auto id = eve::TransactionId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

eve::OperationId operationId(const char* text) {
    auto id = eve::OperationId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

Plan& openPlan(Ledger& ledger, const char* id, std::string correlation = {}, std::string causation = {}) {
    auto opened = ledger.create(std::move(correlation), std::move(causation), transactionId(id));
    REQUIRE(opened.ok());
    return *opened.value();
}

eve::OperationId stage(Plan& plan, const char* id, std::string kind, std::string target, std::string payload) {
    auto staged = plan.stage(std::move(kind), std::move(target), std::move(payload), operationId(id));
    REQUIRE(staged.ok());
    return staged.value();
}

constexpr auto tx1 = "00000000-0000-4000-8000-000000000001";
constexpr auto tx2 = "00000000-0000-4000-8000-000000000002";
constexpr auto op1 = "00000000-0000-4000-8000-000000000003";
constexpr auto op2 = "00000000-0000-4000-8000-000000000004";

eve::Result<void> failure(std::string_view phase) {
    return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::Failed, std::string(phase), "test.transaction"));
}

class Participant final : public ITransactionParticipant {
public:
    Participant(std::string name, std::vector<std::string>& trace) : name_(std::move(name)), trace_(trace) {}
    std::string_view name() const noexcept override { return name_; }

    eve::Result<void> prepare(const TransactionContext& context) override {
        record("prepare", context);
        if (failPrepare) return failure("prepare");
        prepared = true;
        return eve::Result<void>::success();
    }
    eve::Result<void> commit(const TransactionContext& context) override {
        record("commit", context);
        if (!prepared || failCommit) return failure("commit");
        prepared  = false;
        committed = true;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }
    eve::Result<void> rollback(const TransactionContext& context) override {
        record("rollback", context);
        if (!prepared || failRollback) return failure("rollback");
        prepared = false;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }
    eve::Result<void> compensate(const TransactionContext& context) override {
        record("compensate", context);
        if (!committed || failCompensate) return failure("compensate");
        committed = false;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    bool        failPrepare    = false;
    bool        failCommit     = false;
    bool        failRollback   = false;
    bool        failCompensate = false;
    bool        prepared       = false;
    bool        committed      = false;
    std::string transaction;
    std::string correlation;
    std::string causation;

private:
    void record(std::string_view phase, const TransactionContext& context) {
        trace_.push_back(name_ + "." + std::string(phase));
        transaction = context.transactionId();
        correlation = context.correlationId();
        causation   = context.causationId();
    }
    std::string               name_;
    std::vector<std::string>& trace_;
};

template <size_t N>
auto participants(std::array<ITransactionParticipant*, N>& items) {
    return std::span<ITransactionParticipant*>(items.data(), items.size());
}

}  // namespace

// Ledger/Plan: creation and staging form a canonical, ordered transaction document.
TEST_CASE("transaction.plan.documentValidationTerminalStateAndEventContract") {
    SUB_CASE("canonical document and stable identity") {
        Ledger ledger;
        auto&  plan  = openPlan(ledger, tx1, "request-7", "command-3");
        auto   first = stage(plan, op1, "assign", "actor", "{\"z\":2,\"a\":1}");
        CHECK_EQ(plan.id(), std::string(tx1));
        CHECK_EQ(first.format(), std::string(op1));
        CHECK_EQ(plan.correlation(), std::string("request-7"));
        CHECK_EQ(plan.causation(), std::string("command-3"));
        REQUIRE(plan.operationAt(0) != nullptr);
        CHECK_EQ(plan.operationAt(0)->payload, std::string("{\"a\":1,\"z\":2}"));
        CHECK(plan.findOperation(first) == plan.operationAt(0));
    };
    SUB_CASE("every staged operation must validate before commit") {
        Ledger ledger;
        auto&  plan   = openPlan(ledger, tx1);
        auto   debit  = stage(plan, op1, "debit", "treasury", "10");
        auto   credit = stage(plan, op2, "credit", "army", "10");
        CHECK(!plan.validate().ok());
        REQUIRE(plan.markValid(debit).ok());
        CHECK(!plan.validate().ok());
        REQUIRE(plan.markInvalid(credit, "capacity exceeded").ok());
        CHECK(!plan.validate().ok());
        CHECK_EQ(plan.error(), std::string("capacity exceeded"));
        REQUIRE(plan.markValid(credit).ok());
        REQUIRE(plan.validate().ok());
        CHECK_EQ(stateName(plan.state()), std::string("validated"));
        REQUIRE(plan.commit().ok());
        CHECK_EQ(stateName(plan.state()), std::string("committed"));
        CHECK(!plan.stage("late", "actor", "null", operationId(tx2)).ok());
        CHECK(!plan.rollback("late").ok());
        CHECK(!plan.fail("late").ok());
    };
    SUB_CASE("rollback and failure are terminal with reasons") {
        Ledger ledger;
        auto&  rolled = openPlan(ledger, tx1);
        auto&  failed = openPlan(ledger, tx2);
        REQUIRE(rolled.rollback("cancelled").ok());
        REQUIRE(failed.fail("external error").ok());
        CHECK_EQ(stateName(rolled.state()), std::string("rolled_back"));
        CHECK_EQ(rolled.error(), std::string("cancelled"));
        CHECK_EQ(stateName(failed.state()), std::string("failed"));
        CHECK_EQ(failed.error(), std::string("external error"));
        CHECK(!rolled.validate().ok());
        CHECK(!failed.commit().ok());
    };
    SUB_CASE("events follow operation and state transitions") {
        Ledger ledger;
        auto&  plan = openPlan(ledger, tx1);
        auto   id   = stage(plan, op1, "assign", "actor", "{}");
        REQUIRE(plan.markValid(id).ok());
        REQUIRE(plan.validate().ok());
        REQUIRE(plan.commit().ok());
        const std::vector<std::string> types{"opened", "staged", "operation_valid", "validated", "committed"};
        REQUIRE(static_cast<size_t>(plan.eventCount()) == types.size());
        for (int index = 0; index < plan.eventCount(); ++index) {
            REQUIRE(plan.eventAt(index) != nullptr);
            CHECK_EQ(plan.eventAt(index)->type, types[static_cast<size_t>(index)]);
            CHECK_EQ(plan.eventAt(index)->sequence, static_cast<uint64_t>(index + 1));
        }
    };
}

TEST_CASE("transaction.ledger.restoreIsRoundTripAndAtomicOnMalformedInput") {
    Ledger source;
    auto&  plan = openPlan(source, tx1, "request", "command");
    auto   id   = stage(plan, op1, "assign", "actor", "{\"role\":\"guard\"}");
    REQUIRE(plan.markValid(id).ok());
    REQUIRE(plan.validate().ok());
    const auto snapshot = source.snapshotJson();
    Ledger     restored;
    REQUIRE(restored.restore(snapshot).ok());
    CHECK_EQ(restored.snapshotJson(), snapshot);
    REQUIRE(restored.find(plan.id()) != nullptr);
    REQUIRE(restored.find(plan.id())->findOperation(id) != nullptr);
    CHECK_EQ(restored.find(plan.id())->findOperation(id)->payload, std::string("{\"role\":\"guard\"}"));
    CHECK(!restored.restore("{\"version\":1}").ok());
    CHECK_EQ(restored.snapshotJson(), snapshot);
}

// Coordinator: prepare in registration order; undo in reverse order on failures.
TEST_CASE("transaction.coordinator.successPrepareCommitAndCompensationContract") {
    SUB_CASE("success returns receipt and context") {
        std::vector<std::string>                trace;
        Participant                             first("first", trace), second("second", trace);
        std::array<ITransactionParticipant*, 2> items{&first, &second};
        Coordinator                             coordinator;
        auto result = coordinator.execute(TransactionContext("tx", "request", "command"), participants(items));
        REQUIRE(result.ok());
        CHECK_EQ(result.value().transactionId, std::string("tx"));
        CHECK_EQ(result.value().correlationId, std::string("request"));
        CHECK_EQ(result.value().causationId, std::string("command"));
        CHECK_EQ(result.value().participantCount, size_t{2});
        CHECK_EQ(result.value().preparedCount, size_t{2});
        CHECK_EQ(result.value().committedCount, size_t{2});
        CHECK_EQ(first.transaction, std::string("tx"));
        CHECK_EQ(second.correlation, std::string("request"));
        CHECK(trace == (std::vector<std::string>{"first.prepare", "second.prepare", "first.commit", "second.commit"}));
    };
    SUB_CASE("invalid participant list has no side effects") {
        std::vector<std::string>                trace;
        Participant                             one("one", trace);
        Coordinator                             coordinator;
        std::array<ITransactionParticipant*, 1> nullItems{nullptr};
        auto nullResult = coordinator.execute(TransactionContext("tx"), participants(nullItems));
        CHECK(!nullResult.ok());
        CHECK(static_cast<int>(nullResult.code()) == static_cast<int>(eve::StatusCode::Rejected));
        std::array<ITransactionParticipant*, 2> duplicate{&one, &one};
        auto duplicateResult = coordinator.execute(TransactionContext("tx"), participants(duplicate));
        CHECK(!duplicateResult.ok());
        CHECK(static_cast<int>(duplicateResult.code()) == static_cast<int>(eve::StatusCode::Rejected));
        CHECK(trace.empty());
    };
    SUB_CASE("prepare failure rolls back only prepared participants") {
        std::vector<std::string> trace;
        Participant              first("first", trace), second("second", trace), third("third", trace);
        third.failPrepare = true;
        std::array<ITransactionParticipant*, 3> items{&first, &second, &third};
        Coordinator                             coordinator;
        auto result = coordinator.execute(TransactionContext("tx"), participants(items));
        CHECK(!result.ok());
        CHECK(static_cast<int>(result.code()) == static_cast<int>(eve::StatusCode::Failed));
        CHECK(trace == (std::vector<std::string>{"first.prepare", "second.prepare", "third.prepare", "second.rollback",
                                                 "first.rollback"}));
        CHECK(!first.prepared);
        CHECK(!second.prepared);
        CHECK(!third.prepared);
    };
    SUB_CASE("commit failure rolls back and compensates in reverse") {
        std::vector<std::string> trace;
        Participant              first("first", trace), second("second", trace), third("third", trace);
        second.failCommit = true;
        std::array<ITransactionParticipant*, 3> items{&first, &second, &third};
        Coordinator                             coordinator;
        auto result = coordinator.execute(TransactionContext("tx"), participants(items));
        CHECK(!result.ok());
        CHECK(static_cast<int>(result.code()) == static_cast<int>(eve::StatusCode::Failed));
        CHECK(trace ==
              (std::vector<std::string>{"first.prepare", "second.prepare", "third.prepare", "first.commit",
                                        "second.commit", "third.rollback", "second.rollback", "first.compensate"}));
        CHECK(!first.committed);
        CHECK(!second.prepared);
        CHECK(!third.prepared);
    };
    SUB_CASE("compensation failure remains observable") {
        std::vector<std::string> trace;
        Participant              external("external", trace), failing("failing", trace);
        external.failCompensate = true;
        failing.failCommit      = true;
        std::array<ITransactionParticipant*, 2> items{&external, &failing};
        Coordinator                             coordinator;
        auto result = coordinator.execute(TransactionContext("tx"), participants(items));
        CHECK(!result.ok());
        CHECK(result.error() != nullptr);
        CHECK(external.committed);
        CHECK(trace == (std::vector<std::string>{"external.prepare", "failing.prepare", "external.commit",
                                                 "failing.commit", "failing.rollback", "external.compensate"}));
    };
}

// Store participant: the prepared candidate commits once and conflicts preserve external writes.
TEST_CASE("transaction.statepatch.prepareCommitConflictAndCompensationContract") {
    SUB_CASE("commit publishes prepared candidate") {
        eve::statepatch::Store store;
        auto                   seed = eve::test_support::openStatePatchBatch(store);
        REQUIRE(seed.view.isBound());
        REQUIRE(seed.view->set("actor", "value", "1"));
        REQUIRE(store.commit(seed.view.get()).ok());
        auto batch = eve::test_support::openStatePatchBatch(store);
        REQUIRE(batch.view.isBound());
        REQUIRE(batch.view->setExpected("actor", "value", "2", "1"));
        eve::statepatch::StoreTransactionParticipant participant(store, *batch.view);
        std::array<ITransactionParticipant*, 1>      items{&participant};
        Coordinator                                  coordinator;
        auto result = coordinator.execute(TransactionContext("tx", "request", "command"), participants(items));
        REQUIRE(result.ok());
        CHECK_EQ(store.get("actor", "value"), std::string("2"));
        CHECK_EQ(store.revision(), uint64_t{2});
        CHECK_EQ(store.eventCount(), 2);
    };
    SUB_CASE("invalid candidate does not mutate store") {
        eve::statepatch::Store store;
        auto                   seed = eve::test_support::openStatePatchBatch(store);
        REQUIRE(seed.view.isBound());
        REQUIRE(seed.view->set("actor", "value", "1"));
        REQUIRE(store.commit(seed.view.get()).ok());
        const auto before = store.snapshotJson();
        auto       batch  = eve::test_support::openStatePatchBatch(store);
        REQUIRE(batch.view.isBound());
        CHECK(!batch.view->set("actor", "value", "{"));
        eve::statepatch::StoreTransactionParticipant participant(store, *batch.view);
        std::array<ITransactionParticipant*, 1>      items{&participant};
        Coordinator                                  coordinator;
        CHECK(!coordinator.execute(TransactionContext("tx"), participants(items)).ok());
        CHECK_EQ(store.snapshotJson(), before);
    };
    SUB_CASE("revision conflict preserves external write") {
        eve::statepatch::Store store;
        auto                   seed = eve::test_support::openStatePatchBatch(store);
        REQUIRE(seed.view.isBound());
        REQUIRE(seed.view->set("actor", "value", "1"));
        REQUIRE(store.commit(seed.view.get()).ok());
        auto batch = eve::test_support::openStatePatchBatch(store);
        REQUIRE(batch.view.isBound());
        REQUIRE(batch.view->setExpected("actor", "value", "2", "1"));
        eve::statepatch::StoreTransactionParticipant participant(store, *batch.view);
        const TransactionContext                     context("tx");
        REQUIRE(participant.prepare(context).ok());
        auto external = eve::test_support::openStatePatchBatch(store);
        REQUIRE(external.view.isBound());
        REQUIRE(external.view->set("actor", "other", "9"));
        REQUIRE(store.commit(external.view.get()).ok());
        auto result = participant.commit(context);
        CHECK(!result.ok());
        CHECK(static_cast<int>(result.code()) == static_cast<int>(eve::StatusCode::Conflict));
        CHECK_EQ(store.get("actor", "value"), std::string("1"));
        CHECK_EQ(store.get("actor", "other"), std::string("9"));
        REQUIRE(participant.rollback(context).ok());
    };
    SUB_CASE("compensation restores precommit snapshot") {
        eve::statepatch::Store store;
        auto                   seed = eve::test_support::openStatePatchBatch(store);
        REQUIRE(seed.view.isBound());
        REQUIRE(seed.view->set("actor", "value", "1"));
        REQUIRE(store.commit(seed.view.get()).ok());
        const auto before = store.snapshotJson();
        auto       batch  = eve::test_support::openStatePatchBatch(store);
        REQUIRE(batch.view.isBound());
        REQUIRE(batch.view->setExpected("actor", "value", "2", "1"));
        eve::statepatch::StoreTransactionParticipant participant(store, *batch.view);
        const TransactionContext                     context("tx");
        REQUIRE(participant.prepare(context).ok());
        REQUIRE(participant.commit(context).ok());
        CHECK_EQ(store.get("actor", "value"), std::string("2"));
        REQUIRE(participant.compensate(context).ok());
        CHECK_EQ(store.snapshotJson(), before);
    };
}
