#pragma once

/**
 * @file ResourceAccount.h
 * @brief Strongly typed, transaction-friendly resource cost protocol.
 *
 * The protocol deliberately owns no gameplay policy and no balance storage.
 * An implementation may keep resources in an AttributeSet, an economy
 * ledger, or another domain-specific store; the protocol only defines the
 * atomic operation and reservation semantics at the boundary.
 */

#include "common/Result.h"
#include "common/StrongUint64.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace eve::detail {

struct ResourceReservationIdTag {};
struct ResourceReceiptIdTag {};

}  // namespace eve::detail

namespace eve::resource {

/** @brief Strong identity of a resource kind, such as `mana` or `gold`. */
class ResourceId {
public:
    /** @brief Construct an invalid resource id. */
    ResourceId() = default;

    /**
     * @brief Parse a resource id at a public input boundary.
     * @param text Non-empty stable name without whitespace or control characters.
     * @return The validated id, or a rejected Result with a diagnostic.
     */
    [[nodiscard]] static eve::Result<ResourceId> parse(std::string_view text);

    /** @brief Whether this id passed the resource-name validation. */
    [[nodiscard]] bool isValid() const noexcept { return !value_.empty(); }

    /** @brief Return the canonical owned spelling. */
    [[nodiscard]] const std::string& value() const noexcept { return value_; }

    friend bool operator==(const ResourceId&, const ResourceId&) noexcept = default;
    friend auto operator<=>(const ResourceId&, const ResourceId&) noexcept = default;

private:
    explicit ResourceId(std::string value) : value_(std::move(value)) {}

    std::string value_;
};

/**
 * @brief Non-negative integral quantity used by resource accounts.
 *
 * The raw constructor is explicit so an adapter can construct a value after
 * validating a native balance. External input should use from(), which
 * rejects negative values.
 */
class Amount {
public:
    /** @brief Construct zero. */
    constexpr Amount() noexcept = default;

    /**
     * @brief Construct an amount from a validated native value.
     * @param value Non-negative quantity; callers handling untrusted input
     *        should use from() instead.
     */
    explicit constexpr Amount(std::int64_t value) noexcept : value_(value) {}

    /**
     * @brief Validate and construct an amount.
     * @param value Native quantity, which must be non-negative.
     * @return A validated amount or a rejected Result.
     */
    [[nodiscard]] static eve::Result<Amount> from(std::int64_t value);

    /** @brief Return zero. */
    [[nodiscard]] static constexpr Amount zero() noexcept { return Amount(0); }

    /** @brief Return the native quantity at an explicit adapter boundary. */
    [[nodiscard]] constexpr std::int64_t value() const noexcept { return value_; }

    /** @brief Whether the quantity is zero. */
    [[nodiscard]] constexpr bool isZero() const noexcept { return value_ == 0; }

    /**
     * @brief Add two quantities with overflow checking.
     * @return The sum, or a failed Result when int64_t would overflow.
     */
    [[nodiscard]] eve::Result<Amount> checkedAdd(Amount other) const;

    friend constexpr bool operator==(const Amount&, const Amount&) noexcept = default;
    friend constexpr auto operator<=>(const Amount&, const Amount&) noexcept = default;

private:
    std::int64_t value_ = 0;
};

/** @brief One positive resource and quantity pair in a canonical cost. */
struct ResourceCost {
    ResourceId resource;
    Amount     amount;

    friend bool operator==(const ResourceCost&, const ResourceCost&) noexcept = default;

    /**
     * @brief Construct and validate one cost item.
     * @param resourceId Stable resource name.
     * @param amountValue Positive quantity.
     * @return A validated item or a rejected Result.
     */
    [[nodiscard]] static eve::Result<ResourceCost> create(std::string_view resourceId,
                                                           std::int64_t amountValue);
};

/** @brief Convenient unvalidated input used by CostSpec::from(). */
struct CostInput {
    std::string_view resource;
    std::int64_t     amount = 0;
};

/**
 * @brief Immutable canonical multi-resource cost.
 *
 * Items are sorted by ResourceId and duplicate resources are merged with
 * checked arithmetic. Empty costs and zero/negative items are rejected, so a
 * successful CostSpec is safe to use in reserve/debit/credit operations.
 */
class CostSpec {
public:
    /** @brief Construct an invalid empty cost; use create/from/single for input. */
    CostSpec() = default;

