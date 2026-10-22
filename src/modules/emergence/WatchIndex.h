#pragma once
#include "common/Export.h"

/**
 * @file WatchIndex.h
 * @brief Inverted index from fact keys to interested rule ids.
 */

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eve::emergence {

/**
 * @brief Dense, sorted multi-map used to wake O(watchers) rules on a fact change.
 *
 * Rule ids are stored as dense integers assigned by RuleEngine. String ids are
 * resolved by the engine; this index never owns string storage.
 */
class EVENGINE_API_FOUNDATION WatchIndex {
public:
    /** @brief Remove every subscription. */
    void clear();

    /**
     * @brief Index one rule under each of its watch keys.
     * @param ruleIndex Dense rule slot owned by the catalogue.
     * @param keys Canonical fact keys; duplicates are ignored.
     */
    void addRule(std::uint32_t ruleIndex, const std::vector<std::string>& keys);

    /**
     * @brief Collect unique rule indexes that watch a key into `out`.
     * @param key Canonical fact key.
     * @param out Appended dense indexes; caller clears or uses a set for uniqueness.
     * @return Number of subscribers appended (including duplicates across calls).
     */
    [[nodiscard]] std::size_t collect(std::string_view key, std::vector<std::uint32_t>& out) const;

    /** @brief Number of distinct keys with at least one subscriber. */
    [[nodiscard]] std::size_t keyCount() const noexcept { return index_.size(); }

private:
    std::unordered_map<std::string, std::vector<std::uint32_t>> index_;
};

}  // namespace eve::emergence
