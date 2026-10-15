#include "transaction/Transaction.h"
#include <array>
#include <initializer_list>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "StatePatchTestSupport.h"
#include "common/Module.h"
#include "statepatch/StatePatch.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// transaction.cpp
using namespace eve::transaction;

namespace {

[[maybe_unused]] eve::TransactionId transactionId(const char* text) {
    const auto parsed = eve::TransactionId::parse(text);
    return parsed ? *parsed : eve::TransactionId::nil();
}

[[maybe_unused]] eve::OperationId operationId(const char* text) {
    const auto parsed = eve::OperationId::parse(text);
    return parsed ? *parsed : eve::OperationId::nil();
}

[[maybe_unused]] Plan* createPlan(Ledger& ledger, const char* transaction, const char* correlation = "", const char* causation = "") {
    auto result = ledger.create(correlation, causation, transactionId(transaction));
    REQUIRE(result.ok());
    return result.value();
}

[[maybe_unused]] eve::OperationId stageOperation(Plan& plan, const char* operation, const char* kind, const char* target,
                                const char* payload) {
    auto result = plan.stage(kind, target, payload, operationId(operation));
    REQUIRE(result.ok());
    return result.value();
}

}  // namespace


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

// transaction_participant.cpp
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "StatePatchTestSupport.h"
#include "statepatch/StatePatch.h"
#include "transaction/Transaction.h"

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

[[maybe_unused]] Result<void> participantFailure(std::string_view name, std::string_view phase) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed,
                                                   std::string(name) + " failed during " + std::string(phase),
                                                   "test." + std::string(phase)));
}

class RecordingParticipant final : public ITransactionParticipant {
public:
    RecordingParticipant(std::string name, std::vector<std::string>& events)
        : name_(std::move(name)), events_(events) {}

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

    bool        failPrepare    = false;
    bool        failCommit     = false;
    bool        failRollback   = false;
    bool        failCompensate = false;
    bool        prepared       = false;
    bool        committed      = false;
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

    std::string               name_;
    std::vector<std::string>& events_;
};

[[maybe_unused]] void checkEvents(const std::vector<std::string>& actual, std::initializer_list<std::string_view> expected) {
    CHECK_EQ(actual.size(), expected.size());
    size_t index = 0;
    for (const auto value : expected) {
        const std::string actualValue = actual[index];
        ++index;
        CHECK_EQ(actualValue, std::string(value));
    }
}

template <size_t N>
[[maybe_unused]] auto participantSpan(std::array<ITransactionParticipant*, N>& participants) {
    return std::span<ITransactionParticipant*>(participants.data(), participants.size());
}

}  // namespace
