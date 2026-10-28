#pragma once

/**
 * @file StateAccessAdapter.h
 * @brief AttributeSet implementation of the common world-state contracts.
 */

#include "attributes/AttributeSet.h"
#include "common/StateAccess.h"

namespace eve::attributes {

/**
 * @brief Adapts one AttributeSet to numeric world reads and volatile writes.
 *
 * Reputation is represented by an ordinary canonical attribute name (for
 * example `reputation.factionA`); no dialogue-owned reputation mirror is
 * introduced. Persistent changes must be sent to a StatePatch participant and
 * later projected into the authoritative AttributeSet by its owning system.
 */
class AttributeSetStateAdapter final : public eve::IStateQuery, public eve::IStateMutation {
public:
    /** @brief Bind a borrowed authoritative set for one subject. */
    explicit AttributeSetStateAdapter(AttributeSet& attributes) noexcept : attributes_(attributes) {}

    /** @copydoc eve::IStateQuery::value */
    /** @brief Value. */
    [[nodiscard]] std::optional<eve::Value> value(std::string_view subject, std::string_view key) const override;
    /** @copydoc eve::IStateQuery::hasTag */
    /** @brief True when tag. */
    [[nodiscard]] std::optional<bool> hasTag(std::string_view, std::string_view) const override { return std::nullopt; }
    /** @copydoc eve::IStateQuery::attribute */
    /** @brief Attribute. */
    [[nodiscard]] std::optional<eve::Value> attribute(std::string_view subject, std::string_view key) const override;
    /** @copydoc eve::IStateQuery::resource */
    /** @brief Resource. */
    [[nodiscard]] std::optional<eve::Value> resource(std::string_view, std::string_view) const override {
        return std::nullopt;
    }
    /** @copydoc eve::IStateQuery::state */
    /** @brief State. */
    [[nodiscard]] std::optional<eve::Value> state(std::string_view subject, std::string_view key) const override;
    /** @copydoc eve::IStateQuery::authority */
    /** @brief Authority. */
    [[nodiscard]] std::optional<bool> authority(std::string_view, std::string_view) const override {
        return std::nullopt;
    }

    /** @copydoc eve::IStateMutation::apply */
    /** @brief Applies . */
    [[nodiscard]] eve::Result<eve::MutationReceipt> apply(std::span<const eve::StateMutation> mutations,
                                                          const eve::MutationContext&         context) override;

private:
    bool          owns(std::string_view subject) const noexcept;
    AttributeSet& attributes_;
};

}  // namespace eve::attributes
