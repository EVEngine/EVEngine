#include "attributes/AttributeSetResourceAccount.h"
#include "dialogue/ConversationAuthoring.h"
#include "dialogue/DialogueFlow.h"
#include "dialogue/DialoguePayment.h"
#include "economy/EconomyLedgerResourceAccount.h"
#include "statepatch/StateAccessAdapter.h"
#include "statepatch/StatePatch.h"
#include "zeroerr/unittest.h"

#include <array>
#include <cstdint>
#include <span>
#include <utility>

#include <simplesquirrel/simplesquirrel.hpp>

namespace {

using eve::StateMutation;
using eve::Value;
using eve::dialogue::DialogueAccountBindings;
using eve::dialogue::DialoguePaymentAdapter;
using eve::dialogue::PaymentSpec;

DialoguePaymentAdapter makeAdapter(eve::economy::EconomyLedgerResourceAccount& money,
                                   eve::attributes::AttributeSetResourceAccount& reputation) {
    DialogueAccountBindings bindings;
    bindings.money = &money;
    bindings.reputation = &reputation;
    return DialoguePaymentAdapter(std::move(bindings));
}

}  // namespace

TEST_CASE("dialoguePayment.composesMoneyReputationAndStatePatch") {
    eve::economy::EconomyLedger ledger;
    CHECK(ledger.credit("gold", 20) == 20);
    eve::attributes::AttributeSet attributes("actor");
    attributes.setBase("reputation", 10.0);
    eve::economy::EconomyLedgerResourceAccount money(ledger);
    eve::attributes::AttributeSetResourceAccount reputation(attributes);
    DialoguePaymentAdapter adapter = makeAdapter(money, reputation);

    eve::statepatch::Store store;
    eve::statepatch::StatePatchStateAdapter state(store);
    PaymentSpec payment{5, 2};
    const std::array<StateMutation, 1> mutations{{
        StateMutation{"actor", "accepted", Value(true), eve::MutationKind::Set, true},
    }};

    auto result = adapter.executeStateMutation("dialogue-payment-success", payment, state, mutations);
    REQUIRE(result.ok());
    (void)std::move(result).takeValue();
    CHECK(ledger.get("gold") == 15);
    CHECK(attributes.getBase("reputation") == 8.0);
    CHECK(store.has("actor", "accepted"));
    CHECK(store.get("actor", "accepted") == "true");
}

TEST_CASE("dialoguePayment.failureDoesNotDebitOrPublishState") {
    eve::economy::EconomyLedger ledger;
    CHECK(ledger.credit("gold", 20) == 20);
    eve::attributes::AttributeSet attributes("actor");
    attributes.setBase("reputation", 10.0);
    eve::economy::EconomyLedgerResourceAccount money(ledger);
    eve::attributes::AttributeSetResourceAccount reputation(attributes);
    DialoguePaymentAdapter adapter = makeAdapter(money, reputation);

    eve::statepatch::Store store;
    eve::statepatch::StatePatchStateAdapter state(store);
    const std::array<StateMutation, 2> invalidMutations{{
        StateMutation{"actor", "must_not_publish", Value(true), eve::MutationKind::Set, true},
        StateMutation{"actor", "not_a_number", Value("bad"), eve::MutationKind::AddNumber, true},
    }};
    auto result = adapter.executeStateMutation("dialogue-payment-failure", PaymentSpec{5, 2}, state,
                                               invalidMutations);
    REQUIRE(!result.ok());
    (void)result.status();
    CHECK(ledger.get("gold") == 20);
    CHECK(attributes.getBase("reputation") == 10.0);
    CHECK(!store.has("actor", "must_not_publish"));

    auto insufficient = adapter.executeStateMutation(
        "dialogue-payment-insufficient", PaymentSpec{100, 1}, state,
        std::span<const StateMutation>{});
    REQUIRE(!insufficient.ok());
    (void)insufficient.status();
    CHECK(ledger.get("gold") == 20);
    CHECK(attributes.getBase("reputation") == 10.0);
}

TEST_CASE("dialoguePayment.specRejectsUnknownAndNonPositiveFields") {
    const auto unknown = PaymentSpec::fromValue(
        Value::Object{{"money", Value(1)}, {"tokens", Value(2)}});
    REQUIRE(!unknown.ok());
    (void)unknown.status();

    const auto negative = PaymentSpec::fromValue(Value::Object{{"reputation", Value(0)}});
    REQUIRE(!negative.ok());
    (void)negative.status();

    PaymentSpec payment{3, 4};
    const Value serialized = payment.toValue();
    auto roundTrip = PaymentSpec::fromValue(serialized);
    REQUIRE(roundTrip.ok());
    const PaymentSpec restored = std::move(roundTrip).takeValue();
    CHECK(restored.money == 3);
    CHECK(restored.reputation == 4);
}

TEST_CASE("dialoguePayment.choiceUsesTheSameAtomicFacade") {
    eve::economy::EconomyLedger ledger;
    CHECK(ledger.credit("gold", 20) == 20);
    eve::attributes::AttributeSet attributes("actor");
    attributes.setBase("reputation", 10.0);
    eve::economy::EconomyLedgerResourceAccount money(ledger);
    eve::attributes::AttributeSetResourceAccount reputation(attributes);
    eve::statepatch::Store store;
    eve::statepatch::StatePatchStateAdapter state(store);

    eve::dialogue::ConversationAsset asset;
    asset.id = "dialogue.payment-choice";
    asset.entry = "choice";
    eve::dialogue::ConversationAsset::Node choice;
    choice.id = "choice";
    choice.kind = eve::dialogue::ConversationAsset::Node::Kind::Choice;
    choice.routes.emplace_back("buy", "end");
    choice.routes.front().payment = PaymentSpec{5, 2};
    choice.routes.front().stateMutations.push_back(
        StateMutation{"actor", "accepted", Value(true), eve::MutationKind::Set, true});
    eve::dialogue::ConversationAsset::Node end;
    end.id = "end";
    end.kind = eve::dialogue::ConversationAsset::Node::Kind::End;
    asset.nodes = {choice, end};
    eve::dialogue::ConversationDocument document(asset);

    eve::dialogue::DialogueFlow flow;
    eve::dialogue::DialogueFlow::IntegrationConfig config;
    config.stateMutation = &state;
    config.accounts.money = &money;
    config.accounts.reputation = &reputation;
    flow.configureIntegration(std::move(config));
    REQUIRE(flow.applyDocument(&document));
    CHECK(flow.start(asset.id, ssq::Object{}));
    CHECK(flow.select("buy").ok());
    CHECK(ledger.get("gold") == 15);
    CHECK(attributes.getBase("reputation") == 8.0);
    CHECK(store.has("actor", "accepted"));
}
