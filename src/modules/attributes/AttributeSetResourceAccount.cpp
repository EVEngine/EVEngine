#include "attributes/AttributeSetResourceAccount.h"

#include <cmath>
#include <exception>
#include <limits>
#include <string>
#include <utility>

namespace eve::attributes {
namespace {

using eve::resource::Affordability;
using eve::resource::Amount;
using eve::resource::CostSpec;
using eve::resource::Receipt;
using eve::resource::ReceiptOperation;
using eve::resource::Reservation;
using eve::resource::ReservationId;
using eve::resource::ResourceId;

constexpr double kMaxExactlyRepresentableInteger = 9007199254740991.0;  // 2^53 - 1

eve::Diagnostic invalidArgument(std::string message, std::string path = {}) {
    return eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move(message),
                                  std::move(path));
}

eve::Diagnostic conflict(std::string message, std::string path = {}) {
    return eve::Diagnostic::error(eve::DiagnosticCode::Conflict, std::move(message),
                                  std::move(path));
}

eve::Diagnostic invariantFailure(std::string message, std::string path = {}) {
    return eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, std::move(message),
                                  std::move(path));
}

template <class T>
eve::Result<T> exceptionFailure(std::string_view operation, const std::exception& exception) {
    return eve::Result<T>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::Failed,
        std::string(operation) + " failed while staging an atomic candidate: " + exception.what(),
        "resource.account"));
}

template <>
eve::Result<void> exceptionFailure<void>(std::string_view operation,
                                         const std::exception& exception) {
    return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::Failed,
        std::string(operation) + " failed while staging an atomic candidate: " + exception.what(),
        "resource.account"));
}

template <class T>
eve::Result<T> unknownExceptionFailure(std::string_view operation) {
    return eve::Result<T>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::Failed,
        std::string(operation) + " failed while staging an atomic candidate",
        "resource.account"));
}

template <>
eve::Result<void> unknownExceptionFailure<void>(std::string_view operation) {
    return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::Failed,
        std::string(operation) + " failed while staging an atomic candidate",
        "resource.account"));
}

eve::Result<void> validateCost(const CostSpec& cost) {
    if (!cost.isValid())
        return eve::Result<void>::failure(
            invalidArgument("resource cost must be a validated non-empty CostSpec", "cost"));
    return eve::Result<void>::success();
}

eve::Status insufficientStatus(const Affordability& affordability) {
    eve::DiagnosticDetails details;
    for (const auto& shortfall : affordability.shortfalls) {
        details.emplace_back("resource", shortfall.resource.value());
        details.emplace_back("required", std::to_string(shortfall.required.value()));
        details.emplace_back("available", std::to_string(shortfall.available.value()));
    }
    return eve::Status::failure(
        eve::StatusCode::Rejected,
        eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                               "resource cost exceeds available balance", "cost",
                               std::move(details)));
}

eve::Status unknownReservationStatus() {
    return eve::Status::failure(
        eve::StatusCode::NotFound,
        eve::Diagnostic::error(eve::DiagnosticCode::NotFound,
                               "resource reservation does not belong to this account",
                               "reservation"));
}

eve::Status foreignReservationStatus() {
    return eve::Status::failure(
        eve::StatusCode::Conflict,
        eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                               "resource reservation belongs to a different account",
                               "reservation.account"));
}

eve::Status duplicateReservationStatus() {
    return eve::Status::failure(
        eve::StatusCode::Conflict,
        eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                               "resource reservation is no longer active", "reservation"));
}

eve::Status mismatchedReservationStatus() {
    return eve::Status::failure(
        eve::StatusCode::Conflict,
        eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                               "resource reservation cost does not match its account record",
                               "reservation.cost"));
}

eve::Status receiptIdExhaustedStatus() {
    return eve::Status::failure(
        eve::StatusCode::Failed,
        invariantFailure("resource receipt id exhausted", "receipt"));
}

