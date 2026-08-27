#pragma once

/**
 * @file AttributeProjection.h
 * @brief Owner-aware canonical attribute state shared by gameplay adapters.
 *
 * The projection owns only the selected gameplay numbers. Domain components
 * remain the owners of movement, physics, animation, inventory and other
 * non-attribute state. Legacy fields may be refreshed from this projection,
 * but are never read back after initialization.
 */

#include "attributes/AttributeSet.h"
#include "common/ECS.h"
#include "common/Result.h"
#include "common/Revision.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eve::attributes {

/** @brief One base value captured by an attribute projection snapshot. */
struct AttributeSnapshotBase {
    AttributeId attribute;
    double base = 0.0;
};

/**
 * @brief Transaction-safe snapshot of selected attributes and their modifiers.
 *
 * `owner` is the ECS generation-qualified owner. `capturedRevision` describes
 * the source state; it is not an owner identity and is not interchangeable
 * with the ECS generation.
 */
struct AttributeProjectionSnapshot {
    ecs::EntityHandle owner{};
    Revision capturedRevision = Revision::zero();
    std::vector<AttributeSnapshotBase> bases;
    std::vector<AttributeModifier> modifiers;
};

/**
 * @brief Canonical selected-attribute storage with owner and revision checks.
 *
 * The class is synchronous and thread-affine to its owning ECS world. It owns
 * the `AttributeSet`; callers do not retain pointers into it across structural
 * ECS changes. `restore` first builds a candidate set and publishes it only
 * after all validation and modifier insertion succeeds.
 */
class AttributeProjection {
public:
    /** @brief Construct an empty projection with an optional stable subject id. */
    AttributeProjection(std::string subject = {});

    /** @brief Bind the projection to one live generation-qualified ECS owner. */
    [[nodiscard]] Result<void> bindOwner(ecs::EntityHandle owner);

    /** @brief Return the owner handle used for stale detection. */
    [[nodiscard]] const ecs::EntityHandle& owner() const noexcept { return owner_; }

    /** @brief Return the monotonically changing content revision. */
    [[nodiscard]] Revision revision() const noexcept { return revision_; }

    /** @brief Return whether the bound owner no longer resolves at its generation. */
    [[nodiscard]] bool isStale() const noexcept;

    /** @brief Return whether initial domain values have been installed. */
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }

    /**
     * @brief Seed base values once from an existing domain compatibility field.
     * @return NoOp when already initialized; invalid data is rejected without mutation.
     */
    [[nodiscard]] Result<void> initialize(std::span<const AttributeSnapshotBase> values);

    /** @brief Set a canonical base value and advance the projection revision. */
    [[nodiscard]] Result<void> setBase(std::string_view attribute, double value);

    /** @brief Add to a canonical base value and advance the projection revision. */
    [[nodiscard]] Result<void> modifyBase(std::string_view attribute, double delta);

    /** @brief Read a deterministic final value, or the supplied default when absent. */
    [[nodiscard]] Result<double> getFinal(std::string_view attribute,
                                          double fallback = 0.0) const;

    /** @brief Return whether the canonical set contains a base or modifier for the name. */
    [[nodiscard]] bool has(std::string_view attribute) const;

    /** @brief Add a canonical modifier using the shared source/priority/sequence rules. */
    [[nodiscard]] Result<ModifierId> addModifier(AttributeModifier modifier);

    /** @brief Return the number of canonical modifiers currently retained. */
    [[nodiscard]] int modifierCount() const noexcept { return values_.modifierCount(); }

    /**
     * @brief Return a modifier in deterministic sequence order.
     * @return A nullable borrowed pointer into the canonical attribute set, or
     *         `nullptr` when `index` is outside the current range.
     * @ownership Borrowed; the owning AttributeSet retains destruction and
     *            storage responsibility. The caller must not delete it.
     * @nullable Yes, for an invalid index.
     * @lifetime Valid until the next modifier/base mutation, restore or owner
     *           destruction; copy the modifier if it must outlive that boundary.
     * @thread Owner-thread-affine; no synchronization is provided.
     * @reentrancy Side-effect free and does not invoke callbacks.
     */
    [[nodiscard]] const AttributeModifier* modifierAt(int index) const {
        return values_.modifierAt(index);
    }

    /**
     * @brief Capture only named attributes and their modifiers.
     * @param attributes Stable allow-list owned by the caller for this synchronous call.
     */
    [[nodiscard]] Result<AttributeProjectionSnapshot> snapshot(
        std::span<const std::string_view> attributes) const;

    /**
     * @brief Restore a snapshot after owner and optimistic revision validation.
     * @param snapshot Snapshot captured from this owner generation.
     * @param expectedRevision Current revision the caller intends to replace.
     * @return Conflict/stale failure leaves every observable value unchanged.
     */
    [[nodiscard]] Result<void> restore(const AttributeProjectionSnapshot& snapshot,
                                       Revision expectedRevision);

private:
    [[nodiscard]] Result<void> advanceRevision();
    [[nodiscard]] bool ownsSameLiveEntity(ecs::EntityHandle owner) const noexcept;

    AttributeSet values_;
    ecs::EntityHandle owner_{};
    Revision revision_ = Revision::zero();
    bool initialized_ = false;
};

/** @brief Stable source labels used by all domain attribute adapters. */
namespace source {
inline constexpr std::string_view definition = "definition";
inline constexpr std::string_view runtime = "runtime";
inline constexpr std::string_view effect = "effect";
inline constexpr std::string_view equipment = "equipment";
}  // namespace source

/** @brief Shared default priorities for domain adapters. */
namespace priority {
inline constexpr ModifierPriority definition = 0;
inline constexpr ModifierPriority runtime = 100;
inline constexpr ModifierPriority effect = 200;
inline constexpr ModifierPriority equipment = 300;
}  // namespace priority

}  // namespace eve::attributes
