#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "transaction/Transaction.h"

#include <simplesquirrel/simplesquirrel.hpp>

using namespace eve::transaction;

namespace {

eve::TransactionId transactionId(const char* text) {
    const auto parsed = eve::TransactionId::parse(text);
    return parsed ? *parsed : eve::TransactionId::nil();
}

eve::OperationId operationId(const char* text) {
    const auto parsed = eve::OperationId::parse(text);
    return parsed ? *parsed : eve::OperationId::nil();
}

Plan* createPlan(Ledger& ledger, const char* transaction, const char* correlation = "", const char* causation = "") {
    auto result = ledger.create(correlation, causation, transactionId(transaction));
    REQUIRE(result.ok());
    return result.value();
}

eve::OperationId stageOperation(Plan& plan, const char* operation, const char* kind, const char* target,
                                const char* payload) {
    auto result = plan.stage(kind, target, payload, operationId(operation));
    REQUIRE(result.ok());
    return result.value();
}

}  // namespace

TEST_CASE("transaction.stage.stableIdsAndCanonicalPayload") {
    Ledger ledger;
    Plan*  plan = createPlan(ledger, "00000000-0000-4000-8000-000000000001", "rebellion-7", "order-3");
    CHECK_EQ(plan->id(), std::string("00000000-0000-4000-8000-000000000001"));
    const auto id =
        stageOperation(*plan, "00000000-0000-4000-8000-000000000002", "set_owner", "base-2", "{\"z\":2,\"a\":1}");
    CHECK_EQ(id.format(), std::string("00000000-0000-4000-8000-000000000002"));
    CHECK_EQ(plan->operationAt(0)->payload, std::string("{\"a\":1,\"z\":2}"));
    CHECK_EQ(plan->correlation(), std::string("rebellion-7"));
    CHECK_EQ(plan->causation(), std::string("order-3"));
}

TEST_CASE("transaction.validation.requiresEveryOperation") {
    Ledger ledger;
    Plan*  plan   = createPlan(ledger, "00000000-0000-4000-8000-000000000001");
    auto   first  = stageOperation(*plan, "00000000-0000-4000-8000-000000000002", "debit", "treasury", "10");
    auto   second = stageOperation(*plan, "00000000-0000-4000-8000-000000000003", "credit", "army", "10");
    CHECK(plan->markValid(first).ok());
    CHECK(!plan->validate().ok());
    CHECK(plan->markInvalid(second, "capacity exceeded").ok());
    CHECK(!plan->validate().ok());
    CHECK_EQ(plan->error(), std::string("capacity exceeded"));
    CHECK(plan->markValid(second).ok());
    CHECK(plan->validate().ok());
    CHECK_EQ(stateName(plan->state()), std::string("validated"));
}

TEST_CASE("transaction.commit.freezesPlan") {
    Ledger ledger;
    Plan*  plan = createPlan(ledger, "00000000-0000-4000-8000-000000000001");
    auto   id   = stageOperation(*plan, "00000000-0000-4000-8000-000000000002", "transfer", "asset-1", "null");
    CHECK(plan->markValid(id).ok());
    CHECK(plan->validate().ok());
    CHECK(plan->commit().ok());
    CHECK_EQ(stateName(plan->state()), std::string("committed"));
    auto late = plan->stage("late", "asset-2", "null", operationId("00000000-0000-4000-8000-000000000003"));
    CHECK(!late.ok());
    CHECK(!plan->rollback("too late").ok());
    CHECK(!plan->fail("too late").ok());
}

TEST_CASE("transaction.rollbackAndFail.areTerminal") {
    Ledger ledger;
    Plan*  rolledBack = createPlan(ledger, "00000000-0000-4000-8000-000000000001");
    CHECK(rolledBack->rollback("cancelled by script").ok());
    CHECK_EQ(stateName(rolledBack->state()), std::string("rolled_back"));
    CHECK_EQ(rolledBack->error(), std::string("cancelled by script"));
    Plan* failed = createPlan(ledger, "00000000-0000-4000-8000-000000000002");
    CHECK(failed->fail("external apply failed").ok());
    CHECK_EQ(stateName(failed->state()), std::string("failed"));
    CHECK(!failed->commit().ok());
}

TEST_CASE("transaction.events.areDeterministic") {
    Ledger ledger;
    Plan*  plan = createPlan(ledger, "00000000-0000-4000-8000-000000000001");
    auto   id   = stageOperation(*plan, "00000000-0000-4000-8000-000000000002", "change", "subject", "{}");
    REQUIRE(plan->markValid(id).ok());
    REQUIRE(plan->validate().ok());
    REQUIRE(plan->commit().ok());
    CHECK_EQ(plan->eventCount(), 5);
    CHECK_EQ(plan->eventAt(0)->type, std::string("opened"));
    CHECK_EQ(plan->eventAt(1)->type, std::string("staged"));
    CHECK_EQ(plan->eventAt(4)->type, std::string("committed"));
    CHECK_EQ(plan->eventAt(4)->sequence, uint64_t{5});
}

TEST_CASE("transaction.snapshot.roundTripsTransactionally") {
    Ledger source;
    Plan*  plan = createPlan(source, "00000000-0000-4000-8000-000000000001", "campaign", "command");
    auto   id =
        stageOperation(*plan, "00000000-0000-4000-8000-000000000002", "assign", "base", "{\"role\":\"governor\"}");
    REQUIRE(plan->markValid(id).ok());
    REQUIRE(plan->validate().ok());
    const std::string snapshot = source.snapshotJson();
    Ledger            restored;
    REQUIRE(restored.restore(snapshot).ok());
    CHECK_EQ(restored.snapshotJson(), snapshot);
    CHECK_EQ(restored.find(plan->id())->findOperation(id)->payload, std::string("{\"role\":\"governor\"}"));
    CHECK(!restored.restore("{\"version\":1}").ok());
    CHECK_EQ(restored.snapshotJson(), snapshot);
}

TEST_CASE("transaction.script.api") {
    ssq::VM    vm(1024, ssq::Libs::ALL);
    ssq::Table eve = vm.addTable("eve");
    Transaction::expose(eve);
    vm.run(vm.compileSource(R"(
        result <- "bad";
        local transaction = eve.Transaction();
        local ledger = transaction.newLedger();
        local created = ledger.create("war-1", "order-9", "00000000-0000-4000-8000-000000000001");
        if (created.ok) {
            local plan = ledger.find(created.value.id);
            local staged = plan.stage("change_control", "base-4", "{\"owner\":\"faction-b\"}", "00000000-0000-4000-8000-000000000002");
            if (staged.ok && plan.markValid(staged.value).ok && plan.validate().ok && plan.commit().ok) {
            local copy = transaction.newLedger();
            if (copy.restore(ledger.snapshotJson()).ok)
                result = copy.at(0).getState() + ":" + copy.at(0).operationAt(0).getKind();
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("committed:change_control"));
}