eve::Status accountIdentityUnavailableStatus() {
    return eve::Status::failure(
        eve::StatusCode::Failed,
        invariantFailure("resource account has no valid identity nonce", "account"));
}

}  // namespace

AttributeSetResourceAccount::AttributeSetResourceAccount(AttributeSet& attributes)
    : attributes_(attributes) {
    auto nonce = eve::resource::allocateAccountNonce();
    if (nonce)
        accountNonce_ = std::move(nonce).takeValue();
    else
        nonce.ignore("resource account identity exhausted; adapter remains unusable");
}

eve::Result<std::int64_t> AttributeSetResourceAccount::balanceOf(const ResourceId& resource) const {
    const double value = attributes_.getBase(resource.value(), 0.0);
    if (!std::isfinite(value) || value < 0.0 || std::floor(value) != value ||
        value > kMaxExactlyRepresentableInteger) {
        return eve::Result<std::int64_t>::failure(
            invariantFailure("resource-backed attribute must be a finite non-negative integer",
                             resource.value()));
    }
    return eve::Result<std::int64_t>::success(static_cast<std::int64_t>(value));
}

eve::Result<std::int64_t> AttributeSetResourceAccount::activeReservationsFor(
    const ResourceId& resource) const {
    std::int64_t total = 0;
    for (const auto& [id, record] : reservations_) {
        (void)id;
        if (record.state != ReservationState::Active) continue;
        for (const auto& item : record.cost.items()) {
            if (item.resource != resource) continue;
            if (item.amount.value() > std::numeric_limits<std::int64_t>::max() - total)
                return eve::Result<std::int64_t>::failure(
                    invariantFailure("active resource reservations overflowed", resource.value()));
            total += item.amount.value();
        }
    }
    return eve::Result<std::int64_t>::success(total);
}

eve::Result<Affordability> AttributeSetResourceAccount::canAfford(const CostSpec& cost) const {
    try {
        if (accountNonce_.isZero())
            return eve::Result<Affordability>::failure(accountIdentityUnavailableStatus());
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
    } catch (const std::exception& exception) {
        return exceptionFailure<Affordability>("canAfford", exception);
    } catch (...) {
        return unknownExceptionFailure<Affordability>("canAfford");
    }
}

eve::Result<Reservation> AttributeSetResourceAccount::reserve(const CostSpec& cost) {
    try {
        auto affordability = canAfford(cost);
        if (!affordability) return eve::Result<Reservation>::failure(affordability.status());
        const auto& checked = affordability.value();
        if (!checked.affordable)
            return eve::Result<Reservation>::failure(insufficientStatus(checked));

        if (nextReservation_.value() == std::numeric_limits<std::uint64_t>::max())
            return eve::Result<Reservation>::failure(
                eve::Status::failure(eve::StatusCode::Failed,
                                     invariantFailure("resource reservation id exhausted",
                                                      "reservation")));

        const ReservationId id = nextReservation_;
        const auto [it, inserted] = reservations_.emplace(
            id, ReservationRecord{cost, ReservationState::Active});
        if (!inserted)
            return eve::Result<Reservation>::failure(
                eve::Status::failure(eve::StatusCode::Conflict,
                                     conflict("resource reservation id was already used",
                                              "reservation")));
        nextReservation_ = ReservationId(id.value() + 1);
        return eve::Result<Reservation>::success(
            Reservation{accountNonce_, id, it->second.cost},
            eve::Status::success(eve::StatusCode::Applied));
    } catch (const std::exception& exception) {
        return exceptionFailure<Reservation>("reserve", exception);
    } catch (...) {
        return unknownExceptionFailure<Reservation>("reserve");
    }
}

