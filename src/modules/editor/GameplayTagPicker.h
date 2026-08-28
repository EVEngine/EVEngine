#pragma once

/** @file GameplayTagPicker.h @brief UI-neutral gameplay-tag picker projection. */

#include "common/Result.h"
#include "tags/GameplayTag.h"

#include <string>
#include <vector>

namespace eve::editor {

/** @brief Owning row projected for tree/list UI implementations. */
struct GameplayTagPickerEntry {
    tags::GameplayTagId id;
    std::string         name;
    std::string         description;
    std::size_t         depth       = 0;
    bool                hasChildren = false;
};

/**
 * @brief Searchable selection model shared by developer and in-game editors.
 *
 * The registry is borrowed and must outlive the picker. Methods are owner-thread
 * only and invoke no callbacks. Results own their strings and remain valid after
 * subsequent registry mutation.
 */
class GameplayTagPicker {
public:
    /** @brief Construct a picker borrowing the canonical registry. */
    explicit GameplayTagPicker(const tags::GameplayTagRegistry& registry) : registry_(registry) {}

    /** @brief Set a case-insensitive name/description filter. */
    void setFilter(std::string filter) { filter_ = std::move(filter); }
    /** @brief Restrict results to a tag root and its descendants; empty clears the root. */
    [[nodiscard]] Result<void> setRoot(std::string root);
    /** @brief Select an exact registered definition. */
    [[nodiscard]] Result<void> select(std::string name);
    /** @brief Clear the current selection. */
    void clearSelection() { selected_.clear(); }
    /** @brief Return the selected canonical name, or empty. */
    const std::string& selected() const noexcept { return selected_; }
    /** @brief Build deterministic owning entries for the current root and filter. */
    [[nodiscard]] std::vector<GameplayTagPickerEntry> entries() const;

private:
    const tags::GameplayTagRegistry& registry_;
    std::string                      filter_;
    std::string                      root_;
    std::string                      selected_;
};

}  // namespace eve::editor
