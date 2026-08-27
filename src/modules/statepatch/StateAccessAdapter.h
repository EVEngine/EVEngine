#pragma once

/**
 * @file StateAccessAdapter.h
 * @brief StatePatch implementation of the common world-state contracts.
 */

#include "common/StateAccess.h"
#include "statepatch/StatePatch.h"

namespace eve::statepatch {

/**
 * @brief Adapts one persistent Store to state queries and transactional writes.
 *
 * Every mutation is staged into a StoreTransactionParticipant and committed by
 * the transaction Coordinator. A failed request therefore leaves the Store,
 * revision, dirty set, and change events unchanged.
 */
class StatePatchStateAdapter final : public eve::IStateQuery, public eve::IStateMutation {
public:
    /** @brief Bind a borrowed authoritative persistent store. */
    explicit StatePatchStateAdapter(Store& store) noexcept : store_(store) {}

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
    Store& store_;
};

}  // namespace eve::statepatch