eve::Result<void> AttributeSetResourceAccount::applyDelta(const CostSpec& cost, bool debit) {
    try {
        AttributeSet candidate = attributes_;
        for (const auto& item : cost.items()) {
            auto balance = balanceOf(item.resource);
            if (!balance) return eve::Result<void>::failure(balance.status());
            const auto amount = item.amount.value();
            if (debit) {
                if (balance.value() < amount)
                    return eve::Result<void>::failure(
                        eve::Status::failure(
                            eve::StatusCode::Rejected,
                            eve::Diagnostic::error(
                                eve::DiagnosticCode::PreconditionViolation,
                                "resource debit would make balance negative",
                                item.resource.value())));
                candidate.setBase(item.resource.value(),
                                  static_cast<double>(balance.value() - amount));
            } else {
                if (amount > std::numeric_limits<std::int64_t>::max() - balance.value())
                    return eve::Result<void>::failure(
                        eve::Status::failure(
                            eve::StatusCode::Failed,
                            invariantFailure("resource credit overflowed", item.resource.value())));
                candidate.setBase(item.resource.value(),
                                  static_cast<double>(balance.value() + amount));
            }
        }
        attributes_ = std::move(candidate);
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    } catch (const std::exception& exception) {
        return exceptionFailure<void>("applyDelta", exception);
    } catch (...) {
        return unknownExceptionFailure<void>("applyDelta");
    }
}

eve::Result<Receipt> AttributeSetResourceAccount::debit(const CostSpec& cost) {
    try {
        auto affordability = canAfford(cost);
        if (!affordability) return eve::Result<Receipt>::failure(affordability.status());
        const auto& checked = affordability.value();
        if (!checked.affordable)
            return eve::Result<Receipt>::failure(insufficientStatus(checked));
        if (nextReceipt_.value() == std::numeric_limits<std::uint64_t>::max())
            return eve::Result<Receipt>::failure(receiptIdExhaustedStatus());

        Receipt receipt{accountNonce_, nextReceipt_, ReservationId{}, ReceiptOperation::Debit, cost};
        auto applied = applyDelta(cost, true);
        if (!applied) return eve::Result<Receipt>::failure(applied.status());
        nextReceipt_ = eve::resource::ReceiptId(nextReceipt_.value() + 1);
        return eve::Result<Receipt>::success(
            std::move(receipt), eve::Status::success(eve::StatusCode::Applied));
    } catch (const std::exception& exception) {
        return exceptionFailure<Receipt>("debit", exception);
    } catch (...) {
        return unknownExceptionFailure<Receipt>("debit");
    }
}

eve::Result<Receipt> AttributeSetResourceAccount::credit(const CostSpec& cost) {
    try {
        if (accountNonce_.isZero())
            return eve::Result<Receipt>::failure(accountIdentityUnavailableStatus());
        auto valid = validateCost(cost);
        if (!valid) return eve::Result<Receipt>::failure(valid.status());
        for (const auto& item : cost.items()) {
            auto balance = balanceOf(item.resource);
            if (!balance) return eve::Result<Receipt>::failure(balance.status());
            if (item.amount.value() > std::numeric_limits<std::int64_t>::max() - balance.value())
                return eve::Result<Receipt>::failure(
                    eve::Status::failure(
                        eve::StatusCode::Failed,
                        invariantFailure("resource credit overflowed", item.resource.value())));
        }
        if (nextReceipt_.value() == std::numeric_limits<std::uint64_t>::max())
            return eve::Result<Receipt>::failure(receiptIdExhaustedStatus());

        Receipt receipt{accountNonce_, nextReceipt_, ReservationId{}, ReceiptOperation::Credit, cost};
        auto applied = applyDelta(cost, false);
        if (!applied) return eve::Result<Receipt>::failure(applied.status());
        nextReceipt_ = eve::resource::ReceiptId(nextReceipt_.value() + 1);
        return eve::Result<Receipt>::success(
            std::move(receipt), eve::Status::success(eve::StatusCode::Applied));
    } catch (const std::exception& exception) {
        return exceptionFailure<Receipt>("credit", exception);
    } catch (...) {
        return unknownExceptionFailure<Receipt>("credit");
    }
}

