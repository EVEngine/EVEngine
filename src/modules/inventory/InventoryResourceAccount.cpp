#include "inventory/InventoryResourceAccount.h"

#include "inventory/InventorySystem.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace eve::inventory {
namespace {

using eve::resource::Affordability;
using eve::resource::Amount;
using eve::resource::CostSpec;
using eve::resource::Receipt;
using eve::resource::ReceiptOperation;
using eve::resource::Reservation;
using eve::resource::ReservationId;
using eve::resource::ResourceId;

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message,
                       std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

eve::Result<void> validateCost(const CostSpec& cost) {
    if (!cost.isValid())
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "inventory account requires a validated non-empty CostSpec", "cost");
    return eve::Result<void>::success();
}

eve::Status insufficient(const Affordability& affordability) {
    eve::DiagnosticDetails details;
    for (const auto& shortfall : affordability.shortfalls) {
        details.emplace_back("resource", shortfall.resource.value());
        details.emplace_back("required", std::to_string(shortfall.required.value()));
        details.emplace_back("available", std::to_string(shortfall.available.value()));
    }
    return eve::Status::failure(
        eve::StatusCode::Rejected,
        eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                               "inventory item cost exceeds available quantity", "cost",
                               std::move(details)));
}

eve::Status foreignReservation() {
    return eve::Status::failure(
        eve::StatusCode::Conflict,
        eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                               "resource reservation belongs to a different inventory account",
                               "reservation.account"));
}

eve::Status terminalReservation() {
    return eve::Status::failure(
        eve::StatusCode::Conflict,
        eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                               "inventory resource reservation is no longer active",
                               "reservation"));
}

eve::Status unknownReservation() {
    return eve::Status::failure(
        eve::StatusCode::NotFound,
        eve::Diagnostic::error(eve::DiagnosticCode::NotFound,
                               "inventory resource reservation was not issued by this account",
                               "reservation"));
}

eve::Status receiptExhausted() {
    return eve::Status::failure(
        eve::StatusCode::Failed,
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation,
                               "inventory resource receipt id exhausted", "receipt"));
}

eve::Status identityUnavailable() {
    return eve::Status::failure(
        eve::StatusCode::Failed,
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation,
                               "inventory resource account has no valid identity nonce", "account"));
}

template <class T>
eve::Result<T> exceptionFailure(std::string_view operation, const std::exception& exception) {
    return failure<T>(eve::DiagnosticCode::Failed,
                      std::string(operation) + " failed while staging an inventory candidate: " +
                          exception.what(),
                      "inventory.account");
}

template <class T>
eve::Result<T> unknownExceptionFailure(std::string_view operation) {
    return failure<T>(eve::DiagnosticCode::Failed,
                      std::string(operation) + " failed while staging an inventory candidate",
                      "inventory.account");
}

}  // namespace

InventoryResourceAccount::InventoryResourceAccount(Bag& bag) : bag_(bag) {
    auto nonce = eve::resource::allocateAccountNonce();
    if (nonce)
        accountNonce_ = std::move(nonce).takeValue();
    else
        nonce.ignore("inventory account identity exhausted; adapter remains unusable");
}

eve::Result<std::int64_t> InventoryResourceAccount::balanceOf(const ResourceId& resource) const {
    const int quantity = bag_.countItem(resource.value());
    if (quantity < 0)
        return failure<std::int64_t>(eve::DiagnosticCode::InvariantViolation,
                                     "inventory item quantity must not be negative",
                                     resource.value());
    return eve::Result<std::int64_t>::success(static_cast<std::int64_t>(quantity));
}

eve::Result<std::int64_t> InventoryResourceAccount::activeReservationsFor(
    const ResourceId& resource) const {
    std::int64_t total = 0;
    for (const auto& [id, record] : reservations_) {
        (void)id;
        if (record.state != ReservationState::Active) continue;
        for (const auto& item : record.cost.items()) {
            if (item.resource != resource) continue;
            if (item.amount.value() > std::numeric_limits<std::int64_t>::max() - total)
                return failure<std::int64_t>(eve::DiagnosticCode::InvariantViolation,
                                             "active inventory reservations overflowed",
                                             resource.value());
            total += item.amount.value();
        }
    }
    return eve::Result<std::int64_t>::success(total);
}

eve::Result<Affordability> InventoryResourceAccount::canAfford(const CostSpec& cost) const {
    if (accountNonce_.isZero()) return eve::Result<Affordability>::failure(identityUnavailable());
    auto valid = validateCost(cost);
    if (!valid) return eve::Result<Affordability>::failure(valid.status());

    Affordability result;
    result.affordable = true;
    for (const auto& item : cost.items()) {
        auto balance = balanceOf(item.resource);
        if (!balance) return eve::Result<Affordability>::failure(balance.status());
        auto reserved = activeReservationsFor(item.resource);
        if (!reserved) return eve::Result<Affordability>::failure(reserved.status());
        const std::int64_t available = balance.value() >= reserved.value()
                                           ? balance.value() - reserved.value()
                                           : 0;
        if (available < item.amount.value()) {
            result.affordable = false;
            result.shortfalls.push_back({item.resource, item.amount, Amount(available)});
        }
    }
    return eve::Result<Affordability>::success(std::move(result));
}

