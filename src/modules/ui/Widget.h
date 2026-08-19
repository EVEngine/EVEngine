#pragma once

#include "ui/UIHost.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace eve::ui {

/** @brief Declarative widget description (build once / on dirty → flatten into UIHost::Tree). */
struct WidgetDesc {
    NodeType type = NodeType::Text;
    std::string id;
    /** @brief Reconciliation key; defaults to id when empty. */
    std::string key;
    std::string text;
    std::string valueText;
    bool visible = true;
    bool checked = false;
    bool open = true;
    float value = 0.f;
    float minValue = 0.f;
    float maxValue = 1.f;
    float sizeX = 0.f;
    float sizeY = 0.f;
    FlexDirection flexDirection = FlexDirection::Row;
    FlexAlign alignItems = FlexAlign::Start;
    FlexJustify justifyContent = FlexJustify::Start;
    float gap = -1.f;
    float flexGrow = 0.f;
    std::function<void()> onClick;
    std::function<void(bool)> onToggle;
    std::function<void(float)> onValue;
    std::function<void(const std::string &)> onTextChange;
    std::vector<WidgetDesc> children;

    WidgetDesc &withId(std::string v) {
        id = std::move(v);
        return *this;
    }
    WidgetDesc &withKey(std::string v) {
        key = std::move(v);
        return *this;
    }
    WidgetDesc &withText(std::string v) {
        text = std::move(v);
        return *this;
    }
    WidgetDesc &withValueText(std::string v) {
        valueText = std::move(v);
        return *this;
    }
    WidgetDesc &withVisible(bool v) {
        visible = v;
        return *this;
    }
    WidgetDesc &withChecked(bool v) {
        checked = v;
        return *this;
    }
    WidgetDesc &withOpen(bool v) {
        open = v;
        return *this;
    }
    WidgetDesc &withValue(float v) {
        value = v;
        return *this;
    }
    WidgetDesc &withRange(float lo, float hi) {
        minValue = lo;
        maxValue = hi;
        return *this;
    }
    WidgetDesc &withSize(float w, float h) {
        sizeX = w;
        sizeY = h;
        return *this;
    }
    WidgetDesc &withGap(float g) {
        gap = g;
        return *this;
    }
    WidgetDesc &withFlexDirection(FlexDirection d) {
        flexDirection = d;
        return *this;
    }
    WidgetDesc &withAlign(FlexAlign a) {
        alignItems = a;
        return *this;
    }
    WidgetDesc &withJustify(FlexJustify j) {
        justifyContent = j;
        return *this;
    }
    WidgetDesc &withFlexGrow(float g) {
        flexGrow = g;
        return *this;
    }
    WidgetDesc &withClick(std::function<void()> fn) {
        onClick = std::move(fn);
        return *this;
    }
    WidgetDesc &withToggle(std::function<void(bool)> fn) {
        onToggle = std::move(fn);
        return *this;
    }
    WidgetDesc &withValueFn(std::function<void(float)> fn) {
        onValue = std::move(fn);
        return *this;
    }
    WidgetDesc &withTextChange(std::function<void(const std::string &)> fn) {
        onTextChange = std::move(fn);
        return *this;
    }
    WidgetDesc &child(WidgetDesc c) {
        children.push_back(std::move(c));
        return *this;
    }

    const std::string &reconcileKey() const { return key.empty() ? id : key; }
};

/** @brief Top-level window widget with a title bar. */
WidgetDesc window(std::string title, std::vector<WidgetDesc> children = {}, std::string id = "root");
/** @brief Static text label. */
WidgetDesc text(std::string content, std::string id = "");
/** @brief Clickable button; fires onClick. */
WidgetDesc button(std::string label, std::string id = "", std::function<void()> onClick = {});
/** @brief Plain group container. */
WidgetDesc group(std::vector<WidgetDesc> children = {}, std::string id = "");
/** @brief Holds the next widget on the same line as the previous one. */
WidgetDesc sameLine(std::string id = "");
/** @brief Horizontal separator line. */
WidgetDesc separator(std::string id = "");
/** @brief Checkbox with a label; fires onToggle. */
WidgetDesc checkbox(std::string label, bool checked = false, std::string id = "",
                    std::function<void(bool)> onToggle = {});
/** @brief Horizontal slider; fires onValue. */
WidgetDesc slider(std::string label, float value, float minV, float maxV, std::string id = "",
                  std::function<void(float)> onValue = {});
/** @brief Progress bar; fraction is clamped to [0,1]. */
WidgetDesc progress(float fraction, std::string id = "", std::string overlay = "");
/** @brief Editable text field; fires onTextChange. */
WidgetDesc inputText(std::string label, std::string value, std::string id = "",
                     std::function<void(const std::string &)> onChange = {});
/** @brief Collapsible header containing child widgets. */
WidgetDesc collapsingHeader(std::string label, std::vector<WidgetDesc> children = {},
                            std::string id = "", bool defaultOpen = true);
/** @brief Scrollable child region with an explicit size. */
WidgetDesc child(std::string id, std::vector<WidgetDesc> children = {}, float width = 0.f,
                 float height = 120.f);

/** @brief Elastic layout container (row/column). Prefer `row` / `column` shorthands. */
WidgetDesc flex(FlexDirection direction, std::vector<WidgetDesc> children = {},
                std::string id = "");
/** @brief Horizontal elastic layout row. */
WidgetDesc row(std::vector<WidgetDesc> children = {}, std::string id = "");
/** @brief Vertical elastic layout column. */
WidgetDesc column(std::vector<WidgetDesc> children = {}, std::string id = "");
/** @brief Flexible empty space; default flexGrow=1 so it absorbs free space in a Flex parent. */
WidgetDesc spacer(std::string id = "", float grow = 1.f);

/** @brief Conditional: include `child` only when `cond` is true (empty group otherwise). */
WidgetDesc when(bool cond, WidgetDesc child);
WidgetDesc whenElse(bool cond, WidgetDesc ifTrue, WidgetDesc ifFalse);

/**
 * @brief Expand a string list into a Group of item widgets.
 * `itemFn(label, index)` builds each row; keys default to id.
 */
WidgetDesc list(std::string listId, const std::vector<std::string> &items,
                const std::function<WidgetDesc(const std::string &, int)> &itemFn);

/** @brief Default list: one Button per item, id = listId + "/" + index. */
WidgetDesc listButtons(std::string listId, const std::vector<std::string> &items);

/** @brief Full replace flatten. */
void applyTree(UIHost *host, WidgetDesc root);

/**
 * @brief Patch by key/id when structure (type + child keys) matches; otherwise full replace.
 * Returns true if a structural rebuild occurred.
 */
bool applyTreeReconcile(UIHost *host, WidgetDesc root);

}  // namespace eve::ui
