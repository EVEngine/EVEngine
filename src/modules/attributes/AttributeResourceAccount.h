#pragma once

/**
 * @file AttributeResourceAccount.h
 * @brief Named Mana/Stamina views over the canonical AttributeSet account.
 */

#include "attributes/AttributeSetResourceAccount.h"

#include <cstdint>

namespace eve::attributes {

/** @brief Attribute-backed resource names used by common gameplay adapters. */
enum class AttributeResourceKind : unsigned char {
    Mana,
    Stamina,
};

/**
 * @brief Restricts one AttributeSetResourceAccount to Mana or Stamina.
 *
 * This is a non-owning view, not another balance store and not another
 * account identity.  The wrapped AttributeSetResourceAccount remains the
 * sole nonce issuer and reservation owner, so a credential produced by this
 * view follows the same AccountNonce rules as a direct attribute account.
 * The wrapped account must outlive the view; calls are synchronous on the
 * wrapped account's owner thread.
 */
class AttributeResourceAccountAdapter final : public eve::resource::IResourceAccount {
public:
    /** @brief Bind a named view to an existing AttributeSet resource account. */
    AttributeResourceAccountAdapter(AttributeSetResourceAccount& account,
                                    AttributeResourceKind kind) noexcept;

    AttributeResourceAccountAdapter(const AttributeResourceAccountAdapter&) = delete;
    AttributeResourceAccountAdapter& operator=(const AttributeResourceAccountAdapter&) = delete;
    AttributeResourceAccountAdapter(AttributeResourceAccountAdapter&&) = delete;
    AttributeResourceAccountAdapter& operator=(AttributeResourceAccountAdapter&&) = delete;
    ~AttributeResourceAccountAdapter() override = default;

    /** @brief Return the resource exposed by this view. */
    [[nodiscard]] AttributeResourceKind kind() const noexcept { return kind_; }

    /**
     * @brief Build a positive cost for this view's named resource.
     * @param kind Mana or Stamina.
     * @param amount Positive integer quantity.
     * @return Canonical CostSpec or a validation failure.
     */
    [[nodiscard]] static eve::Result<eve::resource::CostSpec> makeCost(
        AttributeResourceKind kind, std::int64_t amount);

    /** @copydoc eve::resource::IResourceAccount::canAfford */
    [[nodiscard]] eve::Result<eve::resource::Affordability> canAfford(
        const eve::resource::CostSpec& cost) const override;
    /** @copydoc eve::resource::IResourceAccount::reserve */
    [[nodiscard]] eve::Result<eve::resource::Reservation> reserve(
        const eve::resource::CostSpec& cost) override;
    /** @copydoc eve::resource::IResourceAccount::debit */
    [[nodiscard]] eve::Result<eve::resource::Receipt> debit(
        const eve::resource::CostSpec& cost) override;
    /** @copydoc eve::resource::IResourceAccount::credit */
    [[nodiscard]] eve::Result<eve::resource::Receipt> credit(
        const eve::resource::CostSpec& cost) override;
    /** @copydoc eve::resource::IResourceAccount::commit */
    [[nodiscard]] eve::Result<eve::resource::Receipt> commit(
        const eve::resource::Reservation& reservation) override;
    /** @copydoc eve::resource::IResourceAccount::rollback */
    [[nodiscard]] eve::Result<void> rollback(
        const eve::resource::Reservation& reservation) override;

private:
    /**
     * @brief Return the canonical resource id for a named view.
     * @return A non-null borrowed pointer to immutable static text. The
     *         pointed-to string is never allocated or released by the caller.
     * @ownership Borrowed; ownership remains with this translation unit.
     * @lifetime Static for the process lifetime.
     * @thread Thread-safe and side-effect free.
     */
    [[nodiscard]] static const char* resourceName(AttributeResourceKind kind) noexcept;
    /** @brief Reject costs that contain another resource or multiple entries. */
    [[nodiscard]] eve::Result<void> validateCost(const eve::resource::CostSpec& cost) const;

    AttributeSetResourceAccount& account_;
    AttributeResourceKind       kind_;
};

/** @brief Explicit alias used by code that wants a Mana-only view. */
using ManaAccountAdapter = AttributeResourceAccountAdapter;
/** @brief Explicit alias used by code that wants a Stamina-only view. */
using StaminaAccountAdapter = AttributeResourceAccountAdapter;

}  // namespace eve::attributes
