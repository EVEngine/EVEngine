#pragma once

#include "editor/EditorIds.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace eve::editor {

/** @brief Semantic domain of one selection channel. */
enum class SelectionDomain { Scene, Asset, Graph, Timeline, UI, Custom };

/** @brief Stable item stored in a selection snapshot. */
struct SelectionItem {
    SelectionDomain domain = SelectionDomain::Custom;
    TargetId        target;
    StableId        item;
    std::string     type;

    auto operator<=>(const SelectionItem&) const = default;
};

/** @brief Immutable multi-selection captured for property and command work. */
struct SelectionSnapshot {
    std::string                  channel;
    std::vector<SelectionItem>   items;
    std::optional<SelectionItem> primary;
    std::uint64_t                sequence = 0;
};

}  // namespace eve::editor