    /**
     * @brief Validate and canonicalize resource items.
     * @param items Input items; duplicate ids are merged deterministically.
     * @return A canonical cost or a rejected Result.
     */
    [[nodiscard]] static eve::Result<CostSpec> create(std::vector<ResourceCost> items);

    /**
     * @brief Build a cost from string/integer pairs.
     * @param items Non-empty positive resource entries.
     * @return A canonical cost or a rejected Result.
     */
    [[nodiscard]] static eve::Result<CostSpec> from(std::initializer_list<CostInput> items);

    /**
     * @brief Build a one-resource cost.
     * @param resource Stable resource name.
     * @param amount Positive quantity.
     * @return A canonical cost or a rejected Result.
     */
    [[nodiscard]] static eve::Result<CostSpec> single(std::string_view resource,
                                                       std::int64_t amount);

    /** @brief Whether this value contains a validated non-empty cost. */
    [[nodiscard]] bool isValid() const noexcept { return !items_.empty(); }

    /** @brief Whether this cost has no items. */
    [[nodiscard]] bool empty() const noexcept { return items_.empty(); }

    /**
     * @brief Return canonical items as a read-only borrowed view.
     * @remarks The view remains valid while this CostSpec is alive and is
     *          invalidated by assignment or destruction.
     */
    [[nodiscard]] const std::vector<ResourceCost>& items() const noexcept { return items_; }

    /** @brief Return the number of canonical resource entries. */
    [[nodiscard]] std::size_t size() const noexcept { return items_.size(); }

    friend bool operator==(const CostSpec&, const CostSpec&) noexcept = default;

private:
    explicit CostSpec(std::vector<ResourceCost> items) : items_(std::move(items)) {}

    std::vector<ResourceCost> items_;
};

/** @brief A resource missing from an affordability query. */
struct ResourceShortfall {
    ResourceId resource;
    Amount     required;
    Amount     available;
};

/**
 * @brief Structured answer to a canAfford query.
 *
 * Insufficient balance is a successful query with affordable == false. An
 * invalid cost or an implementation failure is represented by the enclosing
 * Result failure instead.
 */
struct Affordability {
    bool                          affordable = false;
    std::vector<ResourceShortfall> shortfalls;

    /** @brief Return whether every item in the queried cost is available. */
    [[nodiscard]] bool isAffordable() const noexcept { return affordable; }
};

/** @brief Operation represented by a receipt returned from an account. */
enum class ReceiptOperation : std::uint8_t {
    Debit,
    Credit,
};

/** @brief Process-local id of an active reservation. */
using ReservationId = eve::detail::StrongUint64<eve::detail::ResourceReservationIdTag>;

/**
 * @brief Opaque process-local identity of the account that issued a credential.
 *
 * The non-zero constructor is private: callers can copy an issued nonce as
 * part of a credential, but cannot manufacture an account identity from an
 * integer. The common allocator is the sole issuer and never stores a raw
 * object pointer.
 */
class AccountNonce {
public:
    /** @brief Construct the invalid zero nonce. */
    constexpr AccountNonce() noexcept = default;

    /** @brief Return the opaque numeric value for diagnostics and comparisons. */
    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }

    /** @brief Whether this nonce is invalid/zero. */
    [[nodiscard]] constexpr bool isZero() const noexcept { return value_ == 0; }

    friend constexpr bool operator==(const AccountNonce&, const AccountNonce&) noexcept = default;
    friend constexpr auto operator<=>(const AccountNonce&, const AccountNonce&) noexcept = default;

private:
    explicit constexpr AccountNonce(std::uint64_t value) noexcept : value_(value) {}

    friend eve::Result<AccountNonce> allocateAccountNonce();

    std::uint64_t value_ = 0;
};

/** @brief Process-local id of an applied resource operation. */
using ReceiptId = eve::detail::StrongUint64<eve::detail::ResourceReceiptIdTag>;

