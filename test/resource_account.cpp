#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "attributes/AttributeSetResourceAccount.h"
#include "economy/EconomyLedgerResourceAccount.h"
#include "economy/ResourceType.h"
#include "transaction/ResourceAccountParticipant.h"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

using eve::resource::CostInput;
using eve::resource::CostSpec;

static_assert(!std::is_constructible_v<eve::resource::AccountNonce, std::uint64_t>);

CostSpec makeCost(std::initializer_list<CostInput> items) {
    auto result = CostSpec::from(items);
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}

template <class Account, class Balance>
void runCommonAccountContract(Account& account, Balance&& balance) {
    const CostSpec cost = makeCost({{"mana", 40}});

    auto affordable = account.canAfford(cost);
    REQUIRE(affordable.ok());
    CHECK(affordable.value().affordable);

    auto reservationResult = account.reserve(cost);
    REQUIRE(reservationResult.ok());
    const auto reservation = reservationResult.value();
    CHECK(reservation.isValid());
    CHECK_EQ(balance("mana"), std::int64_t{100});

    const CostSpec blockedCost = makeCost({{"mana", 70}});
    auto blocked = account.reserve(blockedCost);
    CHECK(!blocked.ok());
    CHECK_EQ(static_cast<int>(blocked.code()), static_cast<int>(eve::StatusCode::Rejected));
    CHECK_EQ(balance("mana"), std::int64_t{100});

    auto committed = account.commit(reservation);
    REQUIRE(committed.ok());
    CHECK(committed.value().isValid());
    CHECK_EQ(static_cast<int>(committed.value().operation),
             static_cast<int>(eve::resource::ReceiptOperation::Debit));
    CHECK_EQ(balance("mana"), std::int64_t{60});

    // A copied credential has the same account nonce and id, but the account
    // lifecycle still permits only one terminal operation.
    const auto copiedReservation = reservation;
    auto duplicateCommit = account.commit(copiedReservation);
    CHECK(!duplicateCommit.ok());
    CHECK_EQ(static_cast<int>(duplicateCommit.code()), static_cast<int>(eve::StatusCode::Conflict));
    auto duplicateRollback = account.rollback(copiedReservation);
    CHECK(!duplicateRollback.ok());
    CHECK_EQ(static_cast<int>(duplicateRollback.code()),
             static_cast<int>(eve::StatusCode::Conflict));

    const CostSpec credit = makeCost({{"mana", 5}, {"stamina", 3}});
    auto credited = account.credit(credit);
    REQUIRE(credited.ok());
    CHECK_EQ(static_cast<int>(credited.value().operation),
             static_cast<int>(eve::resource::ReceiptOperation::Credit));
    CHECK_EQ(balance("mana"), std::int64_t{65});
    CHECK_EQ(balance("stamina"), std::int64_t{3});
}

class CommitFailureParticipant final : public eve::transaction::ITransactionParticipant {
public:
    std::string_view name() const noexcept override { return "test-commit-failure"; }

    [[nodiscard]] eve::Result<void> prepare(
        const eve::transaction::TransactionContext& context) override {
        (void)context;
        prepared = true;
        return eve::Result<void>::success();
    }

    [[nodiscard]] eve::Result<void> commit(
        const eve::transaction::TransactionContext& context) override {
        (void)context;
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed,
                                   "injected commit failure", "test.commit"));
    }

    [[nodiscard]] eve::Result<void> rollback(
        const eve::transaction::TransactionContext& context) override {
        (void)context;
        prepared = false;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> compensate(
        const eve::transaction::TransactionContext& context) override {
        (void)context;
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                   "injected participant was never committed", "test.compensate"));
    }

    bool prepared = false;
};

}  // namespace

TEST_CASE("resource.account.attributeSetMatchesCommonDebitCreditReservationContract") {
    eve::attributes::AttributeSet attributes("account.attributes");
    attributes.setBase("mana", 100.0);
    eve::attributes::AttributeSetResourceAccount account(attributes);

    runCommonAccountContract(account, [&attributes](std::string_view resource) {
        return static_cast<std::int64_t>(attributes.getBase(std::string(resource), 0.0));
    });
}