eve::Result<void> AttributeSetResourceAccount::activeReservationsAreCovered() const {
    try {
        for (const auto& [id, record] : reservations_) {
            (void)id;
            if (record.state != ReservationState::Active) continue;
            for (const auto& item : record.cost.items()) {
                auto balance = balanceOf(item.resource);
                if (!balance) return eve::Result<void>::failure(balance.status());
                auto reserved = activeReservationsFor(item.resource);
                if (!reserved) return eve::Result<void>::failure(reserved.status());
                if (balance.value() < reserved.value())
                    return eve::Result<void>::failure(
                        eve::Status::failure(
                            eve::StatusCode::Conflict,
                            conflict("external attribute change invalidated active reservations",
                                     item.resource.value())));
            }
        }
        return eve::Result<void>::success();
    } catch (const std::exception& exception) {
        return exceptionFailure<void>("reservation coverage check", exception);
    } catch (...) {
        return unknownExceptionFailure<void>("reservation coverage check");
    }
}

eve::Result<Receipt> AttributeSetResourceAccount::commit(const Reservation& reservation) {
    try {
        if (accountNonce_.isZero())
            return eve::Result<Receipt>::failure(accountIdentityUnavailableStatus());
        if (!reservation.isValid())
            return eve::Result<Receipt>::failure(
                invalidArgument("resource reservation is invalid", "reservation"));
        if (reservation.account != accountNonce_)
            return eve::Result<Receipt>::failure(foreignReservationStatus());
        const auto it = reservations_.find(reservation.id);
        if (it == reservations_.end())
            return eve::Result<Receipt>::failure(unknownReservationStatus());
        if (it->second.cost != reservation.cost)
            return eve::Result<Receipt>::failure(mismatchedReservationStatus());
        if (it->second.state != ReservationState::Active)
            return eve::Result<Receipt>::failure(duplicateReservationStatus());

        auto covered = activeReservationsAreCovered();
        if (!covered) return eve::Result<Receipt>::failure(covered.status());
        if (nextReceipt_.value() == std::numeric_limits<std::uint64_t>::max())
            return eve::Result<Receipt>::failure(receiptIdExhaustedStatus());

        Receipt receipt{accountNonce_, nextReceipt_, reservation.id, ReceiptOperation::Debit,
                        it->second.cost};
        auto applied = applyDelta(it->second.cost, true);
        if (!applied) return eve::Result<Receipt>::failure(applied.status());
        it->second.state = ReservationState::Committed;
        nextReceipt_ = eve::resource::ReceiptId(nextReceipt_.value() + 1);
        return eve::Result<Receipt>::success(
            std::move(receipt), eve::Status::success(eve::StatusCode::Applied));
    } catch (const std::exception& exception) {
        return exceptionFailure<Receipt>("commit", exception);
    } catch (...) {
        return unknownExceptionFailure<Receipt>("commit");
    }
}

eve::Result<void> AttributeSetResourceAccount::rollback(const Reservation& reservation) {
    try {
        if (accountNonce_.isZero())
            return eve::Result<void>::failure(accountIdentityUnavailableStatus());
        if (!reservation.isValid())
            return eve::Result<void>::failure(
                invalidArgument("resource reservation is invalid", "reservation"));
        if (reservation.account != accountNonce_)
            return eve::Result<void>::failure(foreignReservationStatus());
        const auto it = reservations_.find(reservation.id);
        if (it == reservations_.end()) return eve::Result<void>::failure(unknownReservationStatus());
        if (it->second.cost != reservation.cost)
            return eve::Result<void>::failure(mismatchedReservationStatus());
        if (it->second.state != ReservationState::Active)
            return eve::Result<void>::failure(duplicateReservationStatus());
        it->second.state = ReservationState::RolledBack;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    } catch (const std::exception& exception) {
        return exceptionFailure<void>("rollback", exception);
    } catch (...) {
        return unknownExceptionFailure<void>("rollback");
    }
}

}  // namespace eve::attributes
