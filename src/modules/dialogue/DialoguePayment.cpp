#include "dialogue/DialoguePayment.h"

#include "transaction/ResourceAccountParticipant.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace eve::dialogue {
namespace {

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "dialogue.payment"));
}

eve::Result<void> failureVoid(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<void>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "dialogue.payment"));
}

eve::Result<std::int64_t> positiveInteger(const eve::Value& value, std::string_view path) {
    if (!value.isInt64())
        return failure<std::int64_t>(eve::DiagnosticCode::InvalidArgument, "dialogue payment amount must be an Int64",
                                     std::string(path));
    const std::int64_t amount = value.asInt();
    if (amount <= 0)
        return failure<std::int64_t>(eve::DiagnosticCode::InvalidArgument, "dialogue payment amount must be positive",
                                     std::string(path));
    return eve::Result<std::int64_t>::success(amount);
}

struct AccountCost {
    eve::resource::IResourceAccount* account = nullptr;
    eve::resource::CostSpec          cost;
};

eve::Result<std::vector<AccountCost>> buildCosts(const DialogueAccountBindings& bindings, const PaymentSpec& payment) {
    auto valid = payment.validate();
    if (!valid) return eve::Result<std::vector<AccountCost>>::failure(valid.status());

    struct Raw {
        eve::resource::IResourceAccount* account;
        const std::string*               resource;
        std::optional<std::int64_t>      amount;
        const char*                      path;
    };
    const std::array<Raw, 2> raw{{
        {bindings.money, &bindings.moneyResource, payment.money, "money"},
        {bindings.reputation, &bindings.reputationResource, payment.reputation, "reputation"},
    }};

    std::vector<AccountCost> result;
    for (const auto& item : raw) {
        if (!item.amount) continue;
        if (item.account == nullptr)
            return failure<std::vector<AccountCost>>(
                eve::DiagnosticCode::Unsupported,
                std::string("dialogue ") + item.path + " payment has no account adapter", item.path);
        auto costItem = eve::resource::ResourceCost::create(*item.resource, *item.amount);
        if (!costItem) return eve::Result<std::vector<AccountCost>>::failure(costItem.status());

        auto found = std::find_if(result.begin(), result.end(),
                                  [&](const AccountCost& candidate) { return candidate.account == item.account; });
        if (found == result.end()) {
            std::vector<eve::resource::ResourceCost> items;
            items.push_back(std::move(costItem).takeValue());
            auto cost = eve::resource::CostSpec::create(std::move(items));
            if (!cost) return eve::Result<std::vector<AccountCost>>::failure(cost.status());
            result.push_back({item.account, std::move(cost).takeValue()});
        } else {
            std::vector<eve::resource::ResourceCost> items = found->cost.items();
            items.push_back(std::move(costItem).takeValue());
            auto cost = eve::resource::CostSpec::create(std::move(items));
            if (!cost) return eve::Result<std::vector<AccountCost>>::failure(cost.status());
            found->cost = std::move(cost).takeValue();
        }
    }
    return eve::Result<std::vector<AccountCost>>::success(std::move(result));
}

eve::Result<void> lifecycleFailure(std::string message) {
    return failureVoid(eve::DiagnosticCode::Conflict, std::move(message), "transaction.lifecycle");
}

}  // namespace

eve::Result<void> PaymentSpec::validate() const {
    if (money && *money <= 0)
        return failureVoid(eve::DiagnosticCode::InvalidArgument, "dialogue money payment must be positive", "money");
    if (reputation && *reputation <= 0)
        return failureVoid(eve::DiagnosticCode::InvalidArgument, "dialogue reputation payment must be positive",
                           "reputation");
    return eve::Result<void>::success();
}

eve::Result<PaymentSpec> PaymentSpec::fromValue(const eve::Value& value) {
    if (!value.isObject())
        return failure<PaymentSpec>(eve::DiagnosticCode::InvalidArgument, "dialogue payment must be an object",
                                    "payment");
    PaymentSpec result;
    for (const auto& key : value.keys()) {
        const eve::Value* member = value.find(key);
        if (key == "money") {
            auto amount = positiveInteger(*member, "money");
            if (!amount) return eve::Result<PaymentSpec>::failure(amount.status());
            result.money = std::move(amount).takeValue();
        } else if (key == "reputation") {
            auto amount = positiveInteger(*member, "reputation");
            if (!amount) return eve::Result<PaymentSpec>::failure(amount.status());
            result.reputation = std::move(amount).takeValue();
        } else {
            return failure<PaymentSpec>(eve::DiagnosticCode::InvalidArgument, "unknown dialogue payment field",
                                        "payment." + key);
        }
    }
    return eve::Result<PaymentSpec>::success(std::move(result));
}