TEST_CASE("resource.account.economyLedgerMatchesCommonDebitCreditReservationContract") {
    eve::economy::EconomyLedger ledger;
    CHECK_EQ(ledger.credit("mana", 100), 100);
    eve::economy::EconomyLedgerResourceAccount account(ledger);

    runCommonAccountContract(account, [&ledger](std::string_view resource) {
        return static_cast<std::int64_t>(ledger.get(std::string(resource)));
    });
}

TEST_CASE("resource.account.multiResourceDebitFailureIsAtomicForBothAdapters") {
    const CostSpec impossible = makeCost({{"gold", 4}, {"mana", 100}});

    eve::attributes::AttributeSet attributes;
    attributes.setBase("gold", 10.0);
    attributes.setBase("mana", 3.0);
    eve::attributes::AttributeSetResourceAccount attributeAccount(attributes);
    auto attributeDebit = attributeAccount.debit(impossible);
    CHECK(!attributeDebit.ok());
    CHECK_EQ(static_cast<int>(attributeDebit.code()), static_cast<int>(eve::StatusCode::Rejected));
    CHECK_EQ(attributes.getBase("gold"), 10.0);
    CHECK_EQ(attributes.getBase("mana"), 3.0);

    eve::economy::EconomyLedger ledger;
    CHECK_EQ(ledger.credit("gold", 10), 10);
    CHECK_EQ(ledger.credit("mana", 3), 3);
    eve::economy::EconomyLedgerResourceAccount economyAccount(ledger);
    auto economyDebit = economyAccount.debit(impossible);
    CHECK(!economyDebit.ok());
    CHECK_EQ(static_cast<int>(economyDebit.code()), static_cast<int>(eve::StatusCode::Rejected));
    CHECK_EQ(ledger.get("gold"), 10);
    CHECK_EQ(ledger.get("mana"), 3);
}

TEST_CASE("resource.account.rollbackReleasesReservationWithoutChangingBalance") {
    eve::attributes::AttributeSet attributes;
    attributes.setBase("mana", 50.0);
    eve::attributes::AttributeSetResourceAccount account(attributes);
    const CostSpec cost = makeCost({{"mana", 30}});

    auto reservationResult = account.reserve(cost);
    REQUIRE(reservationResult.ok());
    const auto reservation = reservationResult.value();
    auto blocked = account.canAfford(makeCost({{"mana", 25}}));
    REQUIRE(blocked.ok());
    CHECK(!blocked.value().affordable);
    CHECK_EQ(attributes.getBase("mana"), 50.0);

    auto rolledBack = account.rollback(reservation);
    REQUIRE(rolledBack.ok());
    CHECK_EQ(attributes.getBase("mana"), 50.0);
    auto available = account.canAfford(makeCost({{"mana", 25}}));
    REQUIRE(available.ok());
    CHECK(available.value().affordable);

    const auto copiedReservation = reservation;
    auto duplicateRollback = account.rollback(copiedReservation);
    CHECK(!duplicateRollback.ok());
    CHECK_EQ(static_cast<int>(duplicateRollback.code()),
             static_cast<int>(eve::StatusCode::Conflict));
    auto duplicateCommit = account.commit(copiedReservation);
    CHECK(!duplicateCommit.ok());
    CHECK_EQ(static_cast<int>(duplicateCommit.code()), static_cast<int>(eve::StatusCode::Conflict));
}

