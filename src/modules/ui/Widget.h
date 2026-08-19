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
    // --- Layout box model (mirrors UINode) ---
    float marginL = 0.f;
    float marginT = 0.f;
    float marginR = 0.f;
    float marginB = 0.f;
    float paddingL = 0.f;
    float paddingT = 0.f;
    float paddingR = 0.f;
    float paddingB = 0.f;
    float minSizeX = 0.f;
    float minSizeY = 0.f;
    float maxSizeX = 0.f;
    float maxSizeY = 0.f;
    float percentW = 0.f;
    float percentH = 0.f;
    float anchorX = 0.f;
    float anchorY = 0.f;
    float posX = 0.f;
    float posY = 0.f;
    bool absolute = false;
    // --- Image / ImageButton props ---
    uint64_t textureId = 0;
    float tintR = 1.f;
    float tintG = 1.f;
    float tintB = 1.f;
    float tintA = 1.f;
    float uv0x = 0.f;
    float uv0y = 0.f;
    float uv1x = 1.f;
    float uv1y = 1.f;
    float borderL = 0.f;
    float borderT = 0.f;
    float borderR = 0.f;
    float borderB = 0.f;
    float cornerRadius = 0.f;
    float wrapWidth = 0.f;
    float itemHeight = 0.f;  // ScrollList uniform row height
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
    WidgetDesc &withMargin(float l, float t, float r, float b) {
        marginL = l;
        marginT = t;
        marginR = r;
        marginB = b;
        return *this;
    }
    WidgetDesc &withPadding(float l, float t, float r, float b) {
        paddingL = l;
        paddingT = t;
        paddingR = r;
        paddingB = b;
        return *this;
    }
    WidgetDesc &withMinSize(float w, float h) {
        minSizeX = w;
        minSizeY = h;
        return *this;
    }
    WidgetDesc &withMaxSize(float w, float h) {
        maxSizeX = w;
        maxSizeY = h;
        return *this;
    }
    WidgetDesc &withPercent(float w, float h) {
        percentW = w;
        percentH = h;
        return *this;
    }
    /** Place absolutely inside a Flex parent: (ax,ay) anchor in parent, (x,y) offset. */
    WidgetDesc &withAbsolute(float ax, float ay, float x = 0.f, float y = 0.f) {
        absolute = true;
        anchorX = ax;
        anchorY = ay;
        posX = x;
        posY = y;
        return *this;
    }
    WidgetDesc &withTint(float r, float g, float b, float a = 1.f) {
        tintR = r;
        tintG = g;
        tintB = b;
        tintA = a;
        return *this;
    }
    WidgetDesc &withUv(float u0, float v0, float u1, float v1) {
        uv0x = u0;
        uv0y = v0;
        uv1x = u1;
        uv1y = v1;
        return *this;
    }
    /** Nine-patch borders in px; keeps the middle stretchable. */
    WidgetDesc &withNinePatch(float l, float t, float r, float b) {
        borderL = l;
        borderT = t;
        borderR = r;
        borderB = b;
        return *this;
    }
    WidgetDesc &withCornerRadius(float radius) {
        cornerRadius = radius;
        return *this;
    }
    WidgetDesc &withWrap(float width) {
        wrapWidth = width;
        return *this;
    }
    WidgetDesc &withItemHeight(float height) {
        itemHeight = height;
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
/** Dropdown; options joined by '\n', selected index in `selected`. */
WidgetDesc combo(std::string label, std::vector<std::string> options, int selected,
                 std::string id = "", std::function<void(int)> onValue = {});
/** Colored / textured image. Size defaults to texture size via layout. */
WidgetDesc image(std::string id = "", float width = 0.f, float height = 0.f,
                 std::function<void()> onClick = {});
/** Clickable image (renders via ImGui ImageButton). */
WidgetDesc imageButton(std::string id, float width, float height, std::function<void()> onClick = {});
/** Embedded render target widget: shows an offscreen Canvas, routes input. */
WidgetDesc viewport(std::string id = "", float width = 0.f, float height = 0.f);
WidgetDesc inputText(std::string label, std::string value, std::string id = "",
                     std::function<void(const std::string &)> onChange = {});
WidgetDesc collapsingHeader(std::string label, std::vector<WidgetDesc> children = {},
                            std::string id = "", bool defaultOpen = true);
WidgetDesc child(std::string id, std::vector<WidgetDesc> children = {}, float width = 0.f,
                 float height = 120.f);
/**
 * Virtualized scroll list: only visible rows are drawn each frame. Children are
 * the full item set (rows laid out top-to-bottom at `itemHeight` px). Set a
 * `height` for the viewport; 0 = fill available space.
 */
WidgetDesc scrollList(std::string id, std::vector<WidgetDesc> children = {}, float height = 0.f,
                      float itemHeight = 0.f);
/** One-line convenience: virtualized list of buttons from string items. */
WidgetDesc virtualList(std::string listId, const std::vector<std::string> &items,
                       float height = 200.f, float itemHeight = 0.f);

/** Elastic layout container (row/column). Prefer `row` / `column` shorthands. */
WidgetDesc flex(FlexDirection direction, std::vector<WidgetDesc> children = {},
                std::string id = "");
WidgetDesc row(std::vector<WidgetDesc> children = {}, std::string id = "");
WidgetDesc column(std::vector<WidgetDesc> children = {}, std::string id = "");
/** Flexible empty space; default flexGrow=1 so it absorbs free space in a Flex parent. */
WidgetDesc spacer(std::string id = "", float grow = 1.f);

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
