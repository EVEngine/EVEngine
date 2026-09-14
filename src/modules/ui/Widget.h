#pragma once

#include "ui/Icons.h"
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
    std::string tooltip;
    bool visible = true;
    bool enabled = true;
    bool checked = false;
    bool open = true;
    FocusMode focusMode = FocusMode::All;
    MouseFilter mouseFilter = MouseFilter::Stop;
    ThemePreset themePreset = ThemePreset::Inherit;
    int tabIndex = 0;
    std::string focusNext;
    std::string focusPrevious;
    std::string focusLeft;
    std::string focusRight;
    std::string focusUp;
    std::string focusDown;
    AccessibilityRole accessibilityRole = AccessibilityRole::Auto;
    std::string accessibilityName;
    std::string accessibilityDescription;
    bool dragSource = false;
    bool dropTarget = false;
    std::string dragPayloadType;
    std::string dragPayloadText;
    std::string acceptedDropType;
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
    WidgetDesc &withTooltip(std::string v) {
        tooltip = std::move(v);
        return *this;
    }
    WidgetDesc &withVisible(bool v) {
        visible = v;
        return *this;
    }
    /** @brief Sets whether the widget accepts interaction. */
    WidgetDesc &withEnabled(bool v) {
        enabled = v;
        return *this;
    }
    /** @brief Selects how the widget participates in keyboard focus navigation. */
    WidgetDesc &withFocusMode(FocusMode v) {
        focusMode = v;
        return *this;
    }
    /** @brief Selects whether pointer input stops, passes through, or ignores this widget. */
    WidgetDesc &withMouseFilter(MouseFilter v) {
        mouseFilter = v;
        return *this;
    }
    /** @brief Overrides the built-in theme for this widget subtree. */
    WidgetDesc &withTheme(ThemePreset v) {
        themePreset = v;
        return *this;
    }
    /** @brief Sets the stable ordering hint used by sequential focus traversal. */
    WidgetDesc &withTabIndex(int v) {
        tabIndex = v;
        return *this;
    }
    /** @brief Assigns explicit previous and next focus-neighbor widget identifiers. */
    WidgetDesc &withFocusOrder(std::string previous, std::string next) {
        focusPrevious = std::move(previous);
        focusNext = std::move(next);
        return *this;
    }
    /** @brief Assigns explicit directional focus-neighbor widget identifiers. */
    WidgetDesc &withFocusNeighbors(std::string left, std::string right, std::string up,
                                   std::string down) {
        focusLeft = std::move(left);
        focusRight = std::move(right);
        focusUp = std::move(up);
        focusDown = std::move(down);
        return *this;
    }
    /** @brief Supplies the semantic role and labels exposed to accessibility clients. */
    WidgetDesc &withAccessibility(AccessibilityRole role, std::string name,
                                  std::string description = {}) {
        accessibilityRole = role;
        accessibilityName = std::move(name);
        accessibilityDescription = std::move(description);
        return *this;
    }
    /** @brief Marks this widget as a desktop drag source with an owning text payload. */
    WidgetDesc &withDragSource(std::string payloadType, std::string payloadText) {
        dragSource = true;
        dragPayloadType = std::move(payloadType);
        dragPayloadText = std::move(payloadText);
        return *this;
    }
    /** @brief Marks this widget as a desktop drop target for one type or "*". */
    WidgetDesc &withDropTarget(std::string acceptedType) {
        dropTarget = true;
        acceptedDropType = std::move(acceptedType);
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

/** @brief Top-level window widget with a title bar. */
WidgetDesc window(std::string title, std::vector<WidgetDesc> children = {}, std::string id = "root");
/** @brief Static text label. */
WidgetDesc text(std::string content, std::string id = "");
/** @brief Clickable button; fires onClick. */
WidgetDesc button(std::string label, std::string id = "", std::function<void()> onClick = {});
/** @brief Semantic vector icon rendered from the bundled editor icon font. */
WidgetDesc icon(Icon value, std::string id = "");
/** @brief Button containing a semantic icon and optional visible label. */
WidgetDesc iconButton(Icon value, std::string label = "", std::string id = "",
                      std::function<void()> onClick = {});
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
/** Dropdown; options joined by '\n', selected index in `selected`. */
WidgetDesc combo(std::string label, std::vector<std::string> options, int selected,
                 std::string id = "", std::function<void(float)> onValue = {});
/** Colored / textured image. Size defaults to texture size via layout. */
WidgetDesc image(std::string id = "", float width = 0.f, float height = 0.f,
                 std::function<void()> onClick = {});
/** @brief Stretchable textured container with asset-defined content padding. */
WidgetDesc ninePatchPanel(std::vector<WidgetDesc> children = {}, std::string id = "",
                          uint64_t textureId = 0);
/** Clickable image (renders via ImGui ImageButton). */
WidgetDesc imageButton(std::string id, float width, float height, std::function<void()> onClick = {});
/** Embedded render target widget: shows an offscreen Canvas, routes input. */
WidgetDesc viewport(std::string id = "", float width = 0.f, float height = 0.f);
/** @brief Editable text field; fires onTextChange. */
WidgetDesc inputText(std::string label, std::string value, std::string id = "",
                     std::function<void(const std::string &)> onChange = {});
/** @brief Compact search field with placeholder text and a bundled search icon. */
WidgetDesc searchField(std::string hint, std::string value = {}, std::string id = "",
                       std::function<void(const std::string &)> onChange = {});
/** @brief Modern boolean toggle; fires onToggle when changed. */
WidgetDesc toggleSwitch(std::string label, bool checked = false, std::string id = "",
                        std::function<void(bool)> onToggle = {});
/** @brief Compact status/category pill. Tint controls its background color. */
WidgetDesc badge(std::string label, std::string id = "");
/**
 * @brief Color editor with a swatch grid (调色板).
 * RGBA is stored in tint channels. `valueText` may list extra swatches as
 * `#rrggbb`, `#rrggbbaa`, or `r,g,b[,a]` lines; empty uses the built-in palette.
 * Changes fire the same value events as sliders (`consumeChange` / `onValue`).
 */
WidgetDesc colorPalette(std::string label, float r, float g, float b, float a = 1.f,
                        std::string id = "", std::function<void(float)> onValue = {});
/** @brief Bordered surface container with editor-friendly padding. */
WidgetDesc card(std::vector<WidgetDesc> children = {}, std::string id = "");
/** @brief Non-collapsible editor section heading with an accent marker. */
WidgetDesc sectionHeader(std::string label, std::string id = "");
/** @brief Window menu bar container. Must be a direct child of a Window. */
WidgetDesc menuBar(std::vector<WidgetDesc> children = {}, std::string id = "");
/** @brief Popup menu container used inside a MenuBar or another Menu. */
WidgetDesc menu(std::string label, std::vector<WidgetDesc> children = {}, std::string id = "");
/** @brief Selectable menu command with an optional shortcut hint. */
WidgetDesc menuItem(std::string label, std::string shortcut = {}, std::string id = "",
                    std::function<void()> onClick = {}, bool selected = false);
/** @brief Horizontal editor command strip; Spacer children absorb free width. */
WidgetDesc toolbar(std::vector<WidgetDesc> children = {}, std::string id = "");
/** @brief Wrapping grid of editor tools with uniform square cells. */
WidgetDesc toolbox(std::vector<WidgetDesc> children = {}, std::string id = "",
                   float cellSize = 0.f, int columns = 0);
/** @brief Vertical editor side panel. Width defaults to 240 logical pixels. */
WidgetDesc sidebar(std::vector<WidgetDesc> children = {}, std::string id = "",
                   float width = 0.f);
/** @brief Compact horizontal status strip; Spacer children absorb free width. */
WidgetDesc statusBar(std::vector<WidgetDesc> children = {}, std::string id = "");
/**
 * @brief Two resizable panes separated by a drag handle.
 * @param direction Row creates left/right panes; Column creates top/bottom panes.
 * @param ratio Fraction assigned to the first pane, clamped to [0.1, 0.9].
 */
WidgetDesc splitPane(FlexDirection direction, WidgetDesc first, WidgetDesc second,
                     float ratio = 0.25f, std::string id = "",
                     std::function<void(float)> onResize = {});
/** @brief Collapsible header containing child widgets. */
WidgetDesc collapsingHeader(std::string label, std::vector<WidgetDesc> children = {},
                            std::string id = "", bool defaultOpen = true);
/** @brief Scrollable child region with an explicit size. */
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