TEST_CASE("resource.account.sameTypeAccountsRejectForeignCredentialsEvenWithSameIdAndCost") {
    eve::attributes::AttributeSet firstAttributes;
    eve::attributes::AttributeSet secondAttributes;
    firstAttributes.setBase("mana", 10.0);
    secondAttributes.setBase("mana", 10.0);
    eve::attributes::AttributeSetResourceAccount first(firstAttributes);
    eve::attributes::AttributeSetResourceAccount second(secondAttributes);
    const CostSpec cost = makeCost({{"mana", 5}});

    auto firstReservationResult = first.reserve(cost);
    auto secondReservationResult = second.reserve(cost);
    REQUIRE(firstReservationResult.ok());
    REQUIRE(secondReservationResult.ok());
    const auto firstReservation = firstReservationResult.value();
    const auto secondReservation = secondReservationResult.value();
    CHECK_EQ(firstReservation.id.value(), secondReservation.id.value());
    CHECK_NE(firstReservation.account.value(), secondReservation.account.value());

    auto foreignCommit = second.commit(firstReservation);
    CHECK(!foreignCommit.ok());
    CHECK_EQ(static_cast<int>(foreignCommit.code()), static_cast<int>(eve::StatusCode::Conflict));
    auto foreignRollback = second.rollback(firstReservation);
    CHECK(!foreignRollback.ok());
    CHECK_EQ(static_cast<int>(foreignRollback.code()),
             static_cast<int>(eve::StatusCode::Conflict));
    auto reverseForeignCommit = first.commit(secondReservation);
    CHECK(!reverseForeignCommit.ok());
    CHECK_EQ(static_cast<int>(reverseForeignCommit.code()),
             static_cast<int>(eve::StatusCode::Conflict));
    auto reverseForeignRollback = first.rollback(secondReservation);
    CHECK(!reverseForeignRollback.ok());
    CHECK_EQ(static_cast<int>(reverseForeignRollback.code()),
             static_cast<int>(eve::StatusCode::Conflict));
    CHECK_EQ(secondAttributes.getBase("mana"), 10.0);

    auto ownCommit = first.commit(firstReservation);
    REQUIRE(ownCommit.ok());
    CHECK_EQ(firstAttributes.getBase("mana"), 5.0);
    auto ownRollback = second.rollback(secondReservation);
    REQUIRE(ownRollback.ok());
    CHECK_EQ(secondAttributes.getBase("mana"), 10.0);
}

TEST_CASE("resource.account.economyAccountsRejectForeignCredentialsEvenWithSameIdAndCost") {
    eve::economy::EconomyLedger firstLedger;
    eve::economy::EconomyLedger secondLedger;
    CHECK_EQ(firstLedger.credit("gold", 10), 10);
    CHECK_EQ(secondLedger.credit("gold", 10), 10);
    eve::economy::EconomyLedgerResourceAccount first(firstLedger);
    eve::economy::EconomyLedgerResourceAccount second(secondLedger);
    const CostSpec cost = makeCost({{"gold", 5}});

    auto firstReservationResult = first.reserve(cost);
    auto secondReservationResult = second.reserve(cost);
    REQUIRE(firstReservationResult.ok());
    REQUIRE(secondReservationResult.ok());
    const auto firstReservation = firstReservationResult.value();
    const auto secondReservation = secondReservationResult.value();
    CHECK_EQ(firstReservation.id.value(), secondReservation.id.value());
    CHECK_NE(firstReservation.account.value(), secondReservation.account.value());

    auto foreignCommit = second.commit(firstReservation);
    CHECK(!foreignCommit.ok());
    CHECK_EQ(static_cast<int>(foreignCommit.code()), static_cast<int>(eve::StatusCode::Conflict));
    auto foreignRollback = second.rollback(firstReservation);
    CHECK(!foreignRollback.ok());
    CHECK_EQ(static_cast<int>(foreignRollback.code()),
             static_cast<int>(eve::StatusCode::Conflict));
    auto reverseForeignCommit = first.commit(secondReservation);
    CHECK(!reverseForeignCommit.ok());
    CHECK_EQ(static_cast<int>(reverseForeignCommit.code()),
             static_cast<int>(eve::StatusCode::Conflict));
    auto reverseForeignRollback = first.rollback(secondReservation);
    CHECK(!reverseForeignRollback.ok());
    CHECK_EQ(static_cast<int>(reverseForeignRollback.code()),
             static_cast<int>(eve::StatusCode::Conflict));
    CHECK_EQ(secondLedger.get("gold"), 10);

    auto ownCommit = first.commit(firstReservation);
    REQUIRE(ownCommit.ok());
    CHECK_EQ(firstLedger.get("gold"), 5);
    auto ownRollback = second.rollback(secondReservation);
    REQUIRE(ownRollback.ok());
    CHECK_EQ(secondLedger.get("gold"), 10);
}

