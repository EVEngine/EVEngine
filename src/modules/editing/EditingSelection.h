#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "editing/EditingIds.h"
namespace eve::editing {
/** @brief SelectionDomain public API. */
enum class SelectionDomain { Scene, Asset, Graph, Timeline, UI, Custom };
/** @brief SelectionItem public API. */
struct SelectionItem {
    SelectionDomain domain = SelectionDomain::Custom;
    TargetId        target;
    StableId        item;
    std::string     type;
    /** @brief Operator <=>. */
    auto            operator<=>(const SelectionItem&) const = default;
};
/** @brief SelectionSnapshot public API. */
struct SelectionSnapshot {
    std::string                  channel;
    std::vector<SelectionItem>   items;
    std::optional<SelectionItem> primary;
    std::uint64_t                sequence = 0;
};
/** @brief FocusSnapshot public API. */
struct FocusSnapshot {
    std::string   channel;
    StableId      surface;
    StableId      item;
    std::uint64_t sequence = 0;
};
}  // namespace eve::editing