eve::Value PaymentSpec::toValue() const {
    eve::Value::Object object;
    if (money) object.emplace("money", eve::Value(*money));
    if (reputation) object.emplace("reputation", eve::Value(*reputation));
    return eve::Value(std::move(object));
}

DialogueStateMutationParticipant::DialogueStateMutationParticipant(eve::IStateMutation&                provider,
                                                                   std::span<const eve::StateMutation> mutations)
    : provider_(provider), mutations_(mutations.begin(), mutations.end()) {}

eve::Result<void> DialogueStateMutationParticipant::prepare(const eve::transaction::TransactionContext& context) {
    if (context.transactionId().empty())
        return failureVoid(eve::DiagnosticCode::InvalidArgument, "dialogue state transaction requires a transaction id",
                           "transactionId");
    if (phase_ != Phase::Idle) return lifecycleFailure("dialogue state participant is not idle");
    phase_ = Phase::Prepared;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> DialogueStateMutationParticipant::commit(const eve::transaction::TransactionContext& context) {
    if (phase_ != Phase::Prepared) return lifecycleFailure("dialogue state participant has no prepared stage");
    auto applied = provider_.apply(
        mutations_, eve::MutationContext{context.transactionId(), context.correlationId(), context.causationId()});
    if (!applied) return eve::Result<void>::failure(applied.status());
    (void)std::move(applied).takeValue();
    phase_ = Phase::Committed;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> DialogueStateMutationParticipant::rollback(const eve::transaction::TransactionContext&) {
    if (phase_ != Phase::Prepared)
        return lifecycleFailure("dialogue state participant has no prepared stage to roll back");
    phase_ = Phase::RolledBack;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> DialogueStateMutationParticipant::compensate(const eve::transaction::TransactionContext&) {
    return failureVoid(eve::DiagnosticCode::Unsupported,
                       "dialogue state mutation has no generic inverse; it must be last", "transaction.compensation");
}

DialoguePaymentAdapter::DialoguePaymentAdapter(DialogueAccountBindings bindings) : bindings_(std::move(bindings)) {}

void DialoguePaymentAdapter::setBindings(DialogueAccountBindings bindings) { bindings_ = std::move(bindings); }

eve::Result<eve::transaction::TransactionReceipt> DialoguePaymentAdapter::execute(
    std::string transactionId, const PaymentSpec& payment,
    std::span<eve::transaction::ITransactionParticipant*> effects) const {
    if (transactionId.empty())
        return failure<eve::transaction::TransactionReceipt>(
            eve::DiagnosticCode::InvalidArgument, "dialogue payment transaction id must not be empty", "transactionId");

    for (std::size_t index = 0; index < effects.size(); ++index) {
        if (effects[index] == nullptr)
            return failure<eve::transaction::TransactionReceipt>(eve::DiagnosticCode::InvalidArgument,
                                                                 "dialogue payment effect must not be null",
                                                                 "effects[" + std::to_string(index) + "]");
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (effects[prior] == effects[index])
                return failure<eve::transaction::TransactionReceipt>(eve::DiagnosticCode::Conflict,
                                                                     "dialogue payment effects must be unique",
                                                                     "effects[" + std::to_string(index) + "]");
        }
    }

    auto costs = buildCosts(bindings_, payment);
    if (!costs) return eve::Result<eve::transaction::TransactionReceipt>::failure(costs.status());
    auto accountCosts = std::move(costs).takeValue();

    std::vector<std::unique_ptr<eve::transaction::ResourceDebitParticipant>> debitParticipants;
    debitParticipants.reserve(accountCosts.size());
    std::vector<eve::transaction::ITransactionParticipant*> participants;
    participants.reserve(accountCosts.size() + effects.size());
    for (std::size_t index = 0; index < accountCosts.size(); ++index) {
        debitParticipants.push_back(std::make_unique<eve::transaction::ResourceDebitParticipant>(
            *accountCosts[index].account, std::move(accountCosts[index].cost),
            "dialogue.resource-debit[" + std::to_string(index) + "]"));
        participants.push_back(debitParticipants.back().get());
    }
    for (auto* effect : effects) participants.push_back(effect);
    if (participants.empty())
        return failure<eve::transaction::TransactionReceipt>(eve::DiagnosticCode::InvalidArgument,
                                                             "dialogue payment has no account or effect", "payment");

    eve::transaction::Coordinator coordinator;
    return coordinator.execute(eve::transaction::TransactionContext(std::move(transactionId)), participants);
}

eve::Result<eve::transaction::TransactionReceipt> DialoguePaymentAdapter::executeStateMutation(
    std::string transactionId, const PaymentSpec& payment, eve::IStateMutation& provider,
    std::span<const eve::StateMutation> mutations) const {
    DialogueStateMutationParticipant                          state(provider, mutations);
    std::array<eve::transaction::ITransactionParticipant*, 1> effects{&state};
    return execute(std::move(transactionId), payment, effects);
}

}  // namespace eve::dialogue
