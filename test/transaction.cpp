#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "transaction/Transaction.h"

#include <simplesquirrel/simplesquirrel.hpp>

using namespace eve::transaction;

TEST_CASE("transaction.stage.stableIdsAndCanonicalPayload") {
    Ledger ledger;
    Plan*  plan = ledger.create("rebellion-7", "order-3");
    CHECK_EQ(plan->id(), std::string("transaction-0000000000000001"));
    CHECK_EQ(plan->stage("set_owner", "base-2", "{\"z\":2,\"a\":1}"), std::string("operation-0000000000000001"));
    CHECK_EQ(plan->operationAt(0)->payload, std::string("{\"a\":1,\"z\":2}"));
    CHECK_EQ(plan->correlation(), std::string("rebellion-7"));
    CHECK_EQ(plan->causation(), std::string("order-3"));
}

TEST_CASE("transaction.validation.requiresEveryOperation") {
    Ledger ledger;
    Plan*  plan   = ledger.create();
    auto   first  = plan->stage("debit", "treasury", "10");
    auto   second = plan->stage("credit", "army", "10");
    CHECK(plan->markValid(first));
    CHECK(!plan->validate());
    CHECK(plan->markInvalid(second, "capacity exceeded"));
    CHECK(!plan->validate());
    CHECK_EQ(plan->error(), std::string("capacity exceeded"));
    CHECK(plan->markValid(second));
    CHECK(plan->validate());
    CHECK_EQ(stateName(plan->state()), std::string("validated"));
}

TEST_CASE("transaction.commit.freezesPlan") {
    Ledger ledger;
    Plan*  plan = ledger.create();
    auto   id   = plan->stage("transfer", "asset-1", "null");
    CHECK(plan->markValid(id));
    CHECK(plan->validate());
    CHECK(plan->commit());
    CHECK_EQ(stateName(plan->state()), std::string("committed"));
    CHECK(plan->stage("late", "asset-2", "null").empty());
    CHECK(!plan->rollback("too late"));
    CHECK(!plan->fail("too late"));
}

TEST_CASE("transaction.rollbackAndFail.areTerminal") {
    Ledger ledger;
    Plan*  rolledBack = ledger.create();
    CHECK(rolledBack->rollback("cancelled by script"));
    CHECK_EQ(stateName(rolledBack->state()), std::string("rolled_back"));
    CHECK_EQ(rolledBack->error(), std::string("cancelled by script"));
    Plan* failed = ledger.create();
    CHECK(failed->fail("external apply failed"));
    CHECK_EQ(stateName(failed->state()), std::string("failed"));
    CHECK(!failed->commit());
}

TEST_CASE("transaction.events.areDeterministic") {
    Ledger ledger;
    Plan*  plan = ledger.create();
    auto   id   = plan->stage("change", "subject", "{}");
    plan->markValid(id);
    plan->validate();
    plan->commit();
    CHECK_EQ(plan->eventCount(), 5);
    CHECK_EQ(plan->eventAt(0)->type, std::string("opened"));
    CHECK_EQ(plan->eventAt(1)->type, std::string("staged"));
    CHECK_EQ(plan->eventAt(4)->type, std::string("committed"));
    CHECK_EQ(plan->eventAt(4)->sequence, uint64_t{5});
}

TEST_CASE("transaction.snapshot.roundTripsTransactionally") {
    Ledger source;
    Plan*  plan = source.create("campaign", "command");
    auto   id   = plan->stage("assign", "base", "{\"role\":\"governor\"}");
    plan->markValid(id);
    plan->validate();
    const std::string snapshot = source.snapshotJson();
    Ledger            restored;
    CHECK(restored.restoreJson(snapshot));
    CHECK_EQ(restored.snapshotJson(), snapshot);
    CHECK_EQ(restored.find(plan->id())->findOperation(id)->payload, std::string("{\"role\":\"governor\"}"));
    CHECK(!restored.restoreJson("{\"version\":1}"));
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
        local plan = ledger.create("war-1", "order-9");
        local op = plan.stage("change_control", "base-4", "{\"owner\":\"faction-b\"}");
        if (plan.markValid(op) && plan.validate() && plan.commit()) {
            local copy = transaction.newLedger();
            if (copy.restoreJson(ledger.snapshotJson()))
                result = copy.at(0).getState() + ":" + copy.at(0).operationAt(0).getKind();
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("committed:change_control"));
}
