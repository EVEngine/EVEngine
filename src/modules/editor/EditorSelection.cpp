#include "editor/EditorSelection.h"

#include <algorithm>

namespace eve::editor {

EditorResult<SelectionSnapshot> EditorSelectionService::set(std::string channel, std::vector<SelectionItem> items,
                                                            std::optional<SelectionItem> primary) {
    if (channel.empty())
        return eve::editing::failed<SelectionSnapshot>(
            EditorStatus::Rejected, RuleId("editor.selection.channel-required"), "Selection channel is required");
    if (primary && std::find(items.begin(), items.end(), *primary) == items.end())
        return eve::editing::failed<SelectionSnapshot>(EditorStatus::Rejected,
                                                      RuleId("editor.selection.primary-missing"),
                                                      "Primary selection must be included in items");
    SelectionSnapshot value;
    value.channel  = std::move(channel);
    value.items    = std::move(items);
    value.primary  = std::move(primary);
    value.sequence = ++sequence_;
    selections_.insert_or_assign(value.channel, value);
    for (const auto& [owner, listener] : listeners_) {
        (void)owner;
        listener(value);
    }
    return eve::editing::applied<SelectionSnapshot>(std::move(value));
}

EditorResult<SelectionSnapshot> EditorSelectionService::clear(const std::string& channel) { return set(channel, {}); }

SelectionSnapshot EditorSelectionService::snapshot(const std::string& channel) const {
    const auto found = selections_.find(channel);
    if (found != selections_.end()) return found->second;
    SelectionSnapshot empty;
    empty.channel = channel;
    return empty;
}

EditorResult<void> EditorSelectionService::subscribe(std::string owner, Listener listener) {
    if (owner.empty() || !listener)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.selection.invalid-listener"),
                                         "Selection listener owner and callback are required");
    listeners_.insert_or_assign(std::move(owner), std::move(listener));
    return eve::editing::applied<void>();
}

bool EditorSelectionService::unsubscribe(const std::string& owner) { return listeners_.erase(owner) != 0; }

EditorResult<EditorFocusSnapshot> EditorFocusService::focus(std::string channel, StableId surface, StableId item) {
    if (channel.empty() || surface.empty())
        return eve::editing::failed<EditorFocusSnapshot>(EditorStatus::Rejected, RuleId("editor.focus.invalid"),
                                                        "Focus channel and surface are required");
    EditorFocusSnapshot value;
    value.channel  = std::move(channel);
    value.surface  = std::move(surface);
    value.item     = std::move(item);
    value.sequence = ++sequence_;
    focus_.insert_or_assign(value.channel, value);
    return eve::editing::applied<EditorFocusSnapshot>(std::move(value));
}

EditorFocusSnapshot EditorFocusService::snapshot(const std::string& channel) const {
    const auto found = focus_.find(channel);
    if (found != focus_.end()) return found->second;
    EditorFocusSnapshot empty;
    empty.channel = channel;
    return empty;
}

}  // namespace eve::editor