TEST_CASE("resource.account.crossAdapterCredentialsRejectCommitAndRollback") {
    eve::attributes::AttributeSet attributes;
    attributes.setBase("gold", 10.0);
    eve::attributes::AttributeSetResourceAccount attributeAccount(attributes);

    eve::economy::EconomyLedger ledger;
    CHECK_EQ(ledger.credit("gold", 10), 10);
    eve::economy::EconomyLedgerResourceAccount economyAccount(ledger);
    const CostSpec cost = makeCost({{"gold", 5}});

    auto attributeReservationResult = attributeAccount.reserve(cost);
    auto economyReservationResult = economyAccount.reserve(cost);
    REQUIRE(attributeReservationResult.ok());
    REQUIRE(economyReservationResult.ok());
    const auto attributeReservation = attributeReservationResult.value();
    const auto economyReservation = economyReservationResult.value();
    CHECK_EQ(attributeReservation.id.value(), economyReservation.id.value());
    CHECK_NE(attributeReservation.account.value(), economyReservation.account.value());

    auto wrongEconomyCommit = economyAccount.commit(attributeReservation);
    CHECK(!wrongEconomyCommit.ok());
    CHECK_EQ(static_cast<int>(wrongEconomyCommit.code()),
             static_cast<int>(eve::StatusCode::Conflict));
    auto wrongAttributeRollback = attributeAccount.rollback(economyReservation);
    CHECK(!wrongAttributeRollback.ok());
    CHECK_EQ(static_cast<int>(wrongAttributeRollback.code()),
             static_cast<int>(eve::StatusCode::Conflict));
    auto wrongAttributeCommit = attributeAccount.commit(economyReservation);
    CHECK(!wrongAttributeCommit.ok());
    CHECK_EQ(static_cast<int>(wrongAttributeCommit.code()),
             static_cast<int>(eve::StatusCode::Conflict));
    auto wrongEconomyRollback = economyAccount.rollback(attributeReservation);
    CHECK(!wrongEconomyRollback.ok());
    CHECK_EQ(static_cast<int>(wrongEconomyRollback.code()),
             static_cast<int>(eve::StatusCode::Conflict));
    CHECK_EQ(attributes.getBase("gold"), 10.0);
    CHECK_EQ(ledger.get("gold"), 10);

    REQUIRE(attributeAccount.rollback(attributeReservation).ok());
    REQUIRE(economyAccount.rollback(economyReservation).ok());
}

TEST_CASE("resource.account.capacityFailureDoesNotPartiallyCreditEconomyLedger") {
    eve::economy::ResourceTypeRegistry::clear();
    eve::economy::ResourceTypeDef definition;
    definition.id = "gold";
    definition.stockMax = 10;
    CHECK(eve::economy::ResourceTypeRegistry::registerType(definition));

    eve::economy::EconomyLedger ledger;
    CHECK_EQ(ledger.credit("gold", 8), 8);
    CHECK_EQ(ledger.credit("wood", 1), 1);
    eve::economy::EconomyLedgerResourceAccount account(ledger);
    const CostSpec cost = makeCost({{"gold", 4}, {"wood", 2}});

    auto result = account.credit(cost);
    CHECK(!result.ok());
    CHECK_EQ(static_cast<int>(result.code()), static_cast<int>(eve::StatusCode::Rejected));
    CHECK_EQ(ledger.get("gold"), 8);
    CHECK_EQ(ledger.get("wood"), 1);
    eve::economy::ResourceTypeRegistry::clear();
}

TEST_CASE("resource.account.transactionCommitFailureCompensatesCommittedDebit") {
    eve::attributes::AttributeSet attributes;
    attributes.setBase("mana", 100.0);
    eve::attributes::AttributeSetResourceAccount account(attributes);
    const CostSpec cost = makeCost({{"mana", 40}});
    eve::transaction::ResourceDebitParticipant debit(account, cost);
    CommitFailureParticipant failing;
    std::array<eve::transaction::ITransactionParticipant*, 2> participants{&debit, &failing};

    eve::transaction::Coordinator coordinator;
    auto transaction = coordinator.execute(
        eve::transaction::TransactionContext("resource-atomic"),
        std::span<eve::transaction::ITransactionParticipant*>(participants.data(),
                                                               participants.size()));
    CHECK(!transaction.ok());
    CHECK_EQ(static_cast<int>(transaction.code()), static_cast<int>(eve::StatusCode::Failed));
    CHECK_EQ(static_cast<int>(debit.state()),
             static_cast<int>(eve::transaction::ResourceDebitState::Compensated));
    CHECK(!failing.prepared);
    CHECK_EQ(attributes.getBase("mana"), 100.0);
}