/**
 * @brief Allocate a process-local account identity for a credential issuer.
 * @return A non-zero nonce, or a failed Result after exhaustion.
 * @remarks The nonce is an opaque value and is not a pointer or persistent
 *          object identity. Each adapter instance must retain its own nonce.
 */
[[nodiscard]] eve::Result<AccountNonce> allocateAccountNonce();

/**
 * @brief Reservation credential returned by a successful reserve operation.
 *
 * The credential is a value, but the account owns its lifecycle state. Copies
 * of the credential cannot double-spend: the account rejects a second
 * commit/rollback with Conflict. It is only valid for the account that issued
 * it and while that account remains alive.
 */
struct Reservation {
    AccountNonce  account;
    ReservationId id;
    CostSpec      cost;

    /** @brief Whether this credential has valid account/id identity and cost. */
    [[nodiscard]] bool isValid() const noexcept {
        return account.value() != 0 && id.value() != 0 && cost.isValid();
    }
};

/**
 * @brief Receipt proving one atomic debit or credit was applied.
 *
 * `reservation` is non-zero only for a committed reservation; direct debit
 * and credit operations use the zero reservation id.
 */
struct Receipt {
    AccountNonce     account;
    ReceiptId        id;
    ReservationId    reservation;
    ReceiptOperation operation = ReceiptOperation::Debit;
    CostSpec         cost;

    /** @brief Whether this receipt contains a valid operation identity. */
    [[nodiscard]] bool isValid() const noexcept {
        return account.value() != 0 && id.value() != 0 && cost.isValid();
    }
};

/**
 * @brief Provider-neutral resource account contract.
 *
 * Balances are authoritative in the implementation. Reservations reduce
 * available balance without changing the observable balance; commit applies a
 * debit and rollback only releases the reservation. Every multi-resource
 * operation must preflight all entries and leave the provider unchanged on
 * failure. Implementations are synchronous and must document any stronger
 * thread-affinity rule; the protocol never retains callbacks or borrowed
 * arguments after a call returns.
 */
class IResourceAccount {
public:
    virtual ~IResourceAccount() = default;

    /**
     * @brief Query whether a cost can be paid from currently available funds.
     * @param cost Valid canonical cost borrowed for the call.
     * @return Structured affordability, or a failed Result for invalid input/provider failure.
     */
    [[nodiscard]] virtual eve::Result<Affordability> canAfford(const CostSpec& cost) const = 0;

    /**
     * @brief Reserve all items of a cost without changing visible balances.
     * @param cost Valid canonical cost borrowed for the call.
     * @return A reservation credential, or a failure with no state mutation.
     */
    [[nodiscard]] virtual eve::Result<Reservation> reserve(const CostSpec& cost) = 0;

    /**
     * @brief Atomically debit all items immediately.
     * @param cost Valid canonical cost borrowed for the call.
     * @return A debit receipt, or a failure with no state mutation.
     */
    [[nodiscard]] virtual eve::Result<Receipt> debit(const CostSpec& cost) = 0;

    /**
     * @brief Atomically credit all items immediately.
     * @param cost Valid canonical positive quantities borrowed for the call.
     * @return A credit receipt, or a failure with no state mutation.
     */
    [[nodiscard]] virtual eve::Result<Receipt> credit(const CostSpec& cost) = 0;

    /**
     * @brief Commit one active reservation as an atomic debit.
     * @param reservation Credential borrowed for the call.
     * @return A debit receipt, or Conflict/NotFound without partial mutation.
     */
    [[nodiscard]] virtual eve::Result<Receipt> commit(const Reservation& reservation) = 0;

    /**
     * @brief Release one active reservation without changing balances.
     * @param reservation Credential borrowed for the call.
     * @return Applied on the first rollback; duplicate/foreign credentials fail explicitly.
     */
    [[nodiscard]] virtual eve::Result<void> rollback(const Reservation& reservation) = 0;
};

}  // namespace eve::resource

namespace std {

template <>
struct hash<eve::resource::ResourceId> {
    /** @brief Hash a canonical resource id for account reservation maps. */
    std::size_t operator()(const eve::resource::ResourceId& value) const noexcept {
        return std::hash<std::string>{}(value.value());
    }
};

}  // namespace std
