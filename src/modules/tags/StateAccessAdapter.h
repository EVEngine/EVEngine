#pragma once

/**
 * @file StateAccessAdapter.h
 * @brief TagStore implementation of the common world-state contracts.
 */

#include "common/StateAccess.h"
#include "tags/TagStore.h"

namespace eve::tags {

/**
 * @brief Adapts one TagStore to read tags and apply volatile tag mutations.
 *
 * The store remains the authoritative owner of tag membership. This adapter
 * rejects `persistent` mutations; a StatePatch-backed adapter must be used for
 * persisted projections, so Dialogue never maintains a second tag map.
 */
class TagStoreStateAdapter final : public eve::IStateQuery, public eve::IStateMutation {
public:
    /** @brief Bind a borrowed authoritative TagStore. */
    explicit TagStoreStateAdapter(TagStore& store) noexcept : store_(store) {}

    /** @copydoc eve::IStateQuery::value */
    [[nodiscard]] std::optional<eve::Value> value(std::string_view subject, std::string_view key) const override;
    /** @copydoc eve::IStateQuery::hasTag */
    [[nodiscard]] std::optional<bool> hasTag(std::string_view subject, std::string_view tag) const override;
    /** @copydoc eve::IStateQuery::attribute */
    [[nodiscard]] std::optional<eve::Value> attribute(std::string_view, std::string_view) const override {
        return std::nullopt;
    }
    /** @copydoc eve::IStateQuery::resource */
    [[nodiscard]] std::optional<eve::Value> resource(std::string_view, std::string_view) const override {
        return std::nullopt;
    }
    /** @copydoc eve::IStateQuery::state */
    [[nodiscard]] std::optional<eve::Value> state(std::string_view subject, std::string_view key) const override;
    /** @copydoc eve::IStateQuery::authority */
    [[nodiscard]] std::optional<bool> authority(std::string_view, std::string_view) const override {
        return std::nullopt;
    }

    /** @copydoc eve::IStateMutation::apply */
    [[nodiscard]] eve::Result<eve::MutationReceipt> apply(std::span<const eve::StateMutation> mutations,
                                                          const eve::MutationContext&         context) override;

private:
    TagStore& store_;
};

}  // namespace eve::tags
