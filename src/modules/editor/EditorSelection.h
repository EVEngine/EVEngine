#pragma once

#include "editor/EditorIds.h"
#include "editor/EditorResult.h"

#include <cstdint>
#include <functional>
#include <map>
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

/** @brief Semantic focus independent from keyboard focus of a particular UI toolkit. */
struct EditorFocusSnapshot {
    std::string   channel;
    StableId      surface;
    StableId      item;
    std::uint64_t sequence = 0;
};

/** @brief Channelled selection/focus state shared by editor and in-game presentations. */
class EditorSelectionService {
public:
    using Listener = std::function<void(const SelectionSnapshot&)>;

    /** @brief Atomically replace a channel selection and optional primary item. */
    EditorResult<SelectionSnapshot> set(std::string channel, std::vector<SelectionItem> items,
                                        std::optional<SelectionItem> primary = std::nullopt);
    /** @brief Clear a channel while retaining a monotonic sequence. */
    EditorResult<SelectionSnapshot> clear(const std::string& channel);
    /** @brief Read one channel, returning an empty sequence-zero snapshot when absent. */
    SelectionSnapshot snapshot(const std::string& channel) const;
    /** @brief Subscribe an owner callback; owner replacement and unload are deterministic. */
    EditorResult<void> subscribe(std::string owner, Listener listener);
    /** @brief Remove one listener owner. */
    bool unsubscribe(const std::string& owner);

private:
    std::map<std::string, SelectionSnapshot> selections_;
    std::map<std::string, Listener>          listeners_;
    std::uint64_t                            sequence_ = 0;
};

/** @brief Shared semantic focus service for docked editor and game HUD surfaces. */
class EditorFocusService {
public:
    /** @brief Set the focused surface/item for one channel. */
    EditorResult<EditorFocusSnapshot> focus(std::string channel, StableId surface, StableId item = {});
    /** @brief Read the current focus of one channel. */
    EditorFocusSnapshot snapshot(const std::string& channel) const;

private:
    std::map<std::string, EditorFocusSnapshot> focus_;
    std::uint64_t                              sequence_ = 0;
};

}  // namespace eve::editor
