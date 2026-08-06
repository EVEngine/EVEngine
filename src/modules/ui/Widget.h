#pragma once

#include "ui/UIHost.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace eve::ui {

/** Declarative widget description (build once / on dirty → flatten into UIHost::Tree). */
struct WidgetDesc {
    NodeType type = NodeType::Text;
    std::string id;
    /** Reconciliation key; defaults to id when empty. */
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

WidgetDesc window(std::string title, std::vector<WidgetDesc> children = {}, std::string id = "root");
WidgetDesc text(std::string content, std::string id = "");
WidgetDesc button(std::string label, std::string id = "", std::function<void()> onClick = {});
WidgetDesc group(std::vector<WidgetDesc> children = {}, std::string id = "");
WidgetDesc sameLine(std::string id = "");
WidgetDesc separator(std::string id = "");
WidgetDesc checkbox(std::string label, bool checked = false, std::string id = "",
                    std::function<void(bool)> onToggle = {});
WidgetDesc slider(std::string label, float value, float minV, float maxV, std::string id = "",
                  std::function<void(float)> onValue = {});
WidgetDesc progress(float fraction, std::string id = "", std::string overlay = "");
WidgetDesc inputText(std::string label, std::string value, std::string id = "",
                     std::function<void(const std::string &)> onChange = {});
WidgetDesc collapsingHeader(std::string label, std::vector<WidgetDesc> children = {},
                            std::string id = "", bool defaultOpen = true);
WidgetDesc child(std::string id, std::vector<WidgetDesc> children = {}, float width = 0.f,
                 float height = 120.f);

/** Conditional: include `child` only when `cond` is true (empty group otherwise). */
WidgetDesc when(bool cond, WidgetDesc child);
WidgetDesc whenElse(bool cond, WidgetDesc ifTrue, WidgetDesc ifFalse);

/**
 * Expand a string list into a Group of item widgets.
 * `itemFn(label, index)` builds each row; keys default to id.
 */
WidgetDesc list(std::string listId, const std::vector<std::string> &items,
                const std::function<WidgetDesc(const std::string &, int)> &itemFn);

/** Default list: one Button per item, id = listId + "/" + index. */
WidgetDesc listButtons(std::string listId, const std::vector<std::string> &items);

/** Full replace flatten. */
void applyTree(UIHost *host, WidgetDesc root);

/**
 * Patch by key/id when structure (type + child keys) matches; otherwise full replace.
 * Returns true if a structural rebuild occurred.
 */
bool applyTreeReconcile(UIHost *host, WidgetDesc root);

}  // namespace eve::ui