eve::Result<Reservation> InventoryResourceAccount::reserve(const CostSpec& cost) {
    if (accountNonce_.isZero()) return eve::Result<Reservation>::failure(identityUnavailable());
    auto affordability = canAfford(cost);
    if (!affordability) return eve::Result<Reservation>::failure(affordability.status());
    const auto& checked = affordability.value();
    if (!checked.affordable)
        return eve::Result<Reservation>::failure(insufficient(checked));
    if (nextReservation_.value() == std::numeric_limits<std::uint64_t>::max())
        return failure<Reservation>(eve::DiagnosticCode::InvariantViolation,
                                    "inventory resource reservation id exhausted", "reservation");

    const ReservationId id = nextReservation_;
    const auto [it, inserted] = reservations_.emplace(
        id, ReservationRecord{cost, ReservationState::Active});
    if (!inserted)
        return failure<Reservation>(eve::DiagnosticCode::Conflict,
                                    "inventory resource reservation id was already used",
                                    "reservation");
    nextReservation_ = ReservationId(id.value() + 1);
    return eve::Result<Reservation>::success(
        Reservation{accountNonce_, id, it->second.cost},
        eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> InventoryResourceAccount::applyDelta(const CostSpec& cost, bool debit) {
    auto valid = validateCost(cost);
    if (!valid) return valid;
    for (const auto& item : cost.items()) {
        if (item.amount.value() > std::numeric_limits<int>::max())
            return failure<void>(eve::DiagnosticCode::Unsupported,
                                 "inventory item quantity exceeds the Bag integer range",
                                 item.resource.value());
    }

    const auto originalSlots = bag_.slots();
    const int originalInstanceCounter = InventorySystem::instanceCounter();
    const std::size_t originalEventCount = InventorySystem::eventQueue().size();
    const bool oldHookSuppression = InventorySystem::changeHooksSuppressed();
    InventorySystem::changeHooksSuppressed() = true;

    auto restore = [&] {
        bag_.slots() = originalSlots;
        InventorySystem::instanceCounter() = originalInstanceCounter;
        InventorySystem::eventQueue().resize(originalEventCount);
        InventorySystem::changeHooksSuppressed() = oldHookSuppression;
    };

    try {
        for (const auto& item : cost.items()) {
            const int amount = static_cast<int>(item.amount.value());
            if (debit) {
                auto balance = balanceOf(item.resource);
                if (!balance || balance.value() < amount) {
                    restore();
                    return failure<void>(eve::DiagnosticCode::PreconditionViolation,
                                         "inventory debit would make item quantity negative",
                                         item.resource.value());
                }
                if (InventorySystem::removeItem(&bag_, item.resource.value(), amount) != amount) {
                    restore();
                    return failure<void>(eve::DiagnosticCode::Failed,
                                         "inventory debit was not applied in full",
                                         item.resource.value());
                }
            } else {
                std::string reason;
                if (!InventorySystem::canAdd(&bag_, item.resource.value(), amount, &reason)) {
                    restore();
                    return failure<void>(eve::DiagnosticCode::PreconditionViolation,
                                         "inventory credit was rejected by the Bag policy: " + reason,
                                         item.resource.value());
                }
                if (InventorySystem::addItem(&bag_, item.resource.value(), amount) != amount) {
                    restore();
                    return failure<void>(eve::DiagnosticCode::PreconditionViolation,
                                         "inventory credit was not applied in full",
                                         item.resource.value());
                }
            }
        }
    } catch (const std::exception& exception) {
        restore();
        return exceptionFailure<void>(debit ? "debit" : "credit", exception);
    } catch (...) {
        restore();
        return unknownExceptionFailure<void>(debit ? "debit" : "credit");
    }

    InventorySystem::changeHooksSuppressed() = oldHookSuppression;

    // The state is already committed. Deliver a snapshot of the events after
    // the mutation, outside the suppressed section and without retaining any
    // internal container reference across callbacks.
    const std::vector<InventoryChangeEvent> emitted(
        InventorySystem::eventQueue().begin() + static_cast<std::ptrdiff_t>(originalEventCount),
        InventorySystem::eventQueue().end());
    const auto hooks = InventorySystem::changeHooks();
    for (const auto& event : emitted) {
        for (const auto& [name, hook] : hooks) {
            (void)name;
            if (!hook) continue;
            try {
                hook(event);
            } catch (...) {
                // Event hooks are observers, not part of the account commit.
            }
        }
    }
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<Receipt> InventoryResourceAccount::debit(const CostSpec& cost) {
    if (accountNonce_.isZero()) return eve::Result<Receipt>::failure(identityUnavailable());
    auto affordability = canAfford(cost);
    if (!affordability) return eve::Result<Receipt>::failure(affordability.status());
    const auto& checked = affordability.value();
    if (!checked.affordable) return eve::Result<Receipt>::failure(insufficient(checked));
    if (nextReceipt_.value() == std::numeric_limits<std::uint64_t>::max())
        return eve::Result<Receipt>::failure(receiptExhausted());

    Receipt receipt{accountNonce_, nextReceipt_, ReservationId{}, ReceiptOperation::Debit, cost};
    auto applied = applyDelta(cost, true);
    if (!applied) return eve::Result<Receipt>::failure(applied.status());
    nextReceipt_ = eve::resource::ReceiptId(nextReceipt_.value() + 1);
    return eve::Result<Receipt>::success(std::move(receipt),
                                         eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<Receipt> InventoryResourceAccount::credit(const CostSpec& cost) {
    if (accountNonce_.isZero()) return eve::Result<Receipt>::failure(identityUnavailable());
    if (nextReceipt_.value() == std::numeric_limits<std::uint64_t>::max())
        return eve::Result<Receipt>::failure(receiptExhausted());
    Receipt receipt{accountNonce_, nextReceipt_, ReservationId{}, ReceiptOperation::Credit, cost};
    auto applied = applyDelta(cost, false);
    if (!applied) return eve::Result<Receipt>::failure(applied.status());
    nextReceipt_ = eve::resource::ReceiptId(nextReceipt_.value() + 1);
    return eve::Result<Receipt>::success(std::move(receipt),
                                         eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> InventoryResourceAccount::activeReservationsAreCovered() const {
    for (const auto& [id, record] : reservations_) {
        (void)id;
        if (record.state != ReservationState::Active) continue;
        for (const auto& item : record.cost.items()) {
            auto balance = balanceOf(item.resource);
            if (!balance) return eve::Result<void>::failure(balance.status());
            auto reserved = activeReservationsFor(item.resource);
            if (!reserved) return eve::Result<void>::failure(reserved.status());
            if (balance.value() < reserved.value())
                return failure<void>(eve::DiagnosticCode::Conflict,
                                     "external Bag mutation invalidated active item reservations",
                                     item.resource.value());
        }
    }
    return eve::Result<void>::success();
}

eve::Result<Receipt> InventoryResourceAccount::commit(const Reservation& reservation) {
    if (accountNonce_.isZero()) return eve::Result<Receipt>::failure(identityUnavailable());
    if (!reservation.isValid())
        return failure<Receipt>(eve::DiagnosticCode::InvalidArgument,
                                "inventory resource reservation is invalid", "reservation");
    if (reservation.account != accountNonce_) return eve::Result<Receipt>::failure(foreignReservation());
    const auto it = reservations_.find(reservation.id);
    if (it == reservations_.end()) return eve::Result<Receipt>::failure(unknownReservation());
    if (it->second.cost != reservation.cost)
        return failure<Receipt>(eve::DiagnosticCode::Conflict,
                                "inventory reservation cost does not match its account record",
                                "reservation.cost");
    if (it->second.state != ReservationState::Active)
        return eve::Result<Receipt>::failure(terminalReservation());
    auto covered = activeReservationsAreCovered();
    if (!covered) return eve::Result<Receipt>::failure(covered.status());
    if (nextReceipt_.value() == std::numeric_limits<std::uint64_t>::max())
        return eve::Result<Receipt>::failure(receiptExhausted());

    Receipt receipt{accountNonce_, nextReceipt_, reservation.id, ReceiptOperation::Debit,
                    it->second.cost};
    auto applied = applyDelta(it->second.cost, true);
    if (!applied) return eve::Result<Receipt>::failure(applied.status());
    it->second.state = ReservationState::Committed;
    nextReceipt_ = eve::resource::ReceiptId(nextReceipt_.value() + 1);
    return eve::Result<Receipt>::success(std::move(receipt),
                                         eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> InventoryResourceAccount::rollback(const Reservation& reservation) {
    if (accountNonce_.isZero()) return eve::Result<void>::failure(identityUnavailable());
    if (!reservation.isValid())
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "inventory resource reservation is invalid", "reservation");
    if (reservation.account != accountNonce_) return eve::Result<void>::failure(foreignReservation());
    const auto it = reservations_.find(reservation.id);
    if (it == reservations_.end()) return eve::Result<void>::failure(unknownReservation());
    if (it->second.cost != reservation.cost)
        return failure<void>(eve::DiagnosticCode::Conflict,
                             "inventory reservation cost does not match its account record",
                             "reservation.cost");
    if (it->second.state != ReservationState::Active)
        return eve::Result<void>::failure(terminalReservation());
    it->second.state = ReservationState::RolledBack;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<CostSpec> ItemCostAdapter::itemCost(std::string_view itemId,
                                                std::int64_t quantity) {
    return CostSpec::single(itemId, quantity);
}

eve::Result<CostSpec> ItemCostAdapter::ammoCost(std::string_view ammoItemId,
                                                std::int64_t quantity) {
    return CostSpec::single(ammoItemId, quantity);
}

}  // namespace eve::inventory
