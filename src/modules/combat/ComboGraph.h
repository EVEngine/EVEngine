#pragma once
#include "common/Export.h"

/** @file ComboGraph.h @brief Explicit cancel/combo graph over ability logical ids. */

#include "common/Result.h"
#include "common/Identity.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace eve::combat {

/** @brief Edge that may cancel from one ability into another. */
struct ComboGraphEdge {
    LogicalId   from;
    LogicalId   to;
    std::string requiredInput;
    std::int32_t priority = 0;
    bool         requiresCancelWindow = true;
    bool         requiresComboWindow  = false;

    /** @brief Validate non-nil ids and non-empty input when required. */
    [[nodiscard]] Result<void> validate() const;
};

/** @brief Owning match produced by the combo graph. */
struct ComboGraphMatch {
    LogicalId   from;
    LogicalId   to;
    std::string input;
    std::int32_t priority = 0;
};

/**
 * @brief Deterministic directed graph of combat ability transitions.
 *
 * The graph does not activate abilities. Player/AI adapters query it together
 * with cancel/combo windows, then submit the next AbilityIntent.
 */
class EVENGINE_API_BACKENDS ComboGraph {
public:
    /** @brief Add one validated edge; duplicate from/to/input triples Conflict. */
    [[nodiscard]] Result<void> addEdge(ComboGraphEdge edge);
    /** @brief Remove all edges. */
    void clear() noexcept { edges_.clear(); }
    /**
     * @brief Resolve the highest-priority legal transition for a source ability and input.
     * @param allowCancel Whether an active cancel window currently permits transitions.
     * @param allowCombo Whether an active combo window currently permits transitions.
     */
    [[nodiscard]] Result<ComboGraphMatch> match(const LogicalId& from, std::string_view input, bool allowCancel,
                                                bool allowCombo) const;
    /** @brief Number of stored edges. */
    [[nodiscard]] std::size_t edgeCount() const noexcept { return edges_.size(); }

private:
    std::vector<ComboGraphEdge> edges_;
};

}  // namespace eve::combat
